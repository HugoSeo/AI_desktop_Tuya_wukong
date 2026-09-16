/**
 * @file wukong_tool.h
 * @brief Neutral tool base: registry + per-caller visibility + single exec.
 *
 * Hosts the tool registry (register/list), a single exec entry that shapes the
 * result per caller (MCP serialized array vs agent flattened text), the schema
 * property macros and the content-item builders. Independent of the MCP
 * transport (mcp dir): not gated by MCP_ENABLE_TOOLS, so a board that disables
 * the MCP transport still has a working tool base for agent/fc use. Backend
 * (OpenAI/Anthropic) format differences live here, not in mcp_server_tools.c.
 */
#pragma once

#include "tuya_cloud_types.h"
#include "ty_cJSON.h"
#include "wukong_ai_provider.h"   /* WUKONG_LLM_BACKEND_E */

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                          Tool Visibility Flags                             */
/* ========================================================================== */

/* A tool is visible to a caller only when its registration flags include the
 * caller's bit; exec enforces this (a tool without the caller's bit is treated
 * as if it did not exist). */
#define WUKONG_TOOL_MCP     (1u << 0)   /**< reachable over the MCP transport */
#define WUKONG_TOOL_FC      (1u << 1)   /**< reachable via the fc dispatcher */
#define WUKONG_TOOL_AGENT   (1u << 2)   /**< reachable from the claw agent loop */

/* ========================================================================== */
/*                              Tool Callback                                 */
/* ========================================================================== */

/**
 * Tool handler callback.
 *
 * @param[in]  name         Tool name being invoked
 * @param[in]  arguments    Raw JSON arguments object (may be NULL if no args).
 *                          May be the caller's original object passed through
 *                          on the zero-promotion path (no declared-type
 *                          coercion needed) — the handler must not mutate it
 *                          in place; Duplicate first if a modified copy is
 *                          needed.
 * @param[out] out_content  Handler sets this to a ty_cJSON array of content
 *                          items (use wukong_tool_make_* helpers). Ownership
 *                          transfers to the framework after return.
 * @param[in]  user_data    Opaque pointer passed at registration
 * @return OPRT_OK on success. A non-OK return with a populated out_content is a
 *         business error: the message in out_content explains the failure.
 */
typedef OPERATE_RET (*WUKONG_TOOL_HANDLER_CB)(
    CONST CHAR_T *name,
    CONST ty_cJSON *arguments,
    ty_cJSON **out_content,
    VOID *user_data
);

/* ========================================================================== */
/*                          Schema Property Macros                            */
/* ========================================================================== */

typedef struct {
    CONST CHAR_T *name;
    CONST CHAR_T *type;         /**< "integer"/"string"/"boolean"/"number"/"array"/"object" */
    CONST CHAR_T *description;
    BOOL_T required;
    BOOL_T has_minimum;
    INT_T minimum;
    BOOL_T has_maximum;
    INT_T maximum;
    CONST CHAR_T *enum_values;  /**< Pipe-separated string enum values, e.g. "add|delete" */
} WUKONG_TOOL_SCHEMA_PROP_T;

#define TOOL_SCHEMA_INT(n, d) \
    &(WUKONG_TOOL_SCHEMA_PROP_T){ .name = (n), .type = "integer", .description = (d), \
        .required = TRUE }

#define TOOL_SCHEMA_INT_OPT(n, d) \
    &(WUKONG_TOOL_SCHEMA_PROP_T){ .name = (n), .type = "integer", .description = (d), \
        .required = FALSE }

#define TOOL_SCHEMA_INT_RANGE(n, d, lo, hi) \
    &(WUKONG_TOOL_SCHEMA_PROP_T){ .name = (n), .type = "integer", .description = (d), \
        .required = TRUE, .has_minimum = TRUE, .minimum = (lo), \
        .has_maximum = TRUE, .maximum = (hi) }

#define TOOL_SCHEMA_INT_OPT_RANGE(n, d, lo, hi) \
    &(WUKONG_TOOL_SCHEMA_PROP_T){ .name = (n), .type = "integer", .description = (d), \
        .required = FALSE, .has_minimum = TRUE, .minimum = (lo), \
        .has_maximum = TRUE, .maximum = (hi) }

#define TOOL_SCHEMA_STR(n, d) \
    &(WUKONG_TOOL_SCHEMA_PROP_T){ .name = (n), .type = "string", .description = (d), \
        .required = TRUE }

#define TOOL_SCHEMA_STR_ENUM(n, d, v) \
    &(WUKONG_TOOL_SCHEMA_PROP_T){ .name = (n), .type = "string", .description = (d), \
        .required = TRUE, .enum_values = (v) }

#define TOOL_SCHEMA_STR_OPT(n, d) \
    &(WUKONG_TOOL_SCHEMA_PROP_T){ .name = (n), .type = "string", .description = (d), \
        .required = FALSE }

#define TOOL_SCHEMA_STR_ENUM_OPT(n, d, v) \
    &(WUKONG_TOOL_SCHEMA_PROP_T){ .name = (n), .type = "string", .description = (d), \
        .required = FALSE, .enum_values = (v) }

#define TOOL_SCHEMA_BOOL(n, d) \
    &(WUKONG_TOOL_SCHEMA_PROP_T){ .name = (n), .type = "boolean", .description = (d), \
        .required = TRUE }

#define TOOL_SCHEMA_BOOL_OPT(n, d) \
    &(WUKONG_TOOL_SCHEMA_PROP_T){ .name = (n), .type = "boolean", .description = (d), \
        .required = FALSE }

#define TOOL_SCHEMA_NUM(n, d) \
    &(WUKONG_TOOL_SCHEMA_PROP_T){ .name = (n), .type = "number", .description = (d), \
        .required = TRUE }

#define TOOL_SCHEMA_NUM_OPT(n, d) \
    &(WUKONG_TOOL_SCHEMA_PROP_T){ .name = (n), .type = "number", .description = (d), \
        .required = FALSE }

#define TOOL_SCHEMA_ARRAY(n, d) \
    &(WUKONG_TOOL_SCHEMA_PROP_T){ .name = (n), .type = "array", .description = (d), \
        .required = TRUE }

#define TOOL_SCHEMA_ARRAY_OPT(n, d) \
    &(WUKONG_TOOL_SCHEMA_PROP_T){ .name = (n), .type = "array", .description = (d), \
        .required = FALSE }

#define TOOL_SCHEMA_OBJ(n, d) \
    &(WUKONG_TOOL_SCHEMA_PROP_T){ .name = (n), .type = "object", .description = (d), \
        .required = TRUE }

#define TOOL_SCHEMA_OBJ_OPT(n, d) \
    &(WUKONG_TOOL_SCHEMA_PROP_T){ .name = (n), .type = "object", .description = (d), \
        .required = FALSE }

#define TOOL_SCHEMA_END  NULL

/* ========================================================================== */
/*                           Tool Registration                                */
/* ========================================================================== */

/**
 * Register a tool. @p flags is an OR of WUKONG_TOOL_* visibility bits deciding
 * which callers may reach it. Variadic args are WUKONG_TOOL_SCHEMA_PROP_T*
 * pointers terminated by TOOL_SCHEMA_END (NULL).
 */
OPERATE_RET wukong_tool_register(CONST CHAR_T *name,
                                 CONST CHAR_T *description,
                                 WUKONG_TOOL_HANDLER_CB handler,
                                 VOID *user_data, UINT_T flags, ...);

/**
 * Convenience macro: appends TOOL_SCHEMA_END automatically.
 */
#define WUKONG_TOOL_ADD(name, desc, handler, ud, flags, ...) \
    wukong_tool_register(name, desc, handler, ud, flags, ##__VA_ARGS__, TOOL_SCHEMA_END)

/* ========================================================================== */
/*                              Tool Execution                                */
/* ========================================================================== */

/**
 * Execute a registered tool by name and shape its result for @p caller.
 *
 * Visibility (@p caller flag) is enforced: a tool whose flags omit @p caller is
 * treated as not found. Arguments are validated (required + numeric/enum
 * constraints) and JSON-encoded string arguments are promoted to their declared
 * types before the handler runs. A value still mismatching its declared type
 * after promotion is rejected as a business error ("Invalid argument '<name>':
 * must be a JSON <type>" in *out_text) so the model can self-correct — it is
 * never silently passed through to the handler.
 *
 * @param[in]  caller    The calling surface (one WUKONG_TOOL_* bit).
 * @param[in]  name      Tool name.
 * @param[in]  args      Raw JSON arguments object (may be NULL).
 * @param[out] out_text  Optional; set to a heap string (caller tal_free) with
 *                       the result: for WUKONG_TOOL_AGENT the text content is
 *                       flattened, otherwise the content array is serialized.
 *                       NULL when the handler produced no content.
 * @return OPRT_OK on success. A non-OK return with a non-NULL *out_text is a
 *         business error whose message the caller must consume (and tal_free).
 */
OPERATE_RET wukong_tool_exec(UINT_T caller, CONST CHAR_T *name,
                             CONST ty_cJSON *args, CHAR_T **out_text);

/**
 * Snapshot all tools visible to @p caller as a new JSON array
 * [{name,description,inputSchema}], unpaginated. inputSchema entries are
 * references into registry storage (do not outlive the array). Caller owns the
 * returned array (ty_cJSON_Delete).
 */
ty_cJSON *wukong_tool_list(UINT_T caller);

/**
 * Caller-filtered tool snapshot with optional payload-size pagination (the MCP
 * transport's tools/list path). @p max_payload > 0 trims the array to that byte
 * budget and reports the next start via @p out_next_cursor (points into registry
 * storage, borrow-only); == 0 returns every visible tool. @p cursor resumes from
 * a prior next-cursor (NULL/"" starts at the head). wukong_tool_list is the
 * unpaginated special case. Caller owns the returned array (ty_cJSON_Delete).
 */
ty_cJSON *wukong_tool_collect(UINT_T caller, CONST CHAR_T *cursor,
                              INT_T max_payload, CONST CHAR_T **out_next_cursor);

/**
 * Build a backend-specific tools[] JSON string from the agent-visible tool
 * snapshot. Rebuilt every call (not cached).
 * @param[in] backend  Target LLM backend format.
 * @return Heap string (caller must free), or NULL if no tools are visible or
 *         the backend is not yet supported (M2b: Anthropic -> NULL).
 */
CHAR_T *wukong_tool_build_schema(WUKONG_LLM_BACKEND_E backend);

/**
 * Tear down the registry, freeing every registered entry. Registry lifetime is
 * independent of the MCP transport; kept mainly for test teardown.
 */
VOID mcp_tools_cap_destroy(VOID);

/* ========================================================================== */
/*                           MIME Type Constants                              */
/* ========================================================================== */

#define MCP_MIME_JPEG               "image/jpeg"
#define MCP_MIME_PNG                "image/png"
#define MCP_MIME_TEXT               "text/plain"
#define MCP_MIME_JSON               "application/json"

/* ========================================================================== */
/*                          Content Builder Helpers                           */
/* ========================================================================== */

/**
 * Build a text content item: {"type":"text","text":"..."}
 * @return Newly allocated ty_cJSON object, or NULL on failure. Caller owns it.
 */
ty_cJSON *wukong_tool_make_text(CONST CHAR_T *text);

/**
 * Build an image content item with raw binary data (auto base64-encodes).
 * {"type":"image","data":"<base64>","mimeType":"..."}
 * @return Newly allocated ty_cJSON object, or NULL on failure. Caller owns it.
 */
ty_cJSON *wukong_tool_make_image(CONST CHAR_T *mime_type,
                                 CONST VOID *data, UINT_T data_len);

/**
 * Build an image content item from an already-encoded base64 string.
 * @return Newly allocated ty_cJSON object, or NULL on failure.
 */
ty_cJSON *wukong_tool_make_image_base64(CONST CHAR_T *mime_type,
                                        CONST CHAR_T *base64_data);

/**
 * Build an embedded resource content item.
 * {"type":"resource","resource":{"uri":"...","mimeType":"...","text":"..."}}
 * @return Newly allocated ty_cJSON object, or NULL on failure.
 */
ty_cJSON *wukong_tool_make_resource(CONST CHAR_T *uri,
                                    CONST CHAR_T *mime_type,
                                    CONST CHAR_T *text);

/* ========================================================================== */
/*                          Aggregated Registration                           */
/* ========================================================================== */

/**
 * Register the enabled tool modules onto the registry (gating lives here).
 * Implemented in toolkits/tools/tool_init.c.
 */
OPERATE_RET wukong_tools_init(VOID_T);

#ifdef __cplusplus
}
#endif
