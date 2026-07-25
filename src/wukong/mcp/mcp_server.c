/**
 * @file mcp_server.c
 * @brief MCP Server core — router, transport, lifecycle, initialize, ping
 * @version 3.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#include "mcp_server_internal.h"

#include <string.h>

#include "tal_log.h"
#include "tal_memory.h"
#include "tuya_ai_agent.h"
#include "utilities/mix_method.h"

/* ========================================================================== */
/*                            Internal State                                  */
/* ========================================================================== */

typedef struct {
    BOOL_T initialized;
    CHAR_T *name;
    CHAR_T *version;
    CHAR_T *session_sid;
    CHAR_T *session_eid;
} MCP_SERVER_CTX_T;

STATIC MCP_SERVER_CTX_T s_ctx;

/* ========================================================================== */
/*                            Transport Layer                                 */
/* ========================================================================== */

STATIC VOID __send_message(CHAR_T *sid, CHAR_T *eid, CONST CHAR_T *message)
{
    tuya_ai_agent_mcp_response(sid, eid, (CHAR_T *)message);
}

OPERATE_RET mcp_server_reply_result(CHAR_T *sid, CHAR_T *eid,
                                     CONST CHAR_T *id, ty_cJSON *result)
{
    ty_cJSON *response;
    CHAR_T *json_str;

    if (!id)
        return OPRT_INVALID_PARM;

    response = ty_cJSON_CreateObject();
    if (!response) {
        ty_cJSON_Delete(result);
        return OPRT_MALLOC_FAILED;
    }

    ty_cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    ty_cJSON_AddStringToObject(response, "id", id);
    if (result)
        ty_cJSON_AddItemToObject(response, "result", result);
    else
        ty_cJSON_AddItemToObject(response, "result", ty_cJSON_CreateObject());

    json_str = ty_cJSON_PrintUnformatted(response);
    if (json_str) {
        TAL_PR_DEBUG("MCP -> [%d] %s", strlen(json_str), json_str);
        __send_message(sid, eid, json_str);
        ty_cJSON_FreeBuffer(json_str);
    }

    ty_cJSON_Delete(response);
    return OPRT_OK;
}

OPERATE_RET mcp_server_reply_error(CHAR_T *sid, CHAR_T *eid,
                                    CONST CHAR_T *id, INT_T code,
                                    CONST CHAR_T *msg)
{
    ty_cJSON *response, *error;
    CHAR_T *json_str;

    response = ty_cJSON_CreateObject();
    if (!response)
        return OPRT_MALLOC_FAILED;

    ty_cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    if (id)
        ty_cJSON_AddStringToObject(response, "id", id);
    else
        ty_cJSON_AddNullToObject(response, "id");

    error = ty_cJSON_CreateObject();
    if (!error) {
        ty_cJSON_Delete(response);
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddNumberToObject(error, "code", code);
    ty_cJSON_AddStringToObject(error, "message", msg ? msg : "Unknown error");
    ty_cJSON_AddItemToObject(response, "error", error);

    json_str = ty_cJSON_PrintUnformatted(response);
    if (json_str) {
        TAL_PR_DEBUG("MCP error -> %s", json_str);
        __send_message(sid, eid, json_str);
        ty_cJSON_FreeBuffer(json_str);
    }

    ty_cJSON_Delete(response);
    return OPRT_OK;
}

OPERATE_RET mcp_server_send_notification(CONST CHAR_T *method, ty_cJSON *params)
{
    ty_cJSON *notif;
    CHAR_T *json_str;

    if (!s_ctx.initialized || !s_ctx.session_sid || !s_ctx.session_eid)
        return OPRT_COM_ERROR;

    notif = ty_cJSON_CreateObject();
    if (!notif) {
        if (params)
            ty_cJSON_Delete(params);
        return OPRT_MALLOC_FAILED;
    }

    ty_cJSON_AddStringToObject(notif, "jsonrpc", "2.0");
    ty_cJSON_AddStringToObject(notif, "method", method);
    if (params)
        ty_cJSON_AddItemToObject(notif, "params", params);

    json_str = ty_cJSON_PrintUnformatted(notif);
    if (json_str) {
        TAL_PR_DEBUG("MCP notif -> %s", json_str);
        __send_message(s_ctx.session_sid, s_ctx.session_eid, json_str);
        ty_cJSON_FreeBuffer(json_str);
    }

    ty_cJSON_Delete(notif);
    return OPRT_OK;
}

/* ========================================================================== */
/*                         Method: initialize                                 */
/* ========================================================================== */

STATIC OPERATE_RET __handle_initialize(CHAR_T *sid, CHAR_T *eid,
                                        ty_cJSON *params, CONST CHAR_T *id)
{
    ty_cJSON *result, *caps, *server_info, *obj;

    result = ty_cJSON_CreateObject();
    if (!result)
        return OPRT_MALLOC_FAILED;

    ty_cJSON_AddStringToObject(result, "protocolVersion", MCP_PROTOCOL_VERSION);

    caps = ty_cJSON_CreateObject();
    if (caps) {
#if MCP_ENABLE_TOOLS
        obj = ty_cJSON_CreateObject();
        if (obj) {
            ty_cJSON_AddBoolToObject(obj, "listChanged", TRUE);
            ty_cJSON_AddItemToObject(caps, "tools", obj);
        }
#endif

#if MCP_ENABLE_RESOURCES
        obj = ty_cJSON_CreateObject();
        if (obj) {
            ty_cJSON_AddBoolToObject(obj, "subscribe", TRUE);
            ty_cJSON_AddBoolToObject(obj, "listChanged", TRUE);
            ty_cJSON_AddItemToObject(caps, "resources", obj);
        }
#endif

#if MCP_ENABLE_PROMPTS
        obj = ty_cJSON_CreateObject();
        if (obj) {
            ty_cJSON_AddBoolToObject(obj, "listChanged", TRUE);
            ty_cJSON_AddItemToObject(caps, "prompts", obj);
        }
#endif

#if MCP_ENABLE_LOGGING
        ty_cJSON_AddItemToObject(caps, "logging", ty_cJSON_CreateObject());
#endif

        ty_cJSON_AddItemToObject(result, "capabilities", caps);
    }

    server_info = ty_cJSON_CreateObject();
    if (server_info) {
        ty_cJSON_AddStringToObject(server_info, "name", s_ctx.name);
        ty_cJSON_AddStringToObject(server_info, "version", s_ctx.version);
        ty_cJSON_AddItemToObject(result, "serverInfo", server_info);
    }

    tal_free(s_ctx.session_sid);
    tal_free(s_ctx.session_eid);
    s_ctx.session_sid = mm_strdup(sid);
    s_ctx.session_eid = mm_strdup(eid);

    return mcp_server_reply_result(sid, eid, id, result);
}

/* ========================================================================== */
/*                           Method: ping                                     */
/* ========================================================================== */

STATIC OPERATE_RET __handle_ping(CHAR_T *sid, CHAR_T *eid,
                                  ty_cJSON *params, CONST CHAR_T *id)
{
    return mcp_server_reply_result(sid, eid, id, ty_cJSON_CreateObject());
}

/* ========================================================================== */
/*                         JSON-RPC Router                                    */
/* ========================================================================== */

typedef struct {
    CONST CHAR_T *method;
    MCP_METHOD_HANDLER_FN handler;
    BOOL_T requires_id;
} METHOD_ROUTE_T;

STATIC CONST METHOD_ROUTE_T s_routes[] = {
    { "initialize",                __handle_initialize,                  TRUE  },
    { "ping",                      __handle_ping,                        TRUE  },
#if MCP_ENABLE_TOOLS
    { "tools/list",                mcp_tools_handle_list,                TRUE  },
    { "tools/call",                mcp_tools_handle_call,                TRUE  },
#endif
#if MCP_ENABLE_RESOURCES
    { "resources/list",            mcp_resources_handle_list,            TRUE  },
    { "resources/read",            mcp_resources_handle_read,            TRUE  },
    { "resources/templates/list",  mcp_resources_handle_templates_list,  TRUE  },
    { "resources/subscribe",       mcp_resources_handle_subscribe,       TRUE  },
    { "resources/unsubscribe",     mcp_resources_handle_unsubscribe,     TRUE  },
#endif
#if MCP_ENABLE_PROMPTS
    { "prompts/list",              mcp_prompts_handle_list,              TRUE  },
    { "prompts/get",               mcp_prompts_handle_get,               TRUE  },
#endif
#if MCP_ENABLE_LOGGING
    { "logging/setLevel",          mcp_logging_handle_set_level,         TRUE  },
#endif
};

#define NUM_ROUTES  (sizeof(s_routes) / sizeof(s_routes[0]))

OPERATE_RET mcp_server_handle_message(CHAR_T *sid, CHAR_T *eid,
                                       CONST ty_cJSON *msg, VOID *user_data)
{
    ty_cJSON *node;
    CONST CHAR_T *method;
    CONST CHAR_T *id_str = NULL;
    ty_cJSON *params;

    (VOID)user_data;

    if (!s_ctx.initialized || !msg)
        return OPRT_INVALID_PARM;

    node = ty_cJSON_GetObjectItem(msg, "jsonrpc");
    if (!node || !ty_cJSON_IsString(node) || strcmp(node->valuestring, "2.0") != 0) {
        TAL_PR_ERR("Invalid JSON-RPC version");
        return mcp_server_reply_error(sid, eid, NULL, MCP_ERR_INVALID_REQUEST,
                                       "Invalid JSON-RPC version");
    }

    node = ty_cJSON_GetObjectItem(msg, "method");
    if (!node || !ty_cJSON_IsString(node)) {
        TAL_PR_ERR("Missing method");
        return mcp_server_reply_error(sid, eid, NULL, MCP_ERR_INVALID_REQUEST,
                                       "Missing method");
    }
    method = node->valuestring;

    if (strncmp(method, "notifications/", 14) == 0) {
        if (strcmp(method, "notifications/initialized") == 0)
            TAL_PR_INFO("Client initialized notification received");
        return OPRT_OK;
    }

    node = ty_cJSON_GetObjectItem(msg, "id");
    if (node && ty_cJSON_IsString(node))
        id_str = node->valuestring;

    if (!id_str) {
        TAL_PR_ERR("Missing or invalid ID for method: %s", method);
        return mcp_server_reply_error(sid, eid, NULL, MCP_ERR_INVALID_REQUEST,
                                       "Missing request ID");
    }

    params = ty_cJSON_GetObjectItem(msg, "params");

    for (UINT_T i = 0; i < NUM_ROUTES; i++) {
        if (strcmp(method, s_routes[i].method) == 0)
            return s_routes[i].handler(sid, eid, params, id_str);
    }

    TAL_PR_WARN("Method not found: %s", method);
    return mcp_server_reply_error(sid, eid, id_str, MCP_ERR_METHOD_NOT_FOUND,
                                   "Method not found");
}

/* ========================================================================== */
/*                        Server Lifecycle                                    */
/* ========================================================================== */

OPERATE_RET mcp_server_init(CONST CHAR_T *name, CONST CHAR_T *version)
{
    if (s_ctx.initialized) {
        TAL_PR_WARN("MCP server already initialized");
        return OPRT_OK;
    }

    if (!name || !version)
        return OPRT_INVALID_PARM;

    memset(&s_ctx, 0, sizeof(s_ctx));

    s_ctx.name = mm_strdup(name);
    if (!s_ctx.name)
        return OPRT_MALLOC_FAILED;

    s_ctx.version = mm_strdup(version);
    if (!s_ctx.version) {
        tal_free(s_ctx.name);
        s_ctx.name = NULL;
        return OPRT_MALLOC_FAILED;
    }

    tuya_ai_agent_mcp_set_cb(mcp_server_handle_message, NULL);
    s_ctx.initialized = TRUE;

    TAL_PR_INFO("MCP server initialized: %s v%s", name, version);
    return OPRT_OK;
}

VOID mcp_server_destroy(VOID)
{
    if (!s_ctx.initialized)
        return;

    s_ctx.initialized = FALSE;
    tuya_ai_agent_mcp_set_cb(NULL, NULL);

#if MCP_ENABLE_TOOLS
    mcp_tools_cap_destroy();
#endif
#if MCP_ENABLE_RESOURCES
    mcp_resources_cap_destroy();
#endif
#if MCP_ENABLE_PROMPTS
    mcp_prompts_cap_destroy();
#endif
#if MCP_ENABLE_LOGGING
    mcp_logging_cap_destroy();
#endif

    tal_free(s_ctx.name);
    tal_free(s_ctx.version);
    tal_free(s_ctx.session_sid);
    tal_free(s_ctx.session_eid);

    memset(&s_ctx, 0, sizeof(s_ctx));
    TAL_PR_INFO("MCP server destroyed");
}
