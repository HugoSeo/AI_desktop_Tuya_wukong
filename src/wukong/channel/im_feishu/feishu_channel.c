/**
 * @file feishu_channel.c
 * @brief Feishu (Lark) bot IM channel: WS long-connection (lib_websocket) for
 *        inbound events, REST for replies.
 *
 * One thread owns all socket ops (state machine + receive loop), so ws_handle
 * needs no lock; send_mutex guards the REST path's tenant_token, shared with the
 * provider thread that runs send(). app_id_buf/app_secret_buf are only written
 * at init (first boot) or via feishu_channel_set_creds(), which restarts the
 * channel through deinit/init rather than mutating them while the thread runs,
 * so no separate lock is needed for those two buffers.
 */
#include "feishu_channel.h"
#include "feishu_proto.h"

#include "tuya_ws_db.h"
#include "tuya_app_config.h"
#include "wukong_ai_channel.h"
#include "websocket_client.h"
#include "tuya_simple_http.h"
#include "httpc.h"
#include "tuya_svc_netmgr.h"
#include "tal_time_service.h"
#include "tal_thread.h"
#include "tal_system.h"
#include "tal_memory.h"
#include "tal_mutex.h"
#include "tal_semaphore.h"
#include "ty_cJSON.h"
#include "uni_log.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Credentials come from a git-ignored config header (Kconfig-generated), so a
 * build that enables the channel without wiring credentials may not define these
 * macros at all. Empty fallbacks then let the file compile and the channel
 * degrade gracefully (__feishu_ensure_token reports "no credentials", stays idle)
 * instead of breaking the build. A configured-but-blank value is still defined
 * as "", so this fallback only fires when the macro is entirely absent. */
#ifndef FEISHU_APP_ID
#define FEISHU_APP_ID ""
#endif
#ifndef FEISHU_APP_SECRET
#define FEISHU_APP_SECRET ""
#endif

/* websocket_client_receive() reads one WS message and fires the event callback,
 * but the lib does not export it in its public header (same as cube/xiaozhi). */
extern int websocket_client_receive(websocket_client_handle_t client);

/* ── Feishu API endpoints ──────────────────────────────────────────────── */
#define FEISHU_OPEN_HOST        "https://open.feishu.cn"
#define FEISHU_API_BASE         FEISHU_OPEN_HOST "/open-apis"
#define FEISHU_TOKEN_URL        FEISHU_API_BASE "/auth/v3/tenant_access_token/internal"
#define FEISHU_SEND_MSG_URL     FEISHU_API_BASE "/im/v1/messages"
#define FEISHU_WS_ENDPOINT_URL  FEISHU_OPEN_HOST "/callback/ws/endpoint"

/* Token: refresh 300s before the server-reported expiry; assume 2h if absent. */
#define FEISHU_TOKEN_REFRESH_MARGIN_S 300
#define FEISHU_TOKEN_DEFAULT_TTL_S    7200
#define FEISHU_TOKEN_MAX_LEN          512

/* Outbound text chunking. Feishu's text limit is ~150KB and replies are already
 * capped ~8KB upstream, so this rarely triggers; kept modest to stay well under
 * the API cap while still forcing UTF-8-safe splitting for pathological input. */
#define FEISHU_MAX_MSG_LEN 4000

/* Inbound dedup ring + captured id / route id buffer sizes. */
#define FEISHU_DEDUP_SLOTS    64
#define FEISHU_MESSAGE_ID_MAX 72   /* "om_" prefix, ~40 bytes */
#define FEISHU_ROUTE_ID_MAX   64   /* open_id "ou_" / chat_id "oc_", ~35 bytes */

#define FEISHU_APP_ID_BUF_LEN 64   /* Feishu app_id "cli_..." ~28 chars */

/* app_secret resolved from KV/macro at first boot (see __feishu_resolve_cred);
 * comfortably larger than Feishu's actual secret length. */
#define FEISHU_APP_SECRET_BUF_LEN 128

/* Feishu's own KV keys (private to this channel). */
#define FEISHU_KV_APP_ID     "feishu_app_id"
#define FEISHU_KV_APP_SECRET "feishu_app_secret"

/* Reconnect backoff bounds. */
#define FEISHU_BACKOFF_BASE_MS 2000  /* first step; doubles each failure */
#define FEISHU_BACKOFF_CAP_MS  60000 /* upper bound on the retry interval */
#define FEISHU_BACKOFF_ATTEMPT_MAX 0xFFFF /* saturation cap for the counter */
#define FEISHU_STABLE_MS       30000 /* RUNNING must survive this before backoff resets */

/* Connection / loop tuning. */
#define FEISHU_CLIENT_STACK_SIZE  (8 * 1024)
#define FEISHU_CONNECT_TIMEOUT_MS (10 * 1000)
/* deinit join timeout: must exceed the longest non-interruptible block in the
 * thread (a full TLS connect), so the join never times out in practice. */
#define FEISHU_THREAD_JOIN_TIMEOUT_MS (15 * 1000)
#define FEISHU_POLL_INTERVAL_MS   1000
#define FEISHU_DEFAULT_PING_MS    120000
#define FEISHU_FRAME_OUT_SIZE     (4 * 1024) /* ACK scratch (echoes headers + {"code":200}) */
#define FEISHU_INBOUND_TEXT_SIZE  (4 * 1024) /* cleaned inbound user text handed to channel_input */

/* One-shot light hint for non-text (voice/image/file/...) messages. */
#define FEISHU_NON_TEXT_HINT "俺现在只看得懂文字消息哦"

/* ── Types ─────────────────────────────────────────────────────────────── */

/** Result of the WebSocket endpoint handshake: a one-time wss URL + keep-alive
 *  parameters. The URL is single-use; every reconnect must re-run the handshake. */
typedef struct {
    CHAR_T url[512];              /**< One-time wss URL (query carries service_id). */
    INT_T  service_id;           /**< service_id parsed from the URL query. */
    INT_T  ping_interval_ms;     /**< App-level ping period (ClientConfig.PingInterval). */
} FEISHU_WS_ENDPOINT_T;

/** Message-id dedup ring: Feishu re-delivers un-ACKed events (and may deliver
 *  the same event twice), so this keeps a reply/hint one-shot. */
typedef struct {
    UINT64_T seen[FEISHU_DEDUP_SLOTS];
    UINT32_T next; /**< Next slot to overwrite (monotonic, wraps via modulo). */
} FEISHU_DEDUP_T;

/** What the caller should do with a parsed inbound event. */
typedef enum {
    FEISHU_IN_DROP = 0,  /**< Ignore: wrong event type, group not @-mentioned, or malformed. */
    FEISHU_IN_TEXT,      /**< Route the text (route_id + text buffer are valid). */
    FEISHU_IN_NON_TEXT,  /**< v1 cannot handle it: send the light hint to route_id. */
} FEISHU_IN_KIND_E;

/** Outcome of __feishu_inbound_parse(). */
typedef struct {
    FEISHU_IN_KIND_E kind;
    CHAR_T message_id[FEISHU_MESSAGE_ID_MAX]; /**< "" when absent. */
    CHAR_T route_id[FEISHU_ROUTE_ID_MAX];     /**< open_id (p2p) or chat_id (group). */
    BOOL_T text_truncated;                    /**< Cleaned text exceeded the caller buffer. */
} FEISHU_INBOUND_T;

/** Consecutive-failure backoff counter (reset once a connection reaches RUNNING). */
typedef struct {
    UINT_T attempt;
} FEISHU_BACKOFF_S;

typedef enum {
    FEISHU_STATE_IDLE = 0,
    FEISHU_STATE_SETUP,
    FEISHU_STATE_CONNECT,
    FEISHU_STATE_RUNNING,
} FEISHU_STATE_E;

typedef struct {
    websocket_client_handle_t ws_handle; /* touched only by the receive thread */
    THREAD_HANDLE        thread;
    SEM_HANDLE           exit_sem;  /* posted by the thread as it exits; deinit joins on it */
    BOOL_T               terminate;
    BOOL_T               configured;
    /* Raised by the WS close/disconnect event callback and observed by the
     * receive loop, which then tears the socket down itself — no read/close race.
     * volatile because it may cross threads without the lock. */
    volatile BOOL_T      close_requested;
    FEISHU_STATE_E       state;

    FEISHU_WS_ENDPOINT_T endpoint;
    INT_T                ping_interval_ms;
    UINT_T               last_ping_ms;     /* monotonic ms of the last ping sent */
    UINT_T               connected_at_ms;  /* monotonic ms this connection reached RUNNING */
    FEISHU_BACKOFF_S     backoff;       /* consecutive setup/connect failures */

    /* Credentials + tenant-token cache (RAM only, survives reconnects). Kept in
     * the heap context so the 512-byte token buffer costs no static RAM when the
     * channel is disabled. Both resolved at first boot from KV/macro (see
     * __feishu_resolve_cred) and only ever rewritten via feishu_channel_set_creds,
     * which restarts the channel (deinit/init) rather than mutating them live. */
    CHAR_T               app_id_buf[FEISHU_APP_ID_BUF_LEN];
    CHAR_T               app_secret_buf[FEISHU_APP_SECRET_BUF_LEN];
    CHAR_T               tenant_token[FEISHU_TOKEN_MAX_LEN];
    TIME_T               token_refresh_at; /* posix seconds; refetch once now >= this */
    MUTEX_HANDLE         send_mutex;  /* serializes __feishu_send_text (guards tenant_token) */

    FEISHU_DEDUP_T       dedup;
    BYTE_T               frame_out[FEISHU_FRAME_OUT_SIZE];      /* receive-thread ACK scratch */
    CHAR_T               inbound_text[FEISHU_INBOUND_TEXT_SIZE]; /* receive-thread parse scratch */
} FEISHU_CHANNEL_S;

STATIC FEISHU_CHANNEL_S *s_feishu = NULL;

/* ===========================================================================
 * 1. REST client (tenant token, endpoint handshake, send text)
 * =========================================================================== */

/** Longest UTF-8-safe prefix length of @p text, capped at @p max_bytes.
 *  Never reads past [0, remaining), so it can be used on non-terminated data. */
STATIC UINT_T __feishu_utf8_chunk_len(CONST BYTE_T *text, UINT_T remaining, UINT_T max_bytes)
{
    if (text == NULL || remaining == 0) {
        return 0;
    }

    UINT_T chunk = (remaining > max_bytes) ? max_bytes : remaining;

    /* A UTF-8 continuation byte is 10xxxxxx. If we cut mid-sequence, back off
     * until the split lands right before a lead byte. Only needed when we
     * actually truncate: when chunk == remaining the boundary is the end of the
     * caller's data and text[chunk] must not be read. */
    if (chunk < remaining) {
        while (chunk > 0 && (text[chunk] & 0xC0) == 0x80) {
            chunk--;
        }
    }
    return chunk;
}

/** p2p routes by sender open_id ("ou_"); everything else (groups) by chat_id. */
STATIC CONST CHAR_T *__feishu_receive_id_type(CONST CHAR_T *receive_id)
{
    if (receive_id != NULL && strncmp(receive_id, "ou_", 3) == 0) {
        return "open_id";
    }
    return "chat_id";
}

/** Per-request header context passed to __feishu_add_headers. */
typedef struct {
    BOOL_T with_auth; /**< Add the Bearer tenant token (send calls only). */
} FEISHU_HDR_CTX_T;

/** add_head_cb: always sets Content-Type; adds Authorization when requested. */
STATIC VOID_T __feishu_add_headers(http_session_t session, VOID_T *data)
{
    http_add_header(session, NULL, "Content-Type", "application/json; charset=utf-8");

    CONST FEISHU_HDR_CTX_T *ctx = (CONST FEISHU_HDR_CTX_T *)data;
    if (ctx != NULL && ctx->with_auth) {
        CHAR_T auth[FEISHU_TOKEN_MAX_LEN + 16];
        snprintf(auth, sizeof(auth), "Bearer %s", s_feishu->tenant_token);
        http_add_header(session, NULL, "Authorization", auth);
    }
}

/** POST a JSON body and parse the response into a ty_cJSON tree (caller frees). */
STATIC OPERATE_RET __feishu_http_post_json(CONST CHAR_T *url, CONST CHAR_T *body,
                                         BOOL_T with_auth, ty_cJSON **pp_json)
{
    *pp_json = NULL;

    FEISHU_HDR_CTX_T hctx = { with_auth };
    simple_http_opts_t opts = {
        .add_head_cb   = __feishu_add_headers,
        .add_head_data = &hctx,
    };
    simple_http_response_t resp;
    OPERATE_RET rt = tuya_simple_http_post(url, (CONST BYTE_T *)body,
                                           (UINT_T)strlen(body), &opts, &resp);
    if (rt != OPRT_OK) {
        PR_WARN("feishu POST failed rt=%d", rt);
        return rt;
    }
    if (resp.http_code < 200 || resp.http_code >= 300) {
        PR_WARN("feishu POST HTTP %d", resp.http_code);
        if (resp.data != NULL) {
            SIMPLE_HTTP_FREE(resp.data);
        }
        return OPRT_COM_ERROR;
    }
    if (resp.data == NULL || resp.len == 0) {
        PR_WARN("feishu POST empty response");
        if (resp.data != NULL) {
            SIMPLE_HTTP_FREE(resp.data);
        }
        return OPRT_COM_ERROR;
    }

    *pp_json = ty_cJSON_Parse((CONST CHAR_T *)resp.data);
    SIMPLE_HTTP_FREE(resp.data);
    if (*pp_json == NULL) {
        PR_WARN("feishu POST: JSON parse failed");
        return OPRT_CJSON_PARSE_ERR;
    }
    return OPRT_OK;
}

/** Read the integer business "code" field; -1 when absent. */
STATIC INT_T __feishu_json_code(CONST ty_cJSON *root)
{
    ty_cJSON *code = ty_cJSON_GetObjectItem(root, "code");
    if (code != NULL && ty_cJSON_IsNumber(code)) {
        return code->valueint;
    }
    return -1;
}

/** Read a NUL-terminated string field, or NULL if absent/not a string. */
STATIC CONST CHAR_T *__feishu_json_str(CONST ty_cJSON *obj, CONST CHAR_T *key)
{
    ty_cJSON *item = ty_cJSON_GetObjectItem(obj, key);
    if (item != NULL && ty_cJSON_IsString(item) && item->valuestring != NULL) {
        return item->valuestring;
    }
    return NULL;
}

/** POST {id_key:app_id, secret_key:app_secret} (no auth) and hand back the parsed
 *  response. Both auth flows below post their credentials this way; only Feishu's
 *  field-name casing differs (app_id vs AppID), so the keys are parameters. */
STATIC OPERATE_RET __feishu_post_app_creds(CONST CHAR_T *url, CONST CHAR_T *id_key,
                                           CONST CHAR_T *secret_key, ty_cJSON **pp_root)
{
    *pp_root = NULL;

    /* app_id_buf/app_secret_buf are only ever rewritten by feishu_channel_set_creds
     * via a deinit/init restart (never mutated while this thread runs), so a
     * direct read needs no lock. */
    if (s_feishu->app_id_buf[0] == '\0' || s_feishu->app_secret_buf[0] == '\0') {
        PR_WARN("feishu: no credentials configured");
        return OPRT_INVALID_PARM;
    }

    ty_cJSON *body = ty_cJSON_CreateObject();
    if (body == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddStringToObject(body, id_key, s_feishu->app_id_buf);
    ty_cJSON_AddStringToObject(body, secret_key, s_feishu->app_secret_buf);
    CHAR_T *req = ty_cJSON_PrintUnformatted(body);
    ty_cJSON_Delete(body);
    if (req == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    OPERATE_RET rt = __feishu_http_post_json(url, req, FALSE, pp_root);
    ty_cJSON_FreeBuffer(req);
    return rt;
}

/** Ensure a valid tenant_access_token is cached, refreshing lazily (RAM only). */
STATIC OPERATE_RET __feishu_ensure_token(VOID_T)
{
    TIME_T now = tal_time_get_posix();
    if (s_feishu->tenant_token[0] != '\0' && now < s_feishu->token_refresh_at) {
        return OPRT_OK;
    }

    ty_cJSON *root = NULL;
    OPERATE_RET rt = __feishu_post_app_creds(FEISHU_TOKEN_URL, "app_id", "app_secret", &root);
    if (rt != OPRT_OK) {
        return rt;
    }

    INT_T code = __feishu_json_code(root);
    CONST CHAR_T *token = __feishu_json_str(root, "tenant_access_token");
    if (code != 0 || token == NULL) {
        PR_ERR("feishu token request failed: code=%d", code);
        ty_cJSON_Delete(root);
        return OPRT_COM_ERROR;
    }

    ty_cJSON *expire = ty_cJSON_GetObjectItem(root, "expire");
    INT_T ttl = (expire != NULL && ty_cJSON_IsNumber(expire) && expire->valueint > 0)
                ? expire->valueint : FEISHU_TOKEN_DEFAULT_TTL_S;
    strncpy(s_feishu->tenant_token, token, sizeof(s_feishu->tenant_token) - 1);
    s_feishu->tenant_token[sizeof(s_feishu->tenant_token) - 1] = '\0';
    s_feishu->token_refresh_at = now + ttl - FEISHU_TOKEN_REFRESH_MARGIN_S;
    PR_INFO("feishu got tenant token (ttl=%ds)", ttl);

    ty_cJSON_Delete(root);
    return OPRT_OK;
}

/** Extract a query-string parameter value from a URL. FALSE if absent. */
STATIC BOOL_T __feishu_url_query_param(CONST CHAR_T *url, CONST CHAR_T *key,
                                     CHAR_T *out, UINT_T out_size)
{
    CONST CHAR_T *seg = strchr(url, '?');
    if (seg == NULL || out_size == 0) {
        return FALSE;
    }
    UINT_T key_len = (UINT_T)strlen(key);

    /* Walk one "name=value" segment at a time, bounded by '&'. */
    for (seg++; *seg != '\0'; ) {
        CONST CHAR_T *seg_end = strchr(seg, '&');
        UINT_T seg_len = seg_end ? (UINT_T)(seg_end - seg) : (UINT_T)strlen(seg);

        CONST CHAR_T *sep = memchr(seg, '=', seg_len);
        if (sep != NULL && (UINT_T)(sep - seg) == key_len &&
            memcmp(seg, key, key_len) == 0) {
            CONST CHAR_T *val = sep + 1;
            UINT_T val_len = (UINT_T)(seg + seg_len - val);
            if (val_len > out_size - 1) {
                val_len = out_size - 1;
            }
            memcpy(out, val, val_len);
            out[val_len] = '\0';
            return TRUE;
        }

        if (seg_end == NULL) {
            break;
        }
        seg = seg_end + 1;
    }
    return FALSE;
}

/** Read an integer field from ClientConfig, scaled from seconds to ms. */
STATIC VOID_T __feishu_ccfg_ms(CONST ty_cJSON *ccfg, CONST CHAR_T *key, INT_T *out_ms)
{
    ty_cJSON *item = ty_cJSON_GetObjectItem(ccfg, key);
    if (item != NULL && ty_cJSON_IsNumber(item) && item->valueint > 0) {
        *out_ms = item->valueint * 1000;
    }
}

/** Run the WS endpoint handshake: POST app creds to /callback/ws/endpoint, then
 *  read back the one-time wss URL, its service_id, and the ping interval. */
STATIC OPERATE_RET __feishu_get_ws_endpoint(FEISHU_WS_ENDPOINT_T *out)
{
    if (out == NULL) {
        return OPRT_INVALID_PARM;
    }
    memset(out, 0, sizeof(*out));

    ty_cJSON *root = NULL;
    OPERATE_RET rt = __feishu_post_app_creds(FEISHU_WS_ENDPOINT_URL, "AppID", "AppSecret", &root);
    if (rt != OPRT_OK) {
        return rt;
    }

    INT_T code = __feishu_json_code(root);
    ty_cJSON *data = ty_cJSON_GetObjectItem(root, "data");
    CONST CHAR_T *url = (data != NULL) ? __feishu_json_str(data, "URL") : NULL;
    if (code != 0 || url == NULL) {
        PR_ERR("feishu ws endpoint failed: code=%d", code);
        ty_cJSON_Delete(root);
        return OPRT_COM_ERROR;
    }

    strncpy(out->url, url, sizeof(out->url) - 1);
    out->url[sizeof(out->url) - 1] = '\0';

    CHAR_T sid[24] = {0};
    if (__feishu_url_query_param(out->url, "service_id", sid, sizeof(sid))) {
        out->service_id = atoi(sid);
    }

    /* Keep-alive period from ClientConfig.PingInterval (seconds), or a default. */
    out->ping_interval_ms = FEISHU_DEFAULT_PING_MS;
    ty_cJSON *ccfg = (data != NULL) ? ty_cJSON_GetObjectItem(data, "ClientConfig") : NULL;
    if (ccfg != NULL) {
        __feishu_ccfg_ms(ccfg, "PingInterval", &out->ping_interval_ms);
    }

    PR_INFO("feishu ws endpoint ready: service_id=%d ping=%dms",
            out->service_id, out->ping_interval_ms);
    ty_cJSON_Delete(root);
    return OPRT_OK;
}

/**
 * Build the send body for one chunk as a schema-2.0 interactive card holding a
 * single markdown element: {receive_id, msg_type:"interactive", content:"<card>"}.
 *
 * Feishu 'text' messages render no markdown at all, so replies are sent as cards
 * to get bold/headings/lists/code/tables rendered. The schema-2.0 markdown
 * component renders standard markdown tables directly (no native table component
 * needed). Sending a card needs no bot-side config — only the msg_type/content
 * shape changes.
 */
/** Serialize the card itself: one markdown element in a schema-2.0 card.
 *  {"schema":"2.0","config":{"wide_screen_mode":true},
 *   "body":{"elements":[{"tag":"markdown","content":"<markdown>"}]}}
 *  Returns a heap string (ty_cJSON_FreeBuffer to release), NULL on OOM. */
STATIC CHAR_T *__feishu_card_json(CONST CHAR_T *markdown)
{
    ty_cJSON *card = ty_cJSON_CreateObject();
    if (card == NULL) {
        return NULL;
    }
    ty_cJSON_AddStringToObject(card, "schema", "2.0");
    ty_cJSON *config   = ty_cJSON_AddObjectToObject(card, "config");
    ty_cJSON *body     = ty_cJSON_AddObjectToObject(card, "body");
    ty_cJSON *elements = (body != NULL) ? ty_cJSON_AddArrayToObject(body, "elements") : NULL;
    ty_cJSON *md       = (elements != NULL) ? ty_cJSON_CreateObject() : NULL;
    if (config == NULL || md == NULL) {
        ty_cJSON_Delete(md);
        ty_cJSON_Delete(card);
        return NULL;
    }
    ty_cJSON_AddBoolToObject(config, "wide_screen_mode", TRUE);
    ty_cJSON_AddStringToObject(md, "tag", "markdown");
    ty_cJSON_AddStringToObject(md, "content", markdown);
    ty_cJSON_AddItemToArray(elements, md);

    CHAR_T *out = ty_cJSON_PrintUnformatted(card);
    ty_cJSON_Delete(card);
    return out;
}

/** Wrap one chunk as an interactive-card send body addressed to receive_id:
 *  {receive_id, msg_type:"interactive", content:"<card-json>"}. Caller frees. */
STATIC CHAR_T *__feishu_build_card_body(CONST CHAR_T *receive_id,
                                        CONST CHAR_T *chunk, UINT_T chunk_len)
{
    CHAR_T *markdown = (CHAR_T *)tal_malloc(chunk_len + 1);
    if (markdown == NULL) {
        return NULL;
    }
    memcpy(markdown, chunk, chunk_len);
    markdown[chunk_len] = '\0';

    CHAR_T *card = __feishu_card_json(markdown);
    tal_free(markdown);
    if (card == NULL) {
        return NULL;
    }

    ty_cJSON *body = ty_cJSON_CreateObject();
    if (body == NULL) {
        ty_cJSON_FreeBuffer(card);
        return NULL;
    }
    ty_cJSON_AddStringToObject(body, "receive_id", receive_id);
    ty_cJSON_AddStringToObject(body, "msg_type", "interactive");
    ty_cJSON_AddStringToObject(body, "content", card);
    ty_cJSON_FreeBuffer(card);

    CHAR_T *out = ty_cJSON_PrintUnformatted(body);
    ty_cJSON_Delete(body);
    return out;
}

/** POST one prepared text body; check the business code (log msg on failure). */
STATIC OPERATE_RET __feishu_send_chunk(CONST CHAR_T *url, CONST CHAR_T *body)
{
    ty_cJSON *root = NULL;
    OPERATE_RET rt = __feishu_http_post_json(url, body, TRUE, &root);
    if (rt != OPRT_OK) {
        PR_ERR("feishu send chunk transport failed: %d", rt);
        return rt;
    }

    INT_T code = __feishu_json_code(root);
    if (code != 0) {
        ty_cJSON *msg = ty_cJSON_GetObjectItem(root, "msg");
        PR_ERR("feishu send failed: code=%d msg=%s", code,
               (msg && ty_cJSON_IsString(msg) && msg->valuestring) ? msg->valuestring
                                                                    : "unknown");
        rt = OPRT_COM_ERROR;
    }
    ty_cJSON_Delete(root);
    return rt;
}

/** Send a text message; long text is split on UTF-8 boundaries, each chunk's
 *  business code checked (a failed chunk fails the call, never silent drop).
 *  Must run under send_mutex (writes/reads the shared tenant_token). */
STATIC OPERATE_RET __feishu_send_text_locked(CONST CHAR_T *receive_id, CONST CHAR_T *text)
{
    if (receive_id == NULL || text == NULL) {
        return OPRT_INVALID_PARM;
    }

    OPERATE_RET rt = __feishu_ensure_token();
    if (rt != OPRT_OK) {
        return rt;
    }

    CHAR_T url[256];
    snprintf(url, sizeof(url), "%s?receive_id_type=%s",
             FEISHU_SEND_MSG_URL, __feishu_receive_id_type(receive_id));

    CONST BYTE_T *src = (CONST BYTE_T *)text;
    UINT_T total = (UINT_T)strlen(text);
    UINT_T offset = 0;

    while (offset < total) {
        UINT_T chunk = __feishu_utf8_chunk_len(src + offset, total - offset,
                                             FEISHU_MAX_MSG_LEN);
        if (chunk == 0) {
            /* No UTF-8-safe boundary (malformed input) — stop rather than loop. */
            PR_WARN("feishu send: no valid UTF-8 boundary, dropping tail");
            return OPRT_COM_ERROR;
        }

        CHAR_T *body = __feishu_build_card_body(receive_id,
                                              (CONST CHAR_T *)(src + offset), chunk);
        if (body == NULL) {
            return OPRT_MALLOC_FAILED;
        }
        rt = __feishu_send_chunk(url, body);
        ty_cJSON_FreeBuffer(body);
        if (rt != OPRT_OK) {
            return rt;
        }

        PR_INFO("feishu sent to %s (%u bytes)", receive_id, chunk);
        offset += chunk;
    }
    return OPRT_OK;
}

/** Serialize sends so the shared tenant_token can't be torn between the two
 *  callers: the provider thread (reply) and the receive thread (non-text hint). */
STATIC OPERATE_RET __feishu_send_text(CONST CHAR_T *receive_id, CONST CHAR_T *text)
{
    tal_mutex_lock(s_feishu->send_mutex);
    OPERATE_RET rt = __feishu_send_text_locked(receive_id, text);
    tal_mutex_unlock(s_feishu->send_mutex);
    return rt;
}

/* ===========================================================================
 * 2. Inbound event routing (dedup, @-mention stripping, p2p/group)
 * =========================================================================== */

/* FNV-1a 64-bit, same scheme the WeChat channel uses for its dedup ring. */
STATIC UINT64_T __feishu_fnv1a64(CONST CHAR_T *s)
{
    UINT64_T h = 14695981039346656037ULL;
    if (s == NULL) {
        return h;
    }
    while (*s) {
        h ^= (UINT8_T)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

/** Test whether @p message_id was seen recently; record it if new. */
STATIC BOOL_T __feishu_dedup_seen(FEISHU_DEDUP_T *dedup, CONST CHAR_T *message_id)
{
    if (dedup == NULL || message_id == NULL || message_id[0] == '\0') {
        return FALSE;
    }
    UINT64_T key = __feishu_fnv1a64(message_id);
    for (UINT32_T i = 0; i < FEISHU_DEDUP_SLOTS; i++) {
        if (dedup->seen[i] == key) {
            return TRUE;
        }
    }
    dedup->seen[dedup->next % FEISHU_DEDUP_SLOTS] = key;
    dedup->next++;
    return FALSE;
}

/** Skip a leading "@_user_<n>" mention placeholder (and trailing spaces).
 *  Feishu renders an @bot mention in the text as "@_user_1 ". Returns the
 *  pointer just past the placeholder, or NULL when text has no such prefix. */
STATIC CONST CHAR_T *__feishu_skip_mention(CONST CHAR_T *text)
{
    if (strncmp(text, "@_user_", 7) != 0) {
        return NULL;
    }
    CONST CHAR_T *p = text + 7;
    if (*p < '0' || *p > '9') {
        return NULL; /* "@_user_" not followed by an index: not a placeholder. */
    }
    while (*p >= '0' && *p <= '9') {
        p++;
    }
    while (*p == ' ') {
        p++;
    }
    return p;
}

/** Advance past leading ASCII whitespace. */
STATIC VOID_T __feishu_ltrim(CONST CHAR_T **pp)
{
    CONST CHAR_T *p = *pp;
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
        p++;
    }
    *pp = p;
}

/** Session route id: group -> chat_id, p2p -> sender open_id (chat_id fallback). */
STATIC CONST CHAR_T *__feishu_route_id(ty_cJSON *event, CONST CHAR_T *chat_id, BOOL_T is_group)
{
    if (is_group) {
        return chat_id;
    }
    ty_cJSON *sender    = ty_cJSON_GetObjectItem(event, "sender");
    ty_cJSON *sender_id = (sender != NULL) ? ty_cJSON_GetObjectItem(sender, "sender_id") : NULL;
    CONST CHAR_T *open_id = (sender_id != NULL) ? __feishu_json_str(sender_id, "open_id") : NULL;
    return (open_id != NULL && open_id[0] != '\0') ? open_id : chat_id;
}

/** From a text message's content JSON string ({"text":"..."}), produce the reply
 *  text into @p buf: mention placeholder stripped, whitespace trimmed. Groups only
 *  answer explicit @bot, so an un-mentioned group line yields FALSE (drop). Returns
 *  FALSE (drop) when there is no usable text; owns its own content parse. */
STATIC BOOL_T __feishu_clean_text(CONST CHAR_T *content_str, BOOL_T is_group,
                                  CHAR_T *buf, UINT_T buf_size, BOOL_T *truncated)
{
    ty_cJSON *content = ty_cJSON_Parse(content_str);
    if (content == NULL) {
        return FALSE;
    }

    BOOL_T ok = FALSE;
    CONST CHAR_T *text  = __feishu_json_str(content, "text");
    CONST CHAR_T *after = (text != NULL) ? __feishu_skip_mention(text) : NULL;
    /* Group: require the @bot placeholder. p2p: strip it if present, else keep. */
    CONST CHAR_T *cleaned = is_group ? after : (after != NULL ? after : text);
    if (cleaned != NULL) {
        __feishu_ltrim(&cleaned);
        if (cleaned[0] != '\0' && buf != NULL && buf_size > 0) {
            UINT_T n = (UINT_T)strlen(cleaned);
            if (n > buf_size - 1) {
                n = buf_size - 1;
                *truncated = TRUE;
            }
            memcpy(buf, cleaned, n);
            buf[n] = '\0';
            ok = TRUE;
        }
    }

    ty_cJSON_Delete(content);
    return ok;
}

/** Parse one im.message.receive_v1 event envelope (a WS frame payload).
 *  Extracts message_id + route id, classifies, and for text writes the
 *  mention-stripped/trimmed text into @p text_buf. Dedup is done separately. */
STATIC OPERATE_RET __feishu_inbound_parse(CONST CHAR_T *event_json, CHAR_T *text_buf,
                                          UINT_T text_buf_size, FEISHU_INBOUND_T *out)
{
    if (event_json == NULL || out == NULL) {
        return OPRT_INVALID_PARM;
    }
    memset(out, 0, sizeof(*out));
    out->kind = FEISHU_IN_DROP;
    if (text_buf != NULL && text_buf_size > 0) {
        text_buf[0] = '\0';
    }

    ty_cJSON *root = ty_cJSON_Parse(event_json);
    if (root == NULL) {
        return OPRT_CJSON_PARSE_ERR;
    }

    /* Accept only im.message.receive_v1 with a message body; drop anything else. */
    ty_cJSON *header = ty_cJSON_GetObjectItem(root, "header");
    CONST CHAR_T *etype = (header != NULL) ? __feishu_json_str(header, "event_type") : NULL;
    ty_cJSON *event = ty_cJSON_GetObjectItem(root, "event");
    if (event == NULL) {
        event = root; /* tolerate an envelope without the "event" wrapper */
    }
    ty_cJSON *message = ty_cJSON_GetObjectItem(event, "message");
    CONST CHAR_T *chat_id = (message != NULL) ? __feishu_json_str(message, "chat_id") : NULL;
    if ((etype != NULL && strcmp(etype, "im.message.receive_v1") != 0) || chat_id == NULL) {
        ty_cJSON_Delete(root);
        return OPRT_OK; /* DROP */
    }

    /* Capture id (for dedup) and route id before branching, so a non-text message
     * still gets its hint routed back. Missing chat_type means p2p. */
    CONST CHAR_T *msg_id = __feishu_json_str(message, "message_id");
    if (msg_id != NULL) {
        strncpy(out->message_id, msg_id, sizeof(out->message_id) - 1);
    }
    CONST CHAR_T *chat_type = __feishu_json_str(message, "chat_type");
    BOOL_T is_group = (chat_type != NULL && strcmp(chat_type, "group") == 0);
    strncpy(out->route_id, __feishu_route_id(event, chat_id, is_group),
            sizeof(out->route_id) - 1);

    /* Non-text (voice/image/file/...) gets a light hint; a missing type is text. */
    CONST CHAR_T *msg_type = __feishu_json_str(message, "message_type");
    if (msg_type != NULL && strcmp(msg_type, "text") != 0) {
        out->kind = FEISHU_IN_NON_TEXT;
    } else {
        CONST CHAR_T *content_str = __feishu_json_str(message, "content");
        if (content_str != NULL &&
            __feishu_clean_text(content_str, is_group, text_buf, text_buf_size,
                                &out->text_truncated)) {
            out->kind = FEISHU_IN_TEXT;
        }
    }

    ty_cJSON_Delete(root);
    return OPRT_OK;
}

/* ===========================================================================
 * 3. Reconnect backoff + TLS-cert-not-ready classification
 * =========================================================================== */

STATIC VOID_T __feishu_backoff_reset(FEISHU_BACKOFF_S *bo)
{
    if (bo != NULL) {
        bo->attempt = 0;
    }
}

/** Delay for the current attempt (base << attempt, clamped to cap), then bump. */
STATIC UINT_T __feishu_backoff_next_ms(FEISHU_BACKOFF_S *bo, UINT_T base_ms, UINT_T cap_ms)
{
    if (bo == NULL || base_ms == 0) {
        return 0;
    }

    /* Double from base for each prior attempt, stopping the moment we reach the
     * cap so the left shift can never overflow. */
    UINT_T delay = base_ms;
    for (UINT_T i = 0; i < bo->attempt && delay < cap_ms; i++) {
        delay <<= 1;
    }
    if (delay > cap_ms) {
        delay = cap_ms;
    }

    if (bo->attempt < FEISHU_BACKOFF_ATTEMPT_MAX) {
        bo->attempt++;
    }
    return delay;
}

/** Whether a connect error looks like "TLS cert not ready yet" (first-boot). */
STATIC BOOL_T __feishu_err_is_cert_not_ready(OPERATE_RET rt)
{
    switch (rt) {
    case OPRT_MID_MQTT_TCP_TLS_CONNECD_FAILED:
    case OPRT_MID_TLS_CONNECTION_ERROR:
    case OPRT_MID_TLS_X509_ROOT_CRT_PARSE_ERROR:
    case OPRT_LINK_CORE_TLS_CONNECTION_ERROR:
    case OPRT_MID_TRANSPORT_TCP_TLS_CONNECD_FAILED:
        return TRUE;
    default:
        return FALSE;
    }
}

/* ===========================================================================
 * 4. WebSocket state machine + channel vtable
 * =========================================================================== */

STATIC VOID_T __feishu_set_state(FEISHU_STATE_E state)
{
    PR_NOTICE("***** feishu state %d -> %d *****", s_feishu->state, state);
    s_feishu->state = state;
}

/** Close and free the ws_handle. Idempotent. All socket ops run on the receive
 *  thread (deinit joins it first), so no lock is needed. */
STATIC VOID_T __feishu_ws_close(VOID_T)
{
    if (s_feishu->ws_handle) {
        websocket_client_close(s_feishu->ws_handle);
        s_feishu->ws_handle = NULL;
    }
}

/** Send one binary frame. websocket_client_send_bin returns the number of bytes
 *  written on success, so a full write is result == len. */
STATIC BOOL_T __feishu_ws_send_bin(BYTE_T *data, UINT_T len)
{
    INT_T ret = s_feishu->ws_handle
                ? websocket_client_send_bin(s_feishu->ws_handle, data, len) : -1;
    return (ret == (INT_T)len);
}

/** Tear down the live connection and fall back to IDLE for a fresh handshake. */
STATIC VOID_T __feishu_client_close(VOID_T)
{
    s_feishu->close_requested = FALSE;
    __feishu_ws_close();
    __feishu_set_state(FEISHU_STATE_IDLE);
}

/** Send one application-level protobuf ping (CONTROL, type=ping). Returns
 *  OPRT_COM_ERROR only on a socket write failure (dead link → reconnect); a
 *  build failure is an encode bug, not a dead link, so it does not reconnect. */
STATIC OPERATE_RET __feishu_send_ping(VOID_T)
{
    BYTE_T buf[64];
    UINT_T n = 0;
    if (feishu_frame_build_ping(s_feishu->endpoint.service_id, buf, sizeof(buf), &n) != OPRT_OK) {
        PR_ERR("feishu ping build failed");
        return OPRT_OK;
    }
    if (!__feishu_ws_send_bin(buf, n)) {
        PR_WARN("feishu ping write failed, reconnecting");
        return OPRT_COM_ERROR;
    }
    PR_DEBUG("feishu ping sent");
    return OPRT_OK;
}

/** pong carries an updated ClientConfig.PingInterval (seconds); adopt it. */
STATIC VOID_T __feishu_handle_pong(CONST FEISHU_FRAME_T *frame)
{
    if (frame->payload && frame->payload_len > 0) {
        CHAR_T tmp[128];
        UINT_T n = (frame->payload_len < sizeof(tmp) - 1) ? frame->payload_len : sizeof(tmp) - 1;
        memcpy(tmp, frame->payload, n);
        tmp[n] = '\0';
        ty_cJSON *cfg = ty_cJSON_Parse(tmp);
        if (cfg) {
            ty_cJSON *pi = ty_cJSON_GetObjectItem(cfg, "PingInterval");
            if (pi && ty_cJSON_IsNumber(pi) && pi->valueint > 0) {
                s_feishu->ping_interval_ms = pi->valueint * 1000;
            }
            ty_cJSON_Delete(cfg);
        }
    }
    PR_DEBUG("feishu pong (ping_interval=%dms)", s_feishu->ping_interval_ms);
}

/** Echo the received frame's headers with a {"code":200} payload. */
STATIC VOID_T __feishu_send_ack(CONST FEISHU_FRAME_T *recv)
{
    UINT_T n = 0;
    OPERATE_RET rt = feishu_frame_build_ack(recv, s_feishu->frame_out, FEISHU_FRAME_OUT_SIZE, &n);
    if (rt != OPRT_OK) {
        PR_ERR("feishu build ack failed: %d", rt);
        return;
    }
    if (!__feishu_ws_send_bin(s_feishu->frame_out, n)) {
        PR_WARN("feishu ack write failed");
    }
}

/** Parse one event payload, dedup, and route to the agent (or hint back). */
STATIC VOID_T __feishu_dispatch_event(CONST FEISHU_FRAME_T *frame)
{
    /* Payload aliases the receive buffer and is not NUL-terminated; copy it. */
    CHAR_T *ev = (CHAR_T *)tal_malloc(frame->payload_len + 1);
    if (ev == NULL) {
        PR_ERR("feishu event alloc failed");
        return;
    }
    memcpy(ev, frame->payload, frame->payload_len);
    ev[frame->payload_len] = '\0';

    /* Resident scratch: this runs only on the receive thread, non-reentrant. */
    CHAR_T *text = s_feishu->inbound_text;

    FEISHU_INBOUND_T in;
    OPERATE_RET rt = __feishu_inbound_parse(ev, text, FEISHU_INBOUND_TEXT_SIZE, &in);
    tal_free(ev);
    if (rt != OPRT_OK) {
        PR_WARN("feishu event parse failed: %d", rt);
        return;
    }

    /* Dedup across every kind so a redelivered event neither re-answers nor
     * re-hints (the ACK above already stops the server from re-delivering). */
    if (in.message_id[0] != '\0' && __feishu_dedup_seen(&s_feishu->dedup, in.message_id)) {
        PR_INFO("feishu dup message %s, skip", in.message_id);
        return;
    }

    switch (in.kind) {
    case FEISHU_IN_TEXT: {
        if (in.text_truncated) {
            PR_WARN("feishu inbound text truncated to %d bytes", FEISHU_INBOUND_TEXT_SIZE);
        }
        UINT_T text_len = (UINT_T)strlen(text);
        PR_INFO("feishu recv text: chat_id=%s len=%u", in.route_id, text_len);
        WUKONG_AI_MSG_T msg = {
            .channel  = WUKONG_CHAN_FEISHU,
            .chat_id  = in.route_id,
            .type     = WUKONG_AI_MSG_TYPE_TEXT,
            .data     = (CONST BYTE_T *)text,
            .data_len = text_len,
            .flags    = 0,
        };
        rt = wukong_ai_channel_input(&msg);
        if (rt != OPRT_OK) {
            PR_WARN("feishu channel_input failed: %d", rt);
        }
        break;
    }
    case FEISHU_IN_NON_TEXT:
        PR_INFO("feishu recv non-text msg from %s, replying hint", in.route_id);
        __feishu_send_text(in.route_id, FEISHU_NON_TEXT_HINT);
        break;
    case FEISHU_IN_DROP:
    default:
        PR_DEBUG("feishu event dropped");
        break;
    }
}

/** Handle one decoded frame: pong updates the interval, event frames are ACKed
 *  inline (before dispatch, so Feishu stops re-delivering) and routed. */
STATIC VOID_T __feishu_handle_frame(CONST BYTE_T *buf, UINT_T len)
{
    FEISHU_FRAME_T frame;
    if (feishu_frame_parse(buf, len, &frame) != OPRT_OK) {
        PR_WARN("feishu frame parse failed (len=%u)", len);
        return;
    }

    CONST CHAR_T *type = feishu_frame_header(&frame, "type");

    if (frame.method == FEISHU_METHOD_CONTROL) {
        if (type != NULL && strcmp(type, "pong") == 0) {
            __feishu_handle_pong(&frame);
        }
        return; /* control frames are not ACKed */
    }

    /* DATA: only event frames carry a payload we act on. */
    if (type == NULL || strcmp(type, "event") != 0) {
        return;
    }
    if (frame.payload == NULL || frame.payload_len == 0) {
        return;
    }

    __feishu_send_ack(&frame);
    __feishu_dispatch_event(&frame);
}

/* WebSocket event callback (runs on the receive thread — the lib fires it
 * inline from websocket_client_receive / _open / _close). RECV_DATA carries one
 * whole WS message = one protobuf Frame. CLOSE/DISCONNECT only raise the flag;
 * the receive loop performs the teardown itself to avoid a read/close race. */
STATIC VOID_T __feishu_ws_event_cb(websocket_client_msg_t *msg, VOID_T *priv)
{
    (VOID_T)priv;
    if (msg == NULL || s_feishu == NULL) {
        return;
    }
    switch (msg->event) {
    case WEBSOCKET_RECV_DATA_EVENT:
        if (msg->data != NULL && msg->len > 0) {
            __feishu_handle_frame(msg->data, msg->len);
        }
        break;
    case WEBSOCKET_DISCONNECT_EVENT:
    case WEBSOCKET_CLOSE_EVENT:
        PR_NOTICE("feishu websocket close/disconnect event");
        s_feishu->close_requested = TRUE;
        break;
    default:
        break;
    }
}

/* ── State handlers ────────────────────────────────────────────────────── */

STATIC OPERATE_RET __feishu_idle(VOID_T)
{
    /* Gate on MQTT connected + time synced (TLS validates certificates against
     * the wall clock). Stay in IDLE until both hold. */
    if (tuya_svc_netmgr_get_status() != NETWORK_STATUS_MQTT ||
        tal_time_check_time_sync() != OPRT_OK) {
        tal_system_sleep(1000);
        return OPRT_OK;
    }
    __feishu_set_state(FEISHU_STATE_SETUP);
    return OPRT_OK;
}

STATIC OPERATE_RET __feishu_setup(VOID_T)
{
    OPERATE_RET rt = __feishu_get_ws_endpoint(&s_feishu->endpoint);
    if (rt != OPRT_OK) {
        PR_ERR("feishu get ws endpoint failed: %d", rt);
        return rt;
    }
    PR_NOTICE("feishu endpoint ready (service_id=%d ping=%dms), connecting",
              s_feishu->endpoint.service_id, s_feishu->endpoint.ping_interval_ms);
    __feishu_set_state(FEISHU_STATE_CONNECT);
    return OPRT_OK;
}

STATIC OPERATE_RET __feishu_connect(VOID_T)
{
    /* websocket_client_init parses the "wss://host/path?query" endpoint URL and
     * defaults the port to 443, so no manual host/port parsing is needed. The
     * URL self-authenticates (query carries service_id), so no auth header. TLS
     * certs for the Feishu domain are fetched by the cert manager on first
     * connect (same as bigmodel). */
    websocket_client_cfg_t cfg = {
        .uri      = s_feishu->endpoint.url,
        .priv_data = s_feishu,
        .event_cb = __feishu_ws_event_cb,
    };

    INT_T ir = websocket_client_init(&s_feishu->ws_handle, &cfg);
    if (ir != 0 || s_feishu->ws_handle == NULL) {
        PR_ERR("feishu websocket init failed: %d", ir);
        return OPRT_COM_ERROR;
    }

    INT_T cr = websocket_client_open(s_feishu->ws_handle, FEISHU_CONNECT_TIMEOUT_MS);
    if (cr != 0) {
        PR_ERR("feishu websocket open failed: %d", cr);
        __feishu_ws_close();
        return (OPERATE_RET)cr; /* propagate TLS code so cert-not-ready is classified */
    }

    PR_NOTICE("feishu websocket connected");
    s_feishu->ping_interval_ms = s_feishu->endpoint.ping_interval_ms;
    s_feishu->last_ping_ms     = tal_system_get_millisecond();
    s_feishu->connected_at_ms  = s_feishu->last_ping_ms; /* backoff resets only after this + FEISHU_STABLE_MS */
    /* A prior teardown's websocket_client_close() fires DISCONNECT, which sets
     * close_requested=TRUE; clear it here or the fresh connection self-tears. */
    s_feishu->close_requested  = FALSE;
    __feishu_set_state(FEISHU_STATE_RUNNING);
    return OPRT_OK;
}

STATIC OPERATE_RET __feishu_running(VOID_T)
{
    /* A close raised by the WS event callback is serviced here, on the receive
     * thread, so the ws_handle is only ever freed by its own reader. */
    if (s_feishu->close_requested) {
        PR_NOTICE("feishu close requested, reconnecting");
        return OPRT_COM_ERROR; /* -> handle_err(RUNNING) -> client_close -> IDLE */
    }

    /* Reset backoff only once the connection has proven stable, not on mere
     * connect success — else an accept-then-instant-drop loop keeps resetting
     * and never escalates the retry interval. */
    UINT_T now = tal_system_get_millisecond();
    if (s_feishu->backoff.attempt != 0 &&
        (now - s_feishu->connected_at_ms) >= FEISHU_STABLE_MS) {
        __feishu_backoff_reset(&s_feishu->backoff);
    }

    INT_T poll = websocket_client_poll(s_feishu->ws_handle, FEISHU_POLL_INTERVAL_MS);
    if (poll < 0) {
        PR_ERR("feishu poll error: %d", poll);
        return OPRT_COM_ERROR;
    }
    if (poll == 0) {
        /* Idle: send an application-level ping once PingInterval has elapsed; a
         * write failure means the link is dead, so reconnect instead of waiting
         * for the socket poll to surface it (which can lag minutes). */
        if ((now - s_feishu->last_ping_ms) >= (UINT_T)s_feishu->ping_interval_ms) {
            if (__feishu_send_ping() != OPRT_OK) {
                return OPRT_COM_ERROR;
            }
            s_feishu->last_ping_ms = now;
        }
        return OPRT_OK;
    }

    /* Data available: receive one WS message; the event callback delivers the
     * whole frame inline. A non-zero result means close/error -> reconnect. */
    INT_T rr = websocket_client_receive(s_feishu->ws_handle);
    if (rr != 0) {
        PR_ERR("feishu receive error: %d", rr);
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

/** Sleep up to @p ms but return early once terminate is set, so a backoff (up to
 *  60s) never blocks the thread from exiting promptly on deinit. */
STATIC VOID_T __feishu_sleep_interruptible(UINT_T ms)
{
    while (ms > 0 && !s_feishu->terminate) {
        UINT_T step = (ms > 100) ? 100 : ms;
        tal_system_sleep(step);
        ms -= step;
    }
}

STATIC VOID_T __feishu_handle_err(OPERATE_RET rt)
{
    switch (s_feishu->state) {
    case FEISHU_STATE_SETUP:
    case FEISHU_STATE_CONNECT: {
        /* Endpoint handshake or TLS dial failed: back off and retry the whole
         * handshake (which always pulls a fresh URL). The expected first-boot
         * window where the cert manager hasn't fetched the CA yet logs softer. */
        UINT_T delay = __feishu_backoff_next_ms(&s_feishu->backoff,
                                              FEISHU_BACKOFF_BASE_MS, FEISHU_BACKOFF_CAP_MS);
        if (s_feishu->state == FEISHU_STATE_CONNECT && __feishu_err_is_cert_not_ready(rt)) {
            PR_NOTICE("feishu TLS cert not ready yet %d, backoff %ums", rt, delay);
        } else {
            PR_WARN("feishu handshake failed %d, backoff %ums", rt, delay);
        }
        __feishu_set_state(FEISHU_STATE_IDLE);
        __feishu_sleep_interruptible(delay);
        break;
    }
    case FEISHU_STATE_RUNNING:
        /* Live-connection drop: tear down now and re-handshake immediately. The
         * backoff only escalates if the subsequent reconnect keeps failing. */
        PR_ERR("feishu running error %d, reconnecting", rt);
        __feishu_client_close(); /* -> IDLE, re-runs endpoint handshake with a fresh URL */
        break;
    default:
        __feishu_sleep_interruptible(1000);
        break;
    }
}

STATIC VOID_T __feishu_thread(PVOID_T args)
{
    (VOID_T)args;
    OPERATE_RET rt = OPRT_OK;
    while (!s_feishu->terminate &&
           tal_thread_get_state(s_feishu->thread) == THREAD_STATE_RUNNING) {
        switch (s_feishu->state) {
        case FEISHU_STATE_IDLE:    rt = __feishu_idle();    break;
        case FEISHU_STATE_SETUP:   rt = __feishu_setup();   break;
        case FEISHU_STATE_CONNECT: rt = __feishu_connect(); break;
        case FEISHU_STATE_RUNNING: rt = __feishu_running(); break;
        default:                   rt = OPRT_OK;            break;
        }
        if (rt != OPRT_OK) {
            __feishu_handle_err(rt);
        }
    }
    __feishu_ws_close();
    PR_NOTICE("feishu channel thread exit");
    /* Must be the last touch of s_feishu: deinit frees it once this posts. */
    tal_semaphore_post(s_feishu->exit_sem);
}

/* First-boot: KV 命中用之 / 未命中且宏非空则播种 / 都空则 out=""。 */
STATIC VOID_T __feishu_resolve_cred(CONST CHAR_T *kv_key, CONST CHAR_T *macro,
                                    CHAR_T *out, UINT_T out_cap)
{
    out[0] = '\0';
    BYTE_T *val = NULL;
    UINT_T  len = 0;
    if (wd_common_read(kv_key, &val, &len) == OPRT_OK && val != NULL && len > 0) {
        UINT_T n = (len < out_cap - 1) ? len : (out_cap - 1);
        memcpy(out, val, n);
        out[n] = '\0';
        wd_common_free_data(val);
        return;                                   /* USE_KV */
    }
    if (val != NULL) {
        wd_common_free_data(val);                 /* stored-but-empty: still owned, free it */
    }
    if (macro != NULL && macro[0] != '\0') {
        wd_common_write(kv_key, (CONST BYTE_T *)macro, (UINT_T)strlen(macro)); /* SEED_MACRO */
        snprintf(out, out_cap, "%s", macro);
    }
    /* else WAIT_CONFIG: out stays "" */
}

/* ── Channel vtable ────────────────────────────────────────────────────── */

STATIC OPERATE_RET __feishu_init(CONST VOID_T *cfg)
{
    (VOID_T)cfg;
    if (s_feishu != NULL) {
        return OPRT_OK;
    }
    s_feishu = (FEISHU_CHANNEL_S *)tal_malloc(sizeof(FEISHU_CHANNEL_S));
    if (s_feishu == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    memset(s_feishu, 0, sizeof(FEISHU_CHANNEL_S));
    s_feishu->state            = FEISHU_STATE_IDLE;
    s_feishu->ping_interval_ms = FEISHU_DEFAULT_PING_MS;
    __feishu_resolve_cred(FEISHU_KV_APP_ID, FEISHU_APP_ID,
                          s_feishu->app_id_buf, sizeof(s_feishu->app_id_buf));
    __feishu_resolve_cred(FEISHU_KV_APP_SECRET, FEISHU_APP_SECRET,
                          s_feishu->app_secret_buf, sizeof(s_feishu->app_secret_buf));
    s_feishu->configured = (s_feishu->app_id_buf[0] != '\0' &&
                            s_feishu->app_secret_buf[0] != '\0');

    if (tal_semaphore_create_init(&s_feishu->exit_sem, 0, 1) != OPRT_OK) {
        PR_ERR("feishu exit sem create failed");
        tal_free(s_feishu);
        s_feishu = NULL;
        return OPRT_COM_ERROR;
    }
    if (tal_mutex_create_init(&s_feishu->send_mutex) != OPRT_OK) {
        PR_ERR("feishu send mutex create failed");
        tal_semaphore_release(s_feishu->exit_sem);
        tal_free(s_feishu);
        s_feishu = NULL;
        return OPRT_COM_ERROR;
    }
    if (!s_feishu->configured) {
        PR_WARN("feishu: no credentials configured; channel stays idle");
    }
    PR_NOTICE("feishu channel init");
    return OPRT_OK;
}

STATIC OPERATE_RET __feishu_start(VOID_T)
{
    if (s_feishu == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (!s_feishu->configured) {
        PR_WARN("feishu channel not configured, skip start");
        return OPRT_OK;
    }
    if (s_feishu->thread != NULL) {
        return OPRT_OK;
    }

    THREAD_CFG_T thrd = {0};
    thrd.priority   = THREAD_PRIO_1;
    thrd.thrdname   = "feishu_ws";
    thrd.stackDepth = FEISHU_CLIENT_STACK_SIZE;
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    thrd.psram_mode = 1;
#endif
    OPERATE_RET rt = tal_thread_create_and_start(&s_feishu->thread, NULL, NULL,
                                                 __feishu_thread, NULL, &thrd);
    if (rt != OPRT_OK) {
        PR_ERR("feishu thread create failed: %d", rt);
        return rt;
    }
    PR_NOTICE("feishu channel started");
    return OPRT_OK;
}

STATIC OPERATE_RET __feishu_stop(VOID_T)
{
    if (s_feishu != NULL) {
        s_feishu->terminate = TRUE;
    }
    return OPRT_OK;
}

STATIC OPERATE_RET __feishu_deinit(VOID_T)
{
    if (s_feishu == NULL) {
        return OPRT_OK;
    }
    s_feishu->terminate = TRUE;
    if (s_feishu->thread) {
        /* tal_thread_delete does not join; wait for the thread's exit_sem before
         * freeing s_feishu, which it accesses up to its final post. */
        if (tal_semaphore_wait(s_feishu->exit_sem, FEISHU_THREAD_JOIN_TIMEOUT_MS) != OPRT_OK) {
            PR_ERR("feishu deinit: thread join timed out, leaking context to avoid UAF");
            return OPRT_COM_ERROR; /* thread still running: do NOT free s_feishu */
        }
        tal_thread_delete(s_feishu->thread);
        s_feishu->thread = NULL;
    }
    __feishu_ws_close(); /* idempotent: the thread already closed ws_handle */
    if (s_feishu->exit_sem) {
        tal_semaphore_release(s_feishu->exit_sem);
        s_feishu->exit_sem = NULL;
    }
    if (s_feishu->send_mutex) {
        tal_mutex_release(s_feishu->send_mutex);
        s_feishu->send_mutex = NULL;
    }
    /* Null the global before freeing (not after): __feishu_send's entry guard
     * only protects callers that observe s_feishu == NULL. Freeing first would
     * leave a window where s_feishu is still non-NULL but already dangling. */
    FEISHU_CHANNEL_S *victim = s_feishu;
    s_feishu = NULL;
    tal_free(victim);
    PR_NOTICE("feishu channel deinit");
    return OPRT_OK;
}

/** Send an outbound reply. Called on the wk_provider thread with the complete
 *  accumulated TEXT reply; the synchronous HTTP POST may block. */
STATIC OPERATE_RET __feishu_send(CONST CHAR_T *chat_id, CONST WUKONG_AI_MSG_T *msg)
{
    /* Race guard vs feishu_channel_set_creds -> __feishu_deinit (runs on the CLI
     * thread) freeing s_feishu: bail before touching it if it's already gone or
     * already being torn down. Short-circuit ordering matters — terminate is
     * only read once s_feishu is known non-NULL. This narrows, but does not
     * fully close, the window against a send already past this check when
     * deinit frees s_feishu (see __feishu_deinit's null-before-free ordering
     * and the file header). */
    if (s_feishu == NULL || s_feishu->terminate) {
        return OPRT_COM_ERROR;
    }
    if (chat_id == NULL || msg == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (msg->type != WUKONG_AI_MSG_TYPE_TEXT) {
        PR_WARN("feishu send: unsupported msg type %d", msg->type);
        return OPRT_NOT_SUPPORTED;
    }
    PR_INFO("feishu send reply: chat_id=%s len=%u", chat_id, msg->data_len);
    return __feishu_send_text(chat_id, (CONST CHAR_T *)msg->data);
}

/* ── Descriptor + registration ─────────────────────────────────────────── */

STATIC CONST WUKONG_AI_CHAN_T s_feishu_channel = {
    .name   = WUKONG_CHAN_FEISHU,
    .flags  = WUKONG_CHAN_FLAG_IM,
    .init   = __feishu_init,
    .start  = __feishu_start,
    .stop   = __feishu_stop,
    .deinit = __feishu_deinit,
    .send   = __feishu_send,
};

OPERATE_RET feishu_channel_register(VOID_T)
{
    return wukong_ai_channel_register(&s_feishu_channel, NULL);
}

OPERATE_RET feishu_channel_set_creds(CONST CHAR_T *app_id, CONST CHAR_T *app_secret)
{
    if (app_id == NULL || app_secret == NULL) {
        return OPRT_INVALID_PARM;
    }
    wd_common_write(FEISHU_KV_APP_ID,     (CONST BYTE_T *)app_id,     (UINT_T)strlen(app_id));
    wd_common_write(FEISHU_KV_APP_SECRET, (CONST BYTE_T *)app_secret, (UINT_T)strlen(app_secret));

    OPERATE_RET rt = __feishu_deinit(); /* join 超时返回非 OK 且不 free —— 别继续 */
    if (rt != OPRT_OK) {
        PR_ERR("feishu set_creds: deinit failed (%d), skip restart", rt);
        return rt;
    }
    rt = __feishu_init(NULL);
    if (rt != OPRT_OK) {
        PR_ERR("feishu set_creds: init failed (%d)", rt);
        return rt;
    }
    return __feishu_start();
}

/* app_id 明文,app_secret 打码(前3位+****,空则 (unset))。 */
OPERATE_RET feishu_channel_get_creds_masked(CHAR_T *out, UINT_T out_cap)
{
    if (out == NULL || out_cap == 0) {
        return OPRT_INVALID_PARM;
    }
    CONST CHAR_T *id  = (s_feishu != NULL) ? s_feishu->app_id_buf     : "";
    CONST CHAR_T *sec = (s_feishu != NULL) ? s_feishu->app_secret_buf : "";
    if (sec[0] == '\0') {
        snprintf(out, out_cap, "feishu app_id=%s app_secret=(unset)", id[0] ? id : "(unset)");
    } else {
        snprintf(out, out_cap, "feishu app_id=%s app_secret=%.3s****", id[0] ? id : "(unset)", sec);
    }
    return OPRT_OK;
}
