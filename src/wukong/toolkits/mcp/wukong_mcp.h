/**
 * @file wukong_mcp.h
 * @brief MCP Server shell header — transport bring-up + provider inbound entry.
 * @version 3.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 *
 * Tool registration lives in toolkits/tools (wukong_tools_init); this shell
 * only stands up the MCP transport. Capabilities are controlled by compile-time
 * macros in mcp_server.h: MCP_ENABLE_TOOLS/RESOURCES/PROMPTS/LOGGING.
 */

#ifndef __WUKONG_MCP_H__
#define __WUKONG_MCP_H__

#include "tuya_cloud_types.h"
#include "tuya_app_config.h"
#include "mcp_server.h"
#include "mcp_server_tools.h"
#include "wukong_tool.h"

/** Stand up the MCP server transport (no tool registration). */
OPERATE_RET wukong_mcp_init(VOID);

/** Tear down the MCP server transport. */
OPERATE_RET wukong_mcp_deinit(VOID);

/**
 * Unified inbound entry for providers (wukong_ prefix). Zero-overhead alias to the
 * current MCP library's parsed-message router. A provider, after receiving an MCP
 * message and parsing it into a ty_cJSON, hands it here for routing. Usable both as a
 * direct call and as a function pointer (e.g. registered to the Tuya SDK callback).
 * Swapping the MCP library means re-pointing this single macro.
 */
#define wukong_mcp_recv   mcp_server_handle_message

#endif /* __WUKONG_MCP_H__ */
