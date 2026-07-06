/**
 * @file mcp_server.h
 * @brief MCP Server core public API - MCP 2024-11-05 compliant
 * @version 3.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 *
 * Core framework for a modular MCP server. Capability modules (Tools,
 * Resources, Prompts, Logging) are independently compiled and enabled
 * via compile-time macros.
 */

#ifndef __MCP_SERVER_H__
#define __MCP_SERVER_H__

#include "tuya_cloud_types.h"
#include "ty_cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                        Compile-Time Configuration                          */
/* ========================================================================== */

#ifndef MCP_ENABLE_TOOLS
#define MCP_ENABLE_TOOLS        1
#endif
#ifndef MCP_ENABLE_RESOURCES
#define MCP_ENABLE_RESOURCES    0
#endif
#ifndef MCP_ENABLE_PROMPTS
#define MCP_ENABLE_PROMPTS      0
#endif
#ifndef MCP_ENABLE_LOGGING
#define MCP_ENABLE_LOGGING      0
#endif

/* ========================================================================== */
/*                            Protocol Constants                              */
/* ========================================================================== */

#define MCP_PROTOCOL_VERSION        "2024-11-05"
#define MCP_MAX_PAYLOAD_SIZE        16384

/* JSON-RPC 2.0 standard error codes */
#define MCP_ERR_PARSE               (-32700)
#define MCP_ERR_INVALID_REQUEST     (-32600)
#define MCP_ERR_METHOD_NOT_FOUND    (-32601)
#define MCP_ERR_INVALID_PARAMS      (-32602)
#define MCP_ERR_INTERNAL            (-32603)

/* MCP-specific error codes */
#define MCP_ERR_RESOURCE_NOT_FOUND  (-32002)

/* ========================================================================== */
/*                          Server Lifecycle                                  */
/* ========================================================================== */

/**
 * Initialize the MCP server. Must be called before registering any capabilities.
 * Sets up the SDK callback via tuya_ai_agent_mcp_set_cb().
 */
OPERATE_RET mcp_server_init(CONST CHAR_T *name, CONST CHAR_T *version);

/**
 * Destroy the MCP server and free all registered capabilities.
 */
VOID mcp_server_destroy(VOID);

/**
 * Main message handler. Registered as TY_AI_MCP_CB via the SDK.
 * Parses and routes JSON-RPC 2.0 messages.
 */
OPERATE_RET mcp_server_handle_message(CHAR_T *sid, CHAR_T *eid,
                                       CONST ty_cJSON *msg, VOID *user_data);

#ifdef __cplusplus
}
#endif

#endif /* __MCP_SERVER_H__ */
