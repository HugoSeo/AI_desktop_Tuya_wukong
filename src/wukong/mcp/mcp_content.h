/**
 * @file mcp_content.h
 * @brief MCP content builder helpers and MIME constants
 * @version 3.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __MCP_CONTENT_H__
#define __MCP_CONTENT_H__

#include "tuya_cloud_types.h"
#include "ty_cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

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
 * @param[in] text  The text string
 * @return Newly allocated ty_cJSON object, or NULL on failure. Caller owns it.
 */
ty_cJSON *mcp_content_make_text(CONST CHAR_T *text);

/**
 * Build an image content item with raw binary data (auto base64-encodes).
 * {"type":"image","data":"<base64>","mimeType":"..."}
 * @param[in] mime_type  MIME type (e.g. MCP_MIME_JPEG)
 * @param[in] data       Raw image bytes
 * @param[in] data_len   Length of data
 * @return Newly allocated ty_cJSON object, or NULL on failure. Caller owns it.
 */
ty_cJSON *mcp_content_make_image(CONST CHAR_T *mime_type,
                                  CONST VOID *data, UINT_T data_len);

/**
 * Build an image content item from already-encoded base64 string.
 * @param[in] mime_type    MIME type
 * @param[in] base64_data  Base64-encoded image string
 * @return Newly allocated ty_cJSON object, or NULL on failure.
 */
ty_cJSON *mcp_content_make_image_base64(CONST CHAR_T *mime_type,
                                         CONST CHAR_T *base64_data);

/**
 * Build an embedded resource content item.
 * {"type":"resource","resource":{"uri":"...","mimeType":"...","text":"..."}}
 * @param[in] uri        Resource URI
 * @param[in] mime_type  MIME type (may be NULL)
 * @param[in] text       Text content of the resource
 * @return Newly allocated ty_cJSON object, or NULL on failure.
 */
ty_cJSON *mcp_content_make_resource(CONST CHAR_T *uri,
                                     CONST CHAR_T *mime_type,
                                     CONST CHAR_T *text);

/* ========================================================================== */
/*                            Base64 Utility                                  */
/* ========================================================================== */

/**
 * Base64 encode binary data.
 * @param[in]  input       Input bytes
 * @param[in]  input_len   Length of input
 * @param[out] output      Pre-allocated output buffer
 * @param[in]  output_len  Size of output buffer
 * @return OPRT_OK on success
 */
OPERATE_RET mcp_base64_encode(CONST VOID *input, UINT_T input_len,
                               CHAR_T *output, UINT_T output_len);

#ifdef __cplusplus
}
#endif

#endif /* __MCP_CONTENT_H__ */
