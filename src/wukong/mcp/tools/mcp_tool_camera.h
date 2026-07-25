/**
 * @file mcp_tool_camera.h
 * @brief MCP tool: device_camera_take_photo (requires ENABLE_TUYA_CAMERA)
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __MCP_TOOL_CAMERA_H__
#define __MCP_TOOL_CAMERA_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET mcp_tool_camera_init(VOID);

#ifdef __cplusplus
}
#endif

#endif /* __MCP_TOOL_CAMERA_H__ */
