/**
 * @file mcp_tool_imm.h
 * @brief MCP tools: instant messaging (WeChat, Feishu, Discord, WhatsApp, etc.)
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __MCP_TOOL_IMM_H__
#define __MCP_TOOL_IMM_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Instant messaging platform identifiers.
 */
typedef enum {
    IMM_PLATFORM_WECHAT = 0,
    IMM_PLATFORM_FEISHU,
    IMM_PLATFORM_DISCORD,
    IMM_PLATFORM_WHATSAPP,
    IMM_PLATFORM_MAX,
} IMM_PLATFORM_E;

/**
 * Callback invoked by the framework when sending a message.
 *
 * @param[in] platform  Target platform
 * @param[in] contact   Contact name or ID
 * @param[in] message   Message text
 * @return OPRT_OK on success
 */
typedef OPERATE_RET (*IMM_SEND_CB)(IMM_PLATFORM_E platform,
                                    CONST CHAR_T *contact,
                                    CONST CHAR_T *message);

/**
 * Callback invoked by the framework when querying messages.
 *
 * @param[in]  platform  Target platform
 * @param[in]  contact   Contact name or ID (NULL = all)
 * @param[in]  count     Max messages to return
 * @param[out] out_json  Caller-allocated output: JSON string of messages.
 *                       Caller must free with tal_free().
 * @return OPRT_OK on success
 */
typedef OPERATE_RET (*IMM_QUERY_CB)(IMM_PLATFORM_E platform,
                                     CONST CHAR_T *contact,
                                     INT_T count,
                                     CHAR_T **out_json);

/**
 * Register platform-specific send/query callbacks.
 * Call before mcp_tool_imm_init() or at any time to add a new platform.
 *
 * @param[in] platform  Platform identifier
 * @param[in] send_cb   Send callback (may be NULL if not supported)
 * @param[in] query_cb  Query callback (may be NULL if not supported)
 * @return OPRT_OK on success
 */
OPERATE_RET mcp_tool_imm_register_platform(IMM_PLATFORM_E platform,
                                            IMM_SEND_CB send_cb,
                                            IMM_QUERY_CB query_cb);

/**
 * Initialize the IMM MCP tools (registers device_imm_send, device_imm_query).
 */
OPERATE_RET mcp_tool_imm_init(VOID);

#ifdef __cplusplus
}
#endif

#endif /* __MCP_TOOL_IMM_H__ */
