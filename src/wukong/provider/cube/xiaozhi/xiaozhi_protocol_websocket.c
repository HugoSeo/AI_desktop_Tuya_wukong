/**
 * @file xiaozhi_protocol_websocket.c
 * @brief XiaoZhi AI WebSocket protocol implementation (V1/V3 support)
 * @date 2026-01-13
 * @author linch
 */

#include "tuya_cloud_types.h"
#include "xiaozhi_protocol.h"
#include "xiaozhi_server.h"
#include "xiaozhi_board.h"
#include "xiaozhi_config.h"
#include "tuya_simple_http.h"
#include "tuya_error_code.h"
#include "tal_log.h"
#include "tal_mutex.h"
#include "tal_semaphore.h"
#include "tal_system.h"
#include "tal_memory.h"
#include "tal_thread.h"
#include "websocket_client.h"

/* ========== WebSocket Context (Derived from xiaozhi_protocol_t) ========== */

typedef struct {
    xiaozhi_protocol_t protocol;    // MUST be first member (inheritance simulation)
    // WebSocket specific fields
    websocket_client_handle_t ws_handle;
    volatile bool audio_channel_opened;
    uint8_t  *send_buf;             // reused V3 send buffer (engine-thread only); grows on demand
    uint32_t  send_buf_cap;         // capacity of send_buf
    MUTEX_HANDLE mutex;
    SEM_HANDLE sync_sem;            // Synchronization semaphore for events
    SEM_HANDLE exit_sem;            // Posted by recv thread on exit; CloseAudioChannel waits to join it
    // Receive thread management
    THREAD_HANDLE recv_thread;
    volatile bool recv_thread_running;
    volatile bool close_requested;  // Flag to request graceful close
    uint32_t close_request_time_ms; // Time when close was requested (for timeout)
    bool server_hello_received;
    // Protocol version (1 or 3)
    uint8_t protocol_version;
} xiaozhi_websocket_ctx_t;

/* Global WebSocket context pointer (simulates implicit 'this' pointer in C++) */
static xiaozhi_websocket_ctx_t *g_ws_ctx = NULL;

/* ========== Private function declarations ========== */

static void websocket_event_handler(websocket_client_msg_t *msg, void *priv_data);
static void xiaozhi_websocket_handle_json(xiaozhi_websocket_ctx_t *ctx, const cJSON *root);
static void xiaozhi_websocket_handle_audio(xiaozhi_websocket_ctx_t *ctx, uint8_t *data, uint32_t len);
static void xiaozhi_websocket_parse_server_hello(xiaozhi_websocket_ctx_t *ctx, const cJSON *root);
static char *xiaozhi_websocket_get_hello_message(xiaozhi_websocket_ctx_t *ctx);
static void xiaozhi_websocket_receive_thread(void *arg);

// Operation function implementation (C++ style, no ctx parameter)
static bool xiaozhi_websocket_start(void);
static bool xiaozhi_websocket_open_audio_channel(void);
static void xiaozhi_websocket_close_audio_channel(void);
static bool xiaozhi_websocket_is_audio_channel_opened(void);
static bool xiaozhi_websocket_send_audio(xiaozhi_audio_packet_t *packet);
static bool xiaozhi_websocket_send_text(const char *text);

/* ========== WebSocket Receive Thread ========== */

#define WEBSOCKET_CLOSE_TIMEOUT_MS 3000  // timeout for close handshake

/* Keepalive: the lib's auto-ping lives in its internal task (websocket_client_start),
 * which this protocol bypasses (manual open + own recv thread). We replicate the lib's
 * poll + periodic ping so idle connections aren't dropped by the server/gateway.
 * websocket_client_receive / _send_ping are exported by the lib but not in its public header. */
extern int websocket_client_receive(websocket_client_handle_t client);
extern int websocket_client_send_ping(websocket_client_handle_t client);
#define WEBSOCKET_POLL_INTERVAL_MS   1000
#define WEBSOCKET_KEEPALIVE_TICKS    30    /* poll 1s × 30 ≈ 空闲 30s 发一次 ping 保活 */

static void xiaozhi_websocket_receive_thread(void *arg)
{
    xiaozhi_websocket_ctx_t *ctx = (xiaozhi_websocket_ctx_t *)arg;
    xiaozhi_protocol_t *protocol = &ctx->protocol;
    uint8_t idle_ticks = 0;

    TAL_PR_INFO("WebSocket receive thread started");

    while (ctx->recv_thread_running) {
        // Check if close was requested and timeout exceeded
        if (ctx->close_requested) {
            uint32_t current_time = tal_system_get_millisecond();
            uint32_t elapsed = current_time - ctx->close_request_time_ms;

            if (elapsed >= WEBSOCKET_CLOSE_TIMEOUT_MS) {
                TAL_PR_WARN("Close handshake timeout (%u ms), forcing exit", elapsed);
                break;  // Force exit if server doesn't respond
            }
        }

        // Poll for incoming data; on idle, send a keepalive ping (heartbeat).
        int poll_result = websocket_client_poll(ctx->ws_handle, WEBSOCKET_POLL_INTERVAL_MS);
        if (poll_result < 0) {
            TAL_PR_ERR("WebSocket poll error: %d", poll_result);
            break;
        } else if (poll_result == 0) {
            // Idle timeout: send keepalive ping every ~WEBSOCKET_KEEPALIVE_TICKS seconds.
            if (++idle_ticks >= WEBSOCKET_KEEPALIVE_TICKS) {
                idle_ticks = 0;
                if (websocket_client_send_ping(ctx->ws_handle) != 0) {
                    TAL_PR_ERR("WebSocket keepalive ping failed");
                    break;
                }
                TAL_PR_DEBUG("WebSocket keepalive ping sent");
            }
            continue;
        }

        // Data available, receive it.
        idle_ticks = 0;
        int recv_result = websocket_client_receive(ctx->ws_handle);
        if (recv_result != 0) {
            TAL_PR_ERR("WebSocket receive error: %d", recv_result);
            break;
        }
    }
    
    TAL_PR_WARN("WebSocket receive thread exiting");

    // 退出原因判定：close_requested=true 表示是 CloseAudioChannel 的主动关闭，
    // 此时不应再通知上层（否则会触发不必要的重连）；否则属意外断开（poll/recv 错误、
    // 心跳失败），需通知上层以便重连。
    bool unexpected = !ctx->close_requested;
    ctx->audio_channel_opened = false;

    if (unexpected) {
        if (protocol->OnAudioChannelClosed) {
            protocol->OnAudioChannelClosed(protocol->user_data);
        }
        if (protocol->OnDisconnected) {
            protocol->OnDisconnected(protocol->user_data);
        }
    }

    // 关闭并置空 ws_handle，与发送路径对 ws_handle 的访问用 mutex 互斥，避免"边发边关"
    tal_mutex_lock(ctx->mutex);
    if (ctx->ws_handle) {
        websocket_client_close(ctx->ws_handle);
        ctx->ws_handle = NULL;
    }
    tal_mutex_unlock(ctx->mutex);

    ctx->recv_thread_running = false;

    // 通知 join：本线程已结束对 ctx 的全部访问。CloseAudioChannel 等到此信号后才删除线程
    // 句柄并允许后续 free(ctx)，从而杜绝 UAF。此后不得再访问 ctx，也不在此自删线程。
    tal_semaphore_post(ctx->exit_sem);
}

/* ========== WebSocket Event handler ========== */

static void websocket_event_handler(websocket_client_msg_t *msg, void *priv_data)
{
    xiaozhi_websocket_ctx_t *ctx = (xiaozhi_websocket_ctx_t *)priv_data;
    xiaozhi_protocol_t *protocol = &ctx->protocol;
    
    if (!protocol) {
        TAL_PR_ERR("Protocol pointer is NULL");
        return;
    }
    
    switch (msg->event) {
        case WEBSOCKET_RECV_DATA_EVENT:
            xiaozhi_protocol_update_incoming_time(protocol);
            
            // Check if binary or text message
            if (msg->data && msg->len > 0 && msg->data[0] == '{') {
                TAL_PR_DEBUG("Received JSON message: %s", msg->data);
                // JSON text message
                cJSON *root = cJSON_Parse((char *)msg->data);
                if (root) {
                    xiaozhi_websocket_handle_json(ctx, root);
                    cJSON_Delete(root);
                }
            } else {
                // Binary audio data
                xiaozhi_websocket_handle_audio(ctx, msg->data, msg->len);
            }
            break;
            
        case WEBSOCKET_CONNECTED_EVENT:
            TAL_PR_INFO("WebSocket connected event");
            if (protocol->OnConnected) {
                protocol->OnConnected(protocol->user_data);
            }
            break;
            
        // case WEBSOCKET_DISCONNECT_EVENT:
        // case WEBSOCKET_CLOSE_EVENT:
        //     TAL_PR_WARN("WebSocket disconnected/closed event");
        //     break;
            
        default:
            break;
    }
}

/* ========== JSON Message handling ========== */

static void xiaozhi_websocket_handle_json(xiaozhi_websocket_ctx_t *ctx, const cJSON *root)
{
    xiaozhi_protocol_t *protocol = &ctx->protocol;
    
    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type)) {
        return;
    }
    
    if (strcmp(type->valuestring, "hello") == 0) {
        // Server hello message
        TAL_PR_INFO("Received server hello");
        xiaozhi_websocket_parse_server_hello(ctx, root);
        tal_semaphore_post(ctx->sync_sem);
    } else {
        // Other JSON messages
        if (protocol->OnIncomingJson) {
            protocol->OnIncomingJson(root, protocol->user_data);
        }
    }
}

static void xiaozhi_websocket_parse_server_hello(xiaozhi_websocket_ctx_t *ctx, const cJSON *root)
{
    xiaozhi_protocol_t *protocol = &ctx->protocol;
    
    // Parse session_id
    cJSON *session_id = cJSON_GetObjectItem(root, "session_id");
    if (cJSON_IsString(session_id)) {
        strncpy(protocol->session_id, session_id->valuestring, sizeof(protocol->session_id) - 1);
        TAL_PR_INFO("Session ID: %s", protocol->session_id);
    }
    
    // Parse audio parameters
    cJSON *audio_params = cJSON_GetObjectItem(root, "audio_params");
    if (cJSON_IsObject(audio_params)) {
        cJSON *sample_rate = cJSON_GetObjectItem(audio_params, "sample_rate");
        if (cJSON_IsNumber(sample_rate)) {
            protocol->server_sample_rate = sample_rate->valueint;
            TAL_PR_INFO("Server sample rate: %d", protocol->server_sample_rate);
        }
        
        cJSON *frame_duration = cJSON_GetObjectItem(audio_params, "frame_duration");
        if (cJSON_IsNumber(frame_duration)) {
            protocol->server_frame_duration = frame_duration->valueint;
            TAL_PR_INFO("Server frame duration: %d ms", protocol->server_frame_duration);
        }

        cJSON *format = cJSON_GetObjectItem(audio_params, "format");
        if (cJSON_IsString(format)) {
            if (strcmp(format->valuestring, "mp3") == 0) {
                protocol->server_audio_format = XIAOZHI_AUDIO_FORMAT_MP3;
            } else if (strcmp(format->valuestring, "speex") == 0) {
                protocol->server_audio_format = XIAOZHI_AUDIO_FORMAT_SPEEX;
            } else {
                protocol->server_audio_format = XIAOZHI_AUDIO_FORMAT_OPUS;
            }
            TAL_PR_INFO("Server audio format: %s", format->valuestring);
        }
    }
}

/* ========== Audio data handling (V1/V3 protocol) ========== */

static void xiaozhi_websocket_handle_audio(xiaozhi_websocket_ctx_t *ctx, uint8_t *data, uint32_t len)
{
    xiaozhi_protocol_t *protocol = &ctx->protocol;
    
    if (!protocol->OnIncomingAudio) {
        return;
    }
    
    xiaozhi_audio_packet_t packet;
    
    if (ctx->protocol_version == 3) {
        // V3 protocol: binary header + payload
        if (len < sizeof(xiaozhi_binary_protocol_v3_t)) {
            TAL_PR_ERR("Invalid audio packet size: %u", len);
            return;
        }
        
        xiaozhi_binary_protocol_v3_t *bp3 = (xiaozhi_binary_protocol_v3_t *)data;
        
        // Network byte order conversion (use TuyaOS macro)
        uint16_t payload_size = UNI_NTOHS(bp3->payload_size);

        // 边界校验：payload_size 来自网络，必须确保实际收到的字节数足够。
        // 否则下游按 (2 + payload_size) 读取 payload 会越界（信息泄露/崩溃）。
        if (len < sizeof(xiaozhi_binary_protocol_v3_t) + payload_size) {
            TAL_PR_ERR("Truncated V3 audio packet: len=%u, declared payload=%u", len, payload_size);
            return;
        }

        TAL_PR_TRACE("Received audio (V3): type=%u, size=%u", bp3->type, payload_size);
        
        // Optimization: Point packet directly to payload_size field (2-byte big-endian length + data)
        // This matches Opus VBR format: [2-byte length (big-endian)] + [opus data]
        // No extra memory allocation or copying needed!
        packet.payload = (uint8_t *)&bp3->payload_size;  // Points to [payload_size + payload] as continuous memory
        packet.payload_len = sizeof(bp3->payload_size) + payload_size;  // 2 bytes length + actual data
    } else {
        // V1 protocol: raw Opus data (no header)
        TAL_PR_TRACE("Received audio (V1): raw opus data, size=%u", len);
        
        packet.payload = data;          // Direct pointer to raw Opus data
        packet.payload_len = len;       // Full length
    }
    
    // Call user callback (callback must copy data if it needs to keep it)
    protocol->OnIncomingAudio(&packet, protocol->user_data);
}

/* ========== Audio sending (V1/V3 protocol) ========== */

/* Send a binary frame under ctx->mutex, re-checking ws_handle inside the lock so the recv
 * thread can't close/free the handle mid-send. Returns the lib result (0 = ok), -1 if closed. */
static int xiaozhi_ws_send_bin_locked(uint8_t *data, uint32_t len)
{
    tal_mutex_lock(g_ws_ctx->mutex);
    int ret = g_ws_ctx->ws_handle ? websocket_client_send_bin(g_ws_ctx->ws_handle, data, len) : -1;
    tal_mutex_unlock(g_ws_ctx->mutex);
    return ret;
}

static bool xiaozhi_websocket_send_audio(xiaozhi_audio_packet_t *packet)
{
    if (!g_ws_ctx || !g_ws_ctx->ws_handle || !g_ws_ctx->audio_channel_opened) {
        TAL_PR_ERR("Audio channel not opened");
        return false;
    }
    if (!packet || !packet->payload) {
        TAL_PR_ERR("Invalid audio packet");
        return false;
    }

    if (g_ws_ctx->protocol_version == 3) {
        // V3: prepend the 4-byte binary header. Reuse a per-connection PSRAM buffer instead of
        // malloc/free per frame (audio is a continuous hot path). send_audio runs only on the
        // engine thread, so send_buf needs no extra lock; it grows only if a frame exceeds the
        // current capacity (rare). The 1 memcpy stays: send_bin needs header+opus contiguous.
        uint32_t total_size = sizeof(xiaozhi_binary_protocol_v3_t) + packet->payload_len;
        if (total_size > g_ws_ctx->send_buf_cap) {
            uint8_t *nb = (uint8_t *)tal_psram_malloc(total_size);
            if (!nb) {
                TAL_PR_ERR("Failed to allocate send buffer (%u)", total_size);
                return false;
            }
            if (g_ws_ctx->send_buf) {
                tal_psram_free(g_ws_ctx->send_buf);
            }
            g_ws_ctx->send_buf = nb;
            g_ws_ctx->send_buf_cap = total_size;
        }
        xiaozhi_binary_protocol_v3_t *bp3 = (xiaozhi_binary_protocol_v3_t *)g_ws_ctx->send_buf;
        bp3->type = 0;  // OPUS audio
        bp3->reserved = 0;
        bp3->payload_size = UNI_HTONS(packet->payload_len);  // Network byte order (TuyaOS macro)
        memcpy(bp3->payload, packet->payload, packet->payload_len);

        int sret = xiaozhi_ws_send_bin_locked(g_ws_ctx->send_buf, total_size);
        TAL_PR_TRACE("Sending audio (V3): size=%u", packet->payload_len);
        return (sret == 0);
    } else {
        // V1: raw Opus data, no header
        int sret = xiaozhi_ws_send_bin_locked(packet->payload, packet->payload_len);
        TAL_PR_TRACE("Sending audio (V1): raw opus, size=%u", packet->payload_len);
        return (sret == 0);
    }
}

/* ========== Text sending ========== */

static bool xiaozhi_websocket_send_text(const char *text)
{
    if (!g_ws_ctx || !g_ws_ctx->ws_handle) {
        TAL_PR_ERR("WebSocket not initialized");
        return false;
    }
    
    if (!text) {
        TAL_PR_ERR("Invalid text parameter");
        return false;
    }
    
    TAL_PR_DEBUG("Sending text: %s", text);

    // 与接收线程关闭 ws_handle 互斥
    tal_mutex_lock(g_ws_ctx->mutex);
    int sret = g_ws_ctx->ws_handle ?
               websocket_client_send_text(g_ws_ctx->ws_handle, (uint8_t *)text, strlen(text)) : -1;
    tal_mutex_unlock(g_ws_ctx->mutex);
    return (sret == 0);
}

/* ========== Hello Message generation ========== */

static char *xiaozhi_websocket_get_hello_message(xiaozhi_websocket_ctx_t *ctx)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    
    cJSON_AddStringToObject(root, "type", "hello");
    cJSON_AddNumberToObject(root, "version", ctx->protocol_version);  // Use runtime protocol version
    cJSON_AddStringToObject(root, "transport", "websocket");
    
    // Feature flags
    cJSON *features = cJSON_CreateObject();
    // cJSON_AddBoolToObject(features, "aec", false);  // TODO: AEC support
    cJSON_AddBoolToObject(features, "mcp", true);   // device exposes MCP tools (see wukong_provider_cube mcp_send/recv)
    cJSON_AddItemToObject(root, "features", features);
    
    // Audio parameters
    cJSON *audio_params = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_params, "format", CUBE_UPLINK_AUDIO_FORMAT);
    cJSON_AddNumberToObject(audio_params, "sample_rate", 16000);
    cJSON_AddNumberToObject(audio_params, "channels", 1);
    /* MUST match the uplink encoder frame; uart-speex=20ms, others=40ms (see xiaozhi_config.h) */
    cJSON_AddNumberToObject(audio_params, "frame_duration", CUBE_UPLINK_FRAME_DURATION);
    cJSON_AddItemToObject(root, "audio_params", audio_params);
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    return json_str;  // Must free with cJSON_free
}

/* ========== Audio channel management ========== */
#include "tuya_iot_com_api.h"

static bool xiaozhi_websocket_open_audio_channel(void)
{
    if (!g_ws_ctx) {
        TAL_PR_ERR("WebSocket context not initialized");
        return false;
    }
    
    xiaozhi_protocol_t *protocol = &g_ws_ctx->protocol;
    
    TAL_PR_INFO("Opening audio channel");
    
    // Read WebSocket URL and token from configuration
    xiaozhi_server_config_t *config = xiaozhi_server_get_config();
    if (!config) {
        TAL_PR_ERR("No server configuration");
        return false;
    }
    
    // Priority: use Tuya WebSocket URL if available, otherwise use xiaozhi websocket config
    const char *ws_url = NULL;
    const char *ws_token = NULL;
    
    if (config->tuya.has_config && config->tuya.url[0] != '\0') {
        ws_url = config->tuya.url;
        ws_token = config->tuya.token;
        TAL_PR_INFO("Using Tuya WebSocket: %s", ws_url);
    } else if (config->websocket.has_config) {
        ws_url = config->websocket.url;
        ws_token = config->websocket.token;
        TAL_PR_INFO("Using XiaoZhi WebSocket: %s", ws_url);
    } else {
        TAL_PR_ERR("No WebSocket configuration available");
        return false;
    }
    
    // Create WebSocket client (only set IP:PORT, path will be set separately)
    websocket_client_cfg_t ws_cfg = {
        .uri = ws_url,
        .priv_data = g_ws_ctx,
        .event_cb = websocket_event_handler
    };
    
    if (websocket_client_init(&g_ws_ctx->ws_handle, &ws_cfg) != 0) {
        TAL_PR_ERR("Failed to init websocket client");
        return false;
    }
    
    xiaozhi_board_t *board = xiaozhi_board_get_instance();
    
    // Set authentication headers
    char auth_header[768];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", ws_token);
    websocket_client_add_header(g_ws_ctx->ws_handle, "Authorization", auth_header);
    
    // Set protocol version from configuration (runtime variable)
    const char *version_str = (g_ws_ctx->protocol_version == 3) ? "3" : "1";
    websocket_client_add_header(g_ws_ctx->ws_handle, "Protocol-Version", version_str);
    TAL_PR_INFO("Using Protocol-Version: %s (%s)", 
                version_str,
                g_ws_ctx->protocol_version == 3 ? "binary header" : "raw Opus");
    
    if (config->tuya.has_config) {
        websocket_client_add_header(g_ws_ctx->ws_handle, "Device-Id", tuya_iot_get_gw_id());
    } else {
        websocket_client_add_header(g_ws_ctx->ws_handle, "Device-Id", board->mac_address);
    }
    websocket_client_add_header(g_ws_ctx->ws_handle, "Client-Id", board->uuid);
    
    // Manually connect to server (no auto thread creation)
    TAL_PR_INFO("Connecting to WebSocket server...");
    if (websocket_client_open(g_ws_ctx->ws_handle, 5000) != 0) {
        websocket_client_close(g_ws_ctx->ws_handle);
        TAL_PR_ERR("Failed to connect websocket");
        return false;
    }
    // Create receive thread BEFORE sending hello message
    g_ws_ctx->recv_thread_running = true;
    THREAD_CFG_T thread_cfg = {
        .thrdname = "xiaozhi_ws_recv",
        .stackDepth = 4 * 1024,
        .priority = THREAD_PRIO_2,
    };
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    thread_cfg.psram_mode = 1;
#endif

    if (tal_thread_create_and_start(&g_ws_ctx->recv_thread, NULL, NULL, 
                                     xiaozhi_websocket_receive_thread, g_ws_ctx, 
                                     &thread_cfg) != OPRT_OK) {
        TAL_PR_ERR("Failed to create receive thread");
        g_ws_ctx->recv_thread_running = false;
        websocket_client_close(g_ws_ctx->ws_handle);
        return false;
    }
    
    TAL_PR_INFO("Receive thread started, sending hello message");
    
    // Send hello message
    char *hello_msg = xiaozhi_websocket_get_hello_message(g_ws_ctx);
    if (hello_msg) {
        TAL_PR_DEBUG("hello message: %s", hello_msg);
        websocket_client_send_text(g_ws_ctx->ws_handle, (uint8_t *)hello_msg, strlen(hello_msg));
        cJSON_free(hello_msg);
    }
    
    // Wait for server hello (5 seconds timeout)
    if (tal_semaphore_wait(g_ws_ctx->sync_sem, 5000) != OPRT_OK) {
        TAL_PR_ERR("Timeout waiting for server hello");
        g_ws_ctx->recv_thread_running = false;
        return false;
    }
    
    g_ws_ctx->audio_channel_opened = true;
    
    TAL_PR_INFO("Audio channel opened successfully");
    
    if (protocol->OnAudioChannelOpened) {
        protocol->OnAudioChannelOpened(protocol->user_data);
    }
    
    return true;
}

static void xiaozhi_websocket_close_audio_channel(void)
{
    if (!g_ws_ctx) {
        return;
    }

    // 注意：不能因 audio_channel_opened==false 就早退——接收线程意外断开时已把它置 false，
    // 但线程/句柄/同步原语仍需在此 join 并回收，否则 deinit 的 free 会造成 UAF/泄漏。

    TAL_PR_INFO("Closing audio channel");

    // 标记为主动关闭：接收线程据此判定不再通知上层重连
    g_ws_ctx->audio_channel_opened = false;
    g_ws_ctx->close_requested = true;
    g_ws_ctx->close_request_time_ms = tal_system_get_millisecond();

    // 尽力发送 CLOSE 帧（与接收线程关闭 ws_handle 互斥；锁须在 join 前释放，避免与接收线程死锁）
    tal_mutex_lock(g_ws_ctx->mutex);
    if (g_ws_ctx->ws_handle) {
        websocket_client_disconnect(g_ws_ctx->ws_handle);
    }
    tal_mutex_unlock(g_ws_ctx->mutex);

    // 通知接收线程尽快退出，并【同步等待其完全结束】后再删除线程句柄。
    // 关键：deinit 紧随其后会 free(protocol/ctx)，必须确保接收线程不再访问 ctx，杜绝 UAF。
    g_ws_ctx->recv_thread_running = false;
    if (g_ws_ctx->recv_thread) {
        if (tal_semaphore_wait(g_ws_ctx->exit_sem,
                               WEBSOCKET_CLOSE_TIMEOUT_MS + 2000) != OPRT_OK) {
            TAL_PR_ERR("Timeout joining receive thread");
        }
        tal_thread_delete(g_ws_ctx->recv_thread);
        g_ws_ctx->recv_thread = NULL;
    }

    // 接收线程已退出，回收本连接占用的同步原语并置空全局指针：
    // deinit 之后即 free(ctx)，置空 g_ws_ctx 可避免 SendAudio/SendText 拿到悬垂指针。
    if (g_ws_ctx->send_buf) { tal_psram_free(g_ws_ctx->send_buf);        g_ws_ctx->send_buf = NULL; g_ws_ctx->send_buf_cap = 0; }
    if (g_ws_ctx->sync_sem) { tal_semaphore_release(g_ws_ctx->sync_sem); g_ws_ctx->sync_sem = NULL; }
    if (g_ws_ctx->exit_sem) { tal_semaphore_release(g_ws_ctx->exit_sem); g_ws_ctx->exit_sem = NULL; }
    if (g_ws_ctx->mutex)    { tal_mutex_release(g_ws_ctx->mutex);        g_ws_ctx->mutex = NULL; }
    g_ws_ctx = NULL;

    TAL_PR_INFO("Audio channel closed, receive thread joined");
}

static bool xiaozhi_websocket_is_audio_channel_opened(void)
{
    return g_ws_ctx ? g_ws_ctx->audio_channel_opened : false;
}

static bool xiaozhi_websocket_start(void)
{
    // WebSocket Protocol Start operation usually done in OpenAudioChannel
    TAL_PR_INFO("WebSocket protocol started");
    return true;
}

/* ========== Registration function ========== */

xiaozhi_protocol_t *xiaozhi_protocol_websocket_register(void)
{
    TAL_PR_INFO("Registering WebSocket protocol");
    
    // Allocate full WebSocket context (derived structure)
    xiaozhi_websocket_ctx_t *ctx = (xiaozhi_websocket_ctx_t *)tal_malloc(
        sizeof(xiaozhi_websocket_ctx_t));
    if (!ctx) {
        TAL_PR_ERR("Failed to allocate websocket context");
        return NULL;
    }
    
    memset(ctx, 0, sizeof(xiaozhi_websocket_ctx_t));
    
    // Initialize protocol version from config (default to 3 if not set or invalid)
    ctx->protocol_version = XIAOZHI_PROTOCOL_VERSION;
    if (ctx->protocol_version != 1 && ctx->protocol_version != 3) {
        TAL_PR_WARN("Invalid XIAOZHI_PROTOCOL_VERSION=%u, defaulting to 3", ctx->protocol_version);
        ctx->protocol_version = 3;
    }
    TAL_PR_INFO("WebSocket protocol version: %u", ctx->protocol_version);
    
    // Set global context pointer (simulates implicit 'this' pointer)
    g_ws_ctx = ctx;
    
    // Create mutex
    if (tal_mutex_create_init(&ctx->mutex) != OPRT_OK) {
        TAL_PR_ERR("Failed to create mutex");
        goto err_cleanup;
    }
    
    // Create semaphore
    if (tal_semaphore_create_init(&ctx->sync_sem, 0, 1) != OPRT_OK) {
        TAL_PR_ERR("Failed to create semaphore");
        goto err_cleanup;
    }

    // Create exit semaphore (recv thread posts on exit; CloseAudioChannel joins on it)
    if (tal_semaphore_create_init(&ctx->exit_sem, 0, 1) != OPRT_OK) {
        TAL_PR_ERR("Failed to create exit semaphore");
        goto err_cleanup;
    }

    // Register operation functions (C++ style, no ctx parameter)
    ctx->protocol.Start = xiaozhi_websocket_start;
    ctx->protocol.OpenAudioChannel = xiaozhi_websocket_open_audio_channel;
    ctx->protocol.CloseAudioChannel = xiaozhi_websocket_close_audio_channel;
    ctx->protocol.IsAudioChannelOpened = xiaozhi_websocket_is_audio_channel_opened;
    ctx->protocol.SendAudio = xiaozhi_websocket_send_audio;
    ctx->protocol.SendText = xiaozhi_websocket_send_text;
    
    TAL_PR_INFO("WebSocket protocol registered successfully");
    
    // Return base protocol pointer (first member, same address as ctx)
    return &ctx->protocol;

err_cleanup:
    if (ctx->sync_sem) {
        tal_semaphore_release(ctx->sync_sem);
    }
    if (ctx->exit_sem) {
        tal_semaphore_release(ctx->exit_sem);
    }
    if (ctx->mutex) {
        tal_mutex_release(ctx->mutex);
    }
    tal_free(ctx);
    g_ws_ctx = NULL;
    return NULL;
}

