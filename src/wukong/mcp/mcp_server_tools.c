/**
 * @file mcp_server_tools.c
 * @brief MCP Tools capability — registration, list, call, async worker
 * @version 3.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#include "mcp_server.h"

#if MCP_ENABLE_TOOLS

#include "mcp_server_tools.h"
#include "mcp_server_internal.h"
#include "mcp_content.h"

#include <stdarg.h>
#include <string.h>

#include "tal_log.h"
#include "tal_memory.h"
#include "tal_workq_service.h"
#include "utilities/mix_method.h"

/* ========================================================================== */
/*                            Internal Types                                  */
/* ========================================================================== */

typedef struct MCP_TOOL_ENTRY {
    CHAR_T *name;
    CHAR_T *description;
    ty_cJSON *input_schema;
    MCP_TOOL_HANDLER_CB handler;
    VOID *user_data;
    struct MCP_TOOL_ENTRY *next;
} MCP_TOOL_ENTRY_T;

typedef struct {
    CHAR_T *sid;
    CHAR_T *eid;
    CHAR_T *id;
    ty_cJSON *arguments;
    MCP_TOOL_ENTRY_T *tool;
} TOOL_CALL_MSG_T;

STATIC MCP_TOOL_ENTRY_T *s_tools = NULL;

/* ========================================================================== */
/*                       Schema Builder (internal)                            */
/* ========================================================================== */

STATIC ty_cJSON *__build_input_schema(va_list ap)
{
    ty_cJSON *schema, *props, *required;
    MCP_SCHEMA_PROP_T *p;

    schema = ty_cJSON_CreateObject();
    if (!schema)
        return NULL;

    ty_cJSON_AddStringToObject(schema, "type", "object");

    props = ty_cJSON_CreateObject();
    required = ty_cJSON_CreateArray();
    if (!props || !required) {
        ty_cJSON_Delete(props);
        ty_cJSON_Delete(required);
        ty_cJSON_Delete(schema);
        return NULL;
    }

    while ((p = va_arg(ap, MCP_SCHEMA_PROP_T *)) != NULL) {
        ty_cJSON *prop_obj = ty_cJSON_CreateObject();
        if (!prop_obj)
            continue;

        ty_cJSON_AddStringToObject(prop_obj, "type", p->type);
        if (p->description)
            ty_cJSON_AddStringToObject(prop_obj, "description", p->description);
        if (p->has_minimum)
            ty_cJSON_AddNumberToObject(prop_obj, "minimum", p->minimum);
        if (p->has_maximum)
            ty_cJSON_AddNumberToObject(prop_obj, "maximum", p->maximum);

        ty_cJSON_AddItemToObject(props, p->name, prop_obj);

        if (p->required)
            ty_cJSON_AddItemToArray(required, ty_cJSON_CreateString(p->name));
    }

    ty_cJSON_AddItemToObject(schema, "properties", props);

    if (ty_cJSON_GetArraySize(required) > 0)
        ty_cJSON_AddItemToObject(schema, "required", required);
    else
        ty_cJSON_Delete(required);

    return schema;
}

/**
 * @brief Validate numeric arguments against schema minimum/maximum constraints
 */
STATIC OPERATE_RET __validate_schema_constraints(CONST MCP_TOOL_ENTRY_T *tool,
                                                  CONST ty_cJSON *arguments,
                                                  CHAR_T *err_msg, INT_T err_msg_size)
{
    ty_cJSON *props, *prop, *arg, *min_j, *max_j;

    if (!tool->input_schema || !arguments)
        return OPRT_OK;

    props = ty_cJSON_GetObjectItem(tool->input_schema, "properties");
    if (!props)
        return OPRT_OK;

    for (prop = props->child; prop; prop = prop->next) {
        arg = ty_cJSON_GetObjectItem(arguments, prop->string);
        if (!arg || !ty_cJSON_IsNumber(arg))
            continue;

        min_j = ty_cJSON_GetObjectItem(prop, "minimum");
        if (min_j && ty_cJSON_IsNumber(min_j) && arg->valuedouble < min_j->valuedouble) {
            snprintf(err_msg, err_msg_size, "Parameter '%s' value %g below minimum %g",
                     prop->string, arg->valuedouble, min_j->valuedouble);
            return OPRT_INVALID_PARM;
        }

        max_j = ty_cJSON_GetObjectItem(prop, "maximum");
        if (max_j && ty_cJSON_IsNumber(max_j) && arg->valuedouble > max_j->valuedouble) {
            snprintf(err_msg, err_msg_size, "Parameter '%s' value %g above maximum %g",
                     prop->string, arg->valuedouble, max_j->valuedouble);
            return OPRT_INVALID_PARM;
        }
    }

    return OPRT_OK;
}

/**
 * @brief Validate that all required arguments are present
 * @param[in] tool tool entry with input_schema
 * @param[in] arguments JSON object of call arguments (may be NULL)
 * @param[out] err_msg buffer to receive error description on failure
 * @param[in] err_msg_size size of err_msg buffer
 * @return OPRT_OK if all required params present, OPRT_INVALID_PARM otherwise
 */
STATIC OPERATE_RET __validate_required_args(CONST MCP_TOOL_ENTRY_T *tool,
                                             CONST ty_cJSON *arguments,
                                             CHAR_T *err_msg,
                                             INT_T err_msg_size)
{
    ty_cJSON *required;
    ty_cJSON *item;
    INT_T i, count, offset;

    if (!tool || !tool->input_schema) {
        return OPRT_OK;
    }

    required = ty_cJSON_GetObjectItem(tool->input_schema, "required");
    if (!ty_cJSON_IsArray(required)) {
        return OPRT_OK;
    }

    count = ty_cJSON_GetArraySize(required);
    if (count <= 0) {
        return OPRT_OK;
    }

    offset = snprintf(err_msg, err_msg_size, "Missing required parameter(s):");

    for (i = 0; i < count; i++) {
        item = ty_cJSON_GetArrayItem(required, i);
        if (!ty_cJSON_IsString(item)) {
            continue;
        }

        if (!arguments || !ty_cJSON_GetObjectItem(arguments, item->valuestring)) {
            if (offset < err_msg_size - 1) {
                offset += snprintf(err_msg + offset, err_msg_size - offset,
                                   " %s", item->valuestring);
            }
        }
    }

    if (offset == (INT_T)strlen("Missing required parameter(s):")) {
        return OPRT_OK;
    }

    return OPRT_INVALID_PARM;
}

/* ========================================================================== */
/*                           Tool Registration                                */
/* ========================================================================== */

OPERATE_RET mcp_server_tool_register(CONST CHAR_T *name,
                                      CONST CHAR_T *description,
                                      MCP_TOOL_HANDLER_CB handler,
                                      VOID *user_data, ...)
{
    MCP_TOOL_ENTRY_T *entry, *cur;
    va_list ap;

    if (!name || !description || !handler)
        return OPRT_INVALID_PARM;

    for (cur = s_tools; cur; cur = cur->next) {
        if (strcmp(cur->name, name) == 0) {
            TAL_PR_WARN("Tool '%s' already registered", name);
            return OPRT_COM_ERROR;
        }
    }

    entry = (MCP_TOOL_ENTRY_T *)tal_calloc(1, sizeof(*entry));
    if (!entry)
        return OPRT_MALLOC_FAILED;

    entry->name = mm_strdup(name);
    entry->description = mm_strdup(description);
    if (!entry->name || !entry->description)
        goto err;

    entry->handler = handler;
    entry->user_data = user_data;

    va_start(ap, user_data);
    entry->input_schema = __build_input_schema(ap);
    va_end(ap);

    if (!entry->input_schema)
        goto err;

    entry->next = s_tools;
    s_tools = entry;

    TAL_PR_INFO("Tool registered: %s", name);
    TAL_PR_INFO("Description: %s", entry->description);
    TAL_PR_INFO("Input schema: %s", ty_cJSON_PrintUnformatted(entry->input_schema));
    return OPRT_OK;

err:
    if (entry) {
        tal_free(entry->name);
        tal_free(entry->description);
        if (entry->input_schema)
            ty_cJSON_Delete(entry->input_schema);
        tal_free(entry);
    }
    return OPRT_MALLOC_FAILED;
}

/* ========================================================================== */
/*                         Method: tools/list                                 */
/* ========================================================================== */

OPERATE_RET mcp_tools_handle_list(CHAR_T *sid, CHAR_T *eid,
                                   ty_cJSON *params, CONST CHAR_T *id)
{
    ty_cJSON *result, *tools_arr;
    MCP_TOOL_ENTRY_T *tool;
    CONST CHAR_T *cursor_str = NULL;
    BOOL_T found_cursor = FALSE;
    INT_T payload_len = 0;

    if (params) {
        ty_cJSON *c = ty_cJSON_GetObjectItem(params, "cursor");
        if (ty_cJSON_IsString(c))
            cursor_str = c->valuestring;
    }

    result = ty_cJSON_CreateObject();
    tools_arr = ty_cJSON_CreateArray();
    if (!result || !tools_arr) {
        ty_cJSON_Delete(result);
        ty_cJSON_Delete(tools_arr);
        return OPRT_MALLOC_FAILED;
    }

    found_cursor = (cursor_str == NULL || cursor_str[0] == '\0');

    for (tool = s_tools; tool; tool = tool->next) {
        ty_cJSON *tj;
        CHAR_T *ts;
        INT_T tl;

        if (!found_cursor) {
            if (strcmp(tool->name, cursor_str) == 0)
                found_cursor = TRUE;
            else
                continue;
        }

        tj = ty_cJSON_CreateObject();
        if (!tj)
            continue;

        ty_cJSON_AddStringToObject(tj, "name", tool->name);
        ty_cJSON_AddStringToObject(tj, "description", tool->description);
        ty_cJSON_AddItemReferenceToObject(tj, "inputSchema", tool->input_schema);

        ts = ty_cJSON_PrintUnformatted(tj);
        if (ts) {
            tl = (INT_T)strlen(ts);
            if (payload_len + tl + 128 > MCP_MAX_PAYLOAD_SIZE) {
                ty_cJSON_AddStringToObject(result, "nextCursor", tool->name);
                ty_cJSON_FreeBuffer(ts);
                ty_cJSON_Delete(tj);
                break;
            }
            payload_len += tl;
            ty_cJSON_FreeBuffer(ts);
        }

        ty_cJSON_AddItemToArray(tools_arr, tj);
    }

    ty_cJSON_AddItemToObject(result, "tools", tools_arr);
    return mcp_server_reply_result(sid, eid, id, result);
}

/* ========================================================================== */
/*                         Method: tools/call                                 */
/* ========================================================================== */

STATIC VOID __tool_call_worker(VOID_T *data)
{
    TOOL_CALL_MSG_T *msg = (TOOL_CALL_MSG_T *)data;
    ty_cJSON *content = NULL;
    BOOL_T is_error = FALSE;
    OPERATE_RET rt;
    ty_cJSON *result;

    if (!msg || !msg->tool) {
        TAL_PR_ERR("Invalid tool call message");
        goto cleanup;
    }

    rt = msg->tool->handler(msg->tool->name, msg->arguments,
                            &content, &is_error, msg->tool->user_data);

    result = ty_cJSON_CreateObject();
    if (!result) {
        TAL_PR_ERR("Failed to create result object");
        goto cleanup;
    }

    if (rt != OPRT_OK && !content) {
        content = ty_cJSON_CreateArray();
        if (content)
            ty_cJSON_AddItemToArray(content,
                mcp_content_make_text("Tool execution failed"));
        is_error = TRUE;
    }

    if (content)
        ty_cJSON_AddItemToObject(result, "content", content);
    else
        ty_cJSON_AddItemToObject(result, "content", ty_cJSON_CreateArray());

    if (is_error)
        ty_cJSON_AddBoolToObject(result, "isError", TRUE);

    mcp_server_reply_result(msg->sid, msg->eid, msg->id, result);

    TAL_PR_NOTICE("mcp tool finished name=%s is_error=%d eid=%s", msg->tool->name, is_error, msg->eid ? msg->eid : "");

cleanup:
    if (msg) {
        tal_free(msg->sid);
        tal_free(msg->eid);
        tal_free(msg->id);
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
    MCP_TOOL_ENTRY_T *tool;
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

    for (tool = s_tools; tool; tool = tool->next) {
        if (strcmp(tool->name, tool_name) == 0)
            break;
    }
    if (!tool)
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INVALID_PARAMS,
                                       "Unknown tool");

    {
        CHAR_T err_msg[256];
        if (__validate_required_args(tool, args_j, err_msg, sizeof(err_msg)) != OPRT_OK) {
            return mcp_server_reply_error(sid, eid, id, MCP_ERR_INVALID_PARAMS, err_msg);
        }
        if (__validate_schema_constraints(tool, args_j, err_msg, sizeof(err_msg)) != OPRT_OK) {
            return mcp_server_reply_error(sid, eid, id, MCP_ERR_INVALID_PARAMS, err_msg);
        }
    }

    msg = (TOOL_CALL_MSG_T *)tal_calloc(1, sizeof(*msg));
    if (!msg)
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INTERNAL,
                                       "Allocation failed");

    msg->sid = mm_strdup(sid);
    msg->eid = mm_strdup(eid);
    msg->id = mm_strdup(id);
    if (!msg->sid || !msg->eid || !msg->id)
        goto alloc_err;

    msg->arguments = args_j ? ty_cJSON_Duplicate(args_j, 1) : NULL;
    msg->tool = tool;

    rt = tal_workq_schedule(WORKQ_SYSTEM, __tool_call_worker, msg);
    if (rt != OPRT_OK)
        goto alloc_err;

    return OPRT_OK;

alloc_err:
    tal_free(msg->sid);
    tal_free(msg->eid);
    tal_free(msg->id);
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

/* ========================================================================== */
/*                         Capability Destroy                                 */
/* ========================================================================== */

VOID mcp_tools_cap_destroy(VOID)
{
    MCP_TOOL_ENTRY_T *tool, *next;

    for (tool = s_tools; tool; tool = next) {
        next = tool->next;
        tal_free(tool->name);
        tal_free(tool->description);
        ty_cJSON_Delete(tool->input_schema);
        tal_free(tool);
    }
    s_tools = NULL;
}

#endif /* MCP_ENABLE_TOOLS */
