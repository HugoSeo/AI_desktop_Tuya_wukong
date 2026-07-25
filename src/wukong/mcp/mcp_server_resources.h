/**
 * @file mcp_server_resources.h
 * @brief MCP Resources capability — resource registration, subscribe, notify
 * @version 3.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __MCP_SERVER_RESOURCES_H__
#define __MCP_SERVER_RESOURCES_H__

#include "mcp_server.h"

#if MCP_ENABLE_RESOURCES

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Resource read callback. Must populate out_contents with a ty_cJSON array
 * of resource content objects: [{"uri":"...","mimeType":"...","text":"..."}]
 */
typedef OPERATE_RET (*MCP_RESOURCE_READ_CB)(
    CONST CHAR_T *uri,
    ty_cJSON **out_contents,
    VOID *user_data
);

/**
 * Register a static resource.
 */
OPERATE_RET mcp_server_resource_add(CONST CHAR_T *uri,
                                     CONST CHAR_T *name,
                                     CONST CHAR_T *description,
                                     CONST CHAR_T *mime_type,
                                     MCP_RESOURCE_READ_CB handler,
                                     VOID *user_data);

/**
 * Remove a previously registered resource by URI.
 */
OPERATE_RET mcp_server_resource_remove(CONST CHAR_T *uri);

/**
 * Send notifications/resources/updated for a URI (only to subscribed clients).
 */
OPERATE_RET mcp_server_resource_notify_updated(CONST CHAR_T *uri);

/**
 * Send notifications/resources/list_changed to the client.
 */
OPERATE_RET mcp_server_notify_resources_changed(VOID);

#ifdef __cplusplus
}
#endif

#endif /* MCP_ENABLE_RESOURCES */
#endif /* __MCP_SERVER_RESOURCES_H__ */
