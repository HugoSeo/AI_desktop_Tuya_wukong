/**
 * @file mcp_server_logging.c
 * @brief MCP Logging capability — setLevel handler, log function
 * @version 3.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#include "mcp_server.h"

#if MCP_ENABLE_LOGGING

#include "mcp_server_logging.h"
#include "mcp_server_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "tal_log.h"

/* ========================================================================== */
/*                            Log Level Helpers                               */
/* ========================================================================== */

STATIC CONST CHAR_T *s_log_level_names[] = {
    "emergency", "alert", "critical", "error",
    "warning", "notice", "info", "debug"
};

STATIC INT_T s_log_level = MCP_LOG_DEBUG;

STATIC INT_T __log_level_from_string(CONST CHAR_T *level)
{
    for (INT_T i = 0; i <= MCP_LOG_DEBUG; i++) {
        if (strcmp(s_log_level_names[i], level) == 0)
            return i;
    }
    return -1;
}

/* ========================================================================== */
/*                     Method: logging/setLevel                               */
/* ========================================================================== */

OPERATE_RET mcp_logging_handle_set_level(CHAR_T *sid, CHAR_T *eid,
                                          ty_cJSON *params, CONST CHAR_T *id)
{
    ty_cJSON *level_j;
    INT_T level;

    if (!ty_cJSON_IsObject(params))
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INVALID_PARAMS,
                                       "Missing params");

    level_j = ty_cJSON_GetObjectItem(params, "level");
    if (!ty_cJSON_IsString(level_j))
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INVALID_PARAMS,
                                       "Missing level");

    level = __log_level_from_string(level_j->valuestring);
    if (level < 0)
        return mcp_server_reply_error(sid, eid, id, MCP_ERR_INVALID_PARAMS,
                                       "Invalid log level");

    s_log_level = level;
    TAL_PR_INFO("MCP log level set to: %s (%d)", level_j->valuestring, level);

    return mcp_server_reply_result(sid, eid, id, ty_cJSON_CreateObject());
}

/* ========================================================================== */
/*                             Log Function                                   */
/* ========================================================================== */

OPERATE_RET mcp_server_log(CONST CHAR_T *level, CONST CHAR_T *logger,
                            CONST CHAR_T *fmt, ...)
{
    INT_T severity;
    ty_cJSON *params;
    CHAR_T buf[512];
    va_list ap;

    if (!level || !fmt)
        return OPRT_INVALID_PARM;

    severity = __log_level_from_string(level);
    if (severity < 0)
        severity = MCP_LOG_INFO;

    if (severity > s_log_level)
        return OPRT_OK;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    params = ty_cJSON_CreateObject();
    if (!params)
        return OPRT_MALLOC_FAILED;

    ty_cJSON_AddStringToObject(params, "level", level);
    if (logger)
        ty_cJSON_AddStringToObject(params, "logger", logger);
    ty_cJSON_AddStringToObject(params, "data", buf);

    return mcp_server_send_notification("notifications/message", params);
}

/* ========================================================================== */
/*                         Capability Destroy                                 */
/* ========================================================================== */

VOID mcp_logging_cap_destroy(VOID)
{
    s_log_level = MCP_LOG_DEBUG;
}

#endif /* MCP_ENABLE_LOGGING */
