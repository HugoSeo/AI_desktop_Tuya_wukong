/**
 * @file wukong_ai_agent.c
 * @brief Wukong AI agent wrapper implementation (Tuya AI agent integration).
 *
 * Implements media/text/event callbacks from Tuya AI agent and forwards them to the
 * Wukong application layer (audio player, skills, and the event output callback).
 *
 * Callback flow:
 * - Event callback: maps Tuya events (START/END/BREAK/VAD/EXIT) to wukong audio player
 *   and application events.
 * - Media attribute callback: detects audio codec type (MP3/OPUS) from cloud response
 *   and updates __s_audio_codec_type for TTS stream playback.
 * - Media data callback: feeds audio chunks to wukong audio player TTS stream.
 * - Text callback: forwards JSON text stream to wukong text processor.
 * - Alert callback: filters AT_PLEASE_AGAIN and forwards others as PLAY_ALERT events.
 *
 * Notes:
 * - TTS streaming events are mapped to wukong audio player stream APIs.
 * - Chat break and server VAD are forwarded as application events.
 * - cloud_alert() maps alert types to simple text commands (cmd:x).
 * - Codec selection: OPUS if ENABLE_APP_OPUS_ENCODER, else SPEEX if enabled, else PCM.
 * - UART audio input disables codec (assumes pre-encoded); board input enables codec.
 *
 * @see wukong_ai_agent.h
 */

#include "wukong_ai_agent.h"
#include "wukong_ai_skills.h"
#include "wukong_audio_player.h"
#if defined(ENABLE_TUYA_PICTURE) && (ENABLE_TUYA_PICTURE == 1)
#include "wukong_picture_output.h"
#endif
#include "uni_log.h"
#include "base_event.h"
#include "tuya_device_cfg.h"
#include "tuya_svc_devos.h"
#include "tuya_ai_agent.h"
#include "tuya_ai_output.h"
#include <stdio.h>
#include <string.h>
#if defined(ENABLE_JOYINSIDE) && (ENABLE_JOYINSIDE == 1)
#include "joyinside_biz.h"
#endif
#if defined(ENABLE_APP_OPUS_ENCODER) && (ENABLE_APP_OPUS_ENCODER == 1)
#include "opus.h"
#endif

// #include "hugo_ai_desktop.h"

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
/** Application event callback registered in wukong_ai_agent_init(). */
STATIC WUKONG_EVENT_OUTPUT __s_event_notify_cb = NULL;

/** Current audio codec type for TTS stream (MP3/OPUS); updated from media attr callback. */
STATIC UINT16_T __s_audio_codec_type = AI_AUDIO_CODEC_MP3;

/* ---------------------------------------------------------------------------
 * Tuya AI agent callbacks
 * --------------------------------------------------------------------------- */

/**
 * @brief Tuya AI agent event callback: handles START/END/BREAK/SERVER_VAD/EXIT.
 *
 * - AI_EVENT_START + AI_PT_AUDIO: start TTS stream playback (eid unused, pass NULL/0).
 * - AI_EVENT_CHAT_BREAK: stop foreground player, notify skill layer, emit CHAT_BREAK event.
 * - AI_EVENT_SERVER_VAD: emit SERVER_VAD event.
 * - AI_EVENT_END + AI_PT_AUDIO: end TTS stream.
 * - AI_EVENT_CHAT_EXIT: emit EXIT event.
 *
 * @param[in] type  Event type (AI_EVENT_TYPE).
 * @param[in] ptype Packet type (AI_PACKET_PT; used for START/END).
 * @param[in] eid   Event ID (unused in current implementation).
 * @return OPRT_OK.
 */
STATIC OPERATE_RET __wukong_ai_agent_event_cb(AI_EVENT_TYPE type, AI_PACKET_PT ptype, AI_EVENT_ID eid)
{
    (void)eid;
    PR_DEBUG("wukong ai agent -> recv event type: %d", type);

    if (AI_EVENT_START == type) {
        if (AI_PT_AUDIO == ptype) {
            /* Cloud starts TTS: begin wukong audio player TTS stream. */
            wukong_audio_play_tts_stream(WUKONG_AI_EVENT_TTS_START, __s_audio_codec_type, NULL, 0);
        }
    } else if (AI_EVENT_CHAT_BREAK == type) {
        /* Cloud break: stop foreground player, notify skill layer, emit CHAT_BREAK event. */
        tuya_ai_output_stop(TRUE);
        wukong_audio_player_stop(AI_PLAYER_FG);
        wukong_skill_notify_chat_break();
        wukong_ai_event_notify(WUKONG_AI_EVENT_CHAT_BREAK, NULL);
    } else if (AI_EVENT_SERVER_VAD == type) {
        /* Server-side VAD detected: notify application layer. */
        wukong_ai_event_notify(WUKONG_AI_EVENT_SERVER_VAD, NULL);
    } else if (AI_EVENT_END == type) {
        if (AI_PT_AUDIO == ptype) {
            /* Cloud ends TTS: end wukong audio player TTS stream. */
            wukong_audio_play_tts_stream(WUKONG_AI_EVENT_TTS_STOP, __s_audio_codec_type, NULL, 0);
        }
    } else if (AI_EVENT_CHAT_EXIT == type) {
        /* Chat session exit: notify application layer. */
        wukong_ai_event_notify(WUKONG_AI_EVENT_EXIT, NULL);
    }

    return OPRT_OK;
}

/**
 * @brief Tuya AI agent media attribute callback: detect audio codec type for TTS stream.
 *
 * When cloud sends audio attributes (AI_PT_AUDIO with AI_HAS_ATTR flag), update
 * __s_audio_codec_type so subsequent TTS data chunks are decoded correctly.
 * Supported: MP3, OPUS; others set to AI_AUDIO_CODEC_MAX (may fail playback).
 *
 * @param[in] attr Media attribute info from cloud.
 * @return OPRT_OK.
 */
STATIC OPERATE_RET __wukong_ai_agent_media_attr_cb(AI_BIZ_ATTR_INFO_T *attr)
{
    if (attr->type == AI_PT_AUDIO && (attr->flag & AI_HAS_ATTR)) {
        PR_DEBUG("wukong ai agent -> audio codec type: %d", attr->value.audio.base.codec_type);
        switch (attr->value.audio.base.codec_type) {
        case AUDIO_CODEC_MP3:
            __s_audio_codec_type = AI_AUDIO_CODEC_MP3;
            break;
        case AUDIO_CODEC_OPUS:
            __s_audio_codec_type = AI_AUDIO_CODEC_OPUS;
            break;
        default:
            __s_audio_codec_type = AI_AUDIO_CODEC_MAX;
            break;
        }
    }
    return OPRT_OK;
}

/**
 * @brief Tuya AI agent media data callback: feed audio bytes to wukong audio player.
 *
 * For AI_PT_AUDIO: forward data chunk to wukong audio player TTS stream with current
 * codec type. Video/image/file types are not yet implemented (TBD).
 *
 * @param[in] type      Packet type (AI_PACKET_PT).
 * @param[in] data      Media data buffer.
 * @param[in] len       Length of this chunk.
 * @param[in] total_len Total expected length (unused).
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __wukong_ai_agent_media_data_cb(AI_PACKET_PT type, CHAR_T *data, UINT_T len, UINT_T total_len)
{
    (void)total_len;
    OPERATE_RET rt = OPRT_OK;

    if (type == AI_PT_AUDIO) {
        /* Forward audio chunk to wukong audio player TTS stream. */
        rt = wukong_audio_play_tts_stream(WUKONG_AI_EVENT_TTS_DATA, __s_audio_codec_type, data, len);
    } else if (type == AI_PT_VIDEO) {
        /* TODO: video handling. */
#if defined(ENABLE_TUYA_PICTURE) && (ENABLE_TUYA_PICTURE == 1)
    } else if (type == AI_PT_IMAGE) {
        PR_NOTICE("[pic_chain] recv image chunk, len:%u, total_len:%u", (uint32_t)len, (uint32_t)total_len);
        rt = wukong_picture_output_save_to_album((uint8_t *)data, (uint32_t)len, (uint32_t)total_len);
#endif
    } else if (type == AI_PT_FILE) {
        /* TODO: file handling. */
    }
    return rt;
}

/**
 * @brief Tuya AI agent text callback: forward JSON text stream to wukong text processor.
 *
 * Cloud sends text responses as JSON; this callback forwards them to wukong_ai_text_process()
 * which handles text stream events (start/data/stop) and may trigger TTS or display updates.
 *
 * @param[in] type Text type (AI_TEXT_TYPE_E).
 * @param[in] root JSON root object (must not be NULL).
 * @param[in] eof  End-of-stream flag.
 * @return OPRT_OK on success; OPRT_INVALID_PARM if root is NULL.
 */
STATIC OPERATE_RET __wukong_ai_agent_text_cb(AI_TEXT_TYPE_E type, ty_cJSON *root, BOOL_T eof)
{
    TUYA_CHECK_NULL_RETURN(root, OPRT_INVALID_PARM);
    // TAL_PR_NOTICE("-----------------   %s",root->valuestring);
    wukong_ai_text_process(type, root, eof);

    if(type==AI_TEXT_ASR)
    {
        CHAR_T *content =  ty_cJSON_GetStringValue(root);
        TAL_PR_NOTICE("wukong text -> ASR result: %s", content);
    }

    return OPRT_OK;
}

/**
 * @brief Tuya AI agent alert callback: forward as WUKONG_AI_EVENT_PLAY_ALERT except AT_PLEASE_AGAIN.
 *
 * Cloud may send alert notifications; AT_PLEASE_AGAIN is ignored (handled elsewhere).
 * Other alerts are forwarded to application via wukong_ai_event_notify() with type
 * cast to VOID* as payload.
 *
 * @param[in] type Alert type (AI_ALERT_TYPE_E).
 * @return OPRT_OK.
 */
STATIC OPERATE_RET __wukong_ai_agent_alert_cb(AI_ALERT_TYPE_E type)
{
    if (type == AT_PLEASE_AGAIN) {
        PR_DEBUG("ignored alert: %d", type);
        return OPRT_OK;
    }

    wukong_ai_event_notify(WUKONG_AI_EVENT_PLAY_ALERT, (VOID *)(unsigned long)type);
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Public API (see wukong_ai_agent.h)
 * --------------------------------------------------------------------------- */

/**
 * @brief Initialize the Wukong AI agent wrapper and underlying Tuya AI agent.
 *
 * Configures audio/video codec attributes, registers callbacks (event, media, text, alert),
 * and sets codec enable based on input type (UART vs board). On success, stores the
 * application event callback for later use by wukong_ai_event_notify().
 *
 * Audio codec selection:
 * - ENABLE_APP_OPUS_ENCODER: OPUS
 * - ENABLE_APP_SPEEX_ENCODER: SPEEX
 * - Otherwise: PCM
 *
 * Codec enable:
 * - USING_UART_AUDIO_INPUT: FALSE (assumes pre-encoded); UART_CODEC_UPLOAD_FORMAT=1 -> SPEEX, else OPUS
 * - Board audio input: TRUE (encode locally)
 *
 * Video (if ENABLE_TUYA_CAMERA): H264, 480x480, 30 fps, 90k sample rate.
 *
 * TTS config (if AI_PLAYER_DECODER_OPUS_ENABLE): opus format, 16k sample rate, bitrate from macro.
 *
 * @param[in] cb Application callback to receive events (may be NULL; events still work internally).
 * @return OPRT_OK on success. See tuya_error_code.h for errors.
 */
OPERATE_RET wukong_ai_agent_init(WUKONG_EVENT_OUTPUT cb)
{
    OPERATE_RET rt = OPRT_OK;
    AI_AGENT_CFG_T ai_agent_cfg = {0};

    ai_agent_cfg.attr.audio.bitrate = 16000;
    /* Audio codec: OPUS > SPEEX > PCM. */
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

    /* Register all callbacks. */
    ai_agent_cfg.output.alert_cb = __wukong_ai_agent_alert_cb;
    ai_agent_cfg.output.text_cb = __wukong_ai_agent_text_cb;
    ai_agent_cfg.output.media_data_cb = __wukong_ai_agent_media_data_cb;
    ai_agent_cfg.output.media_attr_cb = __wukong_ai_agent_media_attr_cb;
    ai_agent_cfg.output.event_cb = __wukong_ai_agent_event_cb;

    /* Codec enable: UART input assumes pre-encoded; board input encodes locally. */
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

#if defined(ENABLE_APP_JOYINSIDE) && (ENABLE_APP_JOYINSIDE == 1) && \
    defined(ENABLE_JOYINSIDE) && (ENABLE_JOYINSIDE == 1)
    JD_CHAT_CFG_S jd_cfg;
    memset(&jd_cfg, 0, SIZEOF(JD_CHAT_CFG_S));
    jd_cfg.audio.input.codec = JD_AUDIO_CODEC_OPUS;
    jd_cfg.audio.input.sampleRate = 16000;
    jd_cfg.audio.input.frameSize = (ai_agent_cfg.attr.audio.bitrate * 40) / (8 * 1000);
    jd_cfg.audio.output.codec = JD_AUDIO_CODEC_MP3;
    jd_cfg.audio.output.sampleRate = 16000;
    jd_cfg.needManualCall = TRUE;
    ai_agent_cfg.jd_cfg = &jd_cfg;
#endif

    TUYA_CALL_ERR_RETURN(tuya_ai_agent_init(&ai_agent_cfg));
    __s_event_notify_cb = cb;

    return rt;
}

/**
 * @brief De-initialize the Wukong AI agent wrapper and underlying Tuya AI agent.
 *
 * Clears the event callback and calls tuya_ai_agent_deinit().
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_ai_agent_deinit(VOID)
{
    __s_event_notify_cb = NULL;
    tuya_ai_agent_deinit();
    return OPRT_OK;
}

/**
 * @brief Send a generic audio payload to the AI agent.
 *
 * @param[in] data Pointer to payload bytes. Must not be NULL when len > 0.
 * @param[in] len  Payload length in bytes.
 * @return OPRT_OK on success. See tuya_error_code.h for errors.
 */
OPERATE_RET wukong_ai_agent_send_audio(UINT8_T *data, UINT_T len)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(data, OPRT_INVALID_PARM);

    UINT64_T   pts = 0;
    UINT64_T   timestamp = 0;
    timestamp = pts = tal_system_get_millisecond();
    TUYA_CALL_ERR_LOG(tuya_ai_audio_input(timestamp, pts, (UINT8_T *)data, len, len));

    return rt;
}


/**
 * @brief Send a text command/content to the AI agent.
 *
 * Starts input session, sends text via tuya_ai_text_input(), then stops input.
 * The text is processed by the cloud AI service and may trigger TTS or other responses.
 *
 * @param[in] content Null-terminated text string. Must not be NULL.
 * @return OPRT_OK on success. See tuya_error_code.h for errors.
 */
OPERATE_RET wukong_ai_agent_send_text(CHAR_T *content)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(content, OPRT_INVALID_PARM); 
    TUYA_CALL_ERR_RETURN(tuya_ai_text_input((BYTE_T *)content, strlen(content), strlen(content)));

    return rt;
}

/**
 * @brief Send a generic file payload to the AI agent.
 *
 * @param[in] data Pointer to payload bytes. Must not be NULL when len > 0.
 * @param[in] len  Payload length in bytes.
 * @return OPRT_OK on success. See tuya_error_code.h for errors.
 */
OPERATE_RET wukong_ai_agent_send_file(UINT8_T *data, UINT_T len)
{
    OPERATE_RET rt = OPRT_OK;

    if ((len > 0) && (data == NULL)) {
        return OPRT_INVALID_PARM;
    }

    TUYA_CALL_ERR_RETURN(tuya_ai_file_input((BYTE_T *)data, len, len));
    return rt;
}

/**
 * @brief Send an image payload to the AI agent.
 *
 * Uses current timestamp from tal_system_get_millisecond().
 *
 * @param[in] data Pointer to image bytes (JPEG, PNG, etc.). Must not be NULL when len > 0.
 * @param[in] len  Image length in bytes.
 * @return OPRT_OK on success. See tuya_error_code.h for errors.
 */
OPERATE_RET wukong_ai_agent_send_image(UINT8_T *data, UINT_T len)
{
    OPERATE_RET rt = OPRT_OK;
    UINT64_T timestamp = tal_system_get_millisecond();

    if ((len > 0) && (data == NULL)) {
        return OPRT_INVALID_PARM;
    }

    TUYA_CALL_ERR_RETURN(tuya_ai_image_input(timestamp, (BYTE_T *)data, len, len));
    return rt;
}



/**
 * @brief Send a video payload to the AI agent.
 *
 * Uses current timestamp for both PTS and timestamp.
 *
 * @param[in] data Pointer to video bytes (H264, etc.). Must not be NULL when len > 0.
 * @param[in] len  Video length in bytes.
 * @return OPRT_OK on success. See tuya_error_code.h for errors.
 */
OPERATE_RET wukong_ai_agent_send_video(UINT8_T *data, UINT_T len)
{
    OPERATE_RET rt = OPRT_OK;
    UINT64_T pts = 0;
    UINT64_T timestamp = 0;

    if ((len > 0) && (data == NULL)) {
        return OPRT_INVALID_PARM;
    }

    pts = timestamp = tal_system_get_millisecond();
    TUYA_CALL_ERR_RETURN(tuya_ai_video_input(timestamp, pts, data, len, len));
    return rt;
}

/**
 * @brief Request a cloud alert by type (implementation maps to a text command).
 *
 * Maps alert types to simple text commands (cmd:0~5) and sends via send_text().
 * Supported types: AT_NETWORK_CONNECTED, AT_WAKEUP, AT_LONG_KEY_TALK, AT_KEY_TALK,
 * AT_WAKEUP_TALK, AT_RANDOM_TALK. AT_PLEASE_AGAIN is ignored in alert callback.
 *
 * Note: Alternative implementation (disabled by #if 0) uses tuya_ai_input_alert()
 * when AI_VERSION==2, but current code uses text command mapping for compatibility.
 *
 * @param[in] type Alert type (AI_ALERT_TYPE_E).
 * @return OPRT_OK on success; OPRT_NOT_SUPPORTED for unsupported type.
 */
OPERATE_RET wukong_ai_agent_cloud_alert(INT_T type)
{
#if defined(ENABLE_JOYINSIDE) && (ENABLE_JOYINSIDE == 1)
    return OPRT_OK;
#endif
    OPERATE_RET rt = OPRT_OK;
#if defined(AI_VERSION) && (AI_VERSION==2)
    // Note: use tuya_ai_input_alert_custom to support cloud alert with custom event type
    tuya_ai_input_alert((AI_ALERT_TYPE_E)type);    
#else
    CHAR_T *alert_prompt = NULL;

    TAL_PR_NOTICE("wukong ai agent -> request cloud alert %d", type);

    switch (type) {
    case AT_NETWORK_CONNECTED:
        alert_prompt = "cmd:0";
        break;
    case AT_WAKEUP:
        alert_prompt = "cmd:1";
        break;
    case AT_LONG_KEY_TALK:
        alert_prompt = "cmd:2";
        break;
    case AT_KEY_TALK:
        alert_prompt = "cmd:3";
        break;
    case AT_WAKEUP_TALK:
        alert_prompt = "cmd:4";
        break;
    case AT_RANDOM_TALK:
        alert_prompt = "cmd:5";
        break;
    default:
        return OPRT_NOT_SUPPORTED;
    }
    tuya_ai_input_start(TRUE);
    wukong_ai_agent_send_text(alert_prompt);
    tuya_ai_input_stop();
#endif
    return rt;
}


/**
 * @brief Switch AI agent role by sending a DevOS RPC request.
 *
 * Sends HTTP POST to "thing.ai.agent.switch.role" with JSON payload containing role name.
 * The role name is embedded in a "commandInfo" field. Uses iot_httpc_common_post_simple()
 * and parses the JSON response (which is then freed).
 *
 * @param[in] role Role name string (null-terminated). Must not be NULL.
 * @return OPRT_OK on success. See tuya_error_code.h for errors.
 */
OPERATE_RET wukong_ai_agent_role_switch(CHAR_T *role)
{
    OPERATE_RET rt = OPRT_OK;
    ty_cJSON *result = NULL;
    CHAR_T post_content[128] = {0};

    TUYA_CHECK_NULL_RETURN(role, OPRT_INVALID_PARM);

    (void)snprintf(post_content, sizeof(post_content), "{\"commandInfo\": \"%s\"}", role);

    TUYA_CALL_ERR_LOG(iot_httpc_common_post_simple("thing.ai.agent.switch.role", "1.0", post_content, NULL, &result));
    TUYA_CHECK_NULL_RETURN(result, OPRT_MID_HTTP_GET_RESP_ERROR);

    ty_cJSON_Delete(result);
    return rt;
}

/**
 * @brief Notify an application-level event to the registered output callback.
 *
 * Builds a WUKONG_AI_EVENT_T and calls the callback if registered. Used internally
 * by callbacks and can be called by application code to inject events.
 *
 * @param[in] type Event type.
 * @param[in] data Optional payload pointer (type-dependent; e.g. alert type cast to VOID*).
 */
VOID wukong_ai_event_notify(WUKONG_AI_EVENT_TYPE_E type, VOID *data)
{
    WUKONG_AI_EVENT_T event = {0};

    if (__s_event_notify_cb) {
        event.data = data;
        event.type = type;
        __s_event_notify_cb(&event);
    }
}


OPERATE_RET hugo_cmd_getback1(UINT8_T *data)
{
    OPERATE_RET rt = OPRT_OK;
    hugo_cmd_getback(data);
    TAL_PR_NOTICE("wukong get data = %d",*data);
    return rt;
}
