/**
 * @file mcp_tool_imm.c
 * @brief MCP tools: instant messaging — send/query messages across IM platforms
 *
 * Framework implementation. Actual platform integrations are registered via
 * mcp_tool_imm_register_platform() by external modules.
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#include "mcp_tool_imm.h"
#include "wukong_ai_mcp.h"

#include <string.h>

#include "tal_log.h"

/* ========================================================================== */
/*                          Platform Registry                                 */
/* ========================================================================== */

typedef struct {
    CONST CHAR_T *name;
    IMM_SEND_CB send_cb;
    IMM_QUERY_CB query_cb;
} IMM_PLATFORM_ENTRY_T;

STATIC IMM_PLATFORM_ENTRY_T s_platforms[IMM_PLATFORM_MAX] = {
    [IMM_PLATFORM_WECHAT]   = { .name = "wechat" },
    [IMM_PLATFORM_FEISHU]   = { .name = "feishu" },
    [IMM_PLATFORM_DISCORD]  = { .name = "discord" },
    [IMM_PLATFORM_WHATSAPP] = { .name = "whatsapp" },
};

STATIC IMM_PLATFORM_E __platform_from_string(CONST CHAR_T *str)
{
    if (!str)
        return IMM_PLATFORM_MAX;

    for (INT_T i = 0; i < IMM_PLATFORM_MAX; i++) {
        if (strcmp(s_platforms[i].name, str) == 0)
            return (IMM_PLATFORM_E)i;
    }
    return IMM_PLATFORM_MAX;
}

OPERATE_RET mcp_tool_imm_register_platform(IMM_PLATFORM_E platform,
                                            IMM_SEND_CB send_cb,
                                            IMM_QUERY_CB query_cb)
{
    if (platform >= IMM_PLATFORM_MAX)
        return OPRT_INVALID_PARM;

    s_platforms[platform].send_cb = send_cb;
    s_platforms[platform].query_cb = query_cb;

    TAL_PR_INFO("IMM platform registered: %s", s_platforms[platform].name);
    return OPRT_OK;
}

/* ========================================================================== */
/*                         Tool: device_imm_send                              */
/* ========================================================================== */

STATIC OPERATE_RET __imm_send(CONST CHAR_T *name, CONST ty_cJSON *args,
                               ty_cJSON **out_content, BOOL_T *is_error,
                               VOID *user_data)
{
    CONST CHAR_T *platform_str = NULL;
    CONST CHAR_T *contact = NULL;
    CONST CHAR_T *message = NULL;
    IMM_PLATFORM_E platform;
    ty_cJSON *j;
    OPERATE_RET rt;

    (VOID)name;
    (VOID)user_data;

    if (args) {
        j = ty_cJSON_GetObjectItem(args, "platform");
        if (j && ty_cJSON_IsString(j))
            platform_str = j->valuestring;
        j = ty_cJSON_GetObjectItem(args, "contact");
        if (j && ty_cJSON_IsString(j))
            contact = j->valuestring;
        j = ty_cJSON_GetObjectItem(args, "message");
        if (j && ty_cJSON_IsString(j))
            message = j->valuestring;
    }

    if (!platform_str || !contact || !message) {
        *out_content = ty_cJSON_CreateArray();
        if (*out_content)
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text("Missing required parameters: platform, contact, message"));
        *is_error = TRUE;
        return OPRT_OK;
    }

    platform = __platform_from_string(platform_str);
    if (platform >= IMM_PLATFORM_MAX) {
        *out_content = ty_cJSON_CreateArray();
        if (*out_content)
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text("Unsupported platform"));
        *is_error = TRUE;
        return OPRT_OK;
    }

    if (!s_platforms[platform].send_cb) {
        *out_content = ty_cJSON_CreateArray();
        if (*out_content)
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text("Platform not integrated yet"));
        *is_error = TRUE;
        return OPRT_OK;
    }

    TAL_PR_DEBUG("IMM send: platform=%s, contact=%s", platform_str, contact);

    rt = s_platforms[platform].send_cb(platform, contact, message);

    *out_content = ty_cJSON_CreateArray();
    if (*out_content)
        ty_cJSON_AddItemToArray(*out_content,
            mcp_content_make_text(rt == OPRT_OK ? "true" : "false"));
    if (rt != OPRT_OK)
        *is_error = TRUE;

    return OPRT_OK;
}

/* ========================================================================== */
/*                        Tool: device_imm_query                              */
/* ========================================================================== */

STATIC OPERATE_RET __imm_query(CONST CHAR_T *name, CONST ty_cJSON *args,
                                ty_cJSON **out_content, BOOL_T *is_error,
                                VOID *user_data)
{
    CONST CHAR_T *platform_str = NULL;
    CONST CHAR_T *contact = NULL;
    INT_T count = 10;
    IMM_PLATFORM_E platform;
    CHAR_T *result_json = NULL;
    ty_cJSON *j;
    OPERATE_RET rt;

    (VOID)name;
    (VOID)user_data;

    if (args) {
        j = ty_cJSON_GetObjectItem(args, "platform");
        if (j && ty_cJSON_IsString(j))
            platform_str = j->valuestring;
        j = ty_cJSON_GetObjectItem(args, "contact");
        if (j && ty_cJSON_IsString(j))
            contact = j->valuestring;
        j = ty_cJSON_GetObjectItem(args, "count");
        if (j && ty_cJSON_IsNumber(j))
            count = j->valueint;
    }

    if (!platform_str) {
        *out_content = ty_cJSON_CreateArray();
        if (*out_content)
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text("Missing required parameter: platform"));
        *is_error = TRUE;
        return OPRT_OK;
    }

    platform = __platform_from_string(platform_str);
    if (platform >= IMM_PLATFORM_MAX) {
        *out_content = ty_cJSON_CreateArray();
        if (*out_content)
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text("Unsupported platform"));
        *is_error = TRUE;
        return OPRT_OK;
    }

    if (!s_platforms[platform].query_cb) {
        *out_content = ty_cJSON_CreateArray();
        if (*out_content)
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text("Platform not integrated yet"));
        *is_error = TRUE;
        return OPRT_OK;
    }

    TAL_PR_DEBUG("IMM query: platform=%s, contact=%s, count=%d",
                 platform_str, contact ? contact : "(all)", count);

    rt = s_platforms[platform].query_cb(platform, contact, count, &result_json);

    *out_content = ty_cJSON_CreateArray();
    if (*out_content) {
        if (rt == OPRT_OK && result_json) {
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text(result_json));
        } else {
            ty_cJSON_AddItemToArray(*out_content,
                mcp_content_make_text("Query failed"));
            *is_error = TRUE;
        }
    }

    if (result_json)
        tal_free(result_json);

    return OPRT_OK;
}

/* ========================================================================== */
/*                              Init                                          */
/* ========================================================================== */

OPERATE_RET mcp_tool_imm_init(VOID)
{
    OPERATE_RET rt;

    rt = MCP_TOOL_ADD(
        "device_imm_send",
        "Send a message to a contact via an instant messaging platform "
        "(WeChat, Feishu, Discord, WhatsApp).\n"
        "Returns true if the message was sent successfully.",
        __imm_send, NULL,
        MCP_SCHEMA_STR("platform",
            "Target IM platform: 'wechat', 'feishu', 'discord', 'whatsapp'"),
        MCP_SCHEMA_STR("contact",
            "Contact name or ID to send the message to"),
        MCP_SCHEMA_STR("message",
            "The message text to send")
    );
    if (rt != OPRT_OK)
        return rt;

    rt = MCP_TOOL_ADD(
        "device_imm_query",
        "Query recent messages from an instant messaging platform.\n"
        "Returns a JSON array of recent messages.",
        __imm_query, NULL,
        MCP_SCHEMA_STR("platform",
            "Target IM platform: 'wechat', 'feishu', 'discord', 'whatsapp'"),
        MCP_SCHEMA_STR_OPT("contact",
            "Contact name or ID to filter messages (omit for all contacts)"),
        MCP_SCHEMA_INT_OPT_RANGE("count",
            "Maximum number of messages to return (default 10)", 1, 50)
    );

    return rt;
}
