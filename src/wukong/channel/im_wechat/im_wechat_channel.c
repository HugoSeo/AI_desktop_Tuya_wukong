/**
 * @file im_wechat_channel.c
 * @brief WeChat iLink Bot channel implementation for the wukong AI framework.
 *
 * Implements the WUKONG_AI_CHAN_T vtable:
 *   init   — load token from KV / cfg; create mutex
 *   start  — launch poll thread
 *   stop   — request graceful shutdown
 *   deinit — release resources
 *   send   — dispatch TEXT → __wechat_send_text, IMAGE → __wechat_send_image
 *
 * Inbound messages arrive via long-poll (getupdates) and are forwarded to
 * wukong_ai_channel_input().  QR login is triggered by the application via
 * QR login and long-polling run in a single state-machine thread.
 *
 * @version 1.0
 * @date 2026-06-09
 * @copyright Copyright (c) Tuya Inc.
 */

#include "im_wechat_channel.h"
#include "tuya_cert_manager.h"
#include "wukong_ai_channel.h"
#include "tuya_simple_http.h"
#include "httpc.h"
#include "tal_thread.h"
#include "tal_mutex.h"
#include "tal_memory.h"
#include "tal_system.h"
#include "tal_log.h"
#include "tal_symmetry.h"
#include "tal_hash.h"
#include "uni_base64.h"
#include "uni_random.h"
#include "tuya_ws_db.h"
#include "ty_cJSON.h"
#include "uni_log.h"
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Compile-time constants
 * --------------------------------------------------------------------------- */

#define WECHAT_DEFAULT_BASE_URL     "https://ilinkai.weixin.qq.com"
#define WECHAT_DEFAULT_CDN_BASE_URL "https://novac2c.cdn.weixin.qq.com/c2c"
#define WECHAT_APP_ID               "bot"
#define WECHAT_CLIENT_VERSION       "131329"
#define WECHAT_CHANNEL_VERSION      "wukong-wechat"
#define WECHAT_POLL_TIMEOUT_DEFAULT 35000
#define WECHAT_POLL_STACK_SIZE      (6 * 1024)
#define WECHAT_POLL_PRIORITY        THREAD_PRIO_1
#define WECHAT_MAX_MSG_LEN          4000
#define WECHAT_QR_MAX_WAIT_MS       (300 * 1000)
/* sendmessage retry: iLink can drop a message with HTTP 200 + {"ret":<non-zero>}
 * (e.g. "prepare failed", an unpublished/transient code the protocol spec says
 * to retry). 1 initial attempt + 1 retry, spaced by a short delay. */
#define WECHAT_SEND_MAX_ATTEMPTS    2
#define WECHAT_SEND_RETRY_DELAY_MS  300
#define WECHAT_QR_POLL_INTERVAL_MS  2000
#define WECHAT_CTX_CACHE_SLOTS      32
#define WECHAT_DEDUP_SLOTS          64

#define WECHAT_KV_TOKEN             "wx_token"
#define WECHAT_KV_BASEURL           "wx_baseurl"
#define WECHAT_KV_CURSOR            "wx_cursor"

/* ---------------------------------------------------------------------------
 * Helpers: bytes-to-hex and hex-to-bytes
 * --------------------------------------------------------------------------- */

/**
 * @brief Render @p n bytes as lowercase hex into @p hex (must be 2*n + 1 bytes).
 */
STATIC VOID_T __bytes_to_hex(CONST UINT8_T *bytes, UINT_T n, CHAR_T *hex)
{
    for (UINT_T i = 0; i < n; i++) {
        snprintf(hex + i * 2, 3, "%02x", bytes[i]);
    }
    hex[n * 2] = '\0';
}

/**
 * @brief Generate a unique client_id string: "tywx-{8 hex}{8 hex}".
 * @param[out] buf Buffer of at least 28 bytes.
 */
STATIC VOID_T __make_client_id(CHAR_T buf[28])
{
    snprintf(buf, 28, "tywx-%08x%08x", uni_random(), uni_random());
}

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */

/** Channel thread state machine. */
typedef enum {
    WECHAT_STATE_QR_GET  = 0, /**< GET /get_bot_qrcode, obtain ticket and QR URL. */
    WECHAT_STATE_QR_POLL,     /**< Poll /get_qrcode_status until confirmed/expired. */
    WECHAT_STATE_RUNNING,     /**< Long-poll /getupdates for incoming messages. */
} WECHAT_STATE_E;

/** Context shared between QR_GET and QR_POLL states. */
typedef struct {
    CHAR_T     ticket[256];
    CHAR_T     current_base[160];
    SYS_TIME_T deadline;
} WECHAT_QR_CTX_T;

/** One slot in the chat_id → context_token ring cache. */
typedef struct {
    CHAR_T chat_id[72];
    CHAR_T context_token[160];
} WECHAT_CTX_ENTRY_T;

/** Module-level state (static singleton). */
typedef struct {
    CHAR_T  base_url[160];
    CHAR_T  cdn_base_url[160];
    CHAR_T  token[256];

    INT_T   poll_timeout_ms;
    BOOL_T  configured;
    BOOL_T  stop_req;

    WECHAT_QR_NOTIFY_CB qr_notify_cb;

    THREAD_HANDLE poll_thread;
    MUTEX_HANDLE  lock;

    CHAR_T  *sync_buf;

    UINT64_T seen_ids[WECHAT_DEDUP_SLOTS];
    UINT32_T seen_idx;

    WECHAT_CTX_ENTRY_T ctx_cache[WECHAT_CTX_CACHE_SLOTS];
    UINT32_T           ctx_idx;
} WECHAT_CTX_T;

/* ---------------------------------------------------------------------------
 * File-scope variables
 * --------------------------------------------------------------------------- */

STATIC WECHAT_CTX_T s_ctx;

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */

STATIC OPERATE_RET  __wechat_init(CONST VOID_T *cfg);
STATIC OPERATE_RET  __wechat_start(VOID_T);
STATIC OPERATE_RET  __wechat_stop(VOID_T);
STATIC OPERATE_RET  __wechat_deinit(VOID_T);
STATIC OPERATE_RET  __wechat_send(CONST CHAR_T *chat_id, CONST WUKONG_AI_MSG_T *msg);
STATIC WECHAT_STATE_E __wechat_state_qr_get(WECHAT_QR_CTX_T *qr);
STATIC WECHAT_STATE_E __wechat_state_qr_poll(WECHAT_QR_CTX_T *qr);
STATIC WECHAT_STATE_E __wechat_state_running(VOID_T);

/* ---------------------------------------------------------------------------
 * Channel vtable
 * --------------------------------------------------------------------------- */

/**
 * @brief WeChat iLink channel descriptor exported for registration.
 * @note  WUKONG_CHAN_FLAG_IM: AI replies are routed via channel_output → __wechat_send,
 *        not through the board event_cb.
 */
CONST WUKONG_AI_CHAN_T g_wechat_channel = {
    .name   = WUKONG_CHAN_WECHAT,
    .flags  = WUKONG_CHAN_FLAG_IM,
    .init   = __wechat_init,
    .start  = __wechat_start,
    .stop   = __wechat_stop,
    .deinit = __wechat_deinit,
    .send   = __wechat_send,
};

/**
 * @brief Register and initialise the WeChat channel.
 * @param[in] cfg Channel configuration. Must not be NULL.
 * @return OPRT_OK on success.
 */
OPERATE_RET im_wechat_channel_register(CONST WUKONG_CHAN_WECHAT_CFG_T *cfg)
{
    if (cfg == NULL) {
        return OPRT_INVALID_PARM;
    }
    return wukong_ai_channel_register(&g_wechat_channel, (VOID_T *)cfg);
}

/* ---------------------------------------------------------------------------
 * Helpers: percent-encoding for CDN query params
 * --------------------------------------------------------------------------- */

/**
 * @brief Percent-encode a string into dst (RFC 3986 unreserved chars pass through).
 * @param[in]  src    Source string.
 * @param[out] dst    Destination buffer.
 * @param[in]  dst_sz Destination buffer size including NUL.
 * @return none
 */
STATIC VOID_T __url_encode(CONST CHAR_T *src, CHAR_T *dst, UINT_T dst_sz)
{
    CONST CHAR_T *p = src;
    UINT_T pos = 0;

    while (*p && pos + 4 < dst_sz) {
        UINT8_T c = (UINT8_T)*p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            dst[pos++] = (CHAR_T)c;
        } else {
            snprintf(dst + pos, dst_sz - pos, "%%%02X", c);
            pos += 3;
        }
        p++;
    }
    dst[pos] = '\0';
}

/* ---------------------------------------------------------------------------
 * Helpers: FNV-1a 64-bit deduplication
 * --------------------------------------------------------------------------- */

/**
 * @brief Compute FNV-1a 64-bit hash of a string.
 * @param[in] s Input string.
 * @return 64-bit hash value.
 */
STATIC UINT64_T __fnv1a64(CONST CHAR_T *s)
{
    UINT64_T h = 14695981039346656037ULL;
    while (*s) {
        h ^= (UINT8_T)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

/**
 * @brief Check if a message_id has been seen recently; record it if not.
 * @param[in] msg_id String representation of the message ID.
 * @return TRUE if already seen (should be dropped), FALSE if new.
 */
STATIC BOOL_T __dedup_check(CONST CHAR_T *msg_id)
{
    UINT64_T h = __fnv1a64(msg_id);

    for (UINT32_T i = 0; i < WECHAT_DEDUP_SLOTS; i++) {
        if (s_ctx.seen_ids[i] == h) {
            return TRUE;
        }
    }
    s_ctx.seen_ids[s_ctx.seen_idx % WECHAT_DEDUP_SLOTS] = h;
    s_ctx.seen_idx++;
    return FALSE;
}

/* ---------------------------------------------------------------------------
 * Helpers: context_token cache
 * --------------------------------------------------------------------------- */

/**
 * @brief Store or update a chat_id → context_token mapping.
 * @param[in] chat_id       Chat identifier.
 * @param[in] context_token Token string from the incoming message.
 * @return none
 */
STATIC VOID_T __ctx_cache_put(CONST CHAR_T *chat_id, CONST CHAR_T *context_token)
{
    for (UINT32_T i = 0; i < WECHAT_CTX_CACHE_SLOTS; i++) {
        if (s_ctx.ctx_cache[i].chat_id[0] != '\0' &&
            strncmp(s_ctx.ctx_cache[i].chat_id, chat_id,
                    sizeof(s_ctx.ctx_cache[i].chat_id) - 1) == 0) {
            strncpy(s_ctx.ctx_cache[i].context_token, context_token,
                    sizeof(s_ctx.ctx_cache[i].context_token) - 1);
            s_ctx.ctx_cache[i].context_token[sizeof(s_ctx.ctx_cache[i].context_token) - 1] = '\0';
            return;
        }
    }
    UINT32_T slot = s_ctx.ctx_idx % WECHAT_CTX_CACHE_SLOTS;
    strncpy(s_ctx.ctx_cache[slot].chat_id, chat_id,
            sizeof(s_ctx.ctx_cache[slot].chat_id) - 1);
    s_ctx.ctx_cache[slot].chat_id[sizeof(s_ctx.ctx_cache[slot].chat_id) - 1] = '\0';
    strncpy(s_ctx.ctx_cache[slot].context_token, context_token,
            sizeof(s_ctx.ctx_cache[slot].context_token) - 1);
    s_ctx.ctx_cache[slot].context_token[sizeof(s_ctx.ctx_cache[slot].context_token) - 1] = '\0';
    s_ctx.ctx_idx++;
}

/**
 * @brief Look up a context_token for the given chat_id.
 * @param[in]  chat_id Chat identifier.
 * @param[out] out     Buffer to receive the token (may be empty string if not found).
 * @param[in]  out_sz  Size of out buffer.
 * @return none
 */
STATIC VOID_T __ctx_cache_get(CONST CHAR_T *chat_id, CHAR_T *out, UINT_T out_sz)
{
    out[0] = '\0';
    for (UINT32_T i = 0; i < WECHAT_CTX_CACHE_SLOTS; i++) {
        if (s_ctx.ctx_cache[i].chat_id[0] != '\0' &&
            strncmp(s_ctx.ctx_cache[i].chat_id, chat_id,
                    sizeof(s_ctx.ctx_cache[i].chat_id) - 1) == 0) {
            strncpy(out, s_ctx.ctx_cache[i].context_token, out_sz - 1);
            out[out_sz - 1] = '\0';
            return;
        }
    }
}

/* ---------------------------------------------------------------------------
 * HTTP: custom header callback (auth + iLink protocol headers)
 * --------------------------------------------------------------------------- */

/**
 * @brief HTTP header injection callback for WeChat iLink authenticated requests.
 *
 * Adds Authorization, AuthorizationType, iLink-App-Id, iLink-App-ClientVersion,
 * and a fresh random X-WECHAT-UIN on every call.
 *
 * @param[in] session HTTP session handle.
 * @param[in] data    Pointer to WECHAT_CTX_T (s_ctx).
 * @return none
 */
STATIC VOID_T __wechat_add_headers(http_session_t session, VOID_T *data)
{
    /* data = INT_T *timeout_ms; 0 or NULL means use the library default. */
    if (data != NULL) {
        INT_T ms = *(INT_T *)data;
        if (ms > 0) {
            http_set_timeout(session, ms);
        }
    }

    CHAR_T auth_buf[288];
    snprintf(auth_buf, sizeof(auth_buf), "Bearer %s", s_ctx.token);
    http_add_header(session, NULL, "Authorization", auth_buf);
    http_add_header(session, NULL, "AuthorizationType", "ilink_bot_token");
    http_add_header(session, NULL, "iLink-App-Id", WECHAT_APP_ID);
    http_add_header(session, NULL, "iLink-App-ClientVersion", WECHAT_CLIENT_VERSION);

    /* X-WECHAT-UIN: decimal random uint32 → base64 */
    CHAR_T uin_dec[12];
    snprintf(uin_dec, sizeof(uin_dec), "%u", uni_random());
    /* base64 output: ceil(len * 4/3) + padding + NUL, 12 bytes → ~20 */
    CHAR_T uin_b64[24];
    tuya_base64_encode((CONST UINT8_T *)uin_dec, uin_b64, (INT_T)strlen(uin_dec));
    http_add_header(session, NULL, "X-WECHAT-UIN", uin_b64);
}

/* ---------------------------------------------------------------------------
 * HTTP: JSON POST to WeChat API endpoint
 * --------------------------------------------------------------------------- */

/**
 * @brief POST a JSON body to a WeChat iLink endpoint and parse the response.
 *
 * Builds the full URL from s_ctx.base_url + "/ilink/bot/" + ep, attaches
 * authentication headers via __wechat_add_headers, and parses the response
 * body as JSON.
 *
 * @param[in]  ep      Endpoint suffix, e.g. "getupdates".
 * @param[in]  body    Null-terminated JSON request body.
 * @param[out] pp_json Output cJSON object; caller must ty_cJSON_Delete().
 *                     Set to NULL on error.
 * @param[in]  timeout_ms Request timeout in milliseconds.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __wechat_api_post(CONST CHAR_T *ep, CONST CHAR_T *body,
                                     ty_cJSON **pp_json, INT_T timeout_ms)
{
    CHAR_T url[320];
    snprintf(url, sizeof(url), "%s/ilink/bot/%s", s_ctx.base_url, ep);

    simple_http_opts_t opts = {
        .add_head_cb   = __wechat_add_headers,
        .add_head_data = (VOID_T *)&timeout_ms,
        .field_flags   = HDR_ADD_CONTENT_TYPE_JSON,
    };
    simple_http_response_t resp;
    OPERATE_RET rt = tuya_simple_http_post(
        url,
        (CONST BYTE_T *)body,
        (UINT_T)strlen(body),
        &opts,
        &resp
    );

    if (rt != OPRT_OK) {
        PR_WARN("wechat api_post %s failed: %d", ep, rt);
        *pp_json = NULL;
        return rt;
    }

    if (resp.http_code < 200 || resp.http_code >= 300) {
        PR_WARN("wechat api_post %s HTTP %d", ep, resp.http_code);
        SIMPLE_HTTP_FREE(resp.data);
        *pp_json = NULL;
        return OPRT_COM_ERROR;
    }

    if (resp.data == NULL || resp.len == 0) {
        *pp_json = ty_cJSON_CreateObject();
        return OPRT_OK;
    }

    *pp_json = ty_cJSON_ParseWithLength((CONST CHAR_T *)resp.data, resp.len);
    SIMPLE_HTTP_FREE(resp.data);

    if (*pp_json == NULL) {
        PR_WARN("wechat api_post %s: JSON parse failed", ep);
        return OPRT_CJSON_PARSE_ERR;
    }
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * HTTP: GET with full response body accumulation
 * --------------------------------------------------------------------------- */

/**
 * @brief Set per-session socket timeout; used as add_head_cb for unauthenticated GETs.
 * @param[in] session HTTP session handle.
 * @param[in] data    Pointer to INT_T timeout in milliseconds.
 */
STATIC VOID_T __wechat_timeout_cb(http_session_t session, VOID_T *data)
{
    if (data != NULL) {
        INT_T ms = *(INT_T *)data;
        if (ms > 0) {
            http_set_timeout(session, ms);
        }
    }
}

/**
 * @brief HTTP GET wrapper used internally for WeChat JSON API calls.
 *
 * Delegates to tuya_simple_http_get().  The returned body is allocated by
 * SIMPLE_HTTP_MALLOC; callers must free it with SIMPLE_HTTP_FREE().
 *
 * @param[in]  url         Fully-qualified request URL.
 * @param[in]  timeout_ms  Per-session socket timeout in ms (set via http_set_timeout).
 * @param[out] pp_body     Receives pointer to NUL-terminated response body,
 *                         or NULL on empty response.
 * @param[out] p_http_code HTTP status code (e.g. 200).
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __wechat_api_get(CONST CHAR_T *url, INT_T timeout_ms,
                                    CHAR_T **pp_body, INT_T *p_http_code)
{
    *pp_body     = NULL;
    *p_http_code = 0;

    simple_http_opts_t opts = {
        .add_head_cb   = __wechat_timeout_cb,
        .add_head_data = (VOID_T *)&timeout_ms,
    };
    simple_http_response_t resp;
    OPERATE_RET rt = tuya_simple_http_get(url, &opts, &resp);
    if (rt != OPRT_OK) {
        PR_WARN("wechat api_get failed rt=%d url=%s", rt, url);
        return rt;
    }

    *p_http_code = resp.http_code;
    *pp_body     = (CHAR_T *)resp.data;   /* caller frees with SIMPLE_HTTP_FREE */
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * HTTP: binary POST to CDN, extract x-encrypted-param from response header
 * --------------------------------------------------------------------------- */

/**
 * @brief Upload encrypted binary data to the WeChat CDN.
 *
 * Sends a raw binary POST (Content-Type: application/octet-stream) and
 * copies the x-encrypted-param response header before closing the session.
 *
 * @param[in]  url       Full CDN upload URL.
 * @param[in]  data      Encrypted data buffer.
 * @param[in]  data_len  Data length in bytes.
 * @param[out] param_out Buffer for the x-encrypted-param value.
 * @param[in]  param_sz  Size of param_out.
 * @return OPRT_OK on success.
 */
/* ---------------------------------------------------------------------------
 * CDN binary upload helper
 * --------------------------------------------------------------------------- */

/**
 * @brief HTTP_HEAD_ADD_CB that injects Content-Type: application/octet-stream.
 * @param[in] session HTTP session handle.
 * @param[in] data    Unused.
 * @return none
 */
STATIC VOID_T __cdn_add_octet_stream(http_session_t session, VOID_T *data)
{
    (VOID_T)data;
    http_add_header(session, NULL, "Content-Type", "application/octet-stream");
}

/** Context for the CDN upload response header callback. */
typedef struct {
    CHAR_T *param_out; /**< Output buffer for the x-encrypted-param value. */
    UINT_T  param_sz;  /**< Size of param_out in bytes. */
} __cdn_head_ctx_t;

/**
 * @brief resp_hdr_cb: copies x-encrypted-param into param_out.
 * @param[in] session HTTP session handle.
 * @param[in] data    Pointer to __cdn_head_ctx_t.
 * @return none
 */
STATIC VOID_T __cdn_extract_encrypted_param(http_session_t session, VOID_T *data)
{
    __cdn_head_ctx_t *ctx = (__cdn_head_ctx_t *)data;
    CHAR_T *hdr_val = NULL;
    if (http_get_response_hdr_value(session, "x-encrypted-param", &hdr_val) == 0 &&
        hdr_val != NULL) {
        strncpy(ctx->param_out, hdr_val, ctx->param_sz - 1);
        ctx->param_out[ctx->param_sz - 1] = '\0';
    }
}

/**
 * @brief Upload encrypted binary data to the WeChat CDN endpoint.
 *
 * POSTs raw bytes to @p url and retrieves the x-encrypted-param response
 * header which the caller needs for the subsequent sendmessage call.
 *
 * @param[in]  url       CDN upload URL (HTTPS).
 * @param[in]  data      Encrypted payload bytes.
 * @param[in]  data_len  Byte count of @p data.
 * @param[out] param_out Buffer that receives the x-encrypted-param value.
 * @param[in]  param_sz  Size of @p param_out in bytes.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __wechat_cdn_upload(CONST CHAR_T *url,
                                       CONST UINT8_T *data, UINT32_T data_len,
                                       CHAR_T *param_out, UINT_T param_sz)
{
    param_out[0] = '\0';

    __cdn_head_ctx_t head_ctx = { param_out, param_sz };
    simple_http_opts_t opts = {
        .add_head_cb      = __cdn_add_octet_stream,
        .resp_hdr_cb      = __cdn_extract_encrypted_param,
        .resp_hdr_cb_data = &head_ctx,
    };
    simple_http_response_t resp;
    OPERATE_RET rt = tuya_simple_http_post(url, data, data_len, &opts, &resp);
    if (resp.data != NULL) {
        SIMPLE_HTTP_FREE(resp.data);
    }
    if (rt != OPRT_OK) {
        PR_WARN("wechat cdn_upload request failed: %d", rt);
        return rt;
    }
    if (resp.http_code < 200 || resp.http_code >= 300) {
        PR_WARN("wechat cdn_upload HTTP %d", resp.http_code);
        return OPRT_COM_ERROR;
    }
    if (param_out[0] == '\0') {
        PR_WARN("wechat cdn_upload: missing x-encrypted-param");
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Send: common JSON skeleton builder
 * --------------------------------------------------------------------------- */

/**
 * @brief Create the common sendmessage JSON skeleton: {msg:{…}, base_info:{…}}.
 *
 * Returns a root object with msg and base_info pre-filled (from_user_id="",
 * to_user_id=chat_id, client_id, message_type=2, message_state=2, context_token
 * if available, channel_version).  The caller adds items to msg→item_list,
 * serialises, posts to "sendmessage", and cleans up.
 *
 * @param[in]  chat_id    Destination chat identifier.
 * @param[out] pp_items   Receives the item_list cJSON array to populate.
 * @return Root cJSON object, or NULL on allocation failure.
 */
STATIC ty_cJSON *__wechat_build_send_msg(CONST CHAR_T *chat_id, ty_cJSON **pp_items)
{
    CHAR_T client_id[28];
    __make_client_id(client_id);

    CHAR_T ctx_tok[160];
    __ctx_cache_get(chat_id, ctx_tok, sizeof(ctx_tok));

    ty_cJSON *root      = ty_cJSON_CreateObject();
    ty_cJSON *msg       = ty_cJSON_CreateObject();
    ty_cJSON *items     = ty_cJSON_CreateArray();
    ty_cJSON *base_info = ty_cJSON_CreateObject();

    if (root == NULL || msg == NULL || items == NULL || base_info == NULL) {
        if (root)      { ty_cJSON_Delete(root); }
        if (msg)       { ty_cJSON_Delete(msg); }
        if (items)     { ty_cJSON_Delete(items); }
        if (base_info) { ty_cJSON_Delete(base_info); }
        return NULL;
    }

    ty_cJSON_AddStringToObject(msg, "from_user_id", "");
    ty_cJSON_AddStringToObject(msg, "to_user_id", chat_id);
    ty_cJSON_AddStringToObject(msg, "client_id", client_id);
    ty_cJSON_AddNumberToObject(msg, "message_type", 2);
    ty_cJSON_AddNumberToObject(msg, "message_state", 2);
    ty_cJSON_AddItemToObject(msg, "item_list", items);
    if (ctx_tok[0] != '\0') {
        ty_cJSON_AddStringToObject(msg, "context_token", ctx_tok);
    }

    ty_cJSON_AddStringToObject(base_info, "channel_version", WECHAT_CHANNEL_VERSION);
    ty_cJSON_AddItemToObject(root, "msg", msg);
    ty_cJSON_AddItemToObject(root, "base_info", base_info);

    *pp_items = items;
    return root;
}

/**
 * @brief Serialise, POST to sendmessage, and clean up a sendmessage JSON root.
 */
STATIC OPERATE_RET __wechat_post_send_json(ty_cJSON *root)
{
    CHAR_T *body = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    if (body == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    /* iLink returns HTTP 200 even when it drops the message:
     *   success -> {"message_id":<num>}                (no ret)
     *   failure -> {"ret":<non-zero>,"errmsg":"..."}    e.g. "prepare failed"
     * A non-zero "ret" is the sole failure signal. Retry on failure (the code
     * is unpublished/transient); resending the same body is safe because a
     * rejected message was never delivered (no client_id collision). */
    OPERATE_RET rt = OPRT_COM_ERROR;
    UINT_T attempt;
    for (attempt = 1; attempt <= WECHAT_SEND_MAX_ATTEMPTS; attempt++) {
        ty_cJSON *resp = NULL;
        BOOL_T sent = FALSE;

        if (attempt > 1) {
            PR_NOTICE("wechat sendmessage retry attempt=%u/%u", attempt, WECHAT_SEND_MAX_ATTEMPTS);
        }
        rt = __wechat_api_post("sendmessage", body, &resp, 15000);
        if (rt != OPRT_OK) {
            PR_WARN("wechat sendmessage http failed (attempt %u/%u) rt=%d",
                    attempt, WECHAT_SEND_MAX_ATTEMPTS, rt);
        } else if (resp == NULL) {
            sent = TRUE;   /* HTTP 200 with empty body: treat as delivered */
        } else {
            CHAR_T *rs = ty_cJSON_PrintUnformatted(resp);
            ty_cJSON *ret_j = ty_cJSON_GetObjectItem(resp, "ret");
            sent = !(ret_j != NULL && ty_cJSON_IsNumber(ret_j) && ret_j->valueint != 0);
            if (sent) {
                PR_DEBUG("wechat sendmessage ok: resp=%s", rs ? rs : "(null)");
            } else {
                PR_WARN("wechat sendmessage rejected (attempt %u/%u): resp=%s",
                        attempt, WECHAT_SEND_MAX_ATTEMPTS, rs ? rs : "(null)");
            }
            if (rs != NULL) {
                ty_cJSON_FreeBuffer(rs);
            }
        }
        if (resp != NULL) {
            ty_cJSON_Delete(resp);
        }

        if (sent) {
            rt = OPRT_OK;
            break;
        }
        rt = OPRT_COM_ERROR;
        if (attempt < WECHAT_SEND_MAX_ATTEMPTS) {
            tal_system_sleep(WECHAT_SEND_RETRY_DELAY_MS);
        }
    }

    tal_free(body);
    return rt;
}

/* ---------------------------------------------------------------------------
 * Send: text message
 * --------------------------------------------------------------------------- */

/**
 * @brief Send a text reply to a WeChat chat, splitting into ≤4000-byte chunks.
 *
 * UTF-8 chunk boundaries are adjusted so no multi-byte sequence is split.
 *
 * @param[in] chat_id Destination chat identifier.
 * @param[in] text    Null-terminated UTF-8 text to send.
 * @return OPRT_OK if all chunks were sent successfully.
 */
STATIC OPERATE_RET __wechat_send_text(CONST CHAR_T *chat_id, CONST CHAR_T *text)
{
    if (chat_id == NULL || text == NULL) {
        return OPRT_INVALID_PARM;
    }

    CONST UINT8_T *src = (CONST UINT8_T *)text;
    UINT_T total = (UINT_T)strlen(text);
    UINT_T offset = 0;
    OPERATE_RET rt = OPRT_OK;

    while (offset < total) {
        UINT_T chunk = (total - offset > WECHAT_MAX_MSG_LEN)
                       ? WECHAT_MAX_MSG_LEN : (total - offset);

        /* Ensure we don't cut a multi-byte UTF-8 sequence */
        while (chunk > 0 && (src[offset + chunk] & 0xC0) == 0x80) {
            chunk--;
        }
        if (chunk == 0) {
            break;
        }

        ty_cJSON *items = NULL;
        ty_cJSON *root = __wechat_build_send_msg(chat_id, &items);
        if (root == NULL) {
            rt = OPRT_MALLOC_FAILED;
            break;
        }

        /* Build text item */
        CHAR_T *chunk_str = (CHAR_T *)tal_malloc(chunk + 1);
        if (chunk_str == NULL) {
            ty_cJSON_Delete(root);
            rt = OPRT_MALLOC_FAILED;
            break;
        }
        memcpy(chunk_str, src + offset, chunk);
        chunk_str[chunk] = '\0';

        ty_cJSON *item      = ty_cJSON_CreateObject();
        ty_cJSON *text_item = ty_cJSON_CreateObject();
        if (item == NULL || text_item == NULL) {
            if (item) { ty_cJSON_Delete(item); }
            tal_free(chunk_str);
            ty_cJSON_Delete(root);
            rt = OPRT_MALLOC_FAILED;
            break;
        }

        ty_cJSON_AddStringToObject(text_item, "text", chunk_str);
        tal_free(chunk_str);

        ty_cJSON_AddNumberToObject(item, "type", 1);
        ty_cJSON_AddItemToObject(item, "text_item", text_item);
        ty_cJSON_AddItemToArray(items, item);

        rt = __wechat_post_send_json(root);
        if (rt != OPRT_OK) {
            PR_WARN("wechat send_text chunk failed: %d", rt);
            break;
        }

        offset += chunk;
    }
    return rt;
}

/* ---------------------------------------------------------------------------
 * Send: image message
 * --------------------------------------------------------------------------- */

/**
 * @brief Send an image to a WeChat chat via the 3-step CDN upload flow.
 *
 * Step 1: POST /ilink/bot/getuploadurl to obtain the CDN upload URL.
 * Step 2: AES-ECB-128 encrypt the image, POST to CDN, read x-encrypted-param.
 * Step 3: POST /ilink/bot/sendmessage with image_item payload.
 *
 * @param[in] chat_id  Destination chat identifier.
 * @param[in] img_data Raw image bytes (JPEG/PNG).
 * @param[in] img_len  Image length in bytes.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __wechat_send_image(CONST CHAR_T *chat_id,
                                       CONST UINT8_T *img_data, UINT_T img_len)
{
    if (chat_id == NULL || img_data == NULL || img_len == 0) {
        return OPRT_INVALID_PARM;
    }

    /* MD5 of plaintext */
    UINT8_T md5_raw[16];
    tal_md5_ret(img_data, img_len, md5_raw);
    CHAR_T plain_md5[33];
    __bytes_to_hex(md5_raw, 16, plain_md5);

    /* Random AES-128 key and filekey */
    UINT8_T aes_key_raw[16];
    uni_random_bytes(aes_key_raw, sizeof(aes_key_raw));
    CHAR_T aes_key_hex[33];
    __bytes_to_hex(aes_key_raw, 16, aes_key_hex);

    UINT8_T filekey_raw[16];
    uni_random_bytes(filekey_raw, sizeof(filekey_raw));
    CHAR_T filekey[33];
    __bytes_to_hex(filekey_raw, 16, filekey);

    /* Cipher length: PKCS7 padded to 16-byte boundary */
    UINT32_T cipher_len = ((img_len / 16) + 1) * 16;

    /* Step 1: getuploadurl */

    ty_cJSON *up_root = ty_cJSON_CreateObject();
    ty_cJSON *up_base = ty_cJSON_CreateObject();
    if (up_root == NULL || up_base == NULL) {
        if (up_root) { ty_cJSON_Delete(up_root); }
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddStringToObject(up_root, "filekey", filekey);
    ty_cJSON_AddNumberToObject(up_root, "media_type", 1);
    ty_cJSON_AddStringToObject(up_root, "to_user_id", chat_id);
    ty_cJSON_AddNumberToObject(up_root, "rawsize", (INT_T)img_len);
    ty_cJSON_AddStringToObject(up_root, "rawfilemd5", plain_md5);
    ty_cJSON_AddNumberToObject(up_root, "filesize", (INT_T)cipher_len);
    ty_cJSON_AddStringToObject(up_root, "aeskey", aes_key_hex);
    ty_cJSON_AddTrueToObject(up_root, "no_need_thumb");
    ty_cJSON_AddStringToObject(up_base, "channel_version", WECHAT_CHANNEL_VERSION);
    ty_cJSON_AddItemToObject(up_root, "base_info", up_base);

    CHAR_T *up_body = ty_cJSON_PrintUnformatted(up_root);
    ty_cJSON_Delete(up_root);
    if (up_body == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    ty_cJSON *up_resp = NULL;
    OPERATE_RET rt = __wechat_api_post("getuploadurl", up_body, &up_resp, 15000);
    tal_free(up_body);
    if (rt != OPRT_OK || up_resp == NULL) {
        if (up_resp) { ty_cJSON_Delete(up_resp); }
        return rt;
    }

    CHAR_T upload_url[512] = {0};
    ty_cJSON *j_full_url = ty_cJSON_GetObjectItem(up_resp, "upload_full_url");
    ty_cJSON *j_param    = ty_cJSON_GetObjectItem(up_resp, "upload_param");

    if (j_full_url != NULL && j_full_url->valuestring != NULL &&
        j_full_url->valuestring[0] != '\0') {
        strncpy(upload_url, j_full_url->valuestring, sizeof(upload_url) - 1);
    } else if (j_param != NULL && j_param->valuestring != NULL) {
        CHAR_T enc_param[512] = {0};
        CHAR_T enc_filekey[96] = {0};
        __url_encode(j_param->valuestring, enc_param, sizeof(enc_param));
        __url_encode(filekey, enc_filekey, sizeof(enc_filekey));
        snprintf(upload_url, sizeof(upload_url),
                 "%s/upload?encrypted_query_param=%s&filekey=%s",
                 s_ctx.cdn_base_url, enc_param, enc_filekey);
    }
    ty_cJSON_Delete(up_resp);

    if (upload_url[0] == '\0') {
        PR_WARN("wechat send_image: no upload URL");
        return OPRT_COM_ERROR;
    }

    /* Step 2: AES-ECB-128 encrypt, then CDN upload */
    UINT8_T *cipher_data = NULL;
    UINT32_T out_len = 0;
    rt = tal_aes128_ecb_encode((UINT8_T *)img_data, img_len,
                               &cipher_data, &out_len, aes_key_raw);
    if (rt != OPRT_OK || cipher_data == NULL) {
        PR_WARN("wechat send_image: AES encrypt failed: %d", rt);
        return OPRT_COM_ERROR;
    }

    CHAR_T download_param[256] = {0};
    rt = __wechat_cdn_upload(upload_url, cipher_data, out_len,
                             download_param, sizeof(download_param));
    tal_aes_free_data(cipher_data);

    if (rt != OPRT_OK) {
        return rt;
    }

    /* aes_key_b64: base64(aes_key_hex) */
    /* base64 of 32-byte string → max 48 bytes + NUL */
    CHAR_T aes_key_b64[52];
    tuya_base64_encode((CONST UINT8_T *)aes_key_hex, aes_key_b64,
                       (INT_T)strlen(aes_key_hex));

    /* Step 3: sendmessage with image_item */
    ty_cJSON *sm_items = NULL;
    ty_cJSON *sm_root = __wechat_build_send_msg(chat_id, &sm_items);
    if (sm_root == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    ty_cJSON *sm_item   = ty_cJSON_CreateObject();
    ty_cJSON *img_item  = ty_cJSON_CreateObject();
    ty_cJSON *media_obj = ty_cJSON_CreateObject();
    if (sm_item == NULL || img_item == NULL || media_obj == NULL) {
        if (sm_item)   { ty_cJSON_Delete(sm_item); }
        if (img_item)  { ty_cJSON_Delete(img_item); }
        if (media_obj) { ty_cJSON_Delete(media_obj); }
        ty_cJSON_Delete(sm_root);
        return OPRT_MALLOC_FAILED;
    }

    ty_cJSON_AddStringToObject(media_obj, "encrypt_query_param", download_param);
    ty_cJSON_AddStringToObject(media_obj, "aes_key", aes_key_b64);
    ty_cJSON_AddNumberToObject(media_obj, "encrypt_type", 1);
    ty_cJSON_AddItemToObject(img_item, "media", media_obj);
    ty_cJSON_AddNumberToObject(img_item, "mid_size", (INT_T)out_len);
    ty_cJSON_AddNumberToObject(sm_item, "type", 2);
    ty_cJSON_AddItemToObject(sm_item, "image_item", img_item);
    ty_cJSON_AddItemToArray(sm_items, sm_item);

    return __wechat_post_send_json(sm_root);
}

/* ---------------------------------------------------------------------------
 * Vtable: send dispatcher
 * --------------------------------------------------------------------------- */

/**
 * @brief Route an outbound message to the appropriate send function.
 * @param[in] chat_id Destination chat identifier.
 * @param[in] msg     Message to send (complete text from provider layer).
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __wechat_send(CONST CHAR_T *chat_id, CONST WUKONG_AI_MSG_T *msg)
{
    if (chat_id == NULL || msg == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (!s_ctx.configured) {
        PR_WARN("wechat send: channel not configured");
        return OPRT_COM_ERROR;
    }

    switch (msg->type) {
    case WUKONG_AI_MSG_TYPE_TEXT:
        PR_INFO("wechat send reply: chat_id=%s len=%u", chat_id, msg->data_len);
        return __wechat_send_text(chat_id, (CONST CHAR_T *)msg->data);
    case WUKONG_AI_MSG_TYPE_IMAGE:
        return __wechat_send_image(chat_id, msg->data, msg->data_len);
    default:
        PR_WARN("wechat send: unsupported msg type %d", msg->type);
        return OPRT_NOT_SUPPORTED;
    }
}

/* ---------------------------------------------------------------------------
 * Polling: parse one getupdates response
 * --------------------------------------------------------------------------- */

/**
 * @brief Append a null-terminated string to text_buf if there is room.
 * @param[in,out] buf  Text buffer.
 * @param[in,out] pos  Current write position in buf.
 * @param[in]     cap  Buffer capacity (bytes).
 * @param[in]     text Null-terminated string to append.
 * @return TRUE if text was appended.
 */
STATIC BOOL_T __text_buf_append(CHAR_T *buf, UINT_T *pos, UINT_T cap, CONST CHAR_T *text)
{
    UINT_T tlen = (UINT_T)strlen(text);
    if (tlen == 0) {
        return FALSE;
    }
    if (*pos + tlen < cap) {
        memcpy(buf + *pos, text, tlen);
        *pos += tlen;
        buf[*pos] = '\0';
        return TRUE;
    }
    return FALSE;
}

/**
 * @brief Execute one long-poll cycle: POST getupdates, parse messages,
 *        forward new text messages to wukong_ai_channel_input().
 * @return OPRT_OK on success (even if no messages arrived).
 */
STATIC OPERATE_RET __wechat_poll_once(VOID_T)
{
    /* Build request body */
    ty_cJSON *root      = ty_cJSON_CreateObject();
    ty_cJSON *base_info = ty_cJSON_CreateObject();
    if (root == NULL || base_info == NULL) {
        if (root) { ty_cJSON_Delete(root); }
        return OPRT_MALLOC_FAILED;
    }

    tal_mutex_lock(s_ctx.lock);
    CHAR_T *cursor_copy = NULL;
    if (s_ctx.sync_buf != NULL) {
        UINT_T clen = (UINT_T)strlen(s_ctx.sync_buf) + 1;
        cursor_copy = (CHAR_T *)tal_malloc(clen);
        if (cursor_copy != NULL) {
            memcpy(cursor_copy, s_ctx.sync_buf, clen);
        }
    }
    tal_mutex_unlock(s_ctx.lock);

    ty_cJSON_AddStringToObject(root, "get_updates_buf",
                               cursor_copy ? cursor_copy : "");
    ty_cJSON_AddStringToObject(base_info, "channel_version", WECHAT_CHANNEL_VERSION);
    ty_cJSON_AddItemToObject(root, "base_info", base_info);

    CHAR_T *body = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    if (cursor_copy != NULL) {
        tal_free(cursor_copy);
    }
    if (body == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    ty_cJSON *resp = NULL;
    OPERATE_RET rt = __wechat_api_post("getupdates", body, &resp,
                                       s_ctx.poll_timeout_ms + 5000);
    tal_free(body);

    if (rt != OPRT_OK || resp == NULL) {
        if (resp) { ty_cJSON_Delete(resp); }
        return rt;
    }

    /* Check ret / errcode before processing — session may have been revoked
     * (e.g. bot re-bound to another device via QR scan).  errcode -14 is the
     * well-known "session expired" signal from the WeChat iLink backend. */
    {
        ty_cJSON *j_ret    = ty_cJSON_GetObjectItem(resp, "ret");
        ty_cJSON *j_errcode = ty_cJSON_GetObjectItem(resp, "errcode");
        INT_T ret_val  = (j_ret     && ty_cJSON_IsNumber(j_ret))     ? j_ret->valueint     : 0;
        INT_T err_val  = (j_errcode && ty_cJSON_IsNumber(j_errcode)) ? j_errcode->valueint : 0;

        if (ret_val == -14 || err_val == -14) {
            PR_WARN("wechat session expired (ret=%d errcode=%d), clearing token and restarting QR login",
                    ret_val, err_val);
            tal_mutex_lock(s_ctx.lock);
            s_ctx.configured = FALSE;
            s_ctx.token[0] = '\0';
            tal_mutex_unlock(s_ctx.lock);
            /* Wipe stale token from KV so a cold boot also enters QR flow */
            (VOID_T)wd_common_write(WECHAT_KV_TOKEN, (CONST BYTE_T *)"", 0);
            ty_cJSON_Delete(resp);
            return OPRT_COM_ERROR;  /* caller will transition to QR_GET */
        }
    }

    /* Update cursor */
    ty_cJSON *j_cursor = ty_cJSON_GetObjectItem(resp, "get_updates_buf");
    if (j_cursor != NULL && j_cursor->valuestring != NULL) {
        UINT_T new_len = (UINT_T)strlen(j_cursor->valuestring) + 1;
        tal_mutex_lock(s_ctx.lock);
        if (s_ctx.sync_buf != NULL) {
            tal_free(s_ctx.sync_buf);
        }
        s_ctx.sync_buf = (CHAR_T *)tal_malloc(new_len);
        if (s_ctx.sync_buf != NULL) {
            memcpy(s_ctx.sync_buf, j_cursor->valuestring, new_len);
        }
        tal_mutex_unlock(s_ctx.lock);
        /* Persist cursor to KV */
        (VOID_T)wd_common_write(WECHAT_KV_CURSOR,
                                (CONST BYTE_T *)j_cursor->valuestring,
                                new_len);
    }

    /* Update dynamic poll timeout */
    ty_cJSON *j_timeout = ty_cJSON_GetObjectItem(resp, "longpolling_timeout_ms");
    if (j_timeout != NULL && ty_cJSON_IsNumber(j_timeout) &&
        j_timeout->valueint > 0) {
        s_ctx.poll_timeout_ms = j_timeout->valueint;
    }

    /* Process messages */
    ty_cJSON *msgs = ty_cJSON_GetObjectItem(resp, "msgs");
    if (msgs == NULL || !ty_cJSON_IsArray(msgs)) {
        ty_cJSON_Delete(resp);
        return OPRT_OK;
    }

    INT_T msg_count = ty_cJSON_GetArraySize(msgs);
    for (INT_T i = 0; i < msg_count; i++) {
        ty_cJSON *m = ty_cJSON_GetArrayItem(msgs, i);
        if (m == NULL) {
            continue;
        }

        /* Determine chat_id: group_id or from_user_id */
        CONST CHAR_T *chat_id = "";
        ty_cJSON *j_gid = ty_cJSON_GetObjectItem(m, "group_id");
        ty_cJSON *j_uid = ty_cJSON_GetObjectItem(m, "from_user_id");
        if (j_gid != NULL && j_gid->valuestring != NULL && j_gid->valuestring[0] != '\0') {
            chat_id = j_gid->valuestring;
        } else if (j_uid != NULL && j_uid->valuestring != NULL) {
            chat_id = j_uid->valuestring;
        }
        if (chat_id[0] == '\0') {
            continue;
        }

        /* Deduplication */
        ty_cJSON *j_msgid = ty_cJSON_GetObjectItem(m, "message_id");
        if (j_msgid != NULL && j_msgid->valuestring != NULL &&
            j_msgid->valuestring[0] != '\0') {
            if (__dedup_check(j_msgid->valuestring)) {
                continue;
            }
        }

        /* Update context_token cache */
        ty_cJSON *j_ct = ty_cJSON_GetObjectItem(m, "context_token");
        if (j_ct != NULL && j_ct->valuestring != NULL && j_ct->valuestring[0] != '\0') {
            __ctx_cache_put(chat_id, j_ct->valuestring);
        }

        /* Extract text from item_list */
        ty_cJSON *items = ty_cJSON_GetObjectItem(m, "item_list");
        if (items == NULL || !ty_cJSON_IsArray(items)) {
            continue;
        }

        CHAR_T *text_buf = (CHAR_T *)tal_malloc(WECHAT_MAX_MSG_LEN + 1);
        if (text_buf == NULL) {
            break;
        }
        text_buf[0] = '\0';
        UINT_T text_pos = 0;
        BOOL_T has_text = FALSE;

        INT_T item_count = ty_cJSON_GetArraySize(items);
        for (INT_T k = 0; k < item_count; k++) {
            ty_cJSON *it = ty_cJSON_GetArrayItem(items, k);
            if (it == NULL) {
                continue;
            }
            ty_cJSON *j_type = ty_cJSON_GetObjectItem(it, "type");
            INT_T itype = (j_type != NULL) ? j_type->valueint : 0;

            if (itype == 1) {
                /* Text */
                ty_cJSON *ti = ty_cJSON_GetObjectItem(it, "text_item");
                ty_cJSON *j_text = (ti != NULL) ? ty_cJSON_GetObjectItem(ti, "text") : NULL;
                if (j_text != NULL && j_text->valuestring != NULL) {
                    has_text |= __text_buf_append(text_buf, &text_pos,
                                                  WECHAT_MAX_MSG_LEN, j_text->valuestring);
                }
            } else if (itype == 3) {
                /* Voice: extract server-side transcription (voice_item.text), no download/ASR needed */
                ty_cJSON *vi = ty_cJSON_GetObjectItem(it, "voice_item");
                ty_cJSON *j_vtext = (vi != NULL) ? ty_cJSON_GetObjectItem(vi, "text") : NULL;
                if (j_vtext != NULL && j_vtext->valuestring != NULL
                    && j_vtext->valuestring[0] != '\0') {
                    has_text |= __text_buf_append(text_buf, &text_pos,
                                                  WECHAT_MAX_MSG_LEN, j_vtext->valuestring);
                    PR_INFO("wechat recv voice text: chat_id=%s text=%s",
                            chat_id, j_vtext->valuestring);
                }
            } else if (itype == 2 || itype == 4 || itype == 5) {
                CONST CHAR_T *labels[] = { "", "image", "", "file", "video" };
                PR_WARN("wechat recv: unsupported type=%d (%s) chat_id=%s, not downloaded",
                        itype, labels[itype], chat_id);
            } else {
                PR_WARN("wechat recv: unknown type=%d chat_id=%s", itype, chat_id);
            }
        }

        if (has_text && text_pos > 0) {
            PR_INFO("wechat recv msg: chat_id=%s text=%s", chat_id, text_buf);
            WUKONG_AI_MSG_T wk_msg = {
                .channel  = "wechat",
                .chat_id  = chat_id,
                .type     = WUKONG_AI_MSG_TYPE_TEXT,
                .data     = (CONST BYTE_T *)text_buf,
                .data_len = text_pos,
                .flags    = 0,
            };
            rt = wukong_ai_channel_input(&wk_msg);
            if (rt != OPRT_OK) {
                PR_WARN("wechat channel_input failed: %d", rt);
            }
        }
        tal_free(text_buf);
    }

    ty_cJSON_Delete(resp);
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * State handlers
 * --------------------------------------------------------------------------- */

/**
 * @brief QR_GET handler: fetch QR code URL, notify UI, advance to QR_POLL.
 * @param[out] qr Shared QR context (ticket, current_base, deadline).
 * @return Next state.
 */
STATIC WECHAT_STATE_E __wechat_state_qr_get(WECHAT_QR_CTX_T *qr)
{
    /* Always start from the default base; reset any stale redirect from prior cycle. */
    strncpy(qr->current_base, WECHAT_DEFAULT_BASE_URL,
            sizeof(qr->current_base) - 1);
    qr->current_base[sizeof(qr->current_base) - 1] = '\0';

    CHAR_T url[320];
    snprintf(url, sizeof(url), "%s/ilink/bot/get_bot_qrcode?bot_type=3",
             qr->current_base);

    CHAR_T *body = NULL;
    INT_T   http_code = 0;
    OPERATE_RET rt = __wechat_api_get(url, 10000, &body, &http_code);
    if (rt != OPRT_OK || body == NULL || http_code != 200) {
        PR_WARN("wechat QR_GET failed rt=%d http=%d, retry 5s", rt, http_code);
        if (body != NULL) { SIMPLE_HTTP_FREE(body); }
        tal_system_sleep(5000);
        return WECHAT_STATE_QR_GET;
    }

    ty_cJSON *resp =     ty_cJSON_Parse(body);
    SIMPLE_HTTP_FREE(body);
    if (resp == NULL) {
        tal_system_sleep(5000);
        return WECHAT_STATE_QR_GET;
    }

    qr->ticket[0] = '\0';
    CHAR_T qr_content[512] = {0};
    ty_cJSON *j_t = ty_cJSON_GetObjectItem(resp, "qrcode");
    ty_cJSON *j_i = ty_cJSON_GetObjectItem(resp, "qrcode_img_content");
    if (j_t != NULL && j_t->valuestring != NULL) {
        strncpy(qr->ticket, j_t->valuestring, sizeof(qr->ticket) - 1);
    }
    if (j_i != NULL && j_i->valuestring != NULL) {
        strncpy(qr_content, j_i->valuestring, sizeof(qr_content) - 1);
    }
    ty_cJSON_Delete(resp);

    if (qr->ticket[0] == '\0') {
        PR_WARN("wechat QR_GET: missing ticket, retry 5s");
        tal_system_sleep(5000);
        return WECHAT_STATE_QR_GET;
    }

    /* Notify app — callback is synchronous so qr_content stack buffer is valid */
    if (s_ctx.qr_notify_cb != NULL) {
        s_ctx.qr_notify_cb(qr_content);
    }

    qr->deadline = tal_system_get_millisecond() + WECHAT_QR_MAX_WAIT_MS;
    return WECHAT_STATE_QR_POLL;
}

/**
 * @brief QR_POLL handler: check scan status; handle redirect/expired/confirmed.
 * @param[in,out] qr Shared QR context.
 * @return Next state.
 */
STATIC WECHAT_STATE_E __wechat_state_qr_poll(WECHAT_QR_CTX_T *qr)
{
    if ((SYS_TIME_T)tal_system_get_millisecond() >= qr->deadline) {
        PR_WARN("wechat QR_POLL: timed out, restart QR flow");
        return WECHAT_STATE_QR_GET;
    }

    CHAR_T status_url[320];
    snprintf(status_url, sizeof(status_url),
             "%s/ilink/bot/get_qrcode_status?qrcode=%s",
             qr->current_base, qr->ticket);

    CHAR_T *st_body = NULL;
    INT_T   st_code = 0;
    OPERATE_RET rt = __wechat_api_get(status_url, 40000, &st_body, &st_code);
    if (rt != OPRT_OK || st_body == NULL || st_code != 200) {
        PR_WARN("wechat QR_POLL: request failed rt=%d http=%d", rt, st_code);
        if (st_body != NULL) { SIMPLE_HTTP_FREE(st_body); }
        tal_system_sleep(WECHAT_QR_POLL_INTERVAL_MS);
        return WECHAT_STATE_QR_POLL;
    }
    PR_DEBUG("wechat QR_POLL: resp=%s", st_body);

    ty_cJSON *st = ty_cJSON_Parse(st_body);
    SIMPLE_HTTP_FREE(st_body);
    if (st == NULL) {
        tal_system_sleep(WECHAT_QR_POLL_INTERVAL_MS);
        return WECHAT_STATE_QR_POLL;
    }

    WECHAT_STATE_E next = WECHAT_STATE_QR_POLL;

    ty_cJSON *j_s = ty_cJSON_GetObjectItem(st, "status");
    CONST CHAR_T *s = (j_s != NULL && j_s->valuestring != NULL)
                      ? j_s->valuestring : "";

    if (strcmp(s, "scanned") == 0) {
        PR_INFO("wechat QR_POLL: scanned, waiting for confirmation");

    } else if (strcmp(s, "scaned_but_redirect") == 0) {
        ty_cJSON *j_r = ty_cJSON_GetObjectItem(st, "redirect_host");
        if (j_r != NULL && j_r->valuestring != NULL &&
            j_r->valuestring[0] != '\0') {
            snprintf(qr->current_base, sizeof(qr->current_base),
                     "https://%s", j_r->valuestring);
            PR_INFO("wechat QR_POLL: redirect to %s", qr->current_base);
        }

    } else if (strcmp(s, "expired") == 0) {
        PR_WARN("wechat QR_POLL: expired, restart QR flow");
        next = WECHAT_STATE_QR_GET;

    } else if (strcmp(s, "confirmed") == 0) {
        ty_cJSON *j_tok  = ty_cJSON_GetObjectItem(st, "bot_token");
        ty_cJSON *j_base = ty_cJSON_GetObjectItem(st, "baseurl");
        CONST CHAR_T *new_tok  = (j_tok  != NULL && j_tok->valuestring  != NULL)
                                 ? j_tok->valuestring  : "";
        CONST CHAR_T *new_base = (j_base != NULL && j_base->valuestring != NULL)
                                 ? j_base->valuestring : qr->current_base;

        if (new_tok[0] != '\0') {
            tal_mutex_lock(s_ctx.lock);
            strncpy(s_ctx.token,    new_tok,  sizeof(s_ctx.token)    - 1);
            s_ctx.token[sizeof(s_ctx.token) - 1] = '\0';
            strncpy(s_ctx.base_url, new_base, sizeof(s_ctx.base_url) - 1);
            s_ctx.base_url[sizeof(s_ctx.base_url) - 1] = '\0';
            s_ctx.configured = TRUE;
            tal_mutex_unlock(s_ctx.lock);

            (VOID_T)wd_common_write(WECHAT_KV_TOKEN,
                                    (CONST BYTE_T *)new_tok,
                                    (UINT_T)strlen(new_tok) + 1);
            (VOID_T)wd_common_write(WECHAT_KV_BASEURL,
                                    (CONST BYTE_T *)new_base,
                                    (UINT_T)strlen(new_base) + 1);

            PR_INFO("wechat QR_POLL: login confirmed, switching to RUNNING");
            ty_cJSON_Delete(st);
            return WECHAT_STATE_RUNNING;
        }
        PR_WARN("wechat QR_POLL: confirmed but no bot_token");
    }

    ty_cJSON_Delete(st);
    if (next == WECHAT_STATE_QR_POLL) {
        tal_system_sleep(WECHAT_QR_POLL_INTERVAL_MS);
    }
    return next;
}

/**
 * @brief RUNNING handler: execute one long-poll cycle.
 * @return WECHAT_STATE_RUNNING normally; WECHAT_STATE_QR_GET on session expiry.
 */
STATIC WECHAT_STATE_E __wechat_state_running(VOID_T)
{
    OPERATE_RET rt = __wechat_poll_once();
    if (!s_ctx.configured) {
        /* token cleared by poll_once on session expiry → restart QR login */
        PR_WARN("wechat RUNNING: session expired, restarting QR login");
        return WECHAT_STATE_QR_GET;
    }
    if (rt != OPRT_OK && !s_ctx.stop_req) {
        PR_WARN("wechat RUNNING: poll error %d, retry 5s", rt);
        tal_system_sleep(5000);
    }
    return WECHAT_STATE_RUNNING;
}

/* ---------------------------------------------------------------------------
 * Main channel thread — state machine dispatcher
 * --------------------------------------------------------------------------- */

/**
 * @brief WeChat channel thread: dispatches to per-state handler functions.
 *
 * Each handler returns the next state.  QR context (ticket / base / deadline)
 * is stack-allocated and shared only between QR_GET and QR_POLL handlers.
 *
 * @param[in] arg Unused.
 * @return none
 */
STATIC VOID_T __wechat_main_thread(VOID_T *arg)
{
    PR_INFO("wechat channel thread started");

    WECHAT_STATE_E  state = s_ctx.configured ? WECHAT_STATE_RUNNING
                                             : WECHAT_STATE_QR_GET;
    WECHAT_QR_CTX_T qr    = {0};

    while (!s_ctx.stop_req) {
        switch (state) {
        case WECHAT_STATE_QR_GET:  state = __wechat_state_qr_get(&qr);  break;
        case WECHAT_STATE_QR_POLL: state = __wechat_state_qr_poll(&qr); break;
        case WECHAT_STATE_RUNNING:
        default:                   state = __wechat_state_running();     break;
        }
    }

    PR_INFO("wechat channel thread stopped");
}

/* ---------------------------------------------------------------------------
 * Vtable: init / start / stop / deinit
 * --------------------------------------------------------------------------- */

/**
 * @brief Initialize the WeChat channel: load token from config/KV, create mutex.
 * @param[in] cfg Pointer to WUKONG_CHAN_WECHAT_CFG_T (may be NULL).
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __wechat_init(CONST VOID_T *cfg)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.poll_timeout_ms = WECHAT_POLL_TIMEOUT_DEFAULT;

    /* Register WeChat iLink root CA so TLS connections succeed */
    {
        STATIC CONST CHAR_T s_ca[] = WECHAT_ILINK_ROOT_CA;
        tuya_iot_store_third_cloud_ca(WECHAT_ILINK_HOST,
                                      (CONST UCHAR_T *)s_ca, sizeof(s_ca), FALSE);
    }

    OPERATE_RET rt = tal_mutex_create_init(&s_ctx.lock);
    if (rt != OPRT_OK) {
        PR_ERR("wechat init: mutex create failed: %d", rt);
        return rt;
    }

    CONST WUKONG_CHAN_WECHAT_CFG_T *wcfg = (CONST WUKONG_CHAN_WECHAT_CFG_T *)cfg;

    s_ctx.qr_notify_cb = (wcfg != NULL) ? wcfg->qr_notify_cb : NULL;

    /* Defaults for base URLs — may be overwritten by KV below */
    strncpy(s_ctx.base_url,     WECHAT_DEFAULT_BASE_URL,     sizeof(s_ctx.base_url)     - 1);
    strncpy(s_ctx.cdn_base_url, WECHAT_DEFAULT_CDN_BASE_URL, sizeof(s_ctx.cdn_base_url) - 1);

    /* Token from KV */
    BYTE_T *kv_tok = NULL;
    UINT_T  kv_len = 0;
    if (wd_common_read(WECHAT_KV_TOKEN, &kv_tok, &kv_len) == OPRT_OK &&
        kv_tok != NULL && kv_len > 1) {
        strncpy(s_ctx.token, (CONST CHAR_T *)kv_tok, sizeof(s_ctx.token) - 1);
        s_ctx.configured = TRUE;
    }
    if (kv_tok != NULL) {
        wd_common_free_data(kv_tok);
    }

    if (s_ctx.configured) {
        /* Base URL from KV (may differ from default after redirect during QR) */
        BYTE_T *kv_base = NULL;
        UINT_T  kv_blen = 0;
        if (wd_common_read(WECHAT_KV_BASEURL, &kv_base, &kv_blen) == OPRT_OK &&
            kv_base != NULL && kv_blen > 1) {
            strncpy(s_ctx.base_url, (CONST CHAR_T *)kv_base,
                    sizeof(s_ctx.base_url) - 1);
        }
        if (kv_base != NULL) {
            wd_common_free_data(kv_base);
        }

        /* Restore poll cursor */
        BYTE_T *kv_cur  = NULL;
        UINT_T  kv_clen = 0;
        if (wd_common_read(WECHAT_KV_CURSOR, &kv_cur, &kv_clen) == OPRT_OK &&
            kv_cur != NULL && kv_clen > 1) {
            s_ctx.sync_buf = (CHAR_T *)tal_malloc(kv_clen);
            if (s_ctx.sync_buf != NULL) {
                memcpy(s_ctx.sync_buf, kv_cur, kv_clen);
            }
        }
        if (kv_cur != NULL) {
            wd_common_free_data(kv_cur);
        }

        PR_INFO("wechat init: token restored from KV");
    } else {
        PR_INFO("wechat init: no token in KV, will enter QR login on start");
    }
    return OPRT_OK;
}

/**
 * @brief Start the WeChat channel thread.
 *
 * A single thread handles both QR login (if no token) and long-polling,
 * driven by an internal state machine (WECHAT_STATE_E).
 *
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __wechat_start(VOID_T)
{
    s_ctx.stop_req = FALSE;

    THREAD_CFG_T cfg = {
        .thrdname   = "wk_wechat",
        .stackDepth = WECHAT_POLL_STACK_SIZE,
        .priority   = WECHAT_POLL_PRIORITY,
    };
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    cfg.psram_mode = 1;
#endif
    OPERATE_RET rt = tal_thread_create_and_start(
        &s_ctx.poll_thread, NULL, NULL,
        __wechat_main_thread, NULL, &cfg);
    if (rt != OPRT_OK) {
        PR_ERR("wechat start: thread create failed: %d", rt);
    }
    return rt;
}

/**
 * @brief Request the WeChat poll thread to stop gracefully.
 * @return OPRT_OK always.
 */
STATIC OPERATE_RET __wechat_stop(VOID_T)
{
    s_ctx.stop_req = TRUE;
    return OPRT_OK;
}

/**
 * @brief Release WeChat channel resources (sync_buf + mutex).
 * @return OPRT_OK always.
 */
STATIC OPERATE_RET __wechat_deinit(VOID_T)
{
    if (s_ctx.sync_buf != NULL) {
        tal_free(s_ctx.sync_buf);
        s_ctx.sync_buf = NULL;
    }
    if (s_ctx.lock != NULL) {
        tal_mutex_release(s_ctx.lock);
        s_ctx.lock = NULL;
    }
    return OPRT_OK;
}
