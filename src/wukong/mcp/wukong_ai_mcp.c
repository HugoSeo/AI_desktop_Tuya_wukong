/**
 * @file wukong_ai_mcp.c
 * @brief AI Toy MCP server initialization — registers all tool modules
 * @version 3.0
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
#include "wukong_ai_mcp.h"

#include "tools/mcp_tool_control.h"
#include "tools/mcp_tool_tm.h"
#if defined(ENABLE_TOOLKITS_PLAYBACK) && (ENABLE_TOOLKITS_PLAYBACK == 1)
#include "tools/mcp_tool_playback.h"
#include "wukong_playback_ctrl.h"
#endif
#include "tools/mcp_tool_imm.h"
#include "tools/mcp_tool_social.h"
#include "tools/mcp_tool_camera.h"
#include "tools/mcp_tool_motion.h"

OPERATE_RET wukong_ai_mcp_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_RETURN(mcp_server_init("Wukong AI", "3.0"));

#if defined(ENABLE_TOOLKITS_CONTROL) && (ENABLE_TOOLKITS_CONTROL == 1)
    TUYA_CALL_ERR_LOG(mcp_tool_control_init());
#endif
#if defined(ENABLE_TOOLKITS_TM) && (ENABLE_TOOLKITS_TM == 1)
    TUYA_CALL_ERR_LOG(mcp_tool_tm_init());
#endif
#if defined(ENABLE_TOOLKITS_PLAYBACK) && (ENABLE_TOOLKITS_PLAYBACK == 1)
    TUYA_CALL_ERR_LOG(wukong_playback_ctrl_init());
    TUYA_CALL_ERR_LOG(mcp_tool_playback_init());
#endif
#if defined(ENABLE_TOOLKITS_IMM) && (ENABLE_TOOLKITS_IMM == 1)
    TUYA_CALL_ERR_LOG(mcp_tool_imm_init());
#endif
#if defined(ENABLE_TOOLKITS_SOCIAL) && (ENABLE_TOOLKITS_SOCIAL == 1)
    TUYA_CALL_ERR_LOG(mcp_tool_social_init());
#endif

#if defined(ENABLE_TOOLKITS_CAMERA) && (ENABLE_TOOLKITS_CAMERA == 1)
    TUYA_CALL_ERR_LOG(mcp_tool_camera_init());
#endif

#if defined(ENABLE_TOOLKITS_MOTION) && (ENABLE_TOOLKITS_MOTION == 1)
    TUYA_CALL_ERR_LOG(mcp_tool_motion_init());
#endif

    TAL_PR_DEBUG("MCP Server initialized successfully");
    return OPRT_OK;
}

OPERATE_RET wukong_ai_mcp_deinit(VOID)
{
    mcp_server_destroy();
    TAL_PR_DEBUG("MCP Server deinitialized successfully");
    return OPRT_OK;
}
