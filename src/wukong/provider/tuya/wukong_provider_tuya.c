/**
 * @file wukong_provider_tuya.c
 * @brief Tuya Cloud Platform Provider — extracted from wukong_ai_agent.c.
 *
 * Wraps Tuya A2A SDK as the first WUKONG_AI_PROVIDER_T implementation.
 * Cloud Platform pattern: send() transparently forwards to SDK,
 * llm_infer is NULL (cloud handles inference).
 */

#include "wukong_ai_provider.h"
#include "wukong_ai_channel.h"
#include "wukong_ai_agent.h"
#include "wukong_ai_skills.h"
#include "wukong_audio_player.h"
#if defined(ENABLE_TUYA_PICTURE) && (ENABLE_TUYA_PICTURE == 1)
#include "wukong_picture_output.h"
#endif
#include "tuya_ai_agent.h"
#include "tuya_ai_output.h"
#include "tuya_ai_client.h"
#include "base_event.h"
#include "tuya_device_cfg.h"
#include "tuya_svc_devos.h"
#include "tal_memory.h"
#include "tal_system.h"
#include "tal_workq_service.h"
#include "uni_log.h"
#include <string.h>
#include <stdio.h>
#if defined(ENABLE_APP_OPUS_ENCODER) && (ENABLE_APP_OPUS_ENCODER == 1)
#include "opus.h"
#endif

/* --- Forward declaration of the provider instance (used in callbacks) --- */
STATIC WUKONG_AI_PROVIDER_T s_tuya_provider;

/* --- Private context --- */

typedef struct {
    UINT16_T audio_codec_type;
} TUYA_CTX_T;

/* ================================================================
 * SDK Async Callbacks
 * ================================================================ */

STATIC OPERATE_RET __tuya_event_cb(AI_EVENT_TYPE type, AI_PACKET_PT ptype, AI_EVENT_ID eid)
{
    (VOID_T)eid;
    PR_DEBUG("tuya provider -> recv event type: %d", type);

    CONST WUKONG_AI_ACTIVE_CHAN_T *active = wukong_ai_channel_get_active(NULL);

    if (AI_EVENT_START == type) {
        if (AI_PT_AUDIO == ptype) {
            TUYA_CTX_T *ctx = (TUYA_CTX_T *)s_tuya_provider.ctx;
            wukong_audio_play_tts_stream(WUKONG_AI_EVENT_TTS_START,
                                         ctx->audio_codec_type, NULL, 0);
        }
    } else if (AI_EVENT_CHAT_BREAK == type) {
        tuya_ai_output_stop(TRUE);
        wukong_audio_player_stop(AI_PLAYER_FG);
        wukong_skill_notify_chat_break();
        WUKONG_AI_EVENT_T evt = { .type = WUKONG_AI_EVENT_CHAT_BREAK,
                                   .chat_id = active ? active->chat_id : NULL,
                                   .channel = active ? active->channel : NULL };
        wukong_ai_event_notify_ext(&evt);
    } else if (AI_EVENT_SERVER_VAD == type) {
        WUKONG_AI_EVENT_T evt = { .type = WUKONG_AI_EVENT_SERVER_VAD,
                                   .chat_id = active ? active->chat_id : NULL,
                                   .channel = active ? active->channel : NULL };
        wukong_ai_event_notify_ext(&evt);
    } else if (AI_EVENT_END == type) {
        if (AI_PT_AUDIO == ptype) {
            TUYA_CTX_T *ctx = (TUYA_CTX_T *)s_tuya_provider.ctx;
            wukong_audio_play_tts_stream(WUKONG_AI_EVENT_TTS_STOP,
                                         ctx->audio_codec_type, NULL, 0);
        }
    } else if (AI_EVENT_CHAT_EXIT == type) {
        WUKONG_AI_EVENT_T evt = { .type = WUKONG_AI_EVENT_EXIT,
                                   .chat_id = active ? active->chat_id : NULL,
                                   .channel = active ? active->channel : NULL };
        wukong_ai_event_notify_ext(&evt);
    }

    return OPRT_OK;
}

STATIC OPERATE_RET __tuya_media_attr_cb(AI_BIZ_ATTR_INFO_T *attr)
{
    if (attr->type == AI_PT_AUDIO && (attr->flag & AI_HAS_ATTR)) {
        TUYA_CTX_T *ctx = (TUYA_CTX_T *)s_tuya_provider.ctx;
        PR_DEBUG("tuya provider -> audio codec type: %d", attr->value.audio.base.codec_type);
        switch (attr->value.audio.base.codec_type) {
        case AUDIO_CODEC_MP3:
            ctx->audio_codec_type = AI_AUDIO_CODEC_MP3;
            break;
        case AUDIO_CODEC_OPUS:
            ctx->audio_codec_type = AI_AUDIO_CODEC_OPUS;
            break;
        default:
            ctx->audio_codec_type = AI_AUDIO_CODEC_MAX;
            break;
        }
    }
    return OPRT_OK;
}

STATIC OPERATE_RET __tuya_media_data_cb(AI_PACKET_PT type, CHAR_T *data,
                                        UINT_T len, UINT_T total_len)
{
    (VOID_T)total_len;
    OPERATE_RET rt = OPRT_OK;

    if (type == AI_PT_AUDIO) {
        TUYA_CTX_T *ctx = (TUYA_CTX_T *)s_tuya_provider.ctx;
        rt = wukong_audio_play_tts_stream(WUKONG_AI_EVENT_TTS_DATA,
                                          ctx->audio_codec_type, data, len);
    } else if (type == AI_PT_VIDEO) {
        /* TODO: video handling */
#if defined(ENABLE_TUYA_PICTURE) && (ENABLE_TUYA_PICTURE == 1)
    } else if (type == AI_PT_IMAGE) {
        PR_NOTICE("[pic_chain] recv image chunk, len:%u, total_len:%u",
                  (uint32_t)len, (uint32_t)total_len);
        rt = wukong_picture_output_save_to_album((uint8_t *)data,
                                                  (uint32_t)len, (uint32_t)total_len);
#endif
    } else if (type == AI_PT_FILE) {
        /* TODO: file handling */
    }
    return rt;
}

STATIC OPERATE_RET __tuya_text_cb(AI_TEXT_TYPE_E type, ty_cJSON *root, BOOL_T eof)
{
    TUYA_CHECK_NULL_RETURN(root, OPRT_INVALID_PARM);
    wukong_ai_text_process(type, root, eof);
    return OPRT_OK;
}

STATIC OPERATE_RET __tuya_alert_cb(AI_ALERT_TYPE_E type)
{
    if (type == AT_PLEASE_AGAIN) {
        PR_DEBUG("ignored alert: %d", type);
        return OPRT_OK;
    }
    wukong_ai_event_notify(WUKONG_AI_EVENT_PLAY_ALERT, (VOID_T *)(unsigned long)type);
    return OPRT_OK;
}

/* ================================================================
 * AI Client Run Event — create default session
 * ================================================================ */

STATIC VOID_T __tuya_crt_default_session(VOID_T *data)
{
    (VOID_T)data;
    OPERATE_RET rt = tuya_ai_agent_crt_session(
        tuya_ai_agent_get_scode(NULL), 0, 0, NULL, 0);
    if (OPRT_OK != rt) {
        PR_ERR("tuya provider -> create default session failed: %d", rt);
    }
}

STATIC OPERATE_RET __tuya_on_client_run(VOID_T *data)
{
    (VOID_T)data;
#if defined(ENABLE_DEFAULT_SESSION) && (ENABLE_DEFAULT_SESSION == 1)
    tal_workq_schedule(WORKQ_SYSTEM, __tuya_crt_default_session, NULL);
#endif
    return OPRT_OK;
}

/* ================================================================
 * Provider OPS Implementation
 * ================================================================ */

STATIC OPERATE_RET __tuya_init(VOID_T *handle, CONST WUKONG_AI_PROVIDER_CFG_T *cfg)
{
    OPERATE_RET rt = OPRT_OK;
    WUKONG_AI_PROVIDER_T *self = (WUKONG_AI_PROVIDER_T *)handle;
    (VOID_T)cfg;

    TUYA_CTX_T *ctx = (TUYA_CTX_T *)tal_calloc(1, sizeof(TUYA_CTX_T));
    if (ctx == NULL) return OPRT_MALLOC_FAILED;
    ctx->audio_codec_type = AI_AUDIO_CODEC_MP3;
    self->ctx = ctx;

    AI_AGENT_CFG_T ai_agent_cfg = {0};

    /* Audio codec: OPUS > SPEEX > PCM */
    ai_agent_cfg.attr.audio.bitrate = 16000;
#if defined(ENABLE_APP_OPUS_ENCODER) && (ENABLE_APP_OPUS_ENCODER == 1)
    ai_agent_cfg.attr.audio.codec_type = AUDIO_CODEC_OPUS;
#if defined(APP_OPUS_ENCODER_BITRATE)
    ai_agent_cfg.attr.audio.bitrate = APP_OPUS_ENCODER_BITRATE;
#endif
#if defined(APP_OPUS_ENCODER_BANDWIDTH)
    ai_agent_cfg.attr.audio.bandwidth = APP_OPUS_ENCODER_BANDWIDTH;
#else
    ai_agent_cfg.attr.audio.bandwidth = OPUS_BANDWIDTH_MEDIUMBAND;
#endif
    ai_agent_cfg.attr.audio.frame_size = (ai_agent_cfg.attr.audio.bitrate * 40) / (8 * 1000);
#elif defined(ENABLE_APP_SPEEX_ENCODER) && (ENABLE_APP_SPEEX_ENCODER == 1)
    ai_agent_cfg.attr.audio.codec_type = AUDIO_CODEC_SPEEX;
#else
    ai_agent_cfg.attr.audio.codec_type = AUDIO_CODEC_PCM;
#endif
    ai_agent_cfg.attr.audio.sample_rate = 16000;
    ai_agent_cfg.attr.audio.channels = AUDIO_CHANNELS_MONO;
    ai_agent_cfg.attr.audio.bit_depth = 16;

#if defined(ENABLE_TUYA_CAMERA)
    ai_agent_cfg.attr.video.codec_type = VIDEO_CODEC_H264;
    ai_agent_cfg.attr.video.sample_rate = 90000;
    ai_agent_cfg.attr.video.fps = 30;
    ai_agent_cfg.attr.video.width = 480;
    ai_agent_cfg.attr.video.height = 480;
#endif

    ai_agent_cfg.output.alert_cb = __tuya_alert_cb;
    ai_agent_cfg.output.text_cb = __tuya_text_cb;
    ai_agent_cfg.output.media_data_cb = __tuya_media_data_cb;
    ai_agent_cfg.output.media_attr_cb = __tuya_media_attr_cb;
    ai_agent_cfg.output.event_cb = __tuya_event_cb;

    /* Codec enable: UART input assumes pre-encoded; board input encodes locally */
#if defined(USING_UART_AUDIO_INPUT) && (USING_UART_AUDIO_INPUT == 1)
    ai_agent_cfg.codec_enable = FALSE;
#if defined(UART_CODEC_UPLOAD_FORMAT) && (UART_CODEC_UPLOAD_FORMAT == 1)
    ai_agent_cfg.attr.audio.codec_type = AUDIO_CODEC_SPEEX;
#else
    ai_agent_cfg.attr.audio.codec_type = AUDIO_CODEC_OPUS;
#endif
#else
    ai_agent_cfg.codec_enable = TRUE;
#endif

#if defined(AI_PLAYER_DECODER_OPUS_ENABLE)
    ai_agent_cfg.tts_cfg.format = "opus";
    ai_agent_cfg.tts_cfg.sample_rate = 16000;
    ai_agent_cfg.tts_cfg.bit_rate = AI_PLAYER_DECODER_OPUS_KBPS * 1000;
#endif

    TUYA_CALL_ERR_RETURN(tuya_ai_agent_init(&ai_agent_cfg));

    TUYA_CALL_ERR_RETURN(ty_subscribe_event(EVENT_AI_CLIENT_RUN, "provider_tuya",
                                            __tuya_on_client_run, SUBSCRIBE_TYPE_NORMAL));

    return rt;
}

STATIC OPERATE_RET __tuya_deinit(VOID_T *handle)
{
    WUKONG_AI_PROVIDER_T *self = (WUKONG_AI_PROVIDER_T *)handle;
    ty_unsubscribe_event(EVENT_AI_CLIENT_RUN, "provider_tuya", __tuya_on_client_run);
    tal_free(self->ctx);
    self->ctx = NULL;
    tuya_ai_agent_deinit();
    return OPRT_OK;
}

STATIC BOOL_T __tuya_is_ready(VOID_T *handle)
{
    (VOID_T)handle;
    return tuya_ai_agent_is_ready();
}

STATIC OPERATE_RET __tuya_send(VOID_T *handle, CONST WUKONG_AI_MSG_T *msg)
{
    OPERATE_RET rt = OPRT_OK;
    (VOID_T)handle;

    switch (msg->type) {
    case WUKONG_AI_MSG_TYPE_TEXT:
        TUYA_CALL_ERR_RETURN(tuya_ai_text_input((BYTE_T *)msg->data,
                                                 msg->data_len, msg->data_len));
        break;
    case WUKONG_AI_MSG_TYPE_AUDIO: {
        UINT64_T ts = tal_system_get_millisecond();
        TUYA_CALL_ERR_LOG(tuya_ai_audio_input(ts, ts,
                                              (UINT8_T *)msg->data, msg->data_len, msg->data_len));
        break;
    }
    case WUKONG_AI_MSG_TYPE_IMAGE:
        TUYA_CALL_ERR_RETURN(tuya_ai_image_input(tal_system_get_millisecond(),
                                                  (BYTE_T *)msg->data, msg->data_len, msg->data_len));
        break;
    case WUKONG_AI_MSG_TYPE_VIDEO: {
        UINT64_T ts = tal_system_get_millisecond();
        TUYA_CALL_ERR_RETURN(tuya_ai_video_input(ts, ts,
                                                  (UINT8_T *)msg->data, msg->data_len, msg->data_len));
        break;
    }
    case WUKONG_AI_MSG_TYPE_FILE:
        TUYA_CALL_ERR_RETURN(tuya_ai_file_input((BYTE_T *)msg->data,
                                                 msg->data_len, msg->data_len));
        break;
    default:
        return OPRT_NOT_SUPPORTED;
    }
    return rt;
}

STATIC OPERATE_RET __tuya_abort(VOID_T *handle, CONST CHAR_T *chat_id)
{
    (VOID_T)handle;
    (VOID_T)chat_id;
    tuya_ai_agent_event(AI_EVENT_CHAT_BREAK, 0);
    return OPRT_OK;
}

STATIC OPERATE_RET __tuya_ioctl(VOID_T *handle, INT_T cmd, VOID_T *arg)
{
    OPERATE_RET rt = OPRT_OK;
    (VOID_T)handle;

    switch (cmd) {
    case WUKONG_PROVIDER_CMD_INPUT_START: {
        BOOL_T interrupt = arg ? *(BOOL_T *)arg : TRUE;
        tuya_ai_input_start(interrupt);
        return OPRT_OK;
    }
    case WUKONG_PROVIDER_CMD_INPUT_STOP:
        tuya_ai_input_stop();
        return OPRT_OK;
    case WUKONG_PROVIDER_CMD_OUTPUT_STOP: {
        BOOL_T force = arg ? *(BOOL_T *)arg : TRUE;
        return tuya_ai_output_stop(force);
    }
    case WUKONG_PROVIDER_CMD_SET_SCENE:
        return tuya_ai_agent_set_scode((CONST CHAR_T *)arg);
    case WUKONG_PROVIDER_CMD_SET_EVENT_PARAM:
        tuya_ai_agent_set_event_param(arg);
        return OPRT_OK;
    case WUKONG_PROVIDER_CMD_SERVER_VAD_CTRL: {
        BOOL_T enable = arg ? *(BOOL_T *)arg : FALSE;
        tuya_ai_agent_server_vad_ctrl(enable);
        return OPRT_OK;
    }
    case WUKONG_PROVIDER_CMD_ALERT: {
        INT_T type = arg ? *(INT_T *)arg : 0;
#if defined(ENABLE_JOYINSIDE) && (ENABLE_JOYINSIDE == 1)
        (VOID_T)type;
        return OPRT_OK;
#endif
#if defined(AI_VERSION) && (AI_VERSION == 2)
        tuya_ai_input_alert((AI_ALERT_TYPE_E)type);
#else
        CHAR_T cmd_str[16];
        (VOID_T)snprintf(cmd_str, sizeof(cmd_str), "cmd:%d", type);
        tuya_ai_input_start(TRUE);
        rt = tuya_ai_text_input((BYTE_T *)cmd_str, strlen(cmd_str), strlen(cmd_str));
        tuya_ai_input_stop();
#endif
        return rt;
    }
    case WUKONG_PROVIDER_CMD_SWITCH_TARGET: {
        CONST CHAR_T *target = (CONST CHAR_T *)arg;
        TUYA_CHECK_NULL_RETURN(target, OPRT_INVALID_PARM);
        CHAR_T post_content[128] = {0};
        (VOID_T)snprintf(post_content, sizeof(post_content),
                         "{\"commandInfo\": \"%s\"}", target);
        ty_cJSON *result = NULL;
        TUYA_CALL_ERR_LOG(iot_httpc_common_post_simple(
            "thing.ai.agent.switch.role", "1.0", post_content, NULL, &result));
        if (result) ty_cJSON_Delete(result);
        return rt;
    }
    case WUKONG_PROVIDER_CMD_GET_SESSION: {
        WUKONG_PROVIDER_GET_SESSION_ARG_T *sa = (WUKONG_PROVIDER_GET_SESSION_ARG_T *)arg;
        TUYA_CHECK_NULL_RETURN(sa, OPRT_INVALID_PARM);
        sa->session = (VOID_T *)tuya_ai_agent_get_session(sa->scode);
        return (sa->session != NULL) ? OPRT_OK : OPRT_NOT_FOUND;
    }
    default:
        return OPRT_NOT_SUPPORTED;
    }
}

/* ================================================================
 * OPS Table & Instance
 * ================================================================ */

STATIC CONST WUKONG_AI_PROVIDER_OPS_T s_tuya_ops = {
    .name      = "tuya",
    .caps      = WUKONG_AI_CAP_TEXT | WUKONG_AI_CAP_AUDIO
               | WUKONG_AI_CAP_IMAGE | WUKONG_AI_CAP_TTS_BUILTIN,
    .init      = __tuya_init,
    .deinit    = __tuya_deinit,
    .is_ready  = __tuya_is_ready,
    .send      = __tuya_send,
    .llm_infer = NULL,
    .abort     = __tuya_abort,
    .ioctl     = __tuya_ioctl,
};

STATIC WUKONG_AI_PROVIDER_T s_tuya_provider = {
    .ops = &s_tuya_ops,
    .ctx = NULL,
};

/* --- External accessor --- */

WUKONG_AI_PROVIDER_T *wukong_provider_tuya_get(VOID_T)
{
    return &s_tuya_provider;
}
