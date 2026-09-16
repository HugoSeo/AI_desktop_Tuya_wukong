#include "wukong_memory.h"
#include "wukong_storage.h"
#include "claw_config.h"
#include "tal_memory.h"
#include "mix_method.h"   /* mm_strdup */
#include "tal_time_service.h"
#include <string.h>
#include <stdio.h>

/* Storage namespace: memory files live at <root>/tuyaos/claw/memory/<name>. */
#define MEM_NS      "claw/memory"
#define IDX_NAME    "indexes.json"
#define MEM_NAME    "memories.json"
#define MD_NAME     "MEMORY.md"

typedef struct {
    CHAR_T  id[CLAW_FIXED_MEM_ID_LEN];
    TIME_T  time;
    CHAR_T  tags[CLAW_FIXED_MEM_TAGS_MAX][CLAW_FIXED_MEM_TAG_MAX];
    UINT_T  tag_count;
    INT_T   importance;
    INT_T   access;
    CHAR_T *content;                 /* heap (<=256), freed in reload/delete */
} MEM_ENTRY_T;

/* Memory manager handle: the whole module state lives here, allocated once in
 * init as a single owned handle instead of scattered static variables. Helpers
 * take it as a parameter; public entries fetch the global handle once. */
typedef struct {
    MEM_ENTRY_T entry[CLAW_FIXED_MEM_CEIL];   /* in-RAM cache (a copy of memories.json) */
    UINT_T      count;
    UINT_T      next_id;             /* next id sequence */
    BOOL_T      dirty_access;        /* access has unpersisted changes */
} MEM_CTX_T;

STATIC MEM_CTX_T *s_mem = NULL;

STATIC VOID_T __clear(MEM_CTX_T *mem)
{
    for (UINT_T i = 0; i < mem->count; i++) {
        tal_free(mem->entry[i].content);
        mem->entry[i].content = NULL;
    }
    mem->count = 0;
    mem->next_id = 1;
    mem->dirty_access = FALSE;
}

/* Parse memories.json "data" object into the cache; next_id from indexes.json.
 * Files are written by this component, so field types are trusted (no IsX). */
STATIC VOID_T __load(MEM_CTX_T *mem)
{
    BYTE_T *buf = NULL;
    UINT_T len = 0;

    __clear(mem);
    if (wukong_storage_read(MEM_NS, MEM_NAME, &buf, &len) == OPRT_OK && buf) {
        ty_cJSON *root = ty_cJSON_Parse((CONST CHAR_T *)buf);
        ty_cJSON *data = root ? ty_cJSON_GetObjectItem(root, "data") : NULL;
        if (data) {
            for (ty_cJSON *it = data->child; it && mem->count < CLAW_FIXED_MEM_CEIL; it = it->next) {
                MEM_ENTRY_T *e = &mem->entry[mem->count];
                ty_cJSON *j;
                /* Type-check every node: a corrupt/hand-edited file with a
                 * non-string where a string is expected would otherwise feed
                 * NULL into strncpy/mm_strdup and crash the load path. */
                j = ty_cJSON_GetObjectItem(it, "id");
                CONST CHAR_T *idv = (j && ty_cJSON_IsString(j)) ? j->valuestring : it->string;
                strncpy(e->id, idv ? idv : "", CLAW_FIXED_MEM_ID_LEN - 1);
                j = ty_cJSON_GetObjectItem(it, "time");
                e->time = j ? (TIME_T)j->valuedouble : 0;
                j = ty_cJSON_GetObjectItem(it, "importance");
                e->importance = j ? j->valueint : 5;
                j = ty_cJSON_GetObjectItem(it, "access");
                e->access = j ? j->valueint : 0;
                j = ty_cJSON_GetObjectItem(it, "content");
                e->content = mm_strdup((j && ty_cJSON_IsString(j)) ? j->valuestring : "");
                ty_cJSON *tags = ty_cJSON_GetObjectItem(it, "tags");
                e->tag_count = 0;
                if (tags) {
                    for (ty_cJSON *t = tags->child; t && e->tag_count < CLAW_FIXED_MEM_TAGS_MAX; t = t->next) {
                        if (!ty_cJSON_IsString(t) || !t->valuestring) {
                            continue;   /* skip a corrupt non-string tag element */
                        }
                        strncpy(e->tags[e->tag_count], t->valuestring, CLAW_FIXED_MEM_TAG_MAX - 1);
                        e->tag_count++;
                    }
                }
                mem->count++;
            }
        }
        if (root) {
            ty_cJSON_Delete(root);
        }
        wukong_storage_free(buf);
    }
    /* next_id from indexes.json (fallback: entry count + 1) */
    buf = NULL;
    len = 0;
    mem->next_id = mem->count + 1;
    if (wukong_storage_read(MEM_NS, IDX_NAME, &buf, &len) == OPRT_OK && buf) {
        ty_cJSON *ir = ty_cJSON_Parse((CONST CHAR_T *)buf);
        ty_cJSON *ni = ir ? ty_cJSON_GetObjectItem(ir, "next_id") : NULL;
        if (ni) {
            mem->next_id = (UINT_T)ni->valueint;
        }
        if (ir) {
            ty_cJSON_Delete(ir);
        }
        wukong_storage_free(buf);
    }
}

/* Retention score: importance weighted highest, then access, then recency. */
STATIC INT_T __score(CONST MEM_ENTRY_T *e, TIME_T now)
{
    INT_T recency = 10 - (INT_T)((now - e->time) / 2592000);
    if (recency < 0) {
        recency = 0;
    }
    INT_T acc = e->access;
    if (acc > 10) {
        acc = 10;
    }
    return e->importance * 2 + acc + recency;
}

/* Evict the single lowest-scoring memory (tie -> earliest time). No-op if empty. */
STATIC VOID_T __evict_one(MEM_CTX_T *mem)
{
    if (mem->count == 0) {
        return;
    }
    TIME_T now = tal_time_get_posix();
    UINT_T worst = 0;
    INT_T ws = __score(&mem->entry[0], now);
    for (UINT_T i = 1; i < mem->count; i++) {
        INT_T sc = __score(&mem->entry[i], now);
        if (sc < ws || (sc == ws && mem->entry[i].time < mem->entry[worst].time)) {
            ws = sc;
            worst = i;
        }
    }
    tal_free(mem->entry[worst].content);
    for (UINT_T i = worst; i + 1 < mem->count; i++) {
        mem->entry[i] = mem->entry[i + 1];
    }
    mem->count--;
}

/* Trim the cache down to the current config mem_max (config may have lowered it
 * since the cache was built). Shared by init and reload so both loading paths
 * enforce the same cap. */
STATIC VOID_T __enforce_cap(MEM_CTX_T *mem)
{
    while (mem->count > (UINT_T)claw_config_get()->mem_max) {
        __evict_one(mem);
    }
}

OPERATE_RET wukong_memory_init(VOID_T)
{
    if (s_mem == NULL) {
        s_mem = (MEM_CTX_T *)wukong_claw_malloc(sizeof(MEM_CTX_T));
        if (s_mem == NULL) {
            return OPRT_MALLOC_FAILED;
        }
        memset(s_mem, 0, sizeof(*s_mem));
    }
    __load(s_mem);
    __enforce_cap(s_mem);
    return OPRT_OK;
}

VOID_T wukong_memory_reload(VOID_T)
{
    if (s_mem == NULL) {
        return;
    }
    __load(s_mem);
    __enforce_cap(s_mem);
}

typedef enum { MEM_PEND_NONE, MEM_PEND_APPEND, MEM_PEND_UPDATE, MEM_PEND_DELETE } MEM_PEND_OP;

/* Serialize one entry into a memories.json object node. */
STATIC ty_cJSON *__entry_to_json(CONST MEM_ENTRY_T *e)
{
    ty_cJSON *o = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(o, "id", e->id);
    ty_cJSON_AddNumberToObject(o, "time", (double)e->time);
    ty_cJSON *ta = ty_cJSON_CreateArray();
    for (UINT_T k = 0; k < e->tag_count; k++) {
        ty_cJSON_AddItemToArray(ta, ty_cJSON_CreateString(e->tags[k]));
    }
    ty_cJSON_AddItemToObject(o, "tags", ta);
    ty_cJSON_AddNumberToObject(o, "importance", (double)e->importance);
    ty_cJSON_AddNumberToObject(o, "access", (double)e->access);
    ty_cJSON_AddStringToObject(o, "content", e->content ? e->content : "");
    return o;
}

/* Rewrite memories.json as the full table with a pending change applied to a
 * shadow view — the cache is NOT modified, so callers commit to it only after
 * this returns OK (disk is authoritative). @tmp is the appended/updated entry
 * (NULL for delete). Returns the store write result. */
STATIC OPERATE_RET __persist_memories(MEM_CTX_T *mem, MEM_PEND_OP op, INT_T idx, CONST MEM_ENTRY_T *tmp)
{
    ty_cJSON *root = ty_cJSON_CreateObject();
    ty_cJSON *data = ty_cJSON_CreateObject();
    UINT_T count = mem->count + (op == MEM_PEND_APPEND ? 1 : 0)
                              - (op == MEM_PEND_DELETE ? 1 : 0);
    ty_cJSON_AddStringToObject(root, "version", "1.0");
    ty_cJSON_AddNumberToObject(root, "count", (double)count);
    for (UINT_T i = 0; i < mem->count; i++) {
        if (op == MEM_PEND_DELETE && (INT_T)i == idx) {
            continue;
        }
        CONST MEM_ENTRY_T *e = (op == MEM_PEND_UPDATE && (INT_T)i == idx) ? tmp : &mem->entry[i];
        ty_cJSON_AddItemToObject(data, e->id, __entry_to_json(e));
    }
    if (op == MEM_PEND_APPEND) {
        ty_cJSON_AddItemToObject(data, tmp->id, __entry_to_json(tmp));
    }
    ty_cJSON_AddItemToObject(root, "data", data);
    CHAR_T *s = ty_cJSON_PrintUnformatted(root);
    OPERATE_RET rt = OPRT_MALLOC_FAILED;
    if (s) {
        rt = wukong_storage_write(MEM_NS, MEM_NAME, (CONST BYTE_T *)s, (UINT_T)strlen(s));
        ty_cJSON_FreeBuffer(s);
    }
    ty_cJSON_Delete(root);
    mem->dirty_access = FALSE;
    return rt;
}

/* Rebuild indexes.json (keyword -> ids) from the cache tags + persist. */
STATIC VOID_T __persist_indexes(MEM_CTX_T *mem)
{
    ty_cJSON *root = ty_cJSON_CreateObject();
    ty_cJSON *kw = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(root, "version", "1.0");
    ty_cJSON_AddNumberToObject(root, "next_id", (double)mem->next_id);
    for (UINT_T i = 0; i < mem->count; i++) {
        for (UINT_T k = 0; k < mem->entry[i].tag_count; k++) {
            ty_cJSON *arr = ty_cJSON_GetObjectItem(kw, mem->entry[i].tags[k]);
            if (!arr) {
                arr = ty_cJSON_CreateArray();
                ty_cJSON_AddItemToObject(kw, mem->entry[i].tags[k], arr);
            }
            ty_cJSON_AddItemToArray(arr, ty_cJSON_CreateString(mem->entry[i].id));
        }
    }
    ty_cJSON_AddItemToObject(root, "keyword", kw);
    CHAR_T *s = ty_cJSON_PrintUnformatted(root);
    if (s) {
        wukong_storage_write(MEM_NS, IDX_NAME, (CONST BYTE_T *)s, (UINT_T)strlen(s));
        ty_cJSON_FreeBuffer(s);
    }
    ty_cJSON_Delete(root);
}

/* Rewrite MEMORY.md: header + one line per memory, sorted by time ascending. */
STATIC VOID_T __persist_md(MEM_CTX_T *mem)
{
    UINT_T order[CLAW_FIXED_MEM_CEIL];
    for (UINT_T i = 0; i < mem->count; i++) {
        order[i] = i;
    }
    /* insertion sort by time ascending (count small, mem_max bound) */
    for (UINT_T i = 1; i < mem->count; i++) {
        UINT_T key = order[i];
        TIME_T kt = mem->entry[key].time;
        INT_T j = (INT_T)i - 1;
        while (j >= 0 && mem->entry[order[j]].time > kt) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
    SIZE_T cap = 96;
    for (UINT_T i = 0; i < mem->count; i++) {
        MEM_ENTRY_T *e = &mem->entry[i];
        cap += strlen(e->id) + (e->content ? strlen(e->content) : 0) + e->tag_count * CLAW_FIXED_MEM_TAG_MAX + 16;
    }
    CHAR_T *out = (CHAR_T *)tal_malloc(cap);
    if (!out) {
        return;
    }
    SIZE_T off = (SIZE_T)snprintf(out, cap, "# Long-term Memory\n_Last updated: %lld_\n",
                                  (long long)tal_time_get_posix());
    for (UINT_T i = 0; i < mem->count; i++) {
        MEM_ENTRY_T *e = &mem->entry[order[i]];
        off += (SIZE_T)snprintf(out + off, cap - off, "- **%s** [", e->id);
        for (UINT_T k = 0; k < e->tag_count; k++) {
            off += (SIZE_T)snprintf(out + off, cap - off, "%s%s", e->tags[k], (k + 1 < e->tag_count) ? "," : "");
        }
        off += (SIZE_T)snprintf(out + off, cap - off, "]: %s\n", e->content ? e->content : "");
    }
    wukong_storage_write(MEM_NS, MD_NAME, (CONST BYTE_T *)out, (UINT_T)strlen(out));
    tal_free(out);
}

/* Framing + usage rules for the "## Memory" prompt section (see the design
 * doc: dedup lives in the system prompt, tools stay pure). */
STATIC CONST CHAR_T c_mem_rules[] =
    "The user's long-term memory (keyword: ids). "
    "Save only durable facts the user states about themselves. "
    "If such a fact changes, memory_update its id instead of adding a "
    "duplicate; if the user asks to forget it, memory_delete. "
    "When the user is only asking or recalling, answer from the entries "
    "below and do not save. Persona/style/addressing changes go to "
    "profile_update, not memory.\n";

CHAR_T *wukong_memory_build_index(VOID_T)
{
    MEM_CTX_T *mem = s_mem;

    if (mem == NULL) {
        return NULL;
    }
    if (mem->count == 0) {
        /* Empty store: still inject the section header so the model knows the
         * memory system exists and is just empty right now — hiding the whole
         * section made the model doubt its own past memory tool calls. */
        return mm_strdup(c_mem_rules);
    }
    /* Build keyword->ids in a temp cJSON, then render "kw: id, id\n". */
    ty_cJSON *kw = ty_cJSON_CreateObject();
    for (UINT_T i = 0; i < mem->count; i++) {
        for (UINT_T k = 0; k < mem->entry[i].tag_count; k++) {
            ty_cJSON *arr = ty_cJSON_GetObjectItem(kw, mem->entry[i].tags[k]);
            if (!arr) {
                arr = ty_cJSON_CreateArray();
                ty_cJSON_AddItemToObject(kw, mem->entry[i].tags[k], arr);
            }
            ty_cJSON_AddItemToArray(arr, ty_cJSON_CreateString(mem->entry[i].id));
        }
    }
    /* size + render (base leaves room for the framing + usage rules) */
    SIZE_T cap = 512;
    for (ty_cJSON *e = kw->child; e; e = e->next) {
        cap += strlen(e->string) + 4;
        for (ty_cJSON *id = e->child; id; id = id->next) {
            cap += strlen(id->valuestring) + 2;
        }
    }
    CHAR_T *out = (CHAR_T *)tal_malloc(cap);
    if (!out) {
        ty_cJSON_Delete(kw);
        return NULL;
    }
    SIZE_T off = (SIZE_T)snprintf(out, cap, "%s", c_mem_rules);
    for (ty_cJSON *e = kw->child; e; e = e->next) {
        off += snprintf(out + off, cap - off, "%s: ", e->string);
        for (ty_cJSON *id = e->child; id; id = id->next) {
            off += snprintf(out + off, cap - off, "%s%s", id->valuestring, id->next ? ", " : "");
        }
        off += snprintf(out + off, cap - off, "\n");
    }
    ty_cJSON_Delete(kw);
    return out;
}

/* Locate a memory by id; -1 if not found. */
STATIC INT_T __find(MEM_CTX_T *mem, CONST CHAR_T *id)
{
    for (UINT_T i = 0; i < mem->count; i++) {
        if (strcmp(mem->entry[i].id, id) == 0) {
            return (INT_T)i;
        }
    }
    return -1;
}

OPERATE_RET wukong_memory_list(CHAR_T **out_json)
{
    MEM_CTX_T *mem = s_mem;

    if (!out_json) {
        return OPRT_INVALID_PARM;
    }
    if (mem == NULL) {
        return OPRT_COM_ERROR;
    }
    ty_cJSON *root = ty_cJSON_CreateObject();
    ty_cJSON *arr = ty_cJSON_CreateArray();
    ty_cJSON_AddBoolToObject(root, "success", TRUE);
    ty_cJSON_AddNumberToObject(root, "count", (double)mem->count);
    for (UINT_T i = 0; i < mem->count; i++) {
        MEM_ENTRY_T *e = &mem->entry[i];
        ty_cJSON *m = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(m, "id", e->id);
        ty_cJSON *ta = ty_cJSON_CreateArray();
        for (UINT_T k = 0; k < e->tag_count; k++) {
            ty_cJSON_AddItemToArray(ta, ty_cJSON_CreateString(e->tags[k]));
        }
        ty_cJSON_AddItemToObject(m, "tags", ta);
        ty_cJSON_AddItemToArray(arr, m);
    }
    ty_cJSON_AddItemToObject(root, "memories", arr);
    *out_json = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    return *out_json ? OPRT_OK : OPRT_MALLOC_FAILED;
}

OPERATE_RET wukong_memory_get(CONST CHAR_T *CONST *ids, UINT_T n, CHAR_T **out)
{
    MEM_CTX_T *mem = s_mem;

    if (!out) {
        return OPRT_INVALID_PARM;
    }
    if (mem == NULL) {
        return OPRT_COM_ERROR;
    }
    ty_cJSON *root = ty_cJSON_CreateObject();
    ty_cJSON *arr = ty_cJSON_CreateArray();
    ty_cJSON_AddBoolToObject(root, "success", TRUE);
    for (UINT_T i = 0; i < n; i++) {
        INT_T idx = ids[i] ? __find(mem, ids[i]) : -1;
        if (idx < 0) {
            continue;
        }
        mem->entry[idx].access++;              /* hit accumulates, flushed on next write */
        mem->dirty_access = TRUE;
        ty_cJSON *m = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(m, "id", mem->entry[idx].id);
        ty_cJSON_AddStringToObject(m, "content", mem->entry[idx].content ? mem->entry[idx].content : "");
        ty_cJSON_AddItemToArray(arr, m);
    }
    ty_cJSON_AddItemToObject(root, "memories", arr);
    *out = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    return *out ? OPRT_OK : OPRT_MALLOC_FAILED;
}

STATIC VOID_T __set_tags(MEM_ENTRY_T *e, CONST CHAR_T *CONST *tags, UINT_T ntag)
{
    e->tag_count = 0;
    for (UINT_T k = 0; k < ntag && e->tag_count < CLAW_FIXED_MEM_TAGS_MAX; k++) {
        if (tags[k] && tags[k][0]) {
            strncpy(e->tags[e->tag_count], tags[k], CLAW_FIXED_MEM_TAG_MAX - 1);
            e->tags[e->tag_count][CLAW_FIXED_MEM_TAG_MAX - 1] = '\0';
            e->tag_count++;
        }
    }
}

OPERATE_RET wukong_memory_save(CONST CHAR_T *content, CONST CHAR_T *CONST *tags,
                               UINT_T ntag, INT_T importance, CHAR_T **out_id)
{
    MEM_CTX_T *mem = s_mem;

    if (!content || !content[0] || !out_id) {
        return OPRT_INVALID_PARM;
    }
    *out_id = NULL;
    if (mem == NULL) {
        return OPRT_COM_ERROR;
    }
    /* Evict from cache to make room. Note: a failed persist below won't restore
     * the evicted entry (rare: only when the table is full AND the medium is
     * unavailable); it reappears from disk on the next boot. */
    if (mem->count >= (UINT_T)claw_config_get()->mem_max) {
        __evict_one(mem);
    }
    MEM_ENTRY_T tmp;                           /* built on the stack; committed only on write OK */
    memset(&tmp, 0, sizeof(tmp));
    tmp.content = mm_strdup(content);          /* over-length truncation: MVP leaves to tool layer */
    if (tmp.content == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    snprintf(tmp.id, CLAW_FIXED_MEM_ID_LEN, "mem_%04u", mem->next_id);
    tmp.time = tal_time_get_posix();
    tmp.importance = (importance >= 1 && importance <= 10) ? importance : 5;
    tmp.access = 0;
    __set_tags(&tmp, tags, ntag);
    /* Write the would-be table first; touch the cache only after it lands, so
     * a failed flush leaves the cache untouched (no false "saved"). */
    if (__persist_memories(mem, MEM_PEND_APPEND, -1, &tmp) != OPRT_OK) {
        tal_free(tmp.content);
        return OPRT_COM_ERROR;
    }
    mem->entry[mem->count++] = tmp;            /* commit: content ownership moves into the cache */
    mem->next_id++;
    __persist_indexes(mem);
    __persist_md(mem);
    *out_id = mm_strdup(tmp.id);
    return (*out_id != NULL) ? OPRT_OK : OPRT_MALLOC_FAILED;
}

OPERATE_RET wukong_memory_update(CONST CHAR_T *id, CONST ty_cJSON *u)
{
    MEM_CTX_T *mem = s_mem;

    if (!id || !u) {
        return OPRT_INVALID_PARM;
    }
    if (mem == NULL) {
        return OPRT_COM_ERROR;
    }
    INT_T idx = __find(mem, id);
    if (idx < 0) {
        return OPRT_NOT_FOUND;
    }
    MEM_ENTRY_T tmp = mem->entry[idx];  /* shadow copy of the entry being modified */
    CHAR_T *new_content = NULL;         /* set only when content changes */
    ty_cJSON *j;
    if ((j = ty_cJSON_GetObjectItem(u, "content"))) {
        new_content = mm_strdup(j->valuestring);
        if (new_content == NULL) {
            return OPRT_MALLOC_FAILED;  /* nothing changed */
        }
        tmp.content = new_content;
    }
    if ((j = ty_cJSON_GetObjectItem(u, "importance"))) {
        /* clamp to 1..10 like wukong_memory_save; the value is an LLM tool arg.
         * Out of range keeps the entry's current importance (partial update). */
        INT_T imp = j->valueint;
        tmp.importance = (imp >= 1 && imp <= 10) ? imp : tmp.importance;
    }
    BOOL_T tags_changed = FALSE;
    if ((j = ty_cJSON_GetObjectItem(u, "tags"))) {
        CONST CHAR_T *t2[CLAW_FIXED_MEM_TAGS_MAX];
        UINT_T nt = 0;
        for (ty_cJSON *t = j->child; t && nt < CLAW_FIXED_MEM_TAGS_MAX; t = t->next) {
            t2[nt++] = t->valuestring;
        }
        __set_tags(&tmp, t2, nt);
        tags_changed = TRUE;
    }
    tmp.time = tal_time_get_posix();
    /* Write the modified table first; commit to the cache only on success, so
     * a failed flush leaves the cache untouched (no false "updated"). */
    if (__persist_memories(mem, MEM_PEND_UPDATE, idx, &tmp) != OPRT_OK) {
        if (new_content) {
            tal_free(new_content);
        }
        return OPRT_COM_ERROR;
    }
    if (new_content) {
        tal_free(mem->entry[idx].content);  /* drop the old string */
    }
    mem->entry[idx] = tmp;                  /* commit */
    if (tags_changed) {
        __persist_indexes(mem);             /* skip index rebuild if tags unchanged */
    }
    __persist_md(mem);
    return OPRT_OK;
}

OPERATE_RET wukong_memory_delete(CONST CHAR_T *id)
{
    MEM_CTX_T *mem = s_mem;

    if (!id) {
        return OPRT_INVALID_PARM;
    }
    if (mem == NULL) {
        return OPRT_COM_ERROR;
    }
    INT_T idx = __find(mem, id);
    if (idx < 0) {
        return OPRT_OK;                 /* idempotent */
    }
    /* Write the table without this entry first; touch the cache only on OK, so
     * a failed flush leaves the cache untouched (no false "deleted"). */
    if (__persist_memories(mem, MEM_PEND_DELETE, idx, NULL) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }
    tal_free(mem->entry[idx].content);
    for (UINT_T i = (UINT_T)idx; i + 1 < mem->count; i++) {
        mem->entry[i] = mem->entry[i + 1];
    }
    mem->count--;
    __persist_indexes(mem);
    __persist_md(mem);
    return OPRT_OK;
}
