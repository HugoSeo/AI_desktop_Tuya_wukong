/**
 * @file mcp_server_prompts.h
 * @brief MCP Prompts capability — prompt registration and argument descriptors
 * @version 3.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __MCP_SERVER_PROMPTS_H__
#define __MCP_SERVER_PROMPTS_H__

#include "mcp_server.h"

#if MCP_ENABLE_PROMPTS

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Prompt argument descriptor. An array of these (terminated by {NULL})
 * defines the prompt's accepted arguments.
 */
typedef struct {
    CONST CHAR_T *name;
    CONST CHAR_T *description;
    BOOL_T required;
} MCP_PROMPT_ARG_T;

#define MCP_PROMPT_ARG_END  { NULL, NULL, FALSE }

/**
 * Prompt get callback. Must populate out_messages with a ty_cJSON array of
 * message objects: [{"role":"user","content":{"type":"text","text":"..."}}]
 */
typedef OPERATE_RET (*MCP_PROMPT_GET_CB)(
    CONST CHAR_T *name,
    CONST ty_cJSON *arguments,
    ty_cJSON **out_messages,
    VOID *user_data
);

/**
 * Register a prompt.
 * @param args  Array of MCP_PROMPT_ARG_T terminated by MCP_PROMPT_ARG_END,
 *              or NULL if the prompt takes no arguments.
 */
OPERATE_RET mcp_server_prompt_add(CONST CHAR_T *name,
                                   CONST CHAR_T *description,
                                   CONST MCP_PROMPT_ARG_T *args,
                                   MCP_PROMPT_GET_CB handler,
                                   VOID *user_data);

/**
 * Send notifications/prompts/list_changed to the client.
 */
OPERATE_RET mcp_server_notify_prompts_changed(VOID);

#ifdef __cplusplus
}
#endif

#endif /* MCP_ENABLE_PROMPTS */
#endif /* __MCP_SERVER_PROMPTS_H__ */
