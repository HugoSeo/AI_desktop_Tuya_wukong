#include "wukong_session.h"
#include "wukong_storage.h"
#include "claw_config.h"
#include "tal_memory.h"
#include "mix_method.h"   /* mm_strdup */
#include "tal_time_service.h"
#include <string.h>
#include <stdio.h>

/* ---- end-of-turn compaction (see specs/2026-08-13-claw-session-compaction) ----
 * compact_trigger/compact_keep/send_tokens now come from claw_config_get()
 * (runtime-tunable via wukong.json; see provider/claw/config/claw_config.h). */
#define WK_SESSION_COMPACT_MSG_CHARS  512   /* per-message clip in the summarize input */
#define WK_SESSION_COMPACT_SUMMARY_CHARS 1024 /* wider clip for the rolling summary line:
                                               * real summaries run ~600B, clipping at 512
                                               * cut off their tail sections */
#define WK_SESSION_COMPACT_INPUT_MAX  8192  /* total summarize-input cap */

typedef enum {
    WK_MSG_TEXT = 0,     /* fallback: unknown role (legacy tolerance) */
    WK_MSG_TOOL_CALL,    /* assistant message carrying tool_calls */
    WK_MSG_TOOL,         /* tool result message */
    WK_MSG_USER,         /* user message = turn start marker */
    WK_MSG_ASSISTANT,    /* assistant final text */
    WK_MSG_SUMMARY,      /* role=system: the one rolling summary line */
} WK_MSG_KIND_E;

typedef struct {
    CHAR_T *json;   /* heap: one serialized message (no trailing newline) */
    UINT_T  len;
    UINT8_T kind;   /* WK_MSG_KIND_E */
} wk_msg_t;

typedef struct {
    CHAR_T   chat_id[CLAW_FIXED_SESSION_ID_MAX];   /* [0]=='\0' => free slot */
    wk_msg_t msgs[CLAW_FIXED_SESSION_MAX_MSGS];
    UINT_T   count;
    UINT_T   bytes;
    UINT_T   file_bytes;   /* approx on-disk size, drives compaction */
    BOOL_T   restored;     /* file window merged into RAM (or medium absent) */
    UINT64_T lru;          /* monotonic, higher = newer */
} wk_session_t;

/* Session manager handle: all slots + LRU tick in one owned allocation,
 * malloc'd lazily on first use instead of a static array (Task 4). */
typedef struct {
    wk_session_t slots[CLAW_FIXED_SESSION_SLOTS];
    UINT64_T     lru_tick;
} WK_SESSION_CTX_T;

STATIC WK_SESSION_CTX_T *s_sess = NULL;

/* Storage namespace: session files live at <root>/tuyaos/claw/session/. */
#define WK_SESSION_NS  "claw/session"

/* Entry name "<chat_id>.jsonl", non [A-Za-z0-9_-] chars mapped to '_'. */
STATIC VOID_T __rel_path(CONST CHAR_T *chat_id, CHAR_T *buf, UINT_T len)
{
    UINT_T off = 0;
    for (UINT_T i = 0; chat_id[i] != '\0' && off + 8 < len && i < CLAW_FIXED_SESSION_ID_MAX; i++) {
        CHAR_T c = chat_id[i];
        BOOL_T ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '-';
        buf[off++] = ok ? c : '_';
    }
    (VOID_T)snprintf(buf + off, len - off, ".jsonl");
}

/* Message kind from its parsed JSON shape. role=system is reserved for the
 * rolling summary (ordinary session messages are only user/assistant/tool). */
STATIC UINT8_T __kind_of(CONST ty_cJSON *msg)
{
    ty_cJSON *role = ty_cJSON_GetObjectItem((ty_cJSON *)msg, "role");
    CONST CHAR_T *r = (role && role->valuestring) ? role->valuestring : "";
    if (strcmp(r, "tool") == 0) {
        return WK_MSG_TOOL;
    }
    if (ty_cJSON_GetObjectItem((ty_cJSON *)msg, "tool_calls") != NULL) {
        return WK_MSG_TOOL_CALL;
    }
    if (strcmp(r, "system") == 0) {
        return WK_MSG_SUMMARY;
    }
    if (strcmp(r, "user") == 0) {
        return WK_MSG_USER;
    }
    if (strcmp(r, "assistant") == 0) {
        return WK_MSG_ASSISTANT;
    }
    return WK_MSG_TEXT;
}

STATIC INT_T __find(CONST CHAR_T *chat_id)
{
    for (INT_T i = 0; i < CLAW_FIXED_SESSION_SLOTS; i++) {
        if (s_sess->slots[i].chat_id[0] && strcmp(s_sess->slots[i].chat_id, chat_id) == 0) {
            return i;
        }
    }
    return -1;
}

STATIC VOID_T __reset(wk_session_t *s)
{
    for (UINT_T i = 0; i < s->count; i++) {
        tal_free(s->msgs[i].json);
        s->msgs[i].json = NULL;
    }
    s->count = 0;
    s->bytes = 0;
    s->file_bytes = 0;
    s->restored = FALSE;
}

/* Lazily allocate the handle so callers work with no explicit init (matches
 * wukong_memory's MEM_CTX_T pattern). */
STATIC OPERATE_RET __ensure_ctx(VOID_T)
{
    if (s_sess == NULL) {
        s_sess = (WK_SESSION_CTX_T *)wukong_claw_malloc(sizeof(*s_sess));
        if (s_sess == NULL) {
            return OPRT_MALLOC_FAILED;
        }
        memset(s_sess, 0, sizeof(*s_sess));
    }
    return OPRT_OK;
}

OPERATE_RET wukong_session_init(VOID_T)
{
    return __ensure_ctx();
}

VOID_T wukong_session_deinit(VOID_T)
{
    if (s_sess == NULL) {
        return;
    }
    for (INT_T i = 0; i < CLAW_FIXED_SESSION_SLOTS; i++) {
        __reset(&s_sess->slots[i]);
    }
    WK_SESSION_CTX_T *victim = s_sess;   /* null-before-free */
    s_sess = NULL;
    wukong_claw_free(victim);
}

/* Find existing slot for chat_id, or a free slot, or LRU-evict one. */
STATIC INT_T __alloc(CONST CHAR_T *chat_id)
{
    INT_T idx = __find(chat_id);
    if (idx >= 0) {
        return idx;
    }

    for (INT_T i = 0; i < CLAW_FIXED_SESSION_SLOTS; i++) {
        if (!s_sess->slots[i].chat_id[0]) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        idx = 0;
        for (INT_T i = 1; i < CLAW_FIXED_SESSION_SLOTS; i++) {
            if (s_sess->slots[i].lru < s_sess->slots[idx].lru) {
                idx = i;
            }
        }
        __reset(&s_sess->slots[idx]);
    }

    wk_session_t *s = &s_sess->slots[idx];
    strncpy(s->chat_id, chat_id, CLAW_FIXED_SESSION_ID_MAX - 1);
    s->chat_id[CLAW_FIXED_SESSION_ID_MAX - 1] = '\0';
    s->count = 0;
    s->bytes = 0;
    s->file_bytes = 0;
    s->restored = FALSE;
    return idx;
}

STATIC VOID_T __drop_oldest(wk_session_t *s)
{
    UINT_T victim = 0;

    if (s->count == 0) {
        return;
    }
    /* The summary's lifecycle belongs to compaction alone: it is only ever
     * replaced by the next apply_summary, never evicted here. Losing one
     * slot costs one raw message; losing the summary costs dozens of
     * compacted ones. */
    if (s->msgs[0].kind == WK_MSG_SUMMARY && s->count > 1) {
        victim = 1;
    }
    tal_free(s->msgs[victim].json);
    s->bytes -= s->msgs[victim].len;
    memmove(&s->msgs[victim], &s->msgs[victim + 1],
            (SIZE_T)(s->count - victim - 1) * sizeof(wk_msg_t));
    s->count--;
}

/* Push one owned serialized message into the RAM window (no file I/O). */
STATIC VOID_T __push(wk_session_t *s, UINT8_T kind, CHAR_T *json)
{
    if (s->count >= CLAW_FIXED_SESSION_MAX_MSGS) {
        __drop_oldest(s);
    }
    wk_msg_t *m = &s->msgs[s->count];
    m->json = json;
    m->len  = (UINT_T)strlen(json);
    m->kind = kind;
    s->bytes += m->len;
    s->count++;
}

/* Merge the persisted tail window into RAM: file lines first (older), then any
 * messages accumulated in RAM before the medium came up (newer). Runs once per
 * slot; retried on every access until the volume is ready. */
STATIC VOID_T __restore(wk_session_t *s)
{
    if (s->restored) {
        return;
    }
    if (!wukong_storage_ready()) {
        return;    /* no medium yet: stay RAM-only, retry on next access */
    }
    s->restored = TRUE;

    CHAR_T path[CLAW_FIXED_SESSION_PATH_MAX];
    __rel_path(s->chat_id, path, sizeof(path));

    BYTE_T *buf = NULL;
    UINT_T blen = 0;
    if (wukong_storage_read(WK_SESSION_NS, path, &buf, &blen) != OPRT_OK || buf == NULL) {
        return;    /* no history file */
    }
    s->file_bytes = blen;

    /* Stage the pre-restore RAM messages (ownership moves to the stage). */
    wk_msg_t *s_stage = (wk_msg_t *)wukong_claw_malloc(sizeof(wk_msg_t) * CLAW_FIXED_SESSION_MAX_MSGS);
    if (s_stage == NULL) {
        wukong_storage_free(buf);
        s->restored = FALSE;   /* retry next access instead of skipping restore forever */
        return;    /* can't stage safely: leave RAM window untouched */
    }
    UINT_T staged = s->count;
    memcpy(s_stage, s->msgs, (SIZE_T)staged * sizeof(wk_msg_t));
    s->count = 0;
    s->bytes = 0;

    /* Replay complete lines; a partial tail (power cut) is skipped. */
    CHAR_T *p = (CHAR_T *)buf;
    while (*p != '\0') {
        CHAR_T *nl = strchr(p, '\n');
        if (nl == NULL) {
            break;
        }
        *nl = '\0';
        if (nl > p) {
            ty_cJSON *m = ty_cJSON_Parse(p);
            if (m != NULL) {
                CHAR_T *dup = mm_strdup(p);
                if (dup != NULL) {
                    __push(s, __kind_of(m), dup);
                }
                ty_cJSON_Delete(m);
            }
        }
        p = nl + 1;
    }
    wukong_storage_free(buf);

    for (UINT_T i = 0; i < staged; i++) {
        __push(s, s_stage[i].kind, s_stage[i].json);
    }
    wukong_claw_free(s_stage);
}

/* Rewrite the backing file as the current RAM window (atomic). */
STATIC VOID_T __compact(wk_session_t *s, CONST CHAR_T *path)
{
    UINT_T total = s->bytes + s->count;   /* + one '\n' per line */
    CHAR_T *buf = (CHAR_T *)tal_malloc(total + 1);
    if (buf == NULL) {
        return;
    }
    UINT_T off = 0;
    for (UINT_T i = 0; i < s->count; i++) {
        memcpy(buf + off, s->msgs[i].json, s->msgs[i].len);
        off += s->msgs[i].len;
        buf[off++] = '\n';
    }
    if (wukong_storage_write(WK_SESSION_NS, path, (CONST BYTE_T *)buf, off) == OPRT_OK) {
        s->file_bytes = off;
    }
    tal_free(buf);
}

/* Append one owned serialized message: RAM window + write-through file line. */
STATIC OPERATE_RET __commit(CONST CHAR_T *chat_id, UINT8_T kind, CHAR_T *json)
{
    INT_T idx = __alloc(chat_id);
    wk_session_t *s = &s_sess->slots[idx];
    __restore(s);

    UINT_T len = (UINT_T)strlen(json);
    CHAR_T path[CLAW_FIXED_SESSION_PATH_MAX];
    __rel_path(chat_id, path, sizeof(path));

    /* Write-through as one "json\n" chunk (single open/sync). Degrades to
     * RAM-only when the medium is unavailable. */
    CHAR_T *line = (CHAR_T *)tal_malloc(len + 2);
    if (line != NULL) {
        memcpy(line, json, len);
        line[len] = '\n';
        line[len + 1] = '\0';
        if (wukong_storage_append(WK_SESSION_NS, path, (CONST BYTE_T *)line, len + 1) == OPRT_OK) {
            s->file_bytes += len + 1;
        }
        tal_free(line);
    }

    __push(s, kind, json);
    if (s->file_bytes > (UINT_T)claw_config_get()->session_file_max) {
        __compact(s, path);
    }
    s->lru = ++s_sess->lru_tick;
    return OPRT_OK;
}

/* Truncate a UTF-8 string in place at most @p max chars (byte-boundary safe). */
STATIC VOID_T __truncate_utf8(CHAR_T *str, UINT_T max)
{
    UINT_T len = (UINT_T)strlen(str);
    if (len <= max) {
        return;
    }
    UINT_T n = max;
    while (n > 0 && ((BYTE_T)str[n] & 0xC0) == 0x80) {
        n--;
    }
    str[n] = '\0';
}

OPERATE_RET wukong_session_append(CONST CHAR_T *chat_id, CONST CHAR_T *role,
                                  CONST CHAR_T *content)
{
    if (__ensure_ctx() != OPRT_OK) {
        return OPRT_MALLOC_FAILED;
    }
    if (!chat_id || !chat_id[0] || !role || !content) {
        return OPRT_INVALID_PARM;
    }

    CHAR_T *dup = mm_strdup(content);
    if (dup == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    __truncate_utf8(dup, (UINT_T)claw_config_get()->session_msg_chars);

    ty_cJSON *msg = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(msg, "role", role);
    ty_cJSON_AddStringToObject(msg, "content", dup);
    ty_cJSON_AddNumberToObject(msg, "ts", (double)tal_time_get_posix());
    tal_free(dup);

    UINT8_T kind = __kind_of(msg);
    if (kind == WK_MSG_SUMMARY) {
        kind = WK_MSG_TEXT;   /* role=system is reserved for apply_summary */
    }
    CHAR_T *json = ty_cJSON_PrintUnformatted(msg);
    ty_cJSON_Delete(msg);
    if (json == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    return __commit(chat_id, kind, json);
}

OPERATE_RET wukong_session_append_msg(CONST CHAR_T *chat_id, CONST ty_cJSON *msg)
{
    if (__ensure_ctx() != OPRT_OK) {
        return OPRT_MALLOC_FAILED;
    }
    if (!chat_id || !chat_id[0] || !msg) {
        return OPRT_INVALID_PARM;
    }

    ty_cJSON *dup = ty_cJSON_Duplicate((ty_cJSON *)msg, 1);
    if (dup == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON *content = ty_cJSON_GetObjectItem(dup, "content");
    if (content != NULL && content->valuestring != NULL) {
        __truncate_utf8(content->valuestring, (UINT_T)claw_config_get()->session_msg_chars);
    }
    ty_cJSON_AddNumberToObject(dup, "ts", (double)tal_time_get_posix());

    UINT8_T kind = __kind_of(dup);
    if (kind == WK_MSG_SUMMARY) {
        kind = WK_MSG_TEXT;   /* role=system is reserved for apply_summary */
    }
    CHAR_T *json = ty_cJSON_PrintUnformatted(dup);
    ty_cJSON_Delete(dup);
    if (json == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    return __commit(chat_id, kind, json);
}

ty_cJSON *wukong_session_get_messages(CONST CHAR_T *chat_id)
{
    if (__ensure_ctx() != OPRT_OK) {
        return NULL;
    }
    if (!chat_id || !chat_id[0]) {
        return NULL;
    }
    INT_T idx = __alloc(chat_id);    /* also restores after reboot */
    wk_session_t *s = &s_sess->slots[idx];
    __restore(s);
    if (s->count == 0) {
        return NULL;
    }

    /* Send window: newest backwards under the token budget (>=1 message). */
    UINT_T start = s->count;
    UINT_T est = 0;
    while (start > 0) {
        UINT_T t = s->msgs[start - 1].len / 3 + 1;   /* rough tokens: bytes/3 */
        if (est + t > (UINT_T)claw_config_get()->send_tokens && start < s->count) {
            break;
        }
        est += t;
        start--;
    }
    /* Never start mid-turn: skip forward past a leading continuation message
     * (tool round or assistant reply) whose turn-opening user message fell
     * out of the window — same "turn = user through final reply" boundary
     * the compaction dump/apply share. Whitelist form: advance to the first
     * USER (or legacy TEXT) — this also steps over a leading summary line,
     * so orphans sitting BEHIND the summary (hard-cap eviction ate their
     * turn-opening user) are skipped too instead of being sent as an
     * invalid tool-first sequence. */
    while (start < s->count &&
           s->msgs[start].kind != WK_MSG_USER &&
           s->msgs[start].kind != WK_MSG_TEXT) {
        start++;
    }
    if (start >= s->count) {
        return NULL;
    }

    ty_cJSON *arr = ty_cJSON_CreateArray();
    if (arr == NULL) {
        return NULL;
    }
    for (UINT_T i = start; i < s->count; i++) {
        if (s->msgs[i].kind == WK_MSG_SUMMARY) {
            continue;   /* delivered via the system prompt, not the dialogue */
        }
        ty_cJSON *m = ty_cJSON_Parse(s->msgs[i].json);
        if (m == NULL) {
            continue;
        }
        ty_cJSON_DeleteItemFromObject(m, "ts");   /* device-only field */
        ty_cJSON_AddItemToArray(arr, m);
    }
    s->lru = ++s_sess->lru_tick;
    return arr;
}

VOID_T wukong_session_clear(CONST CHAR_T *chat_id)
{
    if (__ensure_ctx() != OPRT_OK) {
        return;
    }
    if (!chat_id) {
        return;
    }
    CHAR_T path[CLAW_FIXED_SESSION_PATH_MAX];
    __rel_path(chat_id, path, sizeof(path));
    (VOID_T)wukong_storage_delete(WK_SESSION_NS, path);

    INT_T idx = __find(chat_id);
    if (idx >= 0) {
        __reset(&s_sess->slots[idx]);
        s_sess->slots[idx].chat_id[0] = '\0';
    }
}

VOID_T wukong_session_stat(CONST CHAR_T *chat_id, UINT_T *out_count, UINT_T *out_bytes)
{
    UINT_T count = 0;
    UINT_T bytes = 0;
    if (__ensure_ctx() == OPRT_OK && chat_id) {
        INT_T idx = __find(chat_id);
        if (idx >= 0) {
            count = s_sess->slots[idx].count;
            bytes = s_sess->slots[idx].bytes;
        }
    }
    if (out_count) {
        *out_count = count;
    }
    if (out_bytes) {
        *out_bytes = bytes;
    }
}

/* ==================== compaction primitives ==================== */

BOOL_T wukong_session_needs_compact(CONST CHAR_T *chat_id)
{
    if (__ensure_ctx() != OPRT_OK) {
        return FALSE;
    }
    if (!chat_id || !chat_id[0]) {
        return FALSE;
    }
    INT_T idx = __find(chat_id);
    return (idx >= 0 && s_sess->slots[idx].count >= (UINT_T)claw_config_get()->compact_trigger);
}

/* One "role: content"-ish line for the summarize input. Tool-call messages
 * render as "[called <fn>]"; content is clipped at a UTF-8 boundary. */
STATIC UINT_T __dump_line(CONST wk_msg_t *m, CHAR_T *line, UINT_T cap)
{
    ty_cJSON *j = ty_cJSON_Parse(m->json);
    if (j == NULL) {
        return 0;
    }
    ty_cJSON *role = ty_cJSON_GetObjectItem(j, "role");
    CONST CHAR_T *r = (role && role->valuestring) ? role->valuestring : "?";
    UINT_T clip = (m->kind == WK_MSG_SUMMARY) ? WK_SESSION_COMPACT_SUMMARY_CHARS
                                              : WK_SESSION_COMPACT_MSG_CHARS;
    CHAR_T body[WK_SESSION_COMPACT_SUMMARY_CHARS + 1];
    body[0] = '\0';
    if (m->kind == WK_MSG_TOOL_CALL) {
        ty_cJSON *tc = ty_cJSON_GetObjectItem(j, "tool_calls");
        ty_cJSON *t0 = tc ? ty_cJSON_GetArrayItem(tc, 0) : NULL;
        ty_cJSON *fn = t0 ? ty_cJSON_GetObjectItem(t0, "function") : NULL;
        ty_cJSON *nm = fn ? ty_cJSON_GetObjectItem(fn, "name") : NULL;
        (VOID_T)snprintf(body, sizeof(body), "[called %s]",
                         (nm && nm->valuestring) ? nm->valuestring : "?");
    } else {
        ty_cJSON *content = ty_cJSON_GetObjectItem(j, "content");
        if (content && content->valuestring) {
            /* Clip on a character boundary. A plain byte-level strncpy used to
             * cut multi-byte chars in half (__truncate_utf8 early-outs when
             * len==max, so it never repaired the cut); the lone lead byte plus
             * the escaped "\n" after it made the LLM endpoint reject the whole
             * request as invalid UTF-8. */
            CONST CHAR_T *src = content->valuestring;
            UINT_T copy = (UINT_T)strlen(src);
            if (copy > clip) {
                copy = clip;
                while (copy > 0 && ((BYTE_T)src[copy] & 0xC0) == 0x80) {
                    copy--;   /* cut point sits inside a char: back off to its lead */
                }
            }
            memcpy(body, src, copy);
            body[copy] = '\0';
        }
    }
    UINT_T n = (UINT_T)snprintf(line, cap, "%s: %s\n", r, body);
    ty_cJSON_Delete(j);
    return (n >= cap) ? cap - 1 : n;
}

CHAR_T *wukong_session_dump_oldest_for_summary(CONST CHAR_T *chat_id, UINT_T *out_covered)
{
    if (out_covered) {
        *out_covered = 0;
    }
    if (__ensure_ctx() != OPRT_OK) {
        return NULL;
    }
    if (!chat_id || !chat_id[0] || !out_covered) {
        return NULL;
    }
    INT_T idx = __find(chat_id);
    if (idx < 0) {
        return NULL;
    }
    wk_session_t *s = &s_sess->slots[idx];
    if (s->count < (UINT_T)claw_config_get()->compact_trigger) {
        return NULL;
    }

    /* Keep-region start = first USER at/after count-KEEP (turn boundary);
     * fall back towards older when that range has none. keep_start==0 means
     * one giant turn: nothing compactable. */
    UINT_T keep_start = 0;
    /* Guard the subtraction: with a misconfigured (or test-lowered) trigger
     * below KEEP, count-KEEP would underflow and scan out of bounds. */
    UINT_T keep = (UINT_T)claw_config_get()->compact_keep;
    UINT_T from = (s->count > keep) ? s->count - keep : 0;
    for (UINT_T i = from; i < s->count; i++) {
        if (s->msgs[i].kind == WK_MSG_USER) {
            keep_start = i;
            break;
        }
    }
    if (keep_start == 0) {
        for (UINT_T i = from; i > 0; i--) {
            if (s->msgs[i - 1].kind == WK_MSG_USER) {
                keep_start = i - 1;
                break;
            }
        }
    }
    if (keep_start == 0) {
        return NULL;
    }

    CHAR_T *buf = (CHAR_T *)tal_malloc(WK_SESSION_COMPACT_INPUT_MAX + 1);
    if (buf == NULL) {
        return NULL;
    }
    /* line: role prefix + clipped body + "\n" */
    CHAR_T line[WK_SESSION_COMPACT_SUMMARY_CHARS + 64];
    UINT_T off = 0;
    UINT_T covered = 0;
    UINT_T last_turn = 0;   /* latest USER index seen: back-off point on cap hit */
    for (UINT_T i = 0; i < keep_start; i++) {
        if (s->msgs[i].kind == WK_MSG_USER && i > 0) {
            last_turn = i;
        }
        UINT_T n = __dump_line(&s->msgs[i], line, sizeof(line));
        if (n == 0) {
            covered = i + 1;   /* unparsable line: count it covered, skip */
            continue;
        }
        if (off + n > WK_SESSION_COMPACT_INPUT_MAX) {
            /* cap hit: back off to the last complete turn so nothing is
             * dropped without having been summarized */
            covered = last_turn;
            break;
        }
        memcpy(buf + off, line, n);
        off += n;
        covered = i + 1;
    }
    buf[off] = '\0';
    if (covered == 0 || off == 0) {
        tal_free(buf);
        return NULL;
    }
    *out_covered = covered;
    return buf;
}

OPERATE_RET wukong_session_apply_summary(CONST CHAR_T *chat_id, UINT_T covered,
                                         CONST CHAR_T *summary)
{
    if (__ensure_ctx() != OPRT_OK) {
        return OPRT_MALLOC_FAILED;
    }
    if (!chat_id || !chat_id[0] || covered == 0 || !summary || !summary[0]) {
        return OPRT_INVALID_PARM;
    }
    INT_T idx = __find(chat_id);
    if (idx < 0) {
        return OPRT_INVALID_PARM;
    }
    wk_session_t *s = &s_sess->slots[idx];
    if (covered > s->count) {
        return OPRT_INVALID_PARM;
    }

    /* Build the one role=system summary line (same msg_chars cap as any
     * message — the append path clips implicitly, this path must do it
     * explicitly). */
    CHAR_T *dup = mm_strdup(summary);
    if (dup == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    __truncate_utf8(dup, (UINT_T)claw_config_get()->session_msg_chars);
    ty_cJSON *msg = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(msg, "role", "system");
    ty_cJSON_AddStringToObject(msg, "content", dup);
    ty_cJSON_AddNumberToObject(msg, "ts", (double)tal_time_get_posix());
    tal_free(dup);
    CHAR_T *json = ty_cJSON_PrintUnformatted(msg);
    ty_cJSON_Delete(msg);
    if (json == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    /* Drop the covered batch, shift the kept region up, summary at front. */
    for (UINT_T i = 0; i < covered; i++) {
        s->bytes -= s->msgs[i].len;
        tal_free(s->msgs[i].json);
    }
    memmove(&s->msgs[1], &s->msgs[covered],
            (SIZE_T)(s->count - covered) * sizeof(wk_msg_t));
    s->msgs[0].json = json;
    s->msgs[0].len  = (UINT_T)strlen(json);
    s->msgs[0].kind = WK_MSG_SUMMARY;
    s->bytes += s->msgs[0].len;
    s->count = s->count - covered + 1;

    /* Structural change: rewrite (not append) the backing file atomically. */
    CHAR_T path[CLAW_FIXED_SESSION_PATH_MAX];
    __rel_path(chat_id, path, sizeof(path));
    __compact(s, path);
    s->lru = ++s_sess->lru_tick;
    return OPRT_OK;
}

CHAR_T *wukong_session_get_summary(CONST CHAR_T *chat_id)
{
    if (__ensure_ctx() != OPRT_OK) {
        return NULL;
    }
    if (!chat_id || !chat_id[0]) {
        return NULL;
    }
    INT_T idx = __alloc(chat_id);      /* restore-on-access, same as reads */
    wk_session_t *s = &s_sess->slots[idx];
    __restore(s);
    if (s->count == 0 || s->msgs[0].kind != WK_MSG_SUMMARY) {
        return NULL;
    }
    ty_cJSON *j = ty_cJSON_Parse(s->msgs[0].json);
    if (j == NULL) {
        return NULL;
    }
    ty_cJSON *content = ty_cJSON_GetObjectItem(j, "content");
    CHAR_T *out = (content && content->valuestring) ? mm_strdup(content->valuestring)
                                                    : NULL;
    ty_cJSON_Delete(j);
    return out;
}
