/**
 * @file wukong_ai_mode_detection.c
 * @brief Detection mode implementation.
 *
 * Implements the motion detection device mode. When active, enables camera
 * motion detection; when motion is detected, takes a photo and sends it
 * to the cloud for AI analysis. Detection is paused during the AI
 * conversation cycle (upload → think → speak) and resumes after TTS ends.
 *
 * @copyright Copyright (c) 2023 Tuya Inc. All Rights Reserved.
 */

#include "wukong_ai_mode.h"
#include "tuya_ai_toy.h"
#include "wukong_kws.h"
#include "tuya_ai_toy_camera.h"
#include "tuya_device_board.h"

#if defined(ENABLE_AI_MODE_DETECTION) && (ENABLE_AI_MODE_DETECTION == 1)
#include "tuya_ipc_video_proc.h"
#include "tal_system.h"
#include "tal_memory.h"


/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define MD_DS_WIDTH     (TUYA_AI_TOY_ISP_WIDTH / 3)
#define MD_DS_HEIGHT    (TUYA_AI_TOY_ISP_HEIGHT / 3)
#define MD_Y_THD        50
#define MD_SENSITIVITY  3
#define MD_SKIP_FRAMES  50
#define MD_WARMUP_FRAMES 10    /* frames fed to algorithm to update reference, results ignored */
#define MD_COOLDOWN_MS          10000  /* min interval between notifications (ms) */
#define MD_THINK_TIMEOUT_MS     10000  /* max wait for cloud response after upload (ms) */

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC AI_CHAT_MODE_HANDLE_T s_ai_detection_cb = {0};
STATIC AI_CHAT_MODE_PARAM_T  s_ai_detection    = {0};

STATIC BOOL_T   s_md_inited         = FALSE;
STATIC UCHAR_T *s_md_y_buf          = NULL;
STATIC UINT32_T s_md_frame_cnt      = 0;
STATIC UINT32_T s_md_last_motion_ms = 0;
STATIC BOOL_T   s_md_photo_pending   = FALSE;
STATIC UINT32_T s_md_think_start_ms  = 0;

VOID_T __on_get_detection_msg()
{
    /* If already processing (motion-detect or prior request), cancel first */
    if (s_ai_detection.state == AI_CHAT_THINK ||
        s_ai_detection.state == AI_CHAT_SPEAK) {
        wukong_audio_player_stop(AI_PLAYER_ALL);
        tuya_ai_agent_event(AI_EVENT_CHAT_BREAK, 0);
    }

    /* Stop camera MD to prevent concurrent motion triggers */
    tuya_ai_toy_camera_unsubscribe(CAM_STREAM_YUV422, CAM_CONSUMER_MD);

    /* Transition to THINK */
    DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_DETECTION,
                             s_ai_detection.state, AI_CHAT_THINK);

    tuya_ai_agent_set_scode(AI_AGENT_SCODE_DETECTION);
    tuya_ai_input_start(TRUE);
    wukong_ai_agent_send_text("summary");
    tuya_ai_input_stop();

    /* Arm the timeout guard */
    s_md_think_start_ms = tal_system_get_millisecond();
}

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID __on_yuv_frame(const CAM_FRAME_T *frame, VOID *ctx);

/* ---------------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------------- */

/**
 * @brief Resume motion detection after AI conversation ends or on error
 */
STATIC VOID_T __md_resume_detection(VOID_T)
{
    s_md_frame_cnt = 0;
    s_md_photo_pending = FALSE;
    s_md_think_start_ms = 0;
    s_md_last_motion_ms = tal_system_get_millisecond();
    DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_DETECTION,
                             s_ai_detection.state, AI_CHAT_IDLE);
    tuya_ai_toy_camera_subscribe(CAM_STREAM_YUV422, CAM_CONSUMER_MD, __on_yuv_frame, NULL);
    TAL_PR_DEBUG("[ai_detection] detection resumed");
}

/**
 * @brief Handle text stream event in detection mode (UI display).
 */
STATIC VOID_T __md_text_stream(WUKONG_AI_EVENT_TYPE_E type, UCHAR_T *data)
{
    TUYA_CHECK_NULL_RETURN(data, OPRT_INVALID_PARM);

    WUKONG_AI_TEXT_T *text = (WUKONG_AI_TEXT_T *)data;
    TAL_PR_DEBUG("[ai_detection] text stream: %s", text->data);

    switch (type) {
    case WUKONG_AI_EVENT_TEXT_STREAM_START:
#ifdef ENABLE_TUYA_UI
        tuya_ai_display_msg((UINT8_T *)text->data, text->datalen, TY_DISPLAY_TP_AI_CHAT_START);
#endif
        break;
    case WUKONG_AI_EVENT_TEXT_STREAM_DATA:
#ifdef ENABLE_TUYA_UI
        tuya_ai_display_msg((UINT8_T *)text->data, text->datalen, TY_DISPLAY_TP_AI_CHAT_DATA);
#endif
        break;
    case WUKONG_AI_EVENT_TEXT_STREAM_STOP:
    case WUKONG_AI_EVENT_TEXT_STREAM_ABORT:
#ifdef ENABLE_TUYA_UI
        tuya_ai_display_msg(NULL, 0, TY_DISPLAY_TP_AI_CHAT_STOP);
#endif
        break;
    default:
        break;
    }
}

/**
 * @brief Handle emotion event in detection mode (UI display).
 */
STATIC VOID_T __md_emotion(WUKONG_AI_EVENT_TYPE_E type, UCHAR_T *data)
{
    TUYA_CHECK_NULL_RETURN(data, OPRT_INVALID_PARM);

    WUKONG_AI_EMO_T *emo = (WUKONG_AI_EMO_T *)data;
    TAL_PR_DEBUG("[ai_detection] emotion: %s", emo->name);
#ifdef ENABLE_TUYA_UI
    tuya_ai_display_msg((UINT8_T *)emo->name, strlen(emo->name), TY_DISPLAY_TP_EMOJI);
#endif
}

/**
 * @brief Handle accepted picture event in detection mode (UI display).
 */
STATIC VOID_T __md_image(CHAR_T *name)
{
    TAL_PR_INFO("[ai_detection] ACCEPT_PICTURE, name:%s", name ? name : "null");
#ifdef ENABLE_TUYA_UI
    tuya_ai_display_msg((UINT8_T *)name, strlen(name), TY_DISPLAY_TP_AI_IMAGE);
#endif
}

/**
 * @brief Extract Y channel from YUV422(UYVY) with downsampling
 */
STATIC VOID_T __extract_y_downsample(CONST UCHAR_T *yuv422, INT_T src_w, INT_T src_h,
                                     UCHAR_T *y_out, INT_T dst_w, INT_T dst_h)
{
    INT_T step_x = src_w / dst_w;
    INT_T step_y = src_h / dst_h;
    INT_T i, j;

    for (j = 0; j < dst_h; j++) {
        CONST UCHAR_T *row = yuv422 + (j * step_y) * src_w * 2;
        for (i = 0; i < dst_w; i++) {
            y_out[j * dst_w + i] = row[(i * step_x) * 2 + 1];
        }
    }
}

/**
 * @brief Initialize MD algorithm (idempotent, protected by s_md_inited)
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __md_algo_init(VOID_T)
{
    if (s_md_inited) {
        return OPRT_OK;
    }

    TUYA_MOTION_TRACKING_CFG_T md_cfg = {0};
    md_cfg.frame_w            = MD_DS_WIDTH;
    md_cfg.frame_h            = MD_DS_HEIGHT;
    md_cfg.y_thd              = MD_Y_THD;
    md_cfg.sensitivity        = MD_SENSITIVITY;
    md_cfg.rect_type          = SINGLE;
    md_cfg.roi.x_percent      = 0;
    md_cfg.roi.y_percent      = 0;
    md_cfg.roi.width_percent  = 100;
    md_cfg.roi.height_percent = 100;
    md_cfg.tracking_enable    = 0;

    OPERATE_RET rt = tuya_ipc_motion_init(md_cfg);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("[ai_detection] motion init failed, ret=%d", rt);
        return rt;
    }

    s_md_y_buf = (UCHAR_T *)tal_malloc(MD_DS_WIDTH * MD_DS_HEIGHT);
    if (s_md_y_buf == NULL) {
        TAL_PR_ERR("[ai_detection] y_buf malloc failed");
        tuya_ipc_motion_release();
        return OPRT_MALLOC_FAILED;
    }

    s_md_inited = TRUE;
    TAL_PR_DEBUG("[ai_detection] MD algo init ok, %dx%d", MD_DS_WIDTH, MD_DS_HEIGHT);
    return OPRT_OK;
}

/**
 * @brief Motion event handler — pause detection and set photo pending flag
 *
 * Called from camera frame callback context, so must NOT block.
 * Actual photo capture is deferred to the task callback thread.
 */
STATIC VOID_T __on_motion_detected(INT_T motion_flag, INT_T point_x, INT_T point_y)
{
    TAL_PR_DEBUG("[ai_detection] motion! flag=%d point=(%d,%d)",
                motion_flag, point_x, point_y);

    s_md_photo_pending = TRUE;
    tuya_ai_toy_camera_unsubscribe(CAM_STREAM_YUV422, CAM_CONSUMER_MD);
}

/**
 * @brief YUV frame callback registered to the camera adaptation layer
 */
STATIC VOID __on_yuv_frame(const CAM_FRAME_T *frame, VOID *ctx)
{
    (VOID)ctx;
    if (!frame || !s_md_inited || s_md_y_buf == NULL) {
        return;
    }

    if (s_md_frame_cnt < MD_SKIP_FRAMES) {
        s_md_frame_cnt++;
        return;
    }

    __extract_y_downsample(frame->data, frame->width, frame->height,
                           s_md_y_buf, MD_DS_WIDTH, MD_DS_HEIGHT);

    INT_T motion_flag = 0;
    TUYA_POINT_T motion_point = {0};
    OPERATE_RET ret = tuya_ipc_motion(s_md_y_buf, &motion_flag, &motion_point);

    /* Warmup: feed frames to update reference, ignore detection results */
    if (s_md_frame_cnt < MD_SKIP_FRAMES + MD_WARMUP_FRAMES) {
        s_md_frame_cnt++;
        return;
    }

    if (ret == OPRT_OK && motion_flag == 1) {
        UINT32_T now_ms = tal_system_get_millisecond();

        if (now_ms - s_md_last_motion_ms >= MD_COOLDOWN_MS) {
            s_md_last_motion_ms = now_ms;
            __on_motion_detected(motion_flag, motion_point.x, motion_point.y);
        }
    }
}

/**
 * @brief Initialize detection mode
 */
STATIC OPERATE_RET wukong_ai_detection_init_cb(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[ai_detection] init");

    wukong_kws_disable();
    wukong_audio_input_wakeup_mode_set(WUKONG_AUDIO_VAD_MANUAL);
    wukong_audio_input_wakeup_set(FALSE);

    s_md_photo_pending = FALSE;
    DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_DETECTION,
                             s_ai_detection.state, AI_CHAT_IDLE);

    __md_algo_init();
    tuya_ai_toy_camera_subscribe(CAM_STREAM_YUV422, CAM_CONSUMER_MD, __on_yuv_frame, NULL);
    return OPRT_OK;
}

/**
 * @brief Deinitialize detection mode
 */
STATIC OPERATE_RET wukong_ai_detection_deinit_cb(VOID *data, INT_T len)
{
    OPERATE_RET rt = OPRT_OK;
    TAL_PR_DEBUG("[ai_detection] deinit");

    tuya_ai_toy_camera_unsubscribe(CAM_STREAM_YUV422, CAM_CONSUMER_MD);

    /* Cancel any ongoing AI session */
    if (s_ai_detection.state == AI_CHAT_THINK || s_ai_detection.state == AI_CHAT_SPEAK) {
        wukong_audio_player_stop(AI_PLAYER_ALL);
        wukong_ai_agent_chat_break(NULL);
    }

    s_md_frame_cnt = 0;
    s_md_last_motion_ms = 0;
    s_md_photo_pending = FALSE;
    s_md_think_start_ms = 0;
    TUYA_CALL_ERR_LOG(wukong_audio_input_reset());
    return rt;
}

/**
 * @brief Key event callback for detection mode
 *
 * NORMAL_KEY: if AI is active (think/speak), cancel and resume detection;
 *             otherwise switch to chat mode.
 */
STATIC OPERATE_RET wukong_ai_detection_key_cb(VOID *data, INT_T len)
{
    PUSH_KEY_TYPE_E event = *(PUSH_KEY_TYPE_E *)data;
    TAL_PR_DEBUG("[ai_detection] key: %d", event);

    switch (event) {
    case NORMAL_KEY:
        if (s_ai_detection.state == AI_CHAT_THINK ||
            s_ai_detection.state == AI_CHAT_SPEAK) {
            /* Cancel ongoing AI conversation, resume detection */
            wukong_audio_player_stop(AI_PLAYER_ALL);
            wukong_ai_agent_chat_break(NULL);
            __md_resume_detection();
        } else {
            wukong_ai_device_mode_switch(AI_DEVICE_MODE_CHAT);
        }
        break;
    default:
        break;
    }
    return OPRT_OK;
}

/**
 * @brief Periodic task callback for detection mode
 *
 * Runs in the ai_toy_state thread. When photo_pending is set (by the
 * camera frame callback), performs the blocking photo capture + upload
 * sequence here where blocking is safe.
 */
STATIC OPERATE_RET wukong_ai_detection_task_cb(VOID *data, INT_T len)
{
    /* Timeout guard: resume detection if cloud doesn't respond in time */
    if (s_ai_detection.state == AI_CHAT_THINK && s_md_think_start_ms != 0) {
        UINT32_T elapsed = tal_system_get_millisecond() - s_md_think_start_ms;
        if (elapsed >= MD_THINK_TIMEOUT_MS) {
            TAL_PR_WARN("[ai_detection] cloud response timeout (%u ms), resuming detection", elapsed);
            s_md_think_start_ms = 0;
            // wukong_audio_player_stop(AI_PLAYER_ALL);
            // tuya_ai_agent_event(AI_EVENT_CHAT_BREAK, 0);
            __md_resume_detection();
            return OPRT_OK;
        }
    }

    if (!s_md_photo_pending || s_ai_detection.state != AI_CHAT_IDLE) {
        return OPRT_OK;
    }

    s_md_photo_pending = FALSE;

    /* State: IDLE → UPLOAD */
    DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_DETECTION,
                             s_ai_detection.state, AI_CHAT_UPLOAD);

    /* Capture one JPEG frame synchronously from the MJPEG stream (independent
     * of any UI/MCP photo path). The adapter fans out to the SNAPSHOT
     * consumer without disrupting other subscribers. */
    BYTE_T *image_data = NULL;
    UINT_T image_size = 0;
    OPERATE_RET rt = tuya_ai_toy_camera_snapshot(CAM_SNAP_JPEG, &image_data, &image_size, 5000);
    if (rt != OPRT_OK || image_data == NULL || image_size == 0) {
        TAL_PR_ERR("[ai_detection] take photo failed, rt=%d", rt);
        __md_resume_detection();
        return rt;
    }

    TAL_PR_INFO("[ai_detection] photo captured, size=%u, sending to cloud", image_size);

    /* Start AI input session, send image + text, then stop to trigger cloud processing */
    wukong_ai_agent_set_scene(AI_AGENT_SCODE_DETECTION);
    wukong_ai_agent_input_start(TRUE);

    rt = wukong_ai_agent_send_image((UINT8_T *)image_data, image_size);
    tal_free(image_data);

    if (rt != OPRT_OK) {
        TAL_PR_ERR("[ai_detection] send image failed, rt=%d", rt);
        wukong_ai_agent_input_stop();
        __md_resume_detection();
        return rt;
    }

    rt = wukong_ai_agent_send_text("帮我检测下这张图片");
    if (rt != OPRT_OK) {
        TAL_PR_ERR("[ai_detection] send text failed, rt=%d", rt);
        wukong_ai_agent_input_stop();
        __md_resume_detection();
        return rt;
    }

    wukong_ai_agent_input_stop();

    /* State: UPLOAD → THINK — wait for TTS events via event_cb */
    DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_DETECTION,
                             s_ai_detection.state, AI_CHAT_THINK);
    s_md_think_start_ms = tal_system_get_millisecond();
    return OPRT_OK;
}

/**
 * @brief Event callback for detection mode
 *
 * Handles AI agent events during the photo→cloud→TTS cycle.
 * Modeled after wukong_ai_hold_event_cb in hold mode.
 */
STATIC OPERATE_RET wukong_ai_detection_event_cb(VOID *data, INT_T len)
{
    TUYA_CHECK_NULL_RETURN(data, OPRT_INVALID_PARM);
    WUKONG_AI_EVENT_T *event = (WUKONG_AI_EVENT_T *)data;

    TAL_PR_DEBUG("[ai_detection] event type: %d, state: %d", event->type, s_ai_detection.state);

    switch (event->type) {
    /* Cloud responded, TTS about to start — cancel timeout */
    case WUKONG_AI_EVENT_TTS_PRE:
        if (s_ai_detection.state != AI_CHAT_THINK) {
            TAL_PR_WARN("[ai_detection] stale TTS_PRE ignored, state=%d", s_ai_detection.state);
            break;
        }
        s_md_think_start_ms = 0;
        DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_DETECTION,
                                 s_ai_detection.state, AI_CHAT_SPEAK);
        break;

    /* TTS stream data — handled by audio player, nothing to do */
    case WUKONG_AI_EVENT_TTS_START:
    case WUKONG_AI_EVENT_TTS_DATA:
    case WUKONG_AI_EVENT_TTS_STOP:
        break;

    /* Text stream events — display AI response text on UI */
    case WUKONG_AI_EVENT_TEXT_STREAM_START:
    case WUKONG_AI_EVENT_TEXT_STREAM_DATA:
    case WUKONG_AI_EVENT_TEXT_STREAM_STOP:
    case WUKONG_AI_EVENT_TEXT_STREAM_ABORT:
        __md_text_stream(event->type, event->data);
        break;

    /* Emotion events — display emoji on UI */
    case WUKONG_AI_EVENT_EMOTION:
    case WUKONG_AI_EVENT_LLM_EMOTION:
        __md_emotion(event->type, event->data);
        break;

    /* Cloud-generated image accepted and saved to album */
    case WUKONG_AI_EVENT_ACCEPT_PICTURE:
        __md_image((CHAR_T *)event->data);
        break;

    /* Picture transfer complete — clear attachment indicator */
    case WUKONG_AI_EVENT_SEND_PICTURE_END:
#ifdef ENABLE_TUYA_UI
        tuya_ai_display_msg(NULL, 0, TY_DISPLAY_TP_CLEAR_ATTACHMENT);
#endif
        break;

    /* Playback finished — resume detection */
    case WUKONG_AI_EVENT_PLAY_CTL_END:
    case WUKONG_AI_EVENT_PLAY_END:
        if (s_ai_detection.state != AI_CHAT_SPEAK) {
            TAL_PR_DEBUG("[ai_detection] stale PLAY_END ignored, state=%d", s_ai_detection.state);
            break;
        }
        TAL_PR_INFO("[ai_detection] playback ended, resuming detection");
        __md_resume_detection();
        break;

    /* Error / abort cases — resume detection */
    case WUKONG_AI_EVENT_ASR_EMPTY:
    case WUKONG_AI_EVENT_ASR_ERROR:
    case WUKONG_AI_EVENT_TTS_ABORT:
    case WUKONG_AI_EVENT_TTS_ERROR:
        if (s_ai_detection.state == AI_CHAT_IDLE) {
            TAL_PR_DEBUG("[ai_detection] stale error event=%d ignored", event->type);
            break;
        }
        TAL_PR_WARN("[ai_detection] AI error (event=%d), resuming detection", event->type);
        __md_resume_detection();
        break;

    /* Playback control events */
    case WUKONG_AI_EVENT_PLAY_CTL_RESUME:
        wukong_audio_player_resume();
        break;
    case WUKONG_AI_EVENT_PLAY_CTL_PAUSE:
        wukong_audio_player_pause();
        break;

    /* Alert sound */
    case WUKONG_AI_EVENT_PLAY_ALERT:
        wukong_audio_player_alert((TY_AI_TOY_ALERT_TYPE_E)event->data, TRUE);
        break;

    default:
        break;
    }

    return OPRT_OK;
}

/**
 * @brief KWS wakeup callback for detection mode
 */
STATIC OPERATE_RET wukong_ai_detection_wakeup_cb(VOID *data, INT_T len)
{
    return OPRT_OK;
}

/**
 * @brief VAD callback for detection mode
 */
STATIC OPERATE_RET wukong_ai_detection_vad_cb(VOID *data, INT_T len)
{
    return OPRT_OK;
}

/**
 * @brief Client run callback for detection mode
 */
STATIC OPERATE_RET wukong_ai_detection_client_cb(VOID *data, INT_T len)
{
    return OPRT_OK;
}

/**
 * @brief Idle notification callback for detection mode
 */
STATIC OPERATE_RET wukong_ai_detection_notify_idle_cb(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[ai_detection] notify idle");
    return OPRT_OK;
}

/**
 * @brief Register detection mode callbacks
 */
OPERATE_RET ai_detection_register(AI_CHAT_MODE_HANDLE_T **cb)
{
    s_ai_detection_cb.on_init        = wukong_ai_detection_init_cb;
    s_ai_detection_cb.on_deinit      = wukong_ai_detection_deinit_cb;
    s_ai_detection_cb.on_key         = wukong_ai_detection_key_cb;
    s_ai_detection_cb.on_task        = wukong_ai_detection_task_cb;
    s_ai_detection_cb.on_event       = wukong_ai_detection_event_cb;
    s_ai_detection_cb.on_wakeup      = wukong_ai_detection_wakeup_cb;
    s_ai_detection_cb.on_vad         = wukong_ai_detection_vad_cb;
    s_ai_detection_cb.on_client      = wukong_ai_detection_client_cb;
    s_ai_detection_cb.on_notify_idle = wukong_ai_detection_notify_idle_cb;
    s_ai_detection_cb.on_audio_input = NULL;
    *cb = &s_ai_detection_cb;
    return OPRT_OK;
}

#endif
