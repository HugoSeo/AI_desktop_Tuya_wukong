#ifndef __WUKONG_AI_MCP_H__
#define __WUKONG_AI_MCP_H__

#include "tuya_cloud_types.h"
#include "ty_cJSON.h"

typedef OPERATE_RET (*MCP_TOOL_HANDLER_CB)(
    CONST CHAR_T *name,
    CONST ty_cJSON *arguments,
    ty_cJSON **out_content,
    BOOL_T *out_is_error,
    VOID *user_data
);

typedef struct {
    CONST CHAR_T *name;
    CONST CHAR_T *type;
    CONST CHAR_T *description;
    BOOL_T required;
    BOOL_T has_minimum;
    INT_T minimum;
    BOOL_T has_maximum;
    INT_T maximum;
} MCP_SCHEMA_PROP_T;

OPERATE_RET mcp_server_tool_register(CONST CHAR_T *name,
                                     CONST CHAR_T *description,
                                     MCP_TOOL_HANDLER_CB handler,
                                     VOID *user_data, ...);

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
#define MCP_SCHEMA_END NULL
#define MCP_TOOL_ADD(name, desc, handler, ud, ...) \
    mcp_server_tool_register(name, desc, handler, ud, ##__VA_ARGS__, MCP_SCHEMA_END)

ty_cJSON *mcp_content_make_text(CONST CHAR_T *text);

#endif
