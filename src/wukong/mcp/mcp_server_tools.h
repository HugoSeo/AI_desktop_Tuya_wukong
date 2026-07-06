/**
 * @file mcp_server_tools.h
 * @brief MCP Tools capability — tool registration, schema macros
 * @version 3.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __MCP_SERVER_TOOLS_H__
#define __MCP_SERVER_TOOLS_H__

#include "mcp_server.h"

#if MCP_ENABLE_TOOLS

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              Tool Callback                                 */
/* ========================================================================== */

/**
 * Tool handler callback.
 *
 * @param[in]  name         Tool name being invoked
 * @param[in]  arguments    Raw JSON arguments object (may be NULL if no args)
 * @param[out] out_content  Caller must set to a ty_cJSON array of content items.
 *                          Use mcp_content_make_* helpers. Ownership transfers
 *                          to the framework after return.
 * @param[out] out_is_error Set to TRUE if this is a tool execution error.
 * @param[in]  user_data    Opaque pointer passed at registration
 * @return OPRT_OK on success
 */
typedef OPERATE_RET (*MCP_TOOL_HANDLER_CB)(
    CONST CHAR_T *name,
    CONST ty_cJSON *arguments,
    ty_cJSON **out_content,
    BOOL_T *out_is_error,
    VOID *user_data
);

/* ========================================================================== */
/*                          Schema Property Macros                            */
/* ========================================================================== */

typedef struct {
    CONST CHAR_T *name;
    CONST CHAR_T *type;         /**< "integer" / "string" / "boolean" / "number" */
    CONST CHAR_T *description;
    BOOL_T required;
    BOOL_T has_minimum;
    INT_T minimum;
    BOOL_T has_maximum;
    INT_T maximum;
} MCP_SCHEMA_PROP_T;

#define MCP_SCHEMA_INT(n, d) \
    &(MCP_SCHEMA_PROP_T){ .name = (n), .type = "integer", .description = (d), \
        .required = TRUE }

#define MCP_SCHEMA_INT_OPT(n, d) \
    &(MCP_SCHEMA_PROP_T){ .name = (n), .type = "integer", .description = (d), \
        .required = FALSE }

#define MCP_SCHEMA_INT_RANGE(n, d, lo, hi) \
    &(MCP_SCHEMA_PROP_T){ .name = (n), .type = "integer", .description = (d), \
        .required = TRUE, .has_minimum = TRUE, .minimum = (lo), \
        .has_maximum = TRUE, .maximum = (hi) }

#define MCP_SCHEMA_INT_OPT_RANGE(n, d, lo, hi) \
    &(MCP_SCHEMA_PROP_T){ .name = (n), .type = "integer", .description = (d), \
        .required = FALSE, .has_minimum = TRUE, .minimum = (lo), \
        .has_maximum = TRUE, .maximum = (hi) }

#define MCP_SCHEMA_STR(n, d) \
    &(MCP_SCHEMA_PROP_T){ .name = (n), .type = "string", .description = (d), \
        .required = TRUE }

#define MCP_SCHEMA_STR_OPT(n, d) \
    &(MCP_SCHEMA_PROP_T){ .name = (n), .type = "string", .description = (d), \
        .required = FALSE }

#define MCP_SCHEMA_BOOL(n, d) \
    &(MCP_SCHEMA_PROP_T){ .name = (n), .type = "boolean", .description = (d), \
        .required = TRUE }

#define MCP_SCHEMA_BOOL_OPT(n, d) \
    &(MCP_SCHEMA_PROP_T){ .name = (n), .type = "boolean", .description = (d), \
        .required = FALSE }

#define MCP_SCHEMA_NUM(n, d) \
    &(MCP_SCHEMA_PROP_T){ .name = (n), .type = "number", .description = (d), \
        .required = TRUE }

#define MCP_SCHEMA_NUM_OPT(n, d) \
    &(MCP_SCHEMA_PROP_T){ .name = (n), .type = "number", .description = (d), \
        .required = FALSE }

#define MCP_SCHEMA_END  NULL

/* ========================================================================== */
/*                           Tool Registration                                */
/* ========================================================================== */

/**
 * Register a tool with the MCP server. Variadic args are MCP_SCHEMA_PROP_T*
 * pointers terminated by MCP_SCHEMA_END (NULL).
 */
OPERATE_RET mcp_server_tool_register(CONST CHAR_T *name,
                                      CONST CHAR_T *description,
                                      MCP_TOOL_HANDLER_CB handler,
                                      VOID *user_data, ...);

/**
 * Convenience macro: appends MCP_SCHEMA_END automatically.
 */
#define MCP_TOOL_ADD(name, desc, handler, ud, ...) \
    mcp_server_tool_register(name, desc, handler, ud, ##__VA_ARGS__, MCP_SCHEMA_END)

/**
 * Send notifications/tools/list_changed to the client.
 */
OPERATE_RET mcp_server_notify_tools_changed(VOID);

#ifdef __cplusplus
}
#endif

#endif /* MCP_ENABLE_TOOLS */
#endif /* __MCP_SERVER_TOOLS_H__ */
