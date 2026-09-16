/**
 * @file tuya_p2p_tmm_voip.c
 * @brief TMM VoIP adapter for tuyaos-ai (device-device & app-device calls).
 *
 * 移植自 ai_video_voice_toy 并按 tuyaos-ai 约定重构(docs/adr/0002,0003):
 *  - AI 与通话「共存」:接通→切 P2P 模式(其 on_init 关 KWS/置手动 VAD),
 *    挂断→切回 CHAT(其 on_init 重开 KWS)。不再逐项 suspend/resume。
 *  - 视频「设备→APP 单向」(ADR 0005 首选路径 / ADR 0007 已落地):is_support_video 随相机能力开启,
 *    不收视频(video_output_cb=NULL);对端(APP)拉流触发 VIDEO_START→订阅相机 H264→
 *    tuya_tmm_stream_video_send 上送。设备↔设备为音频通话,不触发视频。
 *
 * @copyright Copyright (c) Tuya Inc.
 */
#include "tuya_p2p_tmm_voip.h"
#include "tuya_app_config.h"

#if defined(ENABLE_AI_MODE_P2P) && (ENABLE_AI_MODE_P2P == 1)

#include <string.h>

#include "uni_log.h"
#include "tal_mutex.h"
#include "tal_system.h"
#include "tal_time_service.h"
#include "tal_workq_service.h"
#include "base_event.h"
#include "tuya_svc_netmgr.h"
#include "tuya_tmm_manager.h"
#include "tuya_tmm_stream.h"
#include "tuya_g711_utils.h"         /* tuya_g711_encode(上行 G711,与原方案一致) */
#include "wukong_ai_mode.h"
#include "wukong_ai_agent.h"
#include "wukong_audio_player.h"
#include "wukong_audio_input.h"
#include "wukong_audio_output.h"   /* P2P 下行走 TAL,由其管 PA/音量(对齐 tuya_p2p_app.c) */

/* 视频发送(设备→APP,H264):随相机能力开启,已适配 camera-P2P 解耦后
 * 的 wukong_video_service API(视频帧订阅替代旧 tuya_ai_toy_camera H264 回调)。
 * DVP 硬件模式切换(H264+YUV ↔ JPEG+YUV)由 video_input_camera 内部处理。 */
#if defined(ENABLE_TUYA_CAMERA) && (ENABLE_TUYA_CAMERA == 1)
#define TUYA_TOY_TMM_VOIP_VIDEO   1
#include "wukong_video_service.h"    /* wukong_video_{subscribe,unsubscribe}, VIDEO_FMT_H264 */
#include "tuya_ipc_media_stream.h"   /* tuya_ipc_get_client_online_num — 喂帧门控 */
/* P2P SDK 单调毫秒时钟(mid_p2p 内部头未对外,沿用本文件 resample 的 extern 做法) */
extern uint64_t tuya_p2p_misc_get_current_time_ms(void);
#else
#define TUYA_TOY_TMM_VOIP_VIDEO   0
#endif

/* resample helpers 位于 audio_player;沿用 tuya_p2p_app.c 的做法用 extern 声明,
 * 避免为 p2p 目录额外挂 include 路径。 */
extern int resample_to_8k_fixed(const int16_t *in, size_t in_frames, int in_rate, int channels,
                                int16_t *out_buf_out, size_t *out_frames_out);
extern int resample_to_16k_fixed(const int16_t *in, size_t in_frames, int in_rate, int channels,
                                 int16_t *out_buf_out, size_t *out_frames_out);

#define TUYA_TOY_TMM_FEED_PACK_LEN     1280
#define TUYA_TOY_TMM_PCM_8K_BUF_LEN    2048
#define TUYA_TOY_TMM_PCM_16K_BUF_LEN   4096

typedef struct {
    BOOL_T                       inited;
    BOOL_T                       started;
    BOOL_T                       in_call_mode;   /* 是否已切入 P2P 通话模式 */
    BOOL_T                       video_sending;  /* 是否正在发送相机 H264(设备→APP) */
    TUYA_TOY_TMM_VOIP_STATUS_E   status;
    MUTEX_HANDLE                 lock;
} TUYA_TOY_TMM_VOIP_CTX_T;

STATIC TUYA_TOY_TMM_VOIP_CTX_T s_voip = {0};

#if TUYA_TOY_TMM_VOIP_VIDEO
/* 前向声明:__voip_leave_call 早于视频函数定义即需调用 __voip_video_send_stop */
STATIC VOID __voip_video_send_start(VOID);
STATIC VOID __voip_video_send_stop(VOID);
#endif

STATIC VOID __voip_set_status(TUYA_TOY_TMM_VOIP_STATUS_E status)
{
    tal_mutex_lock(s_voip.lock);
    s_voip.status = status;
    tal_mutex_unlock(s_voip.lock);
}

/** @brief 发布 VoIP UI 更新事件(状态 + 对端信息),供 UI 层订阅 */
STATIC VOID __voip_publish_ui(VOID)
{
    TUYA_TOY_VOIP_UI_EVT_T ui_evt = {0};

    ui_evt.status = tuya_toy_tmm_voip_get_status();
    tuya_tmm_manager_get_peer_display(ui_evt.peer_name, sizeof(ui_evt.peer_name),
                                      ui_evt.peer_id, sizeof(ui_evt.peer_id));
    ty_publish_event(EVENT_TOY_VOIP_UI, &ui_evt);
}

/**
 * @brief 进入通话:打断 AI + 切到 P2P 模式(ADR 0003)。
 * P2P 模式 on_init 负责关 KWS / 置手动 VAD,故此处不再逐项 suspend。
 */
STATIC VOID __voip_enter_call(VOID)
{
    if (s_voip.in_call_mode) {
        return;
    }

    wukong_audio_player_stop(AI_PLAYER_ALL);
    wukong_ai_agent_chat_break(NULL);

    if (wukong_ai_device_mode_switch(AI_DEVICE_MODE_P2P) != OPRT_OK) {
        TAL_PR_ERR("tuya_toy_tmm_voip enter p2p mode failed");
        return;
    }

    /* 下行外放走 wukong_audio_output(TAL,管 PA/音量)。上面 player_stop 已触发
     * svc_ai_player consumer.stop 把 output(含 PA)关闭,须重新 start 开 PA+DAC,
     * 否则下行无声;音量沿用全局 TAL 音量。挂断时 __voip_leave_call 里 stop。
     * 对齐 tuya_p2p_app.c 的 SPEAKER_START/STOP 用法。 */
    wukong_audio_output_start_owned(WUKONG_AUDIO_OUTPUT_OWNER_P2P);
    wukong_audio_input_start();

    /* 视频(设备→APP 单向 H264)由 APP 拉流触发 VIDEO_START→__voip_video_send_start,
     * 不由 enter_call 主动启动(避免非通话直连预览误推 Call UI,见 ADR 0005/0007)。 */

    s_voip.in_call_mode = TRUE;
    TAL_PR_NOTICE("tuya_toy_tmm_voip enter call, switched to P2P mode, heap=%d",
                  tal_system_get_free_heap_size());
}

/**
 * @brief 结束通话:切回 CHAT 模式恢复 AI(其 on_init 重开 KWS)。
 * VoIP-only 构建下保持 P2P 模式、不恢复 AI。
 */
STATIC VOID __voip_leave_call(VOID)
{
    if (!s_voip.in_call_mode) {
        return;
    }

#if TUYA_TOY_TMM_VOIP_VIDEO
    /* 兜底:挂断可能先于 VIDEO_STOP 到达,确保相机 H264 退订、模式复位 */
    __voip_video_send_stop();
#endif

    wukong_audio_player_stop(AI_PLAYER_ALL);
    wukong_ai_agent_chat_break(NULL);

    /* 关闭通话期间 start 的下行 output(关 PA+DAC)。此时 player 早已停,不会再触发
     * svc_ai_player 的 consumer.stop,故需显式关;下次 TTS 由 consumer.start 重开。
     * 对齐 tuya_p2p_app.c 的 SPEAKER_STOP。 */
    wukong_audio_output_stop_owned(WUKONG_AUDIO_OUTPUT_OWNER_P2P);

#if defined(ENABLE_TUYA_TOY_VOIP_ONLY) && (ENABLE_TUYA_TOY_VOIP_ONLY == 1)
    TAL_PR_NOTICE("tuya_toy_tmm_voip leave call (voip-only: stay in P2P mode)");
#else
    if (wukong_ai_device_mode_switch(AI_DEVICE_MODE_CHAT) != OPRT_OK) {
        TAL_PR_ERR("tuya_toy_tmm_voip leave: switch to CHAT failed");
    }
#endif

    s_voip.in_call_mode = FALSE;
    TAL_PR_NOTICE("tuya_toy_tmm_voip leave call");
}

/** @brief 下行音频回调:对端 8K(或采样率变化)PCM → 重采样 16K → wukong_audio_output 播放。
 *  走 TAL 层(与 TTS 同源 tal_audio_output),PA/音量由 wukong_audio_output 统一管理;
 *  output 通断由 __voip_enter/leave_call 的 start/stop 控制。对齐 tuya_p2p_app.c。 */
STATIC OPERATE_RET __voip_audio_output_cb(TUYA_TMM_STREAM_AUDIO_DATA_S *in)
{
    STATIC INT16_T pcm_16k[TUYA_TOY_TMM_PCM_16K_BUF_LEN / 2] = {0};
    size_t out_frames = 0;
    INT_T sample;
    OPERATE_RET rt;

    if (in == NULL || in->p_data == NULL || in->data_len == 0) {
        return OPRT_INVALID_PARM;
    }

    sample = (INT_T)in->user_data.audio_sample;
    if (sample <= 0) {
        sample = 8000;
    }

    if (sample == 16000) {
        rt = wukong_audio_output_write_owned(WUKONG_AUDIO_OUTPUT_OWNER_P2P,
                                             (UINT8_T *)in->p_data,
                                             in->data_len);
    } else {
        size_t in_frames = in->data_len / 2;
        /* pcm_16k 容量 = 2048 samples。上采样(如 8k→16k)输出 ≈ 输入×2,
         * 对端 P2P 网络帧长度不可信,必须在调用前钳制避免栈缓冲区溢出 */
        size_t max_out = sizeof(pcm_16k) / sizeof(pcm_16k[0]);
        if (in_frames * 16000 / (size_t)sample > max_out) {
            in_frames = max_out * (size_t)sample / 16000;
        }
        if (resample_to_16k_fixed((const int16_t *)in->p_data, in_frames, sample, 1,
                                  pcm_16k, &out_frames) != 0) {
            rt = OPRT_COM_ERROR;
        } else {
            rt = wukong_audio_output_write_owned(WUKONG_AUDIO_OUTPUT_OWNER_P2P,
                                                 (UINT8_T *)pcm_16k,
                                                 (UINT_T)(out_frames * 2));
        }
    }

    return rt;
}

#if TUYA_TOY_TMM_VOIP_VIDEO
/**
 * @brief H264 视频帧回调:经 wukong_video_service 订阅,把编码帧喂入
 *        tuya_tmm_stream 视频发送环形缓冲(设备→APP)。
 *        底层 ref-count 由 service 管理,退订即停。仅发送态且有在线客户端时喂帧。
 */
STATIC VOID __voip_h264_cb(const VIDEO_FRAME_T *frame, VOID *ctx)
{
    TUYA_TMM_STREAM_VIDEO_DATA_S vd = {0};

    (VOID)ctx;
    if (frame == NULL || frame->data == NULL || frame->length == 0) {
        return;
    }
    /* 对齐原方案 __p2p_h264_cb:客户端全部断线即丢帧,防无人拉流时环形缓冲积压 */
    if (!s_voip.video_sending || tuya_ipc_get_client_online_num() <= 0) {
        return;
    }

    vd.p_data = frame->data;
    vd.data_len = frame->length;
    /* pts 用 P2P SDK 单调时钟(对齐原方案);posix 墙钟 NTP 校时跳变会破坏连续性 */
    vd.user_data.timestamp = tuya_p2p_misc_get_current_time_ms();
    tuya_tmm_stream_video_send(&vd, frame->is_key_frame);
}

/** @brief 对端(APP)请求拉视频:经 wukong_video_service 订阅 H264 帧,开始发送。
 *  DVP 硬件模式切换(H264+YUV ↔ JPEG+YUV)由 video_input_camera 内部处理。 */
STATIC VOID __voip_video_send_start(VOID)
{
    if (s_voip.video_sending) {
        return;
    }
    if (wukong_video_subscribe(VIDEO_FMT_H264, VIDEO_CONSUMER_P2P,
                               __voip_h264_cb, NULL) != OPRT_OK) {
        TAL_PR_ERR("tuya_toy_tmm_voip subscribe H264 failed");
        return;
    }
    s_voip.video_sending = TRUE;
    TAL_PR_NOTICE("tuya_toy_tmm_voip video send start (device->APP)");
}

/** @brief 对端停止拉视频 / 通话结束:退订 H264(DVP 模式由 video_input_camera 内部复位)。 */
STATIC VOID __voip_video_send_stop(VOID)
{
    if (!s_voip.video_sending) {
        return;
    }
    wukong_video_unsubscribe(VIDEO_FMT_H264, VIDEO_CONSUMER_P2P);
    s_voip.video_sending = FALSE;
    TAL_PR_NOTICE("tuya_toy_tmm_voip video send stop");
}
#endif /* TUYA_TOY_TMM_VOIP_VIDEO */

/** @brief TMM manager 通话事件回调:驱动模式切换、状态与 UI */
STATIC OPERATE_RET __voip_manager_event_cb(TUYA_TMM_MANAGER_EVENT_E event, VOID *data, INT_T len)
{
    (VOID)data;
    (VOID)len;

    switch (event) {
    case TUYA_TMM_MANAGER_EVT_INCOMING:
        __voip_set_status(TUYA_TOY_TMM_VOIP_STATUS_INCOMING);
        /* 打断 AI,呈现来电(INCOMING);是否接听交由 UI 层决定
         * (用户点接听 / 或按自动接听设置)——不在此硬编码自动接听。 */
        wukong_audio_player_stop(AI_PLAYER_ALL);
        wukong_ai_agent_chat_break(NULL);
        break;

    case TUYA_TMM_MANAGER_EVT_ACCEPTED:
        /* 接通即进通话态(切 P2P 模式 + 送麦克风),不等 AUDIO_START。
         * 设备↔设备两端都等对方来拉流会死锁,故主叫收到 ACCEPTED 立即开送。 */
        __voip_enter_call();
        __voip_set_status(TUYA_TOY_TMM_VOIP_STATUS_INCALL);
        break;

    /* 以下媒体流转发事件的处理与原方案(tuya_p2p_app.c __tuya_ipc_p2p_event_cb
     * 对 MEDIA_STREAM_EVENT_E 的处理)保持一致:AUDIO/VIDEO 的 START 进通话、
     * STOP 退通话,SPEAKER(PLAY) 仅记录。 */
    case TUYA_TMM_MANAGER_EVT_AUDIO_START:
        /* ≙ MEDIA_STREAM_LIVE_AUDIO_START:打断 AI,切 P2P 模式。
         * 幂等:已在通话态则 __voip_enter_call 直接返回。 */
        __voip_enter_call();
        __voip_set_status(TUYA_TOY_TMM_VOIP_STATUS_INCALL);
        break;

    case TUYA_TMM_MANAGER_EVT_AUDIO_STOP:
        /* ≙ MEDIA_STREAM_LIVE_AUDIO_STOP:打断 AI,切回 CHAT 模式 */
        __voip_leave_call();
        __voip_set_status(TUYA_TOY_TMM_VOIP_STATUS_IDLE);
        break;

#if TUYA_TOY_TMM_VOIP_VIDEO
    /* VIDEO_START/STOP 不驱动通话状态与 UI(对齐原方案:仅 AUDIO/呼叫信令驱动)。
     * APP 直连预览(非 RTC 通话)也走这两个事件——若置 INCALL/publish 会把呼叫页
     * 误推前台;TMM 通话中拉视频时通话态已由 ACCEPTED/AUDIO_START 建立,无需重复。
     * 故仅做模式切换+视频起停,early return 跳过末尾的 __voip_publish_ui。 */
    case TUYA_TMM_MANAGER_EVT_VIDEO_START:
        /* ≙ MEDIA_STREAM_LIVE_VIDEO_START:切 P2P 模式 + 订阅相机 H264 开始发送 */
        __voip_enter_call();
        __voip_video_send_start();
        return OPRT_OK;

    case TUYA_TMM_MANAGER_EVT_VIDEO_STOP:
        /* ≙ MEDIA_STREAM_LIVE_VIDEO_STOP:停发相机 H264 + 切回 CHAT 模式 */
        __voip_video_send_stop();
        __voip_leave_call();
        return OPRT_OK;
#endif

    case TUYA_TMM_MANAGER_EVT_PLAY_START:
    case TUYA_TMM_MANAGER_EVT_PLAY_STOP:
        /* ≙ MEDIA_STREAM_SPEAKER_START/STOP:原方案仅打日志,不动通话状态 */
        TAL_PR_DEBUG("tuya_toy_tmm_voip speaker event %d", event);
        break;

    case TUYA_TMM_MANAGER_EVT_REJECT:
    case TUYA_TMM_MANAGER_EVT_UNANSWERED:
    case TUYA_TMM_MANAGER_EVT_CANCEL:
    case TUYA_TMM_MANAGER_EVT_HANGUP:
    case TUYA_TMM_MANAGER_EVT_BUSY:
    case TUYA_TMM_MANAGER_EVT_STOP:
    case TUYA_TMM_MANAGER_EVT_ERROR:
        __voip_leave_call();
        __voip_set_status(TUYA_TOY_TMM_VOIP_STATUS_IDLE);
        break;

    default:
        break;
    }

    __voip_publish_ui();
    return OPRT_OK;
}

OPERATE_RET tuya_toy_tmm_voip_init(VOID)
{
    TUYA_TMM_MANAGER_INIT_S init_param = {0};

    if (s_voip.inited) {
        return OPRT_OK;
    }

    if (tal_mutex_create_init(&s_voip.lock) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }

    init_param.event_cb = __voip_manager_event_cb;
    init_param.stream_init_param.is_support_audio = TRUE;
    init_param.stream_init_param.audio_output_cb   = __voip_audio_output_cb;
    /* 视频「设备→APP」单向发送(ADR 0005 首选路径已落地,见 ADR 0007):
     * tuya_tmm_stream 已加视频发送数据线,相机 H264 帧经 __voip_h264_cb→
     * tuya_tmm_stream_video_send 上送。设备不收视频(video_output_cb=NULL)。
     * 无相机则退化为音频-only(TUYA_TOY_TMM_VOIP_VIDEO=0)。 */
    init_param.stream_init_param.is_support_video = (TUYA_TOY_TMM_VOIP_VIDEO ? TRUE : FALSE);
    init_param.stream_init_param.video_output_cb  = NULL;

    if (tuya_tmm_manager_init(&init_param) != OPRT_OK) {
        TAL_PR_ERR("tuya_tmm_manager_init failed");
        return OPRT_COM_ERROR;
    }

    s_voip.inited = TRUE;
    s_voip.status = TUYA_TOY_TMM_VOIP_STATUS_IDLE;
    TAL_PR_NOTICE("tuya_toy_tmm_voip_init ok");
    return OPRT_OK;
}

OPERATE_RET tuya_toy_tmm_voip_call(CHAR_T *target_id)
{
    if (target_id == NULL || target_id[0] == '\0') {
        return OPRT_INVALID_PARM;
    }

    if (tuya_toy_tmm_voip_get_status() != TUYA_TOY_TMM_VOIP_STATUS_IDLE) {
        TAL_PR_ERR("voip call: not idle");
        return OPRT_COM_ERROR;
    }

    if (tuya_tmm_manager_call(target_id, TUYA_TMM_MANAGER_CALL_TYPE_AUDIO) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }

    __voip_set_status(TUYA_TOY_TMM_VOIP_STATUS_CALLING);
    __voip_publish_ui();
    TAL_PR_NOTICE("tuya_toy_tmm_voip call target=%s", target_id);
    return OPRT_OK;
}

OPERATE_RET tuya_toy_tmm_voip_answer(VOID)
{
    if (tuya_tmm_manager_answer() != OPRT_OK) {
        __voip_leave_call();
        __voip_set_status(TUYA_TOY_TMM_VOIP_STATUS_IDLE);
        __voip_publish_ui();
        return OPRT_COM_ERROR;
    }

    /* 被叫接听即进通话态(切 P2P 模式 + 送麦克风),同样不等 AUDIO_START。 */
    __voip_enter_call();
    __voip_set_status(TUYA_TOY_TMM_VOIP_STATUS_INCALL);
    __voip_publish_ui();
    return OPRT_OK;
}


OPERATE_RET tuya_toy_tmm_voip_hangup(VOID)
{
    OPERATE_RET rt = tuya_tmm_manager_hangup();
    __voip_leave_call();
    __voip_set_status(TUYA_TOY_TMM_VOIP_STATUS_IDLE);
    __voip_publish_ui();
    return rt;
}

TUYA_TOY_TMM_VOIP_STATUS_E tuya_toy_tmm_voip_get_status(VOID)
{
    TUYA_TOY_TMM_VOIP_STATUS_E status;

    tal_mutex_lock(s_voip.lock);
    status = s_voip.status;
    tal_mutex_unlock(s_voip.lock);
    return status;
}

BOOL_T tuya_toy_tmm_voip_is_incall(VOID)
{
    return (tuya_toy_tmm_voip_get_status() == TUYA_TOY_TMM_VOIP_STATUS_INCALL) ? TRUE : FALSE;
}

BOOL_T tuya_toy_tmm_voip_is_voip_only(VOID)
{
#if defined(ENABLE_TUYA_TOY_VOIP_ONLY) && (ENABLE_TUYA_TOY_VOIP_ONLY == 1)
    return TRUE;
#else
    return FALSE;
#endif
}

/**
 * @brief 上行:16K 单声道 PCM → 分包重采样 8K → G.711 μ-law 编码 → 逐帧即时直送 stream。
 *
 * 与原方案(tuya_p2p_app.c 的 tuya_ipc_app_audio_frame_put)一致:上行 G711、逐帧即时送,
 * **不经 manager 的 jitter 缓冲与 40ms 匀速线程**(那套是为设备↔设备平滑加的,会抬高延迟)。
 * 与视频上行(__voip_h264_cb→tuya_tmm_stream_video_send)对称,直写 stream 环形缓冲。
 */
OPERATE_RET tuya_toy_tmm_voip_audio_feed(VOID *data, INT_T len)
{
    INT_T offset = 0;
    INT_T left = len;
    STATIC INT16_T pcm_8k[TUYA_TOY_TMM_PCM_8K_BUF_LEN / 2] = {0};
    STATIC UCHAR_T g711_buf[TUYA_TOY_TMM_PCM_8K_BUF_LEN / 2] = {0};
    OPERATE_RET rt = OPRT_OK;

    if (!tuya_toy_tmm_voip_is_incall() || data == NULL || len <= 0) {
        return OPRT_OK;
    }

    while (left > 0) {
        INT_T chunk = (left >= TUYA_TOY_TMM_FEED_PACK_LEN) ? TUYA_TOY_TMM_FEED_PACK_LEN : left;
        size_t in_frames = (size_t)chunk / 2;
        size_t out_frames = 0;
        size_t g711_bytes = 0;
        TUYA_TMM_STREAM_AUDIO_DATA_S ad = {0};

        /* 16K PCM → 8K PCM */
        if (resample_to_8k_fixed((const int16_t *)((CHAR_T *)data + offset), in_frames, 16000, 1,
                                 pcm_8k, &out_frames) != 0) {
            rt = OPRT_COM_ERROR;
            break;
        }

        /* 8K 16bit PCM → G.711 μ-law(8-bit) */
        if (tuya_g711_encode(TUYA_G711_MU_LAW, (UCHAR_T *)pcm_8k, out_frames * 2,
                             g711_buf, &g711_bytes) != 0 || g711_bytes == 0) {
            rt = OPRT_COM_ERROR;
            break;
        }

        /* 即时直送 stream 环形缓冲(绕过 manager jitter/40ms 线程),编码 G711U */
        ad.p_data = g711_buf;
        ad.data_len = (UINT_T)g711_bytes;
        ad.user_data.audio_sample = 8000;
        ad.user_data.audio_databits = 8;
        ad.user_data.audio_channel = 1;
        ad.user_data.timestamp = (UINT64_T)tal_time_get_posix_ms();
        snprintf(ad.user_data.audio_codec, sizeof(ad.user_data.audio_codec), "g711u");
        tuya_tmm_stream_audio_send(&ad);

        offset += chunk;
        left -= chunk;
    }

    return rt;
}

STATIC OPERATE_RET __voip_init_cb(VOID *data)
{
    (VOID)data;
    return tuya_toy_tmm_voip_init();
}

STATIC OPERATE_RET __voip_mqtt_ready_cb(VOID *data)
{
    (VOID)data;
    if (s_voip.started) {
        return OPRT_OK;
    }
    s_voip.started = TRUE;
    TAL_PR_NOTICE("tuya_toy_tmm_voip mqtt ready");
    if (!s_voip.inited && tuya_svc_netmgr_linkage_is_up(LINKAGE_TYPE_DEFAULT)) {
        return tuya_toy_tmm_voip_init();
    }
    return OPRT_OK;
}

OPERATE_RET tuya_toy_tmm_voip_start(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    TAL_PR_NOTICE("tuya_toy_tmm_voip_start");
    ty_subscribe_event(EVENT_LINK_UP, "tmm_voip", __voip_init_cb, SUBSCRIBE_TYPE_NORMAL);
    ty_subscribe_event(EVENT_MQTT_CONNECTED, "tmm_voip", __voip_mqtt_ready_cb, SUBSCRIBE_TYPE_ONETIME);

    if (tuya_svc_netmgr_linkage_is_up(LINKAGE_TYPE_DEFAULT)) {
        rt = __voip_init_cb(NULL);
    }

    return rt;
}

#else /* ENABLE_AI_MODE_P2P */

OPERATE_RET tuya_toy_tmm_voip_start(VOID) { return OPRT_OK; }
OPERATE_RET tuya_toy_tmm_voip_init(VOID) { return OPRT_OK; }
OPERATE_RET tuya_toy_tmm_voip_call(CHAR_T *target_id) { (VOID)target_id; return OPRT_NOT_SUPPORTED; }
OPERATE_RET tuya_toy_tmm_voip_answer(VOID) { return OPRT_NOT_SUPPORTED; }
OPERATE_RET tuya_toy_tmm_voip_hangup(VOID) { return OPRT_NOT_SUPPORTED; }
TUYA_TOY_TMM_VOIP_STATUS_E tuya_toy_tmm_voip_get_status(VOID) { return TUYA_TOY_TMM_VOIP_STATUS_IDLE; }
OPERATE_RET tuya_toy_tmm_voip_audio_feed(VOID *data, INT_T len) { (VOID)data; (VOID)len; return OPRT_OK; }
BOOL_T tuya_toy_tmm_voip_is_incall(VOID) { return FALSE; }
BOOL_T tuya_toy_tmm_voip_is_voip_only(VOID) { return FALSE; }

#endif /* ENABLE_AI_MODE_P2P */
