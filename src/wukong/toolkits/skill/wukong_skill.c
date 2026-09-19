#include "wukong_skill.h"
#include "wukong_storage.h"
#include "skill_tool.h"
#include "ty_cJSON.h"
#include "tal_memory.h"
#include "tal_log.h"
#include "base_event.h"
#include <stdio.h>
#include <string.h>

#include "tal_queue.h"

/* Storage layout: one directory per skill, <root>/tuyaos/claw/skills/<id>/,
 * holding a mandatory SKILL.md manifest (id = directory name). */
#define SKILL_NS   "claw/skills"

typedef struct {
    CHAR_T id[WUKONG_SKILL_ID_MAX];
    CHAR_T summary[WUKONG_SKILL_DESC_MAX];
} SKILL_ENTRY_T;

STATIC SKILL_ENTRY_T s_skills[WUKONG_SKILL_MAX];
STATIC UINT_T        s_skill_count = 0;
/* Guards the one-time part of wukong_skill_init (tool registration + storage
 * event subscription): wukong_skill_reload() re-enters wukong_skill_init() on
 * every rescan and must not re-register/re-subscribe. */
STATIC BOOL_T        s_lifecycle_started = FALSE;

/* id must be non-empty, <= max, no '/' and no ".." */
STATIC BOOL_T __id_safe(CONST CHAR_T *id)
{
    if (id == NULL || id[0] == '\0') return FALSE;
    if (strlen(id) >= WUKONG_SKILL_ID_MAX) return FALSE;
    if (strchr(id, '/') != NULL) return FALSE;
    if (strstr(id, "..") != NULL) return FALSE;
    return TRUE;
}

/* Locate the body after a leading "---\n{json}\n---" fence. Returns pointer to
 * body start (past the closing fence) and, if out_desc given, fills it from the
 * JSON "description". No fence -> body = whole text, desc untouched. */
STATIC CONST CHAR_T *__parse(CONST CHAR_T *text, CHAR_T *out_desc, UINT_T desc_cap)
{
    CONST CHAR_T *p = text;
    CONST CHAR_T *json_start, *json_end, *body;
    if (strncmp(p, "---", 3) != 0) {
        return text;   /* no frontmatter */
    }
    json_start = strchr(p, '\n');
    if (json_start == NULL) return text;
    json_start += 1;
    json_end = strstr(json_start, "\n---");
    if (json_end == NULL) return text;
    body = json_end + 4;               /* past "\n---" */
    while (*body == '\r' || *body == '\n') body++;   /* skip to body */

    if (out_desc != NULL && desc_cap > 0) {
        SIZE_T jlen = (SIZE_T)(json_end - json_start);
        CHAR_T *jbuf = (CHAR_T *)tal_malloc(jlen + 1);
        if (jbuf != NULL) {
            memcpy(jbuf, json_start, jlen);
            jbuf[jlen] = '\0';
            ty_cJSON *j = ty_cJSON_Parse(jbuf);
            if (j != NULL) {
                ty_cJSON *d = ty_cJSON_GetObjectItem(j, "description");
                if (d != NULL && ty_cJSON_IsString(d) && d->valuestring != NULL) {
                    strncpy(out_desc, d->valuestring, desc_cap - 1);
                    out_desc[desc_cap - 1] = '\0';
                }
                ty_cJSON_Delete(j);
            }
            tal_free(jbuf);
        }
    }
    return body;
}

/* Storage volume mounts asynchronously; rescan once it becomes usable so
 * skills dropped onto the card before boot show up without a reboot. */
STATIC INT_T __on_storage_ready(VOID_T *data)
{
    (VOID_T)data;
    wukong_skill_reload();
    return OPRT_OK;
}

OPERATE_RET wukong_skill_init(VOID_T)
{
    CHAR_T **dirs = NULL;
    UINT_T n = 0, i;

    /* Subscribe to storage.ready before scanning (not after): the volume
     * mounts asynchronously and can fire ready in the gap between "scan
     * finished" and "subscribe took effect", which would strand skills
     * unregistered until reboot. Subscribe first; if storage is already
     * ready by then, fall through and let this call double as the catch-up
     * scan (mirrors wukong_provider_claw.c's __claw_init ordering). */
    if (!s_lifecycle_started) {
        s_lifecycle_started = TRUE;
        (VOID)skill_tool_init();
        if (ty_subscribe_event(EVENT_WUKONG_STORAGE_READY, "skill",
                               __on_storage_ready, SUBSCRIBE_TYPE_NORMAL) != OPRT_OK) {
            TAL_PR_ERR("skill -> subscribe storage.ready failed; fs skills will not reload");
        }
        if (!wukong_storage_ready()) {
            return OPRT_OK;   /* not mounted yet; __on_storage_ready() drives the first scan */
        }
    }

    s_skill_count = 0;
    wukong_storage_list_dirs(SKILL_NS, &dirs, &n);
    for (i = 0; i < n && s_skill_count < WUKONG_SKILL_MAX; i++) {
        CHAR_T ns[64];                       /* "claw/skills/<id>" */
        BYTE_T *buf = NULL;
        UINT_T len = 0;

        if (!__id_safe(dirs[i])) continue;
        (VOID)snprintf(ns, sizeof(ns), "%s/%s", SKILL_NS, dirs[i]);

        SKILL_ENTRY_T *e = &s_skills[s_skill_count];
        strncpy(e->id, dirs[i], sizeof(e->id) - 1);
        e->id[sizeof(e->id) - 1] = '\0';
        e->summary[0] = '\0';

        if (wukong_storage_read(ns, "SKILL.md", &buf, &len) == OPRT_OK && buf != NULL) {
            (VOID)__parse((CONST CHAR_T *)buf, e->summary, sizeof(e->summary));
            wukong_storage_free(buf);
        } else {
            continue;                         /* no SKILL.md in this dir -> skip it */
        }
        if (e->summary[0] == '\0') {
            strncpy(e->summary, e->id, sizeof(e->summary) - 1);   /* id fallback */
            e->summary[sizeof(e->summary) - 1] = '\0';
        }
        s_skill_count++;
    }
    wukong_storage_free_list(dirs, n);
    return OPRT_OK;
}

/* Section-header rule injected above the catalog: without it the model takes
 * the shortest path (a directly matching tool) and never consults the skills
 * (seen on-device: "查询设备状态" went straight to device_info_get). Lives here
 * so it only costs tokens when at least one skill exists. */
STATIC CONST CHAR_T *c_skill_rules =
    "If a skill below applies, read and follow it before answering.\n";

CHAR_T *wukong_skill_build_summary(VOID_T)
{
    UINT_T i;
    SIZE_T total = 0;
    CHAR_T *out, *p;

    if (s_skill_count == 0) return NULL;
    total += strlen(c_skill_rules);
    for (i = 0; i < s_skill_count; i++) {
        total += strlen("- ") + strlen(s_skills[i].id) + strlen(": ")
               + strlen(s_skills[i].summary) + 1;   /* '\n' */
    }
    out = (CHAR_T *)tal_malloc(total + 1);
    if (out == NULL) return NULL;
    p = out;
    p += snprintf(p, total + 1, "%s", c_skill_rules);
    for (i = 0; i < s_skill_count; i++) {
        int w = snprintf(p, total + 1 - (SIZE_T)(p - out),
                         "- %s: %s\n", s_skills[i].id, s_skills[i].summary);
        if (w < 0) { tal_free(out); return NULL; }
        p += w;
    }
    return out;
}

OPERATE_RET wukong_skill_read(CONST CHAR_T *id, CHAR_T **out_text)
{
    CHAR_T ns[64];                          /* "claw/skills/<id>" */
    BYTE_T *buf = NULL;
    UINT_T len = 0;
    OPERATE_RET rt;

    if (out_text == NULL) return OPRT_INVALID_PARM;
    *out_text = NULL;
    if (!__id_safe(id)) return OPRT_INVALID_PARM;

    snprintf(ns, sizeof(ns), "%s/%s", SKILL_NS, id);
    rt = wukong_storage_read(ns, "SKILL.md", &buf, &len);
    if (rt != OPRT_OK || buf == NULL) {
        return OPRT_NOT_FOUND;
    }

    CONST CHAR_T *body = __parse((CONST CHAR_T *)buf, NULL, 0);
    /* cap at WUKONG_SKILL_BODY_MAX */
    SIZE_T blen = strlen(body);
    if (blen > WUKONG_SKILL_BODY_MAX) blen = WUKONG_SKILL_BODY_MAX;
    CHAR_T *copy = (CHAR_T *)tal_malloc(blen + 1);
    if (copy != NULL) {
        memcpy(copy, body, blen);
        copy[blen] = '\0';
    }
    wukong_storage_free(buf);
    if (copy == NULL) return OPRT_MALLOC_FAILED;
    *out_text = copy;
    return OPRT_OK;
}

VOID_T wukong_skill_reload(VOID_T)
{
    (VOID)wukong_skill_init();
}
