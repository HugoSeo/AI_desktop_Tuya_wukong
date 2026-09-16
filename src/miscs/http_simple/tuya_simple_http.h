/**
 * @file tuya_simple_http.h
 * @brief Simplified HTTP client interface without business logic
 * @date 2026-01-13
 */

#ifndef __TUYA_SIMPLE_HTTP_H__
#define __TUYA_SIMPLE_HTTP_H__

#include "tuya_cloud_types.h"
#include "httpc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Buffer / chunk configuration
 */
#define SIMPLE_HTTP_CHUNK_DATA_PER_BLOCK (2 * 1024)      /**< Per-block read size for chunked POST */
#define SIMPLE_HTTP_CHUNK_DATA_MAX_LEN   (512 * 1024)    /**< Max accumulated size for chunked POST */

/**
 * @brief Memory allocation functions (can be replaced for different memory types)
 */
#define SIMPLE_HTTP_MALLOC(size)  tal_psram_malloc(size)
#define SIMPLE_HTTP_FREE(ptr)     tal_psram_free(ptr)

/**
 * @brief Optional per-request configuration.
 *
 * All fields default to 0 / NULL — no custom request headers, no response-header
 * callback, no extra field flags.  Pass NULL for the entire struct to accept all
 * defaults.
 *
 * Callers that need a custom socket timeout should call
 * http_set_timeout(session, ms) inside add_head_cb; this avoids touching the
 * global http_recv_timeout and provides per-session, millisecond-granularity
 * control.
 */
typedef struct {
    HTTP_HEAD_ADD_CB      add_head_cb;       /**< Inject custom request headers. NULL = none. */
    VOID_T               *add_head_data;     /**< Opaque data forwarded to add_head_cb. */
    HTTP_HEAD_ADD_CB      resp_hdr_cb;       /**< Called after response headers arrive, before body
                                              *   is read; use http_get_response_hdr_value() to
                                              *   extract header values.  NULL = skip. */
    VOID_T               *resp_hdr_cb_data;  /**< Opaque data forwarded to resp_hdr_cb. */
    http_hdr_field_sel_t  field_flags;       /**< Extra request-header flags (POST only). 0 = none. */
} simple_http_opts_t;

/**
 * @brief HTTP response — pure output.
 *
 * The library always zeroes this struct on entry; the caller does not need to
 * initialise it before the call.  Free data with SIMPLE_HTTP_FREE() after use.
 */
typedef struct {
    BYTE_T  *data;       /**< Response body (NUL-terminated); NULL if empty. Free with SIMPLE_HTTP_FREE(). */
    UINT_T   len;        /**< Response body length in bytes. */
    INT_T    http_code;  /**< HTTP status code (200, 404, …). */
} simple_http_response_t;

/**
 * @brief Simplified HTTP GET request.
 *
 * @param[in]  url      Target URL.
 * @param[in]  opts     Per-request options.  NULL = all defaults.
 * @param[out] response Filled by the library on success.
 *                      Free response->data with SIMPLE_HTTP_FREE() when done.
 * @return OPRT_OK on success.
 */
OPERATE_RET tuya_simple_http_get(IN CONST CHAR_T              *url,
                                  IN CONST simple_http_opts_t  *opts,
                                  OUT      simple_http_response_t *response);

/**
 * @brief Simplified HTTP POST request.
 *
 * STANDARD_HDR_FLAGS | HDR_ADD_CONN_KEEP_ALIVE are always included;
 * opts->field_flags are OR-ed in on top.
 *
 * @param[in]  url      Target URL.
 * @param[in]  data     POST body.
 * @param[in]  len      POST body length in bytes.
 * @param[in]  opts     Per-request options.  NULL = all defaults.
 * @param[out] response Filled by the library on success.
 *                      Free response->data with SIMPLE_HTTP_FREE() when done.
 * @return OPRT_OK on success.
 */
OPERATE_RET tuya_simple_http_post(IN CONST CHAR_T              *url,
                                   IN CONST BYTE_T              *data,
                                   IN CONST UINT_T               len,
                                   IN CONST simple_http_opts_t  *opts,
                                   OUT      simple_http_response_t *response);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_SIMPLE_HTTP_H__ */
