/**
 * @file mcp_server_logging.h
 * @brief MCP Logging capability — structured log messages to client
 * @version 3.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __MCP_SERVER_LOGGING_H__
#define __MCP_SERVER_LOGGING_H__

#include "mcp_server.h"

#if MCP_ENABLE_LOGGING

#ifdef __cplusplus
extern "C" {
#endif

/* Log level indices (RFC 5424 syslog severity, 0 = most severe) */
#define MCP_LOG_EMERGENCY           0
#define MCP_LOG_ALERT               1
#define MCP_LOG_CRITICAL            2
#define MCP_LOG_ERROR               3
#define MCP_LOG_WARNING             4
#define MCP_LOG_NOTICE              5
#define MCP_LOG_INFO                6
#define MCP_LOG_DEBUG               7

/**
 * Send a structured log message to the client (notifications/message).
 * Respects the log level set by the client via logging/setLevel.
 *
 * @param level   One of "debug","info","notice","warning","error",
 *                "critical","alert","emergency"
 * @param logger  Logger name (e.g. "tool", "resource")
 * @param fmt     printf-style format string
 */
OPERATE_RET mcp_server_log(CONST CHAR_T *level, CONST CHAR_T *logger,
                            CONST CHAR_T *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* MCP_ENABLE_LOGGING */
#endif /* __MCP_SERVER_LOGGING_H__ */
