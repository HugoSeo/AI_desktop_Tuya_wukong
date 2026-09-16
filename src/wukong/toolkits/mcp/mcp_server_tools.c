/**
 * @file mcp_server_tools.c
 * @brief MCP Tools capability — JSON-RPC transport (list/call handlers, async worker, notify)
 * @version 3.0.0
 *
 * The tool registry, schema validation, content helpers and the single exec
 * entry live in toolkits/wukong_tool.c (neutral, not gated by MCP_ENABLE_TOOLS).
 * This file only frames tools/list and tools/call over JSON-RPC and schedules
 * the exec on the workq, mapping (return code x text) onto the MCP reply.
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#include "mcp_server.h"

#if MCP_ENABLE_TOOLS

#include "mcp_server_tools.h"
#include "mcp_server_internal.h"

#include <stdio.h>
#include <string.h>

#include "tal_log.h"
#include "tal_memory.h"
#include "tal_workq_service.h"
#include "utilities/mix_method.h"

/* ========================================================================== */
/*                            Internal Types                                  */
/* ========================================================================== */

/* The worker no longer holds a registry pointer across the workq — it carries
 * the tool name and re-resolves it through wukong_tool_exec on the worker. */
typedef struct {
    CHAR_T *sid;
    CHAR_T *eid;
    CHAR_T *id;
    CHAR_T *name;
    ty_cJSON *arguments;
} TOOL_CALL_MSG_T;

/* ========================================================================== */
/*                         Method: tools/list                                 */
/* ========================================================================== */

OPERATE_RET mcp_tools_handle_list(CHAR_T *sid, CHAR_T *eid,
                                   ty_cJSON *params, CONST CHAR_T *id)
{
    ty_cJSON *result, *tools_arr;
    CONST CHAR_T *cursor_str = NULL;
    CONST CHAR_T *next = NULL;

    if (params) {
        ty_cJSON *c = ty_cJSON_GetObjectItem(params, "cursor");
        if (ty_cJSON_IsString(c))
            cursor_str = c->valuestring;
    }

    result = ty_cJSON_CreateObject();
    if (!result) {
        return OPRT_MALLOC_FAILED;
    }

    tools_arr = wukong_tool_collect(WUKONG_TOOL_MCP, cursor_str,
                                    MCP_MAX_PAYLOAD_SIZE, &next);
    if (!tools_arr) {
        ty_cJSON_Delete(result);
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddItemToObject(result, "tools", tools_arr);
    if (next)
        ty_cJSON_AddStringToObject(result, "nextCursor", next);
    return mcp_server_reply_result(sid, eid, id, result);
}

/* ========================================================================== */
/*                         Method: tools/call                                 */
/* ========================================================================== */

STATIC VOID __tool_call_worker(VOID_T *data)
{
    TOOL_CALL_MSG_T *msg = (TOOL_CALL_MSG_T *)data;
    ty_cJSON *result = NULL;
    CHAR_T *text = NULL;
    OPERATE_RET rt;

    if (!msg || !msg->name) {
        TAL_PR_ERR("Invalid tool call message");
        goto cleanup;
    }

    rt = wukong_tool_exec(WUKONG_TOOL_MCP, msg->name, msg->arguments, &text);

    result = ty_cJSON_CreateObject();
    if (!result) {
        TAL_PR_ERR("Failed to create result object");
        goto cleanup;
    }

    if (text != NULL) {
        /* exec already serialized the content array for MCP; splice verbatim. */
        ty_cJSON_AddItemToObject(result, "content", ty_cJSON_CreateRaw(text));
        tal_free(text);
    } else {
        /* No content from the handler → the shell fabricates the reply text. */
        ty_cJSON *arr = ty_cJSON_CreateArray();
        CHAR_T buf[64];
        (VOID)snprintf(buf, sizeof(buf), "Tool execution failed (%d)", rt);
        ty_cJSON_AddItemToArray(arr, wukong_tool_make_text(rt == OPRT_OK ? "" : buf));
        ty_cJSON_AddItemToObject(result, "content", arr);
    }

    if (rt != OPRT_OK)
        ty_cJSON_AddBoolToObject(result, "isError", TRUE);

    mcp_server_reply_result(msg->sid, msg->eid, msg->id, result);

    TAL_PR_NOTICE("mcp tool finished name=%s rt=%d eid=%s",
                  msg->name, rt, msg->eid ? msg->eid : "");

cleanup:
    if (msg) {
        tal_free(msg->sid);
        tal_free(msg->eid);
        tal_free(msg->id);
        tal_free(msg->name);
        if (msg->arguments)
            ty_cJSON_Delete(msg->arguments);
        tal_free(msg);
    }
}

OPERATE_RET mcp_tools_handle_call(CHAR_T *sid, CHAR_T *eid,
                                   ty_cJSON *params, CONST CHAR_T *id)
{
    ty_cJSON *name_j, *args_j;
    CONST CHAR_T *tool_name;
    TOOL_CALL_MSG_T *msg;
    OPERATE_RET rt;

    if (!ty_cJSON_IsObject(params))
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INVALID_PARAMS,
                                       "Missing params");

    name_j = ty_cJSON_GetObjectItem(params, "name");
    if (!ty_cJSON_IsString(name_j))
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INVALID_PARAMS,
                                       "Missing tool name");
    tool_name = name_j->valuestring;

    args_j = ty_cJSON_GetObjectItem(params, "arguments");
    if (args_j && !ty_cJSON_IsObject(args_j))
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INVALID_PARAMS,
                                       "Invalid arguments");

    msg = (TOOL_CALL_MSG_T *)tal_calloc(1, sizeof(*msg));
    if (!msg)
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INTERNAL,
                                       "Allocation failed");

    msg->sid = mm_strdup(sid);
    msg->eid = mm_strdup(eid);
    msg->id = mm_strdup(id);
    msg->name = mm_strdup(tool_name);
    if (!msg->sid || !msg->eid || !msg->id || !msg->name)
        goto alloc_err;

    msg->arguments = args_j ? ty_cJSON_Duplicate(args_j, 1) : NULL;

    rt = tal_workq_schedule(WORKQ_SYSTEM, __tool_call_worker, msg);
    if (rt != OPRT_OK)
        goto alloc_err;

    return OPRT_OK;

alloc_err:
    tal_free(msg->sid);
    tal_free(msg->eid);
    tal_free(msg->id);
    tal_free(msg->name);
    if (msg->arguments)
        ty_cJSON_Delete(msg->arguments);
    tal_free(msg);
    return mcp_server_reply_error(sid, eid, id, MCP_ERR_INTERNAL,
                                   "Failed to schedule tool call");
}

/* ========================================================================== */
/*                      List-Changed Notification                             */
/* ========================================================================== */

OPERATE_RET mcp_server_notify_tools_changed(VOID)
{
    return mcp_server_send_notification("notifications/tools/list_changed", NULL);
}

#endif /* MCP_ENABLE_TOOLS */
