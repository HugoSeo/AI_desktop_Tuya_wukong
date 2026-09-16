/**
 * @file wukong_mcp.c
 * @brief AI Toy MCP server shell — brings up the MCP transport only.
 * @version 3.0
 *
 * Tool registration lives in toolkits/tools/tool_init.c (wukong_tools_init);
 * this shell only stands up the MCP server transport. Orchestration (tools ->
 * mcp -> fc -> skill) lives in toolkits/wukong_toolkits.c.
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 *
 * Permission is hereby granted, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), Under the premise of complying
 * with the license of the third-party open source software contained in the software,
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software.
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 */

#include "tal_log.h"
#include "mcp_server.h"
#include "wukong_mcp.h"

OPERATE_RET wukong_mcp_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_RETURN(mcp_server_init("Wukong AI", "3.0"));

    TAL_PR_DEBUG("MCP Server initialized successfully");
    return OPRT_OK;
}

OPERATE_RET wukong_mcp_deinit(VOID)
{
    mcp_server_destroy();
    TAL_PR_DEBUG("MCP Server deinitialized successfully");
    return OPRT_OK;
}
