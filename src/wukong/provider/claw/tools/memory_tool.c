/**
 * @file memory_tool.c
 * @brief MCP tools: long-term memory — memory_get/save/update/delete, calling
 *        into the wukong_memory module (provider/claw/memory).
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */
#include "memory_tool.h"
#include "wukong_tool.h"
#include "wukong_memory.h"
#include "claw_config.h"
#include "tal_memory.h"
#include <stdio.h>
#include <string.h>

/* Wrap one text string as an MCP tool content array. */
STATIC OPERATE_RET __make_text(CONST CHAR_T *text, ty_cJSON **out)
{
    if (out == NULL) return OPRT_INVALID_PARM;
    *out = ty_cJSON_CreateArray();
    if (*out == NULL) return OPRT_MALLOC_FAILED;
    ty_cJSON_AddItemToArray(*out, wukong_tool_make_text(text));
    return OPRT_OK;
}

/* memory_get: args {ids:[...]} -> wukong_memory_get -> content JSON (already
 * a JSON string produced by the module, passed straight through as text). */
STATIC OPERATE_RET __get(CONST CHAR_T *name, CONST ty_cJSON *args,
                         ty_cJSON **out_content,
                         VOID *user_data)
{
    /* "ids" is declared "array" in the schema — string-encoded input is
     * promoted to a real array by wukong_tool_exec before the handler runs. */
    ty_cJSON *arr = args ? ty_cJSON_GetObjectItem(args, "ids") : NULL;
    CONST CHAR_T *ids[16];
    UINT_T n = 0;
    CHAR_T *js = NULL;
    OPERATE_RET rt;

    (VOID)name;
    (VOID)user_data;

    if (ty_cJSON_IsArray(arr)) {
        ty_cJSON *t;
        for (t = arr->child; t != NULL && n < 16; t = t->next) {
            if (ty_cJSON_IsString(t)) ids[n++] = t->valuestring;
        }
    }

    rt = wukong_memory_get(ids, n, &js);

    if (rt != OPRT_OK || js == NULL) {
        (VOID_T)__make_text("{\"success\":false}", out_content);
        return OPRT_COM_ERROR;
    }
    rt = __make_text(js, out_content);
    tal_free(js);
    return rt;
}

/* memory_save: args {content, tags:[...], importance?} -> wukong_memory_save. */
STATIC OPERATE_RET __save(CONST CHAR_T *name, CONST ty_cJSON *args,
                          ty_cJSON **out_content,
                          VOID *user_data)
{
    ty_cJSON *cj = args ? ty_cJSON_GetObjectItem(args, "content") : NULL;
    ty_cJSON *tj = args ? ty_cJSON_GetObjectItem(args, "tags") : NULL;
    ty_cJSON *ij = args ? ty_cJSON_GetObjectItem(args, "importance") : NULL;
    CHAR_T content_buf[CLAW_FIXED_MEM_CONTENT_MAX];
    CHAR_T tag_buf[CLAW_FIXED_MEM_TAGS_MAX][CLAW_FIXED_MEM_TAG_MAX];
    CONST CHAR_T *tags[CLAW_FIXED_MEM_TAGS_MAX];
    UINT_T ntag = 0;
    INT_T importance = 5;
    ty_cJSON *arr = NULL;
    CHAR_T *out_id = NULL;
    OPERATE_RET rt;

    (VOID)name;
    (VOID)user_data;

    content_buf[0] = '\0';
    if (cj != NULL && ty_cJSON_IsString(cj) && cj->valuestring != NULL) {
        /* Cap the length passed to the module even if the model sends more
         * than CLAW_FIXED_MEM_CONTENT_MAX (do not mutate the caller's args cJSON). */
        strncpy(content_buf, cj->valuestring, CLAW_FIXED_MEM_CONTENT_MAX - 1);
        content_buf[CLAW_FIXED_MEM_CONTENT_MAX - 1] = '\0';
    }

    /* "tags" is declared "array" — exec promotes string-encoded input. */
    if (ty_cJSON_IsArray(tj)) {
        arr = tj;
    }
    if (arr != NULL) {
        ty_cJSON *t;
        for (t = arr->child; t != NULL && ntag < CLAW_FIXED_MEM_TAGS_MAX; t = t->next) {
            if (ty_cJSON_IsString(t) && t->valuestring != NULL) {
                strncpy(tag_buf[ntag], t->valuestring, CLAW_FIXED_MEM_TAG_MAX - 1);
                tag_buf[ntag][CLAW_FIXED_MEM_TAG_MAX - 1] = '\0';
                tags[ntag] = tag_buf[ntag];
                ntag++;
            }
        }
    }
    if (ij != NULL && ty_cJSON_IsNumber(ij)) importance = ij->valueint;

    rt = wukong_memory_save(content_buf, tags, ntag, importance, &out_id);
    if (rt != OPRT_OK || out_id == NULL) {
        /* Distinguish a failed flush (no SD / write error) so the model tells
         * the user it was not persisted rather than falsely confirming. */
        (VOID_T)__make_text("{\"success\":false,\"reason\":\"not saved\"}", out_content);
        return OPRT_COM_ERROR;
    }
    {
        CHAR_T buf[CLAW_FIXED_MEM_ID_LEN + 32];
        (VOID)snprintf(buf, sizeof(buf), "{\"success\":true,\"id\":\"%s\"}", out_id);
        tal_free(out_id);
        return __make_text(buf, out_content);
    }
}

/* memory_update: args {id, updates:{...}} -> wukong_memory_update(id, updates). */
STATIC OPERATE_RET __update(CONST CHAR_T *name, CONST ty_cJSON *args,
                            ty_cJSON **out_content,
                            VOID *user_data)
{
    ty_cJSON *idj = args ? ty_cJSON_GetObjectItem(args, "id") : NULL;
    ty_cJSON *uj = args ? ty_cJSON_GetObjectItem(args, "updates") : NULL;
    CONST CHAR_T *id = NULL;
    ty_cJSON *updates = NULL;
    OPERATE_RET rt;

    (VOID)name;
    (VOID)user_data;

    if (idj != NULL && ty_cJSON_IsString(idj)) id = idj->valuestring;

    /* "updates" is declared "object" — exec promotes string-encoded input. */
    if (ty_cJSON_IsObject(uj)) {
        updates = uj;
    }

    if (id == NULL || updates == NULL) {
        (VOID_T)__make_text("{\"success\":false}", out_content);
        return OPRT_COM_ERROR;
    }

    rt = wukong_memory_update(id, updates);

    if (rt == OPRT_NOT_FOUND) {
        (VOID_T)__make_text("{\"success\":false,\"reason\":\"not found\"}", out_content);
        return OPRT_NOT_FOUND;
    }
    if (rt != OPRT_OK) {
        (VOID_T)__make_text("{\"success\":false}", out_content);
        return OPRT_COM_ERROR;
    }
    return __make_text("{\"success\":true}", out_content);
}

/* memory_delete: args {id} -> wukong_memory_delete. */
STATIC OPERATE_RET __delete(CONST CHAR_T *name, CONST ty_cJSON *args,
                            ty_cJSON **out_content,
                            VOID *user_data)
{
    ty_cJSON *idj = args ? ty_cJSON_GetObjectItem(args, "id") : NULL;
    CONST CHAR_T *id = (idj != NULL && ty_cJSON_IsString(idj)) ? idj->valuestring : NULL;

    (VOID)name;
    (VOID)user_data;

    if (id == NULL || wukong_memory_delete(id) != OPRT_OK) {
        (VOID_T)__make_text("{\"success\":false}", out_content);
        return OPRT_COM_ERROR;
    }
    return __make_text("{\"success\":true}", out_content);
}

/* memory_list: no args -> wukong_memory_list (browse everything). */
STATIC OPERATE_RET __list(CONST CHAR_T *name, CONST ty_cJSON *args,
                          ty_cJSON **out_content,
                          VOID *user_data)
{
    CHAR_T *js = NULL;

    (VOID)name;
    (VOID)args;
    (VOID)user_data;

    if (wukong_memory_list(&js) != OPRT_OK || js == NULL) {
        (VOID_T)__make_text("{\"success\":false}", out_content);
        return OPRT_COM_ERROR;
    }
    OPERATE_RET rt = __make_text(js, out_content);
    tal_free(js);
    return rt;
}

OPERATE_RET memory_tool_init(VOID)
{
    OPERATE_RET rt;

    rt = WUKONG_TOOL_ADD(
        "memory_get",
        "Recall long-term memory content by ids (from the ## Memory index).",
        __get, NULL, WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_ARRAY("ids", "Memory ids to recall, e.g. \"ids\": [\"mem_0001\"]."));
    if (rt != OPRT_OK) return rt;

    rt = WUKONG_TOOL_ADD(
        "memory_list",
        "List the index of stored long-term memories (id + tags). Use "
        "memory_get with the ids to read the content.",
        __list, NULL, WUKONG_TOOL_AGENT);
    if (rt != OPRT_OK) return rt;

    rt = WUKONG_TOOL_ADD(
        "memory_save",
        "Save a NEW long-term memory about the user.",
        __save, NULL, WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR("content", "The memory text (<=256 chars)."),
        TOOL_SCHEMA_ARRAY("tags", "1-3 keyword tags, e.g. \"tags\": [\"keyword1\",\"keyword2\"]."),
        TOOL_SCHEMA_INT_OPT_RANGE("importance", "1-10, default 5.", 1, 10));
    if (rt != OPRT_OK) return rt;

    rt = WUKONG_TOOL_ADD(
        "memory_update",
        "Update an existing memory by id (partial: content/tags/importance).",
        __update, NULL, WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR("id", "The memory id."),
        TOOL_SCHEMA_OBJ("updates", "Object with any of content/tags/importance, e.g. "
                                   "\"updates\": {\"tags\": [\"keyword1\",\"keyword2\"]}."));
    if (rt != OPRT_OK) return rt;

    rt = WUKONG_TOOL_ADD(
        "memory_delete",
        "Delete a memory by id.",
        __delete, NULL, WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR("id", "The memory id."));
    return rt;
}
