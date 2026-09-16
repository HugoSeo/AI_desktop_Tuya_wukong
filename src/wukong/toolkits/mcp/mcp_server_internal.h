/**
 * @file mcp_server_internal.h
 * @brief Internal API for MCP capability modules — NOT for application use
 * @version 3.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 *
 * This header is included by capability module implementations (tools,
 * resources, prompts, logging) to access the core transport functions.
 */

#ifndef __MCP_SERVER_INTERNAL_H__
#define __MCP_SERVER_INTERNAL_H__

#include "mcp_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                          Method Handler Typedef                            */
/* ========================================================================== */

typedef OPERATE_RET (*MCP_METHOD_HANDLER_FN)(CHAR_T *sid, CHAR_T *eid,
                                              ty_cJSON *params, CONST CHAR_T *id);

/* ========================================================================== */
/*                          Transport Helpers                                 */
/* ========================================================================== */

/**
 * Send a JSON-RPC 2.0 success response.
 * @note Takes ownership of \p result (will be freed by this function).
 */
OPERATE_RET mcp_server_reply_result(CHAR_T *sid, CHAR_T *eid,
                                     CONST CHAR_T *id, ty_cJSON *result);

/**
 * Send a JSON-RPC 2.0 error response.
 */
OPERATE_RET mcp_server_reply_error(CHAR_T *sid, CHAR_T *eid,
                                    CONST CHAR_T *id, INT_T code,
                                    CONST CHAR_T *msg);

/**
 * Send a JSON-RPC 2.0 notification (no id, no response expected).
 * Uses the stored session sid/eid from the last initialize handshake.
 * @note Takes ownership of \p params (will be freed by this function).
 */
OPERATE_RET mcp_server_send_notification(CONST CHAR_T *method, ty_cJSON *params);

/* ========================================================================== */
/*                    Capability Module Forward Declarations                  */
/* ========================================================================== */

#if MCP_ENABLE_TOOLS
OPERATE_RET mcp_tools_handle_list(CHAR_T *sid, CHAR_T *eid,
                                   ty_cJSON *params, CONST CHAR_T *id);
OPERATE_RET mcp_tools_handle_call(CHAR_T *sid, CHAR_T *eid,
                                   ty_cJSON *params, CONST CHAR_T *id);
VOID mcp_tools_cap_destroy(VOID);
#endif

#if MCP_ENABLE_RESOURCES
OPERATE_RET mcp_resources_handle_list(CHAR_T *sid, CHAR_T *eid,
                                       ty_cJSON *params, CONST CHAR_T *id);
OPERATE_RET mcp_resources_handle_read(CHAR_T *sid, CHAR_T *eid,
                                       ty_cJSON *params, CONST CHAR_T *id);
OPERATE_RET mcp_resources_handle_templates_list(CHAR_T *sid, CHAR_T *eid,
                                                 ty_cJSON *params, CONST CHAR_T *id);
OPERATE_RET mcp_resources_handle_subscribe(CHAR_T *sid, CHAR_T *eid,
                                            ty_cJSON *params, CONST CHAR_T *id);
OPERATE_RET mcp_resources_handle_unsubscribe(CHAR_T *sid, CHAR_T *eid,
                                              ty_cJSON *params, CONST CHAR_T *id);
VOID mcp_resources_cap_destroy(VOID);
#endif

#if MCP_ENABLE_PROMPTS
OPERATE_RET mcp_prompts_handle_list(CHAR_T *sid, CHAR_T *eid,
                                     ty_cJSON *params, CONST CHAR_T *id);
OPERATE_RET mcp_prompts_handle_get(CHAR_T *sid, CHAR_T *eid,
                                    ty_cJSON *params, CONST CHAR_T *id);
VOID mcp_prompts_cap_destroy(VOID);
#endif

#if MCP_ENABLE_LOGGING
OPERATE_RET mcp_logging_handle_set_level(CHAR_T *sid, CHAR_T *eid,
                                          ty_cJSON *params, CONST CHAR_T *id);
VOID mcp_logging_cap_destroy(VOID);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __MCP_SERVER_INTERNAL_H__ */
