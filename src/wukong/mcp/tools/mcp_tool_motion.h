/**
 * @file mcp_tool_motion.h
 * @brief MCP tool: device_motion_control_set (requires T5AI_BOARD_DESKTOP)
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __MCP_TOOL_MOTION_H__
#define __MCP_TOOL_MOTION_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET mcp_tool_motion_init(VOID);

#ifdef __cplusplus
}
#endif

#endif /* __MCP_TOOL_MOTION_H__ */
