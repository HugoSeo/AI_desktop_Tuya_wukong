/**
 * @file wukong_ai_mcp.h
 * @brief MCP Server umbrella header — includes core + all enabled capabilities
 * @version 3.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 *
 * Include this single header to get the full MCP server API.
 * Capabilities are controlled by compile-time macros defined in mcp_server.h:
 *   MCP_ENABLE_TOOLS, MCP_ENABLE_RESOURCES, MCP_ENABLE_PROMPTS, MCP_ENABLE_LOGGING
 */

#ifndef __WUKONG_AI_MCP_H__
#define __WUKONG_AI_MCP_H__

#include "tuya_cloud_types.h"
#include "tuya_app_config.h"
#include "mcp_server_tools.h"
#include "mcp_content.h"

OPERATE_RET wukong_ai_mcp_init(VOID);

OPERATE_RET wukong_ai_mcp_deinit(VOID);

#endif /* __WUKONG_AI_MCP_H__ */
