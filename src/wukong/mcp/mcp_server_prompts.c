/**
 * @file mcp_server_prompts.c
 * @brief MCP Prompts capability — registration, list, get
 * @version 3.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#include "mcp_server.h"

#if MCP_ENABLE_PROMPTS

#include "mcp_server_prompts.h"
#include "mcp_server_internal.h"

#include <string.h>

#include "tal_log.h"
#include "tal_memory.h"
#include "utilities/mix_method.h"

/* ========================================================================== */
/*                            Internal Types                                  */
/* ========================================================================== */

typedef struct MCP_PROMPT_ENTRY {
    CHAR_T *name;
    CHAR_T *description;
    ty_cJSON *args_json;
    MCP_PROMPT_GET_CB handler;
    VOID *user_data;
    struct MCP_PROMPT_ENTRY *next;
} MCP_PROMPT_ENTRY_T;

STATIC MCP_PROMPT_ENTRY_T *s_prompts = NULL;

/* ========================================================================== */
/*                          Args JSON Builder                                 */
/* ========================================================================== */

STATIC ty_cJSON *__build_prompt_args_json(CONST MCP_PROMPT_ARG_T *args)
{
    ty_cJSON *arr;

    if (!args || !args->name)
        return NULL;

    arr = ty_cJSON_CreateArray();
    if (!arr)
        return NULL;

    for (CONST MCP_PROMPT_ARG_T *a = args; a->name; a++) {
        ty_cJSON *obj = ty_cJSON_CreateObject();
        if (!obj)
            continue;
        ty_cJSON_AddStringToObject(obj, "name", a->name);
        if (a->description)
            ty_cJSON_AddStringToObject(obj, "description", a->description);
        ty_cJSON_AddBoolToObject(obj, "required", a->required);
        ty_cJSON_AddItemToArray(arr, obj);
    }
    return arr;
}

/* ========================================================================== */
/*                          Prompt Registration                               */
/* ========================================================================== */

OPERATE_RET mcp_server_prompt_add(CONST CHAR_T *name,
                                   CONST CHAR_T *description,
                                   CONST MCP_PROMPT_ARG_T *args,
                                   MCP_PROMPT_GET_CB handler,
                                   VOID *user_data)
{
    MCP_PROMPT_ENTRY_T *entry, *cur;

    if (!name || !handler)
        return OPRT_INVALID_PARM;

    for (cur = s_prompts; cur; cur = cur->next) {
        if (strcmp(cur->name, name) == 0) {
            TAL_PR_WARN("Prompt '%s' already registered", name);
            return OPRT_COM_ERROR;
        }
    }

    entry = (MCP_PROMPT_ENTRY_T *)tal_calloc(1, sizeof(*entry));
    if (!entry)
        return OPRT_MALLOC_FAILED;

    entry->name = mm_strdup(name);
    entry->description = description ? mm_strdup(description) : NULL;
    if (!entry->name)
        goto err;

    entry->handler = handler;
    entry->user_data = user_data;
    entry->args_json = __build_prompt_args_json(args);

    entry->next = s_prompts;
    s_prompts = entry;

    TAL_PR_INFO("Prompt registered: %s", name);
    return OPRT_OK;

err:
    if (entry) {
        tal_free(entry->name);
        tal_free(entry->description);
        if (entry->args_json)
            ty_cJSON_Delete(entry->args_json);
        tal_free(entry);
    }
    return OPRT_MALLOC_FAILED;
}

/* ========================================================================== */
/*                        Method: prompts/list                                */
/* ========================================================================== */

OPERATE_RET mcp_prompts_handle_list(CHAR_T *sid, CHAR_T *eid,
                                     ty_cJSON *params, CONST CHAR_T *id)
{
    ty_cJSON *result, *arr;
    MCP_PROMPT_ENTRY_T *prompt;

    result = ty_cJSON_CreateObject();
    arr = ty_cJSON_CreateArray();
    if (!result || !arr) {
        ty_cJSON_Delete(result);
        ty_cJSON_Delete(arr);
        return OPRT_MALLOC_FAILED;
    }

    for (prompt = s_prompts; prompt; prompt = prompt->next) {
        ty_cJSON *obj = ty_cJSON_CreateObject();
        if (!obj)
            continue;
        ty_cJSON_AddStringToObject(obj, "name", prompt->name);
        if (prompt->description)
            ty_cJSON_AddStringToObject(obj, "description", prompt->description);
        if (prompt->args_json)
            ty_cJSON_AddItemReferenceToObject(obj, "arguments", prompt->args_json);
        ty_cJSON_AddItemToArray(arr, obj);
    }

    ty_cJSON_AddItemToObject(result, "prompts", arr);
    return mcp_server_reply_result(sid, eid, id, result);
}

/* ========================================================================== */
/*                         Method: prompts/get                                */
/* ========================================================================== */

OPERATE_RET mcp_prompts_handle_get(CHAR_T *sid, CHAR_T *eid,
                                    ty_cJSON *params, CONST CHAR_T *id)
{
    ty_cJSON *name_j, *args_j;
    CONST CHAR_T *prompt_name;
    MCP_PROMPT_ENTRY_T *prompt;
    ty_cJSON *messages = NULL;
    ty_cJSON *result;
    OPERATE_RET rt;

    if (!ty_cJSON_IsObject(params))
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INVALID_PARAMS,
                                       "Missing params");

    name_j = ty_cJSON_GetObjectItem(params, "name");
    if (!ty_cJSON_IsString(name_j))
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INVALID_PARAMS,
                                       "Missing prompt name");
    prompt_name = name_j->valuestring;

    args_j = ty_cJSON_GetObjectItem(params, "arguments");

    for (prompt = s_prompts; prompt; prompt = prompt->next) {
        if (strcmp(prompt->name, prompt_name) == 0)
            break;
    }
    if (!prompt)
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INVALID_PARAMS,
                                       "Unknown prompt");

    rt = prompt->handler(prompt_name, args_j, &messages, prompt->user_data);
    if (rt != OPRT_OK || !messages) {
        if (messages)
            ty_cJSON_Delete(messages);
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INTERNAL,
                                       "Failed to get prompt");
    }

    result = ty_cJSON_CreateObject();
    if (!result) {
        ty_cJSON_Delete(messages);
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddItemToObject(result, "messages", messages);

    return mcp_server_reply_result(sid, eid, id, result);
}

/* ========================================================================== */
/*                      List-Changed Notification                             */
/* ========================================================================== */

OPERATE_RET mcp_server_notify_prompts_changed(VOID)
{
    return mcp_server_send_notification("notifications/prompts/list_changed", NULL);
}

/* ========================================================================== */
/*                         Capability Destroy                                 */
/* ========================================================================== */

VOID mcp_prompts_cap_destroy(VOID)
{
    MCP_PROMPT_ENTRY_T *prompt, *next;

    for (prompt = s_prompts; prompt; prompt = next) {
        next = prompt->next;
        tal_free(prompt->name);
        tal_free(prompt->description);
        if (prompt->args_json)
            ty_cJSON_Delete(prompt->args_json);
        tal_free(prompt);
    }
    s_prompts = NULL;
}

#endif /* MCP_ENABLE_PROMPTS */
