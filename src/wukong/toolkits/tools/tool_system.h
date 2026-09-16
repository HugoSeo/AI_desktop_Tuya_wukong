/**
 * @file tool_system.h
 * @brief MCP tools: device system info (time, uptime, free memory)
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __MCP_TOOL_SYSTEM_H__
#define __MCP_TOOL_SYSTEM_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET tool_system_init(VOID);

#ifdef __cplusplus
}
#endif

#endif /* __MCP_TOOL_SYSTEM_H__ */
