/**
 * @file mcp_server_tools.h
 * @brief MCP Tools capability — JSON-RPC transport declarations
 * @version 3.0.0
 *
 * The tool registry, schema macros and content helpers moved to
 * toolkits/wukong_tool.h (neutral, not gated by MCP_ENABLE_TOOLS). This
 * header is now a thin transport-side header: it re-exports the registry API
 * via wukong_tool.h and declares only the JSON-RPC transport entry points
 * that stay in mcp_server_tools.c.
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __MCP_SERVER_TOOLS_H__
#define __MCP_SERVER_TOOLS_H__

#include "mcp_server.h"
#include "wukong_tool.h"   /* registry API + schema macros migrated there */

#if MCP_ENABLE_TOOLS

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                           JSON-RPC Transport                               */
/* ========================================================================== */

/**
 * Handle a tools/list JSON-RPC request: paginate the registry snapshot and
 * reply with {"tools":[...], "nextCursor":...}.
 */
OPERATE_RET mcp_tools_handle_list(CHAR_T *sid, CHAR_T *eid,
                                   ty_cJSON *params, CONST CHAR_T *id);

/**
 * Handle a tools/call JSON-RPC request: validate arguments, then schedule the
 * handler on the workq and reply asynchronously.
 */
OPERATE_RET mcp_tools_handle_call(CHAR_T *sid, CHAR_T *eid,
                                   ty_cJSON *params, CONST CHAR_T *id);

/**
 * Send notifications/tools/list_changed to the client.
 */
OPERATE_RET mcp_server_notify_tools_changed(VOID);

#ifdef __cplusplus
}
#endif

#endif /* MCP_ENABLE_TOOLS */
#endif /* __MCP_SERVER_TOOLS_H__ */
