/**
 * @file wukong_provider_cube.c
 * @brief Cube (xiaozhi) Cloud Platform Provider — passive, JD-style.
 *
 * Wraps the xiaozhi WebSocket protocol core (xiaozhi_protocol*.c / xiaozhi_server.c).
 * The provider owns ONLY connection + audio I/O + downlink bridging; the conversation
 * state machine lives entirely in the Mode layer (wukong_ai_mode_*). There is no
 * conversation state machine here (cf. JD: __jd_input_thread is a pure I/O pipeline).
 *
 *   - connect : EVENT_MQTT_CONNECTED -> NETWORK_CONNECTED msg; engine thread runs
 *               config/activation (backend = CUBE_BACKEND_TUYA | _XIAOZHI) + OpenAudioChannel.
 *   - uplink  : framework send(AUDIO) copies PCM and posts to the queue; the engine
 *               thread (30KB stack) Opus-encodes and SendAudio's — heavy encode + TLS
 *               off the caller (record_task) stack.
 *   - turn    : ioctl(INPUT_START) -> SendStartListening (+barge-in); INPUT_STOP ->
 *               SendStopListening. The Mode layer's VAD/key drives these per utterance.
 *   - downlink: protocol callbacks -> wukong events (TTS playback, ASR/NLG, emotion).
 *
 * Device mic capture / KWS / conversation state stay with the framework + Mode layer.
 */

#include "wukong_ai_provider.h"
#include "wukong_ai_agent.h"
#include "wukong_mcp.h"
#include "wukong_fc.h"
#include "wukong_audio_player.h"
#include "svc_ai_player.h"
#include "fc_emotion.h"

#include "xiaozhi_protocol.h"
#include "xiaozhi_server.h"
#include "xiaozhi_config.h"

#include "tuya_ai_encoder.h"
#include "tuya_ai_encoder_opus.h"
#include "tuya_cert_manager.h"
#include "base_event.h"
#include "tal_thread.h"
#include "tal_queue.h"
#include "tal_semaphore.h"
#include "tal_memory.h"
#include "tal_system.h"
#include "uni_log.h"
#include "cJSON.h"
#include "ty_cJSON.h"
#include <string.h>

/* Opus encoder defaults (overridable via tuya_app_config.h), aligned with JD. */
#ifndef APP_OPUS_ENCODER_BITRATE
#define APP_OPUS_ENCODER_BITRATE    16000
#endif
#ifndef APP_OPUS_ENCODER_BANDWIDTH
#define APP_OPUS_ENCODER_BANDWIDTH  1102  /* OPUS_BANDWIDTH_MEDIUMBAND */
#endif
#define CUBE_OPUS_SAMPLE_RATE       16000
#define CUBE_OPUS_CHANNELS          1
#define CUBE_OPUS_BITS_PER_SAMPLE   16
/* 0 -> encoder default frame (40ms / 640 samples @16k). Opus packets are self-describing,
 * so the server decodes per-packet regardless of the hello-negotiated frame duration. */
#define CUBE_OPUS_FRAME_SIZE        0

/* Engine thread + queue (== xiaozhi_app's XIAOZHI_THREAD_STACK_SIZE / XIAOZHI_QUEUE_SIZE). */
#if CUBE_UPLINK_PREENCODED
    /* uart 透传：无 opus 编码；12KB 覆盖 reconnect 的 mbedtls TLS 握手(~6KB+) + HTTP + 透传 + queue，留余量 */
    #define CUBE_ENGINE_STACK       (12 * 1024)
#else
    /* board: 本地 opus 编码(libopus encode 栈深) + TLS，需大栈 */
    #define CUBE_ENGINE_STACK       (30 * 1024)
#endif
#define CUBE_QUEUE_DEPTH            32
#define CUBE_ACTIVATE_RETRY_OK_MS   3000   /* HTTP 202 (pending user input) */
#define CUBE_ACTIVATE_RETRY_ERR_MS  10000  /* network / other error */

STATIC WUKONG_AI_PROVIDER_T s_cube_provider;

/* ─── Engine message queue ─── */
typedef enum {
    CUBE_MSG_AUDIO_DATA = 0,        /* PCM slice from send(AUDIO) */
    CUBE_MSG_TEXT_DATA,             /* text string from send(TEXT) */
    CUBE_MSG_NETWORK_CONNECTED,     /* MQTT up / reconnect -> connect */
    CUBE_MSG_NETWORK_DISCONNECTED,  /* network down -> close channel */
    CUBE_MSG_STOP,                  /* engine thread stop sentinel */
} CUBE_MSG_E;

typedef struct {
    CUBE_MSG_E  event;
    UINT8_T    *data;   /* PSRAM copy for AUDIO_DATA; NULL otherwise */
    UINT_T      len;
} CUBE_MSG_T;

typedef struct {
    xiaozhi_protocol_t       *protocol;
    xiaozhi_listening_mode_t  listening_mode;
    TUYA_AI_ENCODER_T        *encoder;
    TUYA_AI_ENCODER_INFO_T    encoder_info;
    THREAD_HANDLE             thread;       /* engine: drains queue (audio + network) */
    QUEUE_HANDLE              queue;
    volatile BOOL_T           running;
    volatile BOOL_T           terminate;
    MUTEX_HANDLE              send_lock;   /* serializes Mode-thread ioctl/abort sends against
                                              engine-thread reconnect deinit/rebuild of `protocol` */
    volatile BOOL_T           tts_active;  /* TTS turn in progress; gates duplicate starts and stray audio */
} CUBE_CTX_T;

STATIC CUBE_CTX_T *__cube_ctx(VOID_T *handle)
{
    return (CUBE_CTX_T *)((WUKONG_AI_PROVIDER_T *)handle)->ctx;
}

STATIC OPERATE_RET __cube_post(CUBE_CTX_T *ctx, CUBE_MSG_E event, UINT8_T *data, UINT_T len)
{
    if (ctx == NULL || ctx->queue == NULL) {
        return OPRT_INVALID_PARM;
    }
    CUBE_MSG_T msg = { .event = event, .data = data, .len = len };
    return tal_queue_post(ctx->queue, &msg, 0);
}

/* Free a dequeued message's payload. Only AUDIO_DATA / TEXT_DATA carry a PSRAM copy;
 * other message types keep data == NULL, so the NULL check covers them. */
STATIC INLINE VOID __cube_free_msg(CONST CUBE_MSG_T *msg)
{
    if (msg->data) {
        tal_psram_free(msg->data);
    }
}

#if !CUBE_UPLINK_PREENCODED   /* board 才需要 opus 编码组件；uart(外挂芯片平台)不编译，省代码+不依赖编码库 */
/* ================================================================
 * Opus encoder (uplink) — mirrors JD provider
 * ================================================================ */

STATIC VOID __cube_encoder_init(CUBE_CTX_T *ctx)
{
    tuya_ai_register_encoder(&g_tuya_ai_encoder_opus);
    ctx->encoder = tuya_ai_get_encoder(AUDIO_CODEC_OPUS);
    if (ctx->encoder == NULL) {
        PR_ERR("cube: get opus encoder failed");
        return;
    }
    if (ctx->encoder->handle) {  /* singleton; drop stale instance */
        ctx->encoder->destroy(ctx->encoder->handle);
        ctx->encoder->handle = NULL;
    }

    ctx->encoder_info.encode_type     = AUDIO_CODEC_OPUS;
    ctx->encoder_info.sample_rate     = CUBE_OPUS_SAMPLE_RATE;
    ctx->encoder_info.channels        = CUBE_OPUS_CHANNELS;
    ctx->encoder_info.bits_per_sample = CUBE_OPUS_BITS_PER_SAMPLE;
    ctx->encoder_info.bitrate         = APP_OPUS_ENCODER_BITRATE;
    ctx->encoder_info.bandwidth       = APP_OPUS_ENCODER_BANDWIDTH;
    ctx->encoder_info.frame_size      = CUBE_OPUS_FRAME_SIZE;

    OPERATE_RET rt = ctx->encoder->create(&ctx->encoder->handle, &ctx->encoder_info);
    if (rt != OPRT_OK || ctx->encoder->handle == NULL) {
        PR_ERR("cube: opus encoder create failed rt=%d", rt);
        tuya_ai_unregister_encoder(&g_tuya_ai_encoder_opus);
        ctx->encoder = NULL;
        return;
    }
    PR_NOTICE("cube: opus encoder ready, bitrate=%u", ctx->encoder_info.bitrate);
}

STATIC VOID __cube_encoder_deinit(CUBE_CTX_T *ctx)
{
    if (ctx->encoder && ctx->encoder->handle) {
        ctx->encoder->destroy(ctx->encoder->handle);
        ctx->encoder->handle = NULL;
    }
    tuya_ai_unregister_encoder(&g_tuya_ai_encoder_opus);
    ctx->encoder = NULL;
}

/* Encoder output callback: wrap each opus frame into a V3 packet and send. (board only) */
STATIC OPERATE_RET __cube_encode_data_cb(AI_AUDIO_CODEC_TYPE codec, UCHAR_T *data, UINT_T len, VOID *usr)
{
    (VOID_T)codec;
    CUBE_CTX_T *ctx = (CUBE_CTX_T *)usr;
    if (ctx == NULL || ctx->protocol == NULL || ctx->protocol->SendAudio == NULL) {
        return OPRT_COM_ERROR;
    }
    xiaozhi_audio_packet_t packet = { .payload = data, .payload_len = (uint16_t)len };
    ctx->protocol->SendAudio(&packet);
    return OPRT_OK;
}
#endif  /* !CUBE_UPLINK_PREENCODED */

/* ================================================================
 * Downlink callbacks — bridge xiaozhi protocol -> wukong events only.
 * NO conversation state here; the Mode layer reacts to these wukong events.
 * ================================================================ */

/* 下行 format → player codec。严格支持：V3(board) 仅 opus → OPUS_VBR；V1(uart) 仅 mp3 → MP3；
 * 其他组合返回 AI_AUDIO_CODEC_MAX，调用方据报错 + 不播放。
 *   V3 下行包 = [2B len][opus] = OPUS_VBR 格式；V1 下行 = raw，mp3 无前缀污染。 */
STATIC AI_AUDIO_CODEC_E __cube_tts_codec(CUBE_CTX_T *ctx)
{
    if (!ctx || !ctx->protocol) {
        return AI_AUDIO_CODEC_MAX;
    }
#if CUBE_UPLINK_PREENCODED   /* V1 (uart): 仅 mp3 */
    return (ctx->protocol->server_audio_format == XIAOZHI_AUDIO_FORMAT_MP3)
           ? AI_AUDIO_CODEC_MP3 : AI_AUDIO_CODEC_MAX;
#else                         /* V3 (board): 仅 opus */
    return (ctx->protocol->server_audio_format == XIAOZHI_AUDIO_FORMAT_OPUS)
           ? AI_AUDIO_CODEC_OPUS_VBR : AI_AUDIO_CODEC_MAX;
#endif
}

STATIC VOID __cube_emit_asr(CONST CHAR_T *text)
{
    PR_NOTICE("[cube-rx] ASR forward len=%d text='%s'", text ? (int)strlen(text) : 0, text ? text : "(null)");
    ty_cJSON *node = ty_cJSON_CreateString(text ? text : "");
    if (node) {
        wukong_fc_process(AI_TEXT_ASR, node, TRUE);
        ty_cJSON_Delete(node);
    }
}

STATIC VOID __cube_emit_nlg(CONST CHAR_T *text, BOOL_T eof)
{
    PR_NOTICE("[cube-rx] NLG forward eof=%d len=%d text='%s'", eof, text ? (int)strlen(text) : 0, text ? text : "(null)");
    ty_cJSON *obj = ty_cJSON_CreateObject();
    if (obj) {
        ty_cJSON_AddStringToObject(obj, "content", text ? text : "");
        wukong_fc_process(AI_TEXT_NLG, obj, eof);
        ty_cJSON_Delete(obj);
    }
}

STATIC VOID on_protocol_incoming_json(const cJSON *root, void *user_data)
{
    CUBE_CTX_T *ctx = (CUBE_CTX_T *)user_data;
    if (root == NULL || ctx == NULL) {
        return;
    }
    cJSON *type_obj = cJSON_GetObjectItem(root, "type");
    if (!cJSON_IsString(type_obj)) {
        return;
    }
    const char *type = type_obj->valuestring;
    PR_NOTICE("[cube-rx] type=%s", type);

    if (strcmp(type, "tts") == 0) {
        cJSON *state_obj = cJSON_GetObjectItem(root, "state");
        if (!cJSON_IsString(state_obj)) {
            return;
        }
        const char *state = state_obj->valuestring;
        PR_NOTICE("[cube-rx] tts state=%s", state);
        if (strcmp(state, "start") == 0) {
            /* Only the first "start" starts the player; ignore later starts (server splits
             * one reply into segments) so playback isn't torn down and restarted mid-utterance. */
            if (!ctx->tts_active) {
                AI_AUDIO_CODEC_E codec = __cube_tts_codec(ctx);
                if (codec < AI_AUDIO_CODEC_MAX) {
                    ctx->tts_active = TRUE;
                    wukong_audio_play_tts_stream(WUKONG_AI_EVENT_TTS_START, codec, NULL, 0);
                } else {
                    /* 下行 codec 不被当前协议版本支持(V3 仅 opus / V1 仅 mp3)：报错且不激活 tts_active，
                     * 后续 DATA 由 on_protocol_incoming_audio 的 !tts_active 守卫自动丢弃。 */
                    PR_ERR("cube: downlink codec not supported (format=%d), skip TTS",
                           ctx->protocol ? ctx->protocol->server_audio_format : -1);
                }
            }
        } else if (strcmp(state, "stop") == 0) {
            /* end of reply: close the NLG stream (sentences are eof=FALSE) so UI idles only now */
            ctx->tts_active = FALSE;
            __cube_emit_nlg("", TRUE);
            wukong_audio_play_tts_stream(WUKONG_AI_EVENT_TTS_STOP, __cube_tts_codec(ctx), NULL, 0);
        } else if (strcmp(state, "sentence_start") == 0) {
            cJSON *text_obj = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text_obj)) {
                /* eof=FALSE: stream per sentence (first START, rest DATA); closed at tts "stop".
                 * Matches tuya/jd, whose eof is true only at end of reply. */
                __cube_emit_nlg(text_obj->valuestring, FALSE);
            }
        }
    } else if (strcmp(type, "stt") == 0) {
        cJSON *text_obj = cJSON_GetObjectItem(root, "text");
        if (cJSON_IsString(text_obj)) {
            __cube_emit_asr(text_obj->valuestring);
        }
    } else if (strcmp(type, "llm") == 0) {
        cJSON *emoji_obj = cJSON_GetObjectItem(root, "emotion");
        if (cJSON_IsString(emoji_obj)) {
            PR_NOTICE("[cube-rx] llm emotion len=%d '%s'",
                      (int)strlen(emoji_obj->valuestring), emoji_obj->valuestring);
            WUKONG_AI_EMO_T emo = { .name = emoji_obj->valuestring };
            wukong_ai_event_notify(WUKONG_AI_EVENT_EMOTION, &emo);
        }
    } else if (strcmp(type, "mcp") == 0) {
        /* MCP inbound: hand the JSON-RPC payload to the router. cJSON IS ty_cJSON
         * in this codebase (cJSON.h aliases to ty_cJSON), so the payload node is
         * passed straight through — no reparse. */
        cJSON *payload = cJSON_GetObjectItem(root, "payload");
        if (payload != NULL) {
            cJSON *sid_obj = cJSON_GetObjectItem(root, "session_id");
            CHAR_T *sid = cJSON_IsString(sid_obj) ? sid_obj->valuestring : (CHAR_T *)"";
            wukong_mcp_recv(sid, (CHAR_T *)"", payload, NULL);
        }
    }
}

STATIC VOID on_protocol_incoming_audio(xiaozhi_audio_packet_t *packet, void *user_data)
{
    CUBE_CTX_T *ctx = (CUBE_CTX_T *)user_data;
    if (ctx == NULL || packet == NULL || packet->payload == NULL) {
        return;
    }
    /* feed only while a TTS turn is active; drop frames arriving after stop/abort */
    if (!ctx->tts_active) {
        return;
    }
    wukong_audio_play_tts_stream(WUKONG_AI_EVENT_TTS_DATA, __cube_tts_codec(ctx),
                                 (CHAR_T *)packet->payload, packet->payload_len);
}

STATIC VOID on_audio_channel_closed(void *user_data)
{
    CUBE_CTX_T *ctx = (CUBE_CTX_T *)user_data;
    PR_WARN("cube: audio channel closed");
    wukong_audio_player_stop(AI_PLAYER_FG);
    if (ctx) {
        ctx->tts_active = FALSE;   /* channel close ends this TTS turn; don't block next start */
        if (!ctx->terminate) {
            __cube_post(ctx, CUBE_MSG_NETWORK_CONNECTED, NULL, 0);  /* reconnect */
        }
    }
}

/* ================================================================
 * Connection (config + activation + protocol init) with backend switch
 * ================================================================ */

/* Sleep up to total_ms in small slices, returning TRUE as soon as teardown is requested,
 * so deinit need not wait a full activation-retry interval for ctx->terminate to be seen. */
STATIC BOOL_T __cube_sleep_interruptible(CUBE_CTX_T *ctx, UINT_T total_ms)
{
    UINT_T elapsed = 0;
    CONST UINT_T step = 100;  /* ms */
    while (elapsed < total_ms) {
        if (ctx->terminate) {
            return TRUE;
        }
        UINT_T chunk = (total_ms - elapsed < step) ? (total_ms - elapsed) : step;
        tal_system_sleep(chunk);
        elapsed += chunk;
    }
    return ctx->terminate;
}

STATIC OPERATE_RET __cube_connect(CUBE_CTX_T *ctx)
{
    /* 锁内只做「拆除旧 protocol + 置空」：保证不会与 Mode 线程 ioctl/abort 的解引用并发。
     * 阻塞型网络 I/O（下面的 config/activate/OpenAudioChannel）不在锁内——此时 protocol==NULL，
     * ioctl/abort 看到 NULL 直接 no-op（重连期间本无活跃会话）。 */
    tal_mutex_lock(ctx->send_lock);
    if (ctx->protocol) {
        xiaozhi_protocol_deinit(ctx->protocol);
        ctx->protocol = NULL;
    }
    tal_mutex_unlock(ctx->send_lock);

    xiaozhi_server_config_t *config = NULL;

#if defined(CUBE_BACKEND_XIAOZHI) && (CUBE_BACKEND_XIAOZHI == 1)
    /* Public xiaozhi (tenclass): HTTP base config + pairing-code activation. */
    if (xiaozhi_server_check_config() != OPRT_OK) {
        PR_ERR("cube: tenclass server config sync failed");
        return OPRT_COM_ERROR;
    }
    config = xiaozhi_server_get_config();
    if (config == NULL) {
        return OPRT_COM_ERROR;
    }
    for (;;) {
        if (ctx->terminate) {
            return OPRT_COM_ERROR;
        }
        OPERATE_RET r = xiaozhi_server_check_activate();
        if (r == OPRT_OK) {
            break;
        }
        /* 退避等待，但可被 terminate 提前打断（deinit 不必等满一个重试周期） */
        UINT_T wait_ms = (r == OPRT_RESOURCE_NOT_READY) ? CUBE_ACTIVATE_RETRY_OK_MS
                                                        : CUBE_ACTIVATE_RETRY_ERR_MS;
        if (__cube_sleep_interruptible(ctx, wait_ms)) {
            return OPRT_COM_ERROR;
        }
    }
#else
    /* Tuya gateway (default): no tenclass contact; device-JWT auth. */
    config = xiaozhi_server_get_config();
    if (config == NULL) {
        config = xiaozhi_server_alloc_config();
    }
    if (config == NULL) {
        return OPRT_COM_ERROR;
    }
    if (tuya_xiaozhi_server_check_config(config) != OPRT_OK || !config->tuya.has_config) {
        PR_ERR("cube: tuya gateway config unavailable");
        return OPRT_COM_ERROR;
    }
#endif

    xiaozhi_protocol_t *protocol = xiaozhi_protocol_init(XIAOZHI_PROTOCOL_WEBSOCKET);
    if (protocol == NULL) {
        PR_ERR("cube: protocol init failed");
        return OPRT_COM_ERROR;
    }
    protocol->OnIncomingJson       = on_protocol_incoming_json;
    protocol->OnIncomingAudio      = on_protocol_incoming_audio;
    protocol->OnAudioChannelClosed = on_audio_channel_closed;
    protocol->user_data            = ctx;

    if (!protocol->OpenAudioChannel || !protocol->OpenAudioChannel()) {
        PR_ERR("cube: open audio channel failed");
        xiaozhi_protocol_deinit(protocol);
        return OPRT_COM_ERROR;
    }
    /* 锁内发布新 protocol，使 ioctl/abort 要么看到旧值(已 NULL)、要么看到新值，无中间态 */
    tal_mutex_lock(ctx->send_lock);
    ctx->protocol = protocol;
    tal_mutex_unlock(ctx->send_lock);
    PR_NOTICE("cube: connected (listen start is per-utterance via ioctl)");
    return OPRT_OK;
}

/* ================================================================
 * Engine thread — audio I/O + connection (cf. JD __jd_input_thread; no conv. state)
 * ================================================================ */

STATIC VOID __cube_handle_audio_data(CUBE_CTX_T *ctx, UINT8_T *data, UINT_T len)
{
    /* No state gating: audio only reaches here when the framework is_ready (channel
     * open) and the Mode layer is feeding (VAD active). Just send.
     * ctx->protocol is read here WITHOUT send_lock on purpose: this runs on the engine
     * thread, the same thread that performs the reconnect deinit/rebuild, so the two are
     * serial and no concurrent free is possible. (Mode-thread paths ioctl/abort/is_ready
     * are cross-thread and DO take send_lock.) */
#if CUBE_UPLINK_PREENCODED
    /* uart 输入：外挂 codec 已编码(opus/speex)，直接透传，不本地编码 */
    if (ctx->protocol && ctx->protocol->SendAudio) {
        xiaozhi_audio_packet_t packet = { .payload = data, .payload_len = (uint16_t)len };
        ctx->protocol->SendAudio(&packet);
    }
#else
    /* board 输入：PCM → opus 编码后发送 */
    if (ctx->protocol && ctx->encoder && ctx->encoder->handle) {
        ctx->encoder->encode(ctx->encoder->handle, data, len, __cube_encode_data_cb, ctx);
    }
#endif
}

STATIC VOID __cube_engine_thread(VOID_T *arg)
{
    CUBE_CTX_T *ctx = (CUBE_CTX_T *)arg;
    CUBE_MSG_T msg;

    PR_NOTICE("cube: engine thread started");
    while (ctx->running) {
        if (tal_queue_fetch(ctx->queue, &msg, SEM_WAIT_FOREVER) != OPRT_OK) {
            continue;
        }
        switch (msg.event) {
        case CUBE_MSG_AUDIO_DATA:
            __cube_handle_audio_data(ctx, msg.data, msg.len);
            break;
        case CUBE_MSG_TEXT_DATA:
            if (ctx->protocol && ctx->protocol->SendText && msg.data) {
                ctx->protocol->SendText((CONST CHAR_T *)msg.data);
            }
            break;
        case CUBE_MSG_NETWORK_CONNECTED:
            if (ctx->protocol == NULL ||
                (ctx->protocol->IsAudioChannelOpened && !ctx->protocol->IsAudioChannelOpened())) {
                __cube_connect(ctx);
            }
            break;
        case CUBE_MSG_NETWORK_DISCONNECTED:
            if (ctx->protocol && ctx->protocol->CloseAudioChannel) {
                ctx->protocol->CloseAudioChannel();
            }
            break;
        case CUBE_MSG_STOP:
            ctx->running = FALSE;
            break;
        default:
            break;
        }
        __cube_free_msg(&msg);
    }

    /* drain remaining audio frames */
    while (tal_queue_fetch(ctx->queue, &msg, 0) == OPRT_OK) {
        __cube_free_msg(&msg);
    }
    if (ctx->protocol) {
        xiaozhi_protocol_deinit(ctx->protocol);
        ctx->protocol = NULL;
    }
    PR_NOTICE("cube: engine thread stopped");
}

/* EVENT_MQTT_CONNECTED: network up -> kick the connect flow. */
STATIC OPERATE_RET __cube_on_mqtt_connected(VOID_T *data)
{
    (VOID_T)data;
    CUBE_CTX_T *ctx = (CUBE_CTX_T *)s_cube_provider.ctx;
    if (ctx) {
        __cube_post(ctx, CUBE_MSG_NETWORK_CONNECTED, NULL, 0);
    }
    return OPRT_OK;
}

/* ================================================================
 * Provider OPS
 * ================================================================ */

/* Tear down and free the cube ctx. Shared by __cube_init's error path and __cube_deinit:
 * stop the engine thread (it frees ctx->protocol on exit), free queue/send_lock/encoder,
 * then free ctx and clear self->ctx. Safe to call with self->ctx == NULL. */
STATIC VOID __cube_ctx_destroy(WUKONG_AI_PROVIDER_T *self)
{
    CUBE_CTX_T *ctx = (CUBE_CTX_T *)self->ctx;
    if (ctx == NULL) {
        return;
    }
    ctx->terminate = TRUE;
    ctx->running = FALSE;
    if (ctx->thread) {
        __cube_post(ctx, CUBE_MSG_STOP, NULL, 0);  /* wake the FOREVER fetch */
        tal_thread_delete(ctx->thread);            /* thread frees protocol on exit */
        ctx->thread = NULL;
    }
    if (ctx->queue) {
        tal_queue_free(ctx->queue);
        ctx->queue = NULL;
    }
    if (ctx->send_lock) {
        tal_mutex_release(ctx->send_lock);
        ctx->send_lock = NULL;
    }
#if !CUBE_UPLINK_PREENCODED
    __cube_encoder_deinit(ctx);
#endif
    tal_free(ctx);
    self->ctx = NULL;
}

STATIC OPERATE_RET __cube_init(VOID_T *handle, CONST WUKONG_AI_PROVIDER_CFG_T *cfg)
{
    WUKONG_AI_PROVIDER_T *self = (WUKONG_AI_PROVIDER_T *)handle;
    (VOID_T)cfg;

    CUBE_CTX_T *ctx = (CUBE_CTX_T *)tal_calloc(1, sizeof(CUBE_CTX_T));
    if (ctx == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    ctx->listening_mode = XIAOZHI_LISTENING_MODE_MANUAL_STOP;
    self->ctx = ctx;

    /* xiaozhi server root CA (was done in xiaozhi_app_init). */
    tuya_iot_store_third_cloud_ca(XIAOZHI_HOST, (CONST UCHAR_T *)XIAOZHI_ROOT_CERTIFICATE,
                                  sizeof(XIAOZHI_ROOT_CERTIFICATE), FALSE);

#if !CUBE_UPLINK_PREENCODED
    /* board 输入才创建本地 opus encoder；uart 输入数据已编码，不编译编码组件 */
    __cube_encoder_init(ctx);
#endif

    if (tal_mutex_create_init(&ctx->send_lock) != OPRT_OK) {
        PR_ERR("cube: send_lock create failed");
        goto err;
    }

    if (tal_queue_create_init(&ctx->queue, sizeof(CUBE_MSG_T), CUBE_QUEUE_DEPTH) != OPRT_OK) {
        PR_ERR("cube: queue create failed");
        goto err;
    }

    ctx->running = TRUE;
    THREAD_CFG_T thcfg = {
        .priority = THREAD_PRIO_2,
        .stackDepth = CUBE_ENGINE_STACK,
        .thrdname = "cube_engine",
    };
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    thcfg.psram_mode = 1;
#endif
    if (tal_thread_create_and_start(&ctx->thread, NULL, NULL, __cube_engine_thread, ctx, &thcfg) != OPRT_OK) {
        PR_ERR("cube: engine thread create failed");
        ctx->running = FALSE;
        goto err;
    }

    ty_subscribe_event(EVENT_MQTT_CONNECTED, "provider_cube",
                       __cube_on_mqtt_connected, SUBSCRIBE_TYPE_NORMAL);
    return OPRT_OK;

err:
    __cube_ctx_destroy(self);
    return OPRT_COM_ERROR;
}

STATIC OPERATE_RET __cube_deinit(VOID_T *handle)
{
    WUKONG_AI_PROVIDER_T *self = (WUKONG_AI_PROVIDER_T *)handle;
    CUBE_CTX_T *ctx = (CUBE_CTX_T *)self->ctx;
    if (ctx == NULL) {
        return OPRT_OK;
    }

    ty_unsubscribe_event(EVENT_MQTT_CONNECTED, "provider_cube", __cube_on_mqtt_connected);

    /* 前置约定：整 provider 销毁依赖上层框架"deinit 之后不再调用任何 provider ops
     * (send/ioctl/abort)"。send_lock 只解决框架不可见的【内部重连】竞态，不覆盖此处的
     * ctx 释放；若框架无此约定，需另行同步 ctx 生命周期。 */
    __cube_ctx_destroy(self);
    return OPRT_OK;
}

STATIC BOOL_T __cube_is_ready(VOID_T *handle)
{
    CUBE_CTX_T *ctx = __cube_ctx(handle);
    if (ctx == NULL) {
        return FALSE;
    }
    /* send_lock：与引擎线程重连时 deinit/重建 protocol 互斥——check(protocol!=NULL) 与
     * use(IsAudioChannelOpened) 之间不能被 free，否则 UAF（与 ioctl/abort 同源）。 */
    tal_mutex_lock(ctx->send_lock);
    BOOL_T ready = (ctx->protocol && ctx->protocol->IsAudioChannelOpened) ?
                   ctx->protocol->IsAudioChannelOpened() : FALSE;
    tal_mutex_unlock(ctx->send_lock);
    return ready;
}

/* Copy a buffer to PSRAM and hand it to the engine thread via the queue, so encode/
 * SendXxx + TLS run on the engine thread (not the caller/record_task stack) and serialize
 * with teardown. On queue-full the copy is freed and the message dropped. */
STATIC OPERATE_RET __cube_copy_and_post(CUBE_CTX_T *ctx, CUBE_MSG_E event,
                                        CONST UINT8_T *src, UINT_T len, CONST CHAR_T *tag)
{
    if (ctx->queue == NULL || src == NULL) {
        return OPRT_RESOURCE_NOT_READY;
    }
    UINT8_T *copy = (UINT8_T *)tal_psram_malloc(len);
    if (copy == NULL) {
        PR_ERR("cube: psram alloc %s %u failed", tag, len);
        return OPRT_MALLOC_FAILED;
    }
    memcpy(copy, src, len);
    if (__cube_post(ctx, event, copy, len) != OPRT_OK) {
        PR_WARN("cube: %s queue full, drop", tag);
        tal_psram_free(copy);
    }
    return OPRT_OK;
}

STATIC OPERATE_RET __cube_send(VOID_T *handle, CONST WUKONG_AI_MSG_T *msg)
{
    CUBE_CTX_T *ctx = __cube_ctx(handle);
    TUYA_CHECK_NULL_RETURN(ctx, OPRT_COM_ERROR);
    if (ctx->protocol == NULL) {
        return OPRT_RESOURCE_NOT_READY;
    }

    switch (msg->type) {
    case WUKONG_AI_MSG_TYPE_AUDIO:
        /* Offload to engine thread: encode + SendAudio run there, not on the caller
         * (record_task) stack. Mirrors xiaozhi on_mic_data_callback. */
        return __cube_copy_and_post(ctx, CUBE_MSG_AUDIO_DATA,
                                    (CONST UINT8_T *)msg->data, msg->data_len, "audio");
    case WUKONG_AI_MSG_TYPE_TEXT:
        /* Route TEXT through the engine thread too, so all sends and the teardown
         * (CloseAudioChannel/deinit) run on one thread — no cross-thread race. Copy
         * includes the trailing '\0' (len+1) since SendText takes a C string. */
        if (msg->data == NULL) {
            return OPRT_RESOURCE_NOT_READY;
        }
        return __cube_copy_and_post(ctx, CUBE_MSG_TEXT_DATA, (CONST UINT8_T *)msg->data,
                                    (UINT_T)strlen((CONST CHAR_T *)msg->data) + 1, "text");
    case WUKONG_AI_MSG_TYPE_IMAGE:
    case WUKONG_AI_MSG_TYPE_VIDEO:
    case WUKONG_AI_MSG_TYPE_FILE:
    default:
        return OPRT_NOT_SUPPORTED;
    }
}

STATIC OPERATE_RET __cube_abort(VOID_T *handle, CONST CHAR_T *chat_id)
{
    CUBE_CTX_T *ctx = __cube_ctx(handle);
    (VOID_T)chat_id;
    if (ctx == NULL) {
        return OPRT_OK;
    }
    /* send_lock：与引擎线程重连时 deinit/重建 protocol 互斥，整段解引用都在锁内 */
    tal_mutex_lock(ctx->send_lock);
    /* Abort the active TTS turn exactly once. Gate on tts_active, not is_playing(): the
     * barge-in path stops the local player before this runs (is_playing()==FALSE), but the
     * realtime backend keeps streaming until it gets an explicit abort (ret:-2 otherwise).
     * Clearing under send_lock dedups the INPUT_START(interrupt) abort on the same barge-in. */
    if (ctx->tts_active) {
        ctx->tts_active = FALSE;
        if (ctx->protocol && ctx->protocol->SendAbortSpeaking) {
            ctx->protocol->SendAbortSpeaking(XIAOZHI_ABORT_REASON_USER_INTERRUPT);
        }
    }
    tal_mutex_unlock(ctx->send_lock);
    return OPRT_OK;
}

STATIC OPERATE_RET __cube_ioctl(VOID_T *handle, INT_T cmd, VOID_T *arg)
{
    CUBE_CTX_T *ctx = __cube_ctx(handle);
    TUYA_CHECK_NULL_RETURN(ctx, OPRT_COM_ERROR);

    switch (cmd) {
    case WUKONG_PROVIDER_CMD_INPUT_START: {
        /* Utterance start: server needs `listen start` before audio is valid (else ASR
         * comes back empty). */
        (VOID_T)arg;
        /* send_lock：与引擎线程重连时 deinit/重建 protocol 互斥，整段解引用都在锁内 */
        tal_mutex_lock(ctx->send_lock);
        if (ctx->protocol == NULL) {
            tal_mutex_unlock(ctx->send_lock);
            return OPRT_RESOURCE_NOT_READY;
        }
        /* Abort a playing TTS turn before starting a new listen so a new utterance barges-in
         * — covers free mode, whose VAD start does not chat_break. Gated on tts_active (same as
         * __cube_abort): a key/wakeup barge-in already aborted+cleared via chat_break so this
         * dedups, and an idle start (no TTS playing) sends no spurious abort. */
        if (ctx->tts_active) {
            ctx->tts_active = FALSE;
            if (ctx->protocol->SendAbortSpeaking) {
                ctx->protocol->SendAbortSpeaking(XIAOZHI_ABORT_REASON_USER_INTERRUPT);
            }
        }
        if (ctx->protocol->SendStartListening) {
            ctx->protocol->SendStartListening(ctx->listening_mode);
        }
        tal_mutex_unlock(ctx->send_lock);
        return OPRT_OK;
    }
    case WUKONG_PROVIDER_CMD_INPUT_STOP:
        /* Utterance end: mark `listen stop` so the server finalizes ASR for this turn. */
        tal_mutex_lock(ctx->send_lock);
        if (ctx->protocol && ctx->protocol->SendStopListening) {
            ctx->protocol->SendStopListening();
        }
        tal_mutex_unlock(ctx->send_lock);
        return OPRT_OK;
    case WUKONG_PROVIDER_CMD_OUTPUT_STOP:
        wukong_audio_player_stop(AI_PLAYER_FG);
        return OPRT_OK;
    case WUKONG_PROVIDER_CMD_ALERT:
        return OPRT_OK;
    case WUKONG_PROVIDER_CMD_SET_SCENE:
    case WUKONG_PROVIDER_CMD_SET_EVENT_PARAM:
    case WUKONG_PROVIDER_CMD_SERVER_VAD_CTRL:
    case WUKONG_PROVIDER_CMD_SWITCH_TARGET:
    case WUKONG_PROVIDER_CMD_GET_SESSION:
    case WUKONG_PROVIDER_CMD_DEL_SESSION:
    default:
        return OPRT_NOT_SUPPORTED;
    }
}

STATIC OPERATE_RET __cube_mcp_send(VOID_T *handle, CHAR_T *sid, CHAR_T *eid,
                                   CONST CHAR_T *message)
{
    CUBE_CTX_T *ctx = __cube_ctx(handle);
    (VOID_T)sid;   /* xiaozhi wraps with its own protocol session_id */
    (VOID_T)eid;   /* xiaozhi has no event id */
    if (ctx == NULL || message == NULL) {
        return OPRT_INVALID_PARM;
    }

    /* The xiaozhi protocol layer owns the MCP wire envelope (SendMcpMessage builds
     * {"session_id","type":"mcp","payload":<message>} and sends it). send_lock
     * serializes against engine-thread reconnect deinit/rebuild of protocol. */
    OPERATE_RET rt = OPRT_COM_ERROR;
    tal_mutex_lock(ctx->send_lock);
    if (ctx->protocol && ctx->protocol->SendMcpMessage) {
        ctx->protocol->SendMcpMessage(message);
        rt = OPRT_OK;
    }
    tal_mutex_unlock(ctx->send_lock);
    return rt;
}

/* ================================================================
 * OPS Table & Instance
 * ================================================================ */

STATIC CONST WUKONG_AI_PROVIDER_OPS_T s_cube_ops = {
    .name      = "cube",
    .caps      = WUKONG_AI_CAP_TEXT | WUKONG_AI_CAP_AUDIO | WUKONG_AI_CAP_TTS_BUILTIN,
    .init      = __cube_init,
    .deinit    = __cube_deinit,
    .is_ready  = __cube_is_ready,
    .send      = __cube_send,
    .llm_infer = NULL,
    .mcp_send  = __cube_mcp_send,
    .abort     = __cube_abort,
    .ioctl     = __cube_ioctl,
};

STATIC WUKONG_AI_PROVIDER_T s_cube_provider = {
    .ops = &s_cube_ops,
    .ctx = NULL,
};

WUKONG_AI_PROVIDER_T *wukong_provider_cube_get(VOID_T)
{
    return &s_cube_provider;
}
