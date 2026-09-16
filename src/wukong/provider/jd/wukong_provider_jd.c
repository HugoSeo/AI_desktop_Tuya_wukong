/**
 * @file wukong_provider_jd.c
 * @brief JD JoyInside Cloud Platform Provider.
 *
 * Wraps svc_joyinside directly (no svc_ai_agent dependency).
 * Audio input pipeline (ringbuf + thread + encoder) is implemented internally.
 */

#include "wukong_ai_provider.h"
#include "wukong_ai_channel.h"
#include "wukong_ai_agent.h"
#include "wukong_audio_player.h"
#include "svc_ai_player.h"
#include "joyinside/joyinside_client.h"
#include "joyinside/joyinside_biz.h"
#include "joyinside/joyinside_auth.h"
#include "joyinside/joyinside_parse.h"
#include "tuya_ai_encoder.h"
#include "tuya_ai_protocol.h"
#include "tuya_ringbuf.h"
#include "tal_memory.h"
#include "tal_system.h"
#include "tal_thread.h"
#include "tal_mutex.h"
#include "tal_queue.h"
#include "uni_log.h"
#include <string.h>
#if defined(ENABLE_APP_OPUS_ENCODER) && (ENABLE_APP_OPUS_ENCODER == 1)
#include "tuya_ai_encoder_opus.h"
#endif

STATIC WUKONG_AI_PROVIDER_T s_jd_provider;

/* ================================================================
 * Internal input pipeline types
 * (No dependency on tuya_ai_input.h / tuya_ai_internal.h)
 * ================================================================ */

/* Input state machine — mirrors AI_INPUT_STATE_E in svc_ai_agent */
typedef enum {
    JD_INPUT_IDLE = 0,
    JD_INPUT_START_LAZY,  /* no interrupt, enter PROC directly */
    JD_INPUT_START,       /* interrupt current output, then enter PROC */
    JD_INPUT_PROC,        /* drain ringbuf → encode → send_audio */
    JD_INPUT_STOPPING,    /* drain ringbuf then stop (max 30 rounds / 300ms) */
    JD_INPUT_STOP,        /* send FINISH if manual_call, reset ringbuf → IDLE */
} JD_INPUT_STATE_E;

/* ringbuf frame header — mirrors AI_RINGBUF_HEAD_T */
typedef struct {
    AI_PACKET_PT type;
    UINT_T       len;
    UINT_T       total_len;
} JD_RINGBUF_HEAD_T;

/* Encode accumulation context */
typedef struct {
    UCHAR_T *buf;
    UINT_T   buf_size;
    UINT_T   buf_len;
} JD_ENCODE_CTX_T;

#define JD_INPUT_RINGBUF_SIZE  (65536)
#define JD_INPUT_BUF_SIZE      (6144)
#define JD_INPUT_STACK_SIZE    40960
#define JD_INPUT_TASK_DELAY    10
#define JD_ENCODE_BUF_SIZE     800

typedef struct {
    TUYA_RINGBUFF_T         ringbuf;
    MUTEX_HANDLE            mutex;
    CHAR_T                 *input_buf;
    THREAD_HANDLE           thread;
    QUEUE_HANDLE            queue;
    BOOL_T                  terminate;
    BOOL_T                  queue_sync;
    JD_INPUT_STATE_E        state;
    UINT32_T                lazy_input;
    TUYA_AI_ENCODER_T      *encoder;
    TUYA_AI_ENCODER_INFO_T  encoder_info;
    UCHAR_T                 encode_buf[JD_ENCODE_BUF_SIZE];  /* avoid large stack allocation */
} JD_INPUT_CTX_T;

/* --- Private context --- */

typedef struct {
    UINT16_T      audio_codec_type;
    JD_CHAT_CFG_S jd_chat_cfg;
    JD_INPUT_CTX_T input;
} JD_CTX_T;

/* ================================================================
 * Input pipeline — encoder, ringbuf, thread, state machine
 * ================================================================ */

/* OPUS encoder defaults (overridable via tuya_app_config.h).
 * Kept unconditionally because APP_OPUS_ENCODER_BITRATE is also used to
 * derive jd_chat_cfg.audio.input.frameSize in PCM-only builds. */
#ifndef APP_OPUS_ENCODER_BITRATE
#define APP_OPUS_ENCODER_BITRATE    16000
#endif
#ifndef APP_OPUS_ENCODER_BANDWIDTH
#define APP_OPUS_ENCODER_BANDWIDTH  1102  /* OPUS_BANDWIDTH_MEDIUMBAND */
#endif

#if defined(ENABLE_APP_OPUS_ENCODER) && (ENABLE_APP_OPUS_ENCODER == 1)
#define JD_OPUS_SAMPLE_RATE         16000
#define JD_OPUS_CHANNELS            1
#define JD_OPUS_BITS_PER_SAMPLE     16
#define JD_OPUS_FRAME_SIZE          0   /* 0 → encoder picks 40ms (640 samples @16k) */
#endif

STATIC VOID __jd_encoder_init(JD_CTX_T *ctx)
{
    JD_INPUT_CTX_T *ictx = &ctx->input;
    ictx->encoder = NULL;

#if defined(ENABLE_APP_OPUS_ENCODER) && (ENABLE_APP_OPUS_ENCODER == 1)
    tuya_ai_register_encoder(&g_tuya_ai_encoder_opus);
    ictx->encoder = tuya_ai_get_encoder(AUDIO_CODEC_OPUS);
    if (ictx->encoder == NULL) {
        PR_ERR("jd: get opus encoder failed, fall back to pcm");
        ctx->jd_chat_cfg.audio.input.codec = JD_AUDIO_CODEC_PCM;
        return;
    }

    /* g_tuya_ai_encoder_opus.handle is a singleton; destroy any stale instance */
    if (ictx->encoder->handle) {
        ictx->encoder->destroy(ictx->encoder->handle);
        ictx->encoder->handle = NULL;
    }

    ictx->encoder_info.encode_type     = AUDIO_CODEC_OPUS;
    ictx->encoder_info.sample_rate     = JD_OPUS_SAMPLE_RATE;
    ictx->encoder_info.channels        = JD_OPUS_CHANNELS;
    ictx->encoder_info.bits_per_sample = JD_OPUS_BITS_PER_SAMPLE;
    ictx->encoder_info.bitrate         = APP_OPUS_ENCODER_BITRATE;
    ictx->encoder_info.bandwidth       = APP_OPUS_ENCODER_BANDWIDTH;
    ictx->encoder_info.frame_size      = JD_OPUS_FRAME_SIZE;

    OPERATE_RET rt = ictx->encoder->create(&ictx->encoder->handle, &ictx->encoder_info);
    if (rt != OPRT_OK || ictx->encoder->handle == NULL) {
        PR_ERR("jd: opus encoder create failed rt=%d, fall back to pcm", rt);
        tuya_ai_unregister_encoder(&g_tuya_ai_encoder_opus);
        ictx->encoder = NULL;
        ctx->jd_chat_cfg.audio.input.codec = JD_AUDIO_CODEC_PCM;
        return;
    }

    PR_NOTICE("jd: opus encoder ready, frame=%u samples, bitrate=%u",
              ictx->encoder_info.frame_size, ictx->encoder_info.bitrate);
    ctx->jd_chat_cfg.audio.input.codec = JD_AUDIO_CODEC_OPUS;
#else
    PR_NOTICE("jd: opus encoder disabled, upload pcm");
    ctx->jd_chat_cfg.audio.input.codec = JD_AUDIO_CODEC_PCM;
#endif
}

STATIC VOID __jd_encoder_deinit(JD_INPUT_CTX_T *ictx)
{
#if defined(ENABLE_APP_OPUS_ENCODER) && (ENABLE_APP_OPUS_ENCODER == 1)
    if (ictx->encoder && ictx->encoder->handle) {
        ictx->encoder->destroy(ictx->encoder->handle);
        ictx->encoder->handle = NULL;
    }
    /* paired with tuya_ai_register_encoder in __jd_encoder_init */
    tuya_ai_unregister_encoder(&g_tuya_ai_encoder_opus);
#endif
    ictx->encoder = NULL;
}

STATIC OPERATE_RET __jd_encode_data_cb(AI_AUDIO_CODEC_TYPE codec,
                                        UCHAR_T *data, UINT_T len, VOID *usr)
{
    JD_ENCODE_CTX_T *ctx = (JD_ENCODE_CTX_T *)usr;
    if (ctx->buf_len + len > ctx->buf_size) {
        joyinside_send_audio((CHAR_T *)ctx->buf, ctx->buf_len);
        ctx->buf_len = 0;
    }
    if (ctx->buf_len + len > ctx->buf_size) {
        PR_ERR("jd: encoded frame too large: %d > %d", len, ctx->buf_size);
        return OPRT_INVALID_PARM;
    }
    memcpy(ctx->buf + ctx->buf_len, data, len);
    ctx->buf_len += len;
    return OPRT_OK;
}

STATIC VOID __jd_proc_audio(JD_INPUT_CTX_T *ictx, CHAR_T *buf, UINT_T len)
{
    if (len == 0) return;

    if (ictx->encoder && ictx->encoder->handle) {
        /* OPUS: encode → accumulate fixed-size frames → flush via send_audio */
        JD_ENCODE_CTX_T ectx = {
            .buf = ictx->encode_buf, .buf_size = JD_ENCODE_BUF_SIZE, .buf_len = 0
        };
        OPERATE_RET rt = ictx->encoder->encode(ictx->encoder->handle,
                                                (UCHAR_T *)buf, len,
                                                __jd_encode_data_cb, &ectx);
        if (rt != OPRT_OK) {
            PR_ERR("jd: opus encode failed: %d", rt);
            return;
        }
        if (ectx.buf_len > 0) {
            joyinside_send_audio((CHAR_T *)ectx.buf, ectx.buf_len);
        }
    } else {
        /* PCM passthrough */
        joyinside_send_audio(buf, len);
    }
}

STATIC INT_T __jd_ringbuf_read_and_send(JD_INPUT_CTX_T *ictx)
{
    JD_RINGBUF_HEAD_T head = {0};
    UINT_T total_len = 0;

    tal_mutex_lock(ictx->mutex);
    while (total_len < JD_INPUT_BUF_SIZE) {
        /* peek head to check if next frame fits */
        UINT_T read_len = tuya_ring_buff_peek(ictx->ringbuf,
                                               (VOID_T *)&head,
                                               SIZEOF(JD_RINGBUF_HEAD_T));
        if (read_len != SIZEOF(JD_RINGBUF_HEAD_T)) break;
        if (head.len + total_len > JD_INPUT_BUF_SIZE) break;

        read_len = tuya_ring_buff_read(ictx->ringbuf, &head, SIZEOF(JD_RINGBUF_HEAD_T));
        if (read_len != SIZEOF(JD_RINGBUF_HEAD_T)) break;

        read_len = tuya_ring_buff_read(ictx->ringbuf,
                                       ictx->input_buf + total_len, head.len);
        if (read_len != head.len) break;
        total_len += read_len;
    }
    tal_mutex_unlock(ictx->mutex);

    if (total_len > 0) {
        __jd_proc_audio(ictx, ictx->input_buf, total_len);
    }
    return (INT_T)total_len;
}

STATIC VOID __jd_input_thread(VOID *arg)
{
    JD_CTX_T *ctx = (JD_CTX_T *)arg;
    JD_INPUT_CTX_T *ictx = &ctx->input;
    JD_INPUT_STATE_E queue_state, new_state;

    while (!ictx->terminate) {
        queue_state = new_state = ictx->state;
        tal_queue_fetch(ictx->queue, &queue_state, 0);

        switch (queue_state) {
        case JD_INPUT_START:
            if (!joyinside_client_is_ready()) {
                wukong_audio_player_alert(AI_TOY_ALERT_TYPE_NETWORK_FAIL, FALSE);
                new_state = JD_INPUT_IDLE;
            } else {
                if (wukong_audio_player_is_playing()) {
                    joyinside_send_event(JD_EVENT_INTERRUPT);
                }
                wukong_audio_player_stop(AI_PLAYER_FG);
                new_state = JD_INPUT_PROC;
            }
            ictx->state      = new_state;
            ictx->queue_sync = TRUE;
            break;

        case JD_INPUT_START_LAZY:
            if (!joyinside_client_is_ready()) {
                wukong_audio_player_alert(AI_TOY_ALERT_TYPE_NETWORK_FAIL, FALSE);
                new_state = JD_INPUT_IDLE;
            } else {
                new_state = JD_INPUT_PROC;
            }
            ictx->state      = new_state;
            ictx->queue_sync = TRUE;
            break;

        case JD_INPUT_PROC:
            __jd_ringbuf_read_and_send(ictx);
            break;

        case JD_INPUT_STOPPING: {
            INT_T sent = 0;
            if (ictx->lazy_input < 30) {
                sent = __jd_ringbuf_read_and_send(ictx);
                new_state = (sent > 0) ? JD_INPUT_STOPPING : JD_INPUT_STOP;
            } else {
                new_state = JD_INPUT_STOP;
            }
            ictx->state = new_state;
            ictx->lazy_input++;
            break;
        }

        case JD_INPUT_STOP:
            joyinside_send_event(JD_EVENT_FINISH);
            tal_mutex_lock(ictx->mutex);
            tuya_ring_buff_reset(ictx->ringbuf);
            tal_mutex_unlock(ictx->mutex);
            ictx->state      = JD_INPUT_IDLE;
            ictx->lazy_input = 0;
            ictx->queue_sync = TRUE;
            break;

        case JD_INPUT_IDLE:
        default:
            break;
        }

        tal_system_sleep(JD_INPUT_TASK_DELAY);
    }
}

STATIC OPERATE_RET __jd_input_init(JD_CTX_T *ctx)
{
    OPERATE_RET rt = OPRT_OK;
    JD_INPUT_CTX_T *ictx = &ctx->input;

    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&ictx->mutex));
    TUYA_CALL_ERR_RETURN(tal_queue_create_init(&ictx->queue,
                                                sizeof(JD_INPUT_STATE_E), 4));
    TUYA_CALL_ERR_RETURN(tuya_ring_buff_create(JD_INPUT_RINGBUF_SIZE,
                                                OVERFLOW_STOP_TYPE,
                                                &ictx->ringbuf));
    ictx->input_buf = Malloc(JD_INPUT_BUF_SIZE);
    if (ictx->input_buf == NULL) return OPRT_MALLOC_FAILED;

    __jd_encoder_init(ctx);

    THREAD_CFG_T thcfg = {
        .priority  = THREAD_PRIO_1,
        .stackDepth = JD_INPUT_STACK_SIZE,
        .thrdname  = "jd_input",
    };
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    thcfg.psram_mode = 1;
#endif
    return tal_thread_create_and_start(&ictx->thread, NULL, NULL,
                                        __jd_input_thread, ctx, &thcfg);
}

STATIC VOID __jd_input_deinit(JD_CTX_T *ctx)
{
    JD_INPUT_CTX_T *ictx = &ctx->input;
    ictx->terminate = TRUE;
    if (ictx->thread) {
        tal_thread_delete(ictx->thread);
        ictx->thread = NULL;
    }
    __jd_encoder_deinit(ictx);
    if (ictx->ringbuf) {
        tuya_ring_buff_free(ictx->ringbuf);
        ictx->ringbuf = NULL;
    }
    if (ictx->input_buf) {
        Free(ictx->input_buf);
        ictx->input_buf = NULL;
    }
    if (ictx->queue) {
        tal_queue_free(ictx->queue);
        ictx->queue = NULL;
    }
    if (ictx->mutex) {
        tal_mutex_release(ictx->mutex);
        ictx->mutex = NULL;
    }
}

STATIC OPERATE_RET __jd_input_start(JD_CTX_T *ctx, BOOL_T interrupt)
{
    JD_INPUT_CTX_T *ictx = &ctx->input;
    JD_INPUT_STATE_E state = interrupt ? JD_INPUT_START : JD_INPUT_START_LAZY;
    ictx->queue_sync = FALSE;
    OPERATE_RET rt = tal_queue_post(ictx->queue, &state, 0);
    if (rt != OPRT_OK) return rt;
    /* wait for thread to process START */
    UINT_T cnt = 0;
    while (!ictx->queue_sync && cnt++ < 100) {
        tal_system_sleep(50);
    }
    return OPRT_OK;
}

STATIC VOID __jd_input_stop(JD_CTX_T *ctx)
{
    JD_INPUT_CTX_T *ictx = &ctx->input;
    ictx->lazy_input = 0;
    ictx->queue_sync = FALSE;
    JD_INPUT_STATE_E state = JD_INPUT_STOPPING;
    tal_queue_post(ictx->queue, &state, 0);
    /* wait for drain to complete, max 500ms */
    UINT_T cnt = 0;
    while (!ictx->queue_sync && cnt++ < 10) {
        tal_system_sleep(50);
    }
}

STATIC OPERATE_RET __jd_input_feed(JD_CTX_T *ctx,
                                    AI_PACKET_PT type,
                                    CONST BYTE_T *data, UINT_T len)
{
    JD_INPUT_CTX_T *ictx = &ctx->input;
    if (len > JD_INPUT_BUF_SIZE) {
        PR_ERR("jd: input frame too large: %d", len);
        return OPRT_INVALID_PARM;
    }
    JD_RINGBUF_HEAD_T head = { .type = type, .len = len, .total_len = len };
    UINT_T cnt = 0;
    OPERATE_RET rt;

    tal_mutex_lock(ictx->mutex);
    UINT_T free_size = tuya_ring_buff_free_size_get(ictx->ringbuf);
    if (free_size < SIZEOF(JD_RINGBUF_HEAD_T) + len) {
        tal_mutex_unlock(ictx->mutex);
        return OPRT_RESOURCE_NOT_READY;
    }
    tuya_ring_buff_write(ictx->ringbuf, (VOID_T *)&head, SIZEOF(JD_RINGBUF_HEAD_T));
    tuya_ring_buff_write(ictx->ringbuf, (VOID_T *)data, len);
    tal_mutex_unlock(ictx->mutex);
    return OPRT_OK;
}

/* ================================================================
 * Parse output callbacks — bridging joyinside → wukong
 * ================================================================ */

STATIC OPERATE_RET __jd_on_tts(BYTE_T *data, UINT_T len, BOOL_T is_start, BOOL_T is_end)
{
    JD_CTX_T *ctx = (JD_CTX_T *)s_jd_provider.ctx;
    UINT16_T codec = ctx ? ctx->audio_codec_type : AI_AUDIO_CODEC_MP3;

    if (is_start) {
        wukong_audio_play_tts_stream(WUKONG_AI_EVENT_TTS_START, codec, NULL, 0);
    }
    if (data && len > 0) {
        wukong_audio_play_tts_stream(WUKONG_AI_EVENT_TTS_DATA, codec, (CHAR_T *)data, len);
    }
    if (is_end) {
        wukong_audio_play_tts_stream(WUKONG_AI_EVENT_TTS_STOP, codec, NULL, 0);
    }
    return OPRT_OK;
}

STATIC OPERATE_RET __jd_on_text(ty_cJSON *root, BOOL_T eof)
{
    ty_cJSON *biz  = ty_cJSON_GetObjectItem(root, "bizType");
    ty_cJSON *data = ty_cJSON_GetObjectItem(root, "data");

    if (!biz || !biz->valuestring) return OPRT_OK;

    if (strcmp(biz->valuestring, "ASR") == 0) {
        /* __wukong_ai_asr_process expects a cJSON string node */
        ty_cJSON *text_item = data ? ty_cJSON_GetObjectItem(data, "text") : NULL;
        CONST CHAR_T *text_str = (text_item && text_item->valuestring) ? text_item->valuestring : "";
        ty_cJSON *str_node = ty_cJSON_CreateString(text_str);
        if (str_node) {
            wukong_fc_process(0x00 /* AI_TEXT_ASR */, str_node, eof);
            ty_cJSON_Delete(str_node);
        }
    } else if (strcmp(biz->valuestring, "NLG") == 0) {
        /* __wukong_ai_nlg_process expects root["content"] */
        if (data) {
            wukong_fc_process(0x01 /* AI_TEXT_NLG */, data, eof);
        }
    }
    return OPRT_OK;
}

STATIC OPERATE_RET __jd_on_event(JD_EVENT_TYPE_E type)
{
    CONST WUKONG_AI_ACTIVE_CHAN_T *active = wukong_ai_channel_get_active(NULL);

    if (type == JD_EVENT_INTERRUPT) {
        wukong_audio_player_stop(AI_PLAYER_FG);
        WUKONG_AI_EVENT_T evt = { .type = WUKONG_AI_EVENT_CHAT_BREAK,
                                   .chat_id = active ? active->chat_id : NULL,
                                   .channel = active ? active->channel : NULL };
        wukong_ai_event_notify_ext(&evt);
    }
    return OPRT_OK;
}

STATIC VOID_T __jd_on_output_stop(BOOL_T force)
{
    (VOID_T)force;
    wukong_audio_player_stop(AI_PLAYER_FG);
}

STATIC CONST JD_OUTPUT_CBS_T s_jd_output_cbs = {
    .on_tts         = __jd_on_tts,
    .on_text        = __jd_on_text,
    .on_event       = __jd_on_event,
    .on_output_stop = __jd_on_output_stop,
};

/* ================================================================
 * Provider OPS Implementation
 * ================================================================ */

STATIC OPERATE_RET __jd_init(VOID_T *handle, CONST WUKONG_AI_PROVIDER_CFG_T *cfg)
{
    OPERATE_RET rt = OPRT_OK;
    WUKONG_AI_PROVIDER_T *self = (WUKONG_AI_PROVIDER_T *)handle;
    (VOID_T)cfg;

    JD_CTX_T *ctx = (JD_CTX_T *)tal_calloc(1, sizeof(JD_CTX_T));
    if (ctx == NULL) return OPRT_MALLOC_FAILED;
    ctx->audio_codec_type = AI_AUDIO_CODEC_MP3;
    self->ctx = ctx;

    /* JD chat config.
     * input.frameSize is the *byte length* of one OPUS frame, required by JD
     * when the client concatenates multiple fixed-size frames per upload.
     * For CBR-OPUS @bitrate, 40ms frame = bitrate * 40 / 8000 bytes. */
    ctx->jd_chat_cfg.audio.binary            = FALSE;
    ctx->jd_chat_cfg.audio.input.sampleRate  = 16000;
    ctx->jd_chat_cfg.audio.input.frameSize   = (APP_OPUS_ENCODER_BITRATE * 40) / (8 * 1000);
    ctx->jd_chat_cfg.audio.output.codec      = JD_AUDIO_CODEC_MP3;
    ctx->jd_chat_cfg.audio.output.sampleRate = 16000;
    ctx->jd_chat_cfg.needManualCall          = TRUE;

    joyinside_parse_set_output_cbs(&s_jd_output_cbs);
    /* input_init must come first: it sets jd_chat_cfg.audio.input.codec */
    TUYA_CALL_ERR_GOTO(__jd_input_init(ctx), EXIT);
    TUYA_CALL_ERR_GOTO(joyinside_client_init(&ctx->jd_chat_cfg), EXIT);

    return rt;

EXIT:
    joyinside_client_deinit();
    __jd_input_deinit(ctx);
    joyinside_parse_set_output_cbs(NULL);
    tal_free(self->ctx);
    self->ctx = NULL;
    return rt;
}

STATIC OPERATE_RET __jd_deinit(VOID_T *handle)
{
    WUKONG_AI_PROVIDER_T *self = (WUKONG_AI_PROVIDER_T *)handle;
    JD_CTX_T *ctx = (JD_CTX_T *)self->ctx;
    if (ctx) {
        __jd_input_deinit(ctx);
    }
    joyinside_parse_set_output_cbs(NULL);
    joyinside_client_deinit();
    tal_free(self->ctx);
    self->ctx = NULL;
    return OPRT_OK;
}

STATIC BOOL_T __jd_is_ready(VOID_T *handle)
{
    (VOID_T)handle;
    return joyinside_client_is_ready();
}

STATIC OPERATE_RET __jd_send(VOID_T *handle, CONST WUKONG_AI_MSG_T *msg)
{
    JD_CTX_T *ctx = (JD_CTX_T *)((WUKONG_AI_PROVIDER_T *)handle)->ctx;
    TUYA_CHECK_NULL_RETURN(ctx, OPRT_COM_ERROR);

    switch (msg->type) {
    case WUKONG_AI_MSG_TYPE_AUDIO: {
        /* write PCM to ringbuf; input thread handles encode + send */
        OPERATE_RET rt;
        UINT_T cnt = 0;
        do {
            rt = __jd_input_feed(ctx, AI_PT_AUDIO, msg->data, msg->data_len);
            if (rt == OPRT_RESOURCE_NOT_READY) {
                tal_system_sleep(10);
            }
        } while (rt == OPRT_RESOURCE_NOT_READY && cnt++ < 1000);
        return rt;
    }
    case WUKONG_AI_MSG_TYPE_TEXT:
        /* text does not go through ringbuf */
        if (!ctx->input.queue_sync) return OPRT_RESOURCE_NOT_READY;
        return joyinside_send_text((CHAR_T *)msg->data, msg->data_len);
    case WUKONG_AI_MSG_TYPE_IMAGE:
    case WUKONG_AI_MSG_TYPE_VIDEO:
    case WUKONG_AI_MSG_TYPE_FILE:
        return OPRT_NOT_SUPPORTED;
    default:
        return OPRT_NOT_SUPPORTED;
    }
}

STATIC OPERATE_RET __jd_abort(VOID_T *handle, CONST CHAR_T *chat_id)
{
    (VOID_T)handle;
    (VOID_T)chat_id;

    /* Mirror original svc_ai_agent JD path (tuya_ai_agent.c:1657-1666):
     *   AI_EVENT_CHAT_BREAK → send INTERRUPT *only* when TTS is playing.
     * Input pipeline stop / FINISH is the responsibility of input_stop
     * (CMD_INPUT_STOP), not of abort. Keep both flows independent. */
    if (!wukong_audio_player_is_playing()) {
        return OPRT_OK;
    }
    return joyinside_send_event(JD_EVENT_INTERRUPT);
}

STATIC OPERATE_RET __jd_ioctl(VOID_T *handle, INT_T cmd, VOID_T *arg)
{
    JD_CTX_T *ctx = (JD_CTX_T *)((WUKONG_AI_PROVIDER_T *)handle)->ctx;
    TUYA_CHECK_NULL_RETURN(ctx, OPRT_COM_ERROR);

    switch (cmd) {
    case WUKONG_PROVIDER_CMD_INPUT_START: {
        BOOL_T interrupt = arg ? *(BOOL_T *)arg : TRUE;
        return __jd_input_start(ctx, interrupt);
    }
    case WUKONG_PROVIDER_CMD_INPUT_STOP:
        __jd_input_stop(ctx);
        return OPRT_OK;
    case WUKONG_PROVIDER_CMD_OUTPUT_STOP:
        wukong_audio_player_stop(AI_PLAYER_FG);
        return OPRT_OK;
    case WUKONG_PROVIDER_CMD_ALERT:
        return OPRT_OK;
    case WUKONG_PROVIDER_CMD_DEL_SESSION:
        return OPRT_OK;
    case WUKONG_PROVIDER_CMD_SET_SCENE:
    case WUKONG_PROVIDER_CMD_SET_EVENT_PARAM:
    case WUKONG_PROVIDER_CMD_SERVER_VAD_CTRL:
    case WUKONG_PROVIDER_CMD_SWITCH_TARGET:
    case WUKONG_PROVIDER_CMD_GET_SESSION:
        return OPRT_NOT_SUPPORTED;
    default:
        return OPRT_NOT_SUPPORTED;
    }
}


/* ================================================================
 * OPS Table & Instance
 * ================================================================ */

STATIC CONST WUKONG_AI_PROVIDER_OPS_T s_jd_ops = {
    .name      = "jd",
    .caps      = WUKONG_AI_CAP_TEXT | WUKONG_AI_CAP_AUDIO
               | WUKONG_AI_CAP_TTS_BUILTIN,
    .init      = __jd_init,
    .deinit    = __jd_deinit,
    .is_ready  = __jd_is_ready,
    .send      = __jd_send,
    .llm_infer = NULL,
    .mcp_send  = NULL,   /* jd cloud has no MCP channel */
    .abort     = __jd_abort,
    .ioctl     = __jd_ioctl,
};

STATIC WUKONG_AI_PROVIDER_T s_jd_provider = {
    .ops = &s_jd_ops,
    .ctx = NULL,
};

WUKONG_AI_PROVIDER_T *wukong_provider_jd_get(VOID_T)
{
    return &s_jd_provider;
}
