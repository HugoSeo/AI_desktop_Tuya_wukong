#include "tuya_cloud_types.h"
#include "tuya_iot_config.h"
#include "tuya_ai_toy_camera.h"
#include "uni_log.h"

#if defined(ENABLE_MQTT_P2P) && (ENABLE_MQTT_P2P == 1)
#include "tkl_thread.h"
#include "tkl_audio.h"
#include "tuya_ai_toy.h"
#include "wukong_ai_mode.h"
#include "tuya_p2p_sdk.h"
#include "tuya_g711_utils.h"
#include "tuya_svc_netmgr.h"
#include "wukong_audio_input.h"
#include "wukong_video_input.h"
#include "tuya_ipc_media_stream_event.h"
#include "tuya_sdk_call.h"

STATIC THREAD_HANDLE                   ty_ipc_thread = NULL;

/* H264 subscriber: pushes frames to the P2P SDK. Subscribing also
 * ref-counts the underlying hardware H264 stream on; unsubscribing stops it. */
STATIC VOID __p2p_h264_cb(const CAM_FRAME_T *frame, VOID *ctx)
{
    (VOID)ctx;
    if (!frame || tuya_ipc_get_client_online_num() <= 0) {
        return;
    }
    MEDIA_FRAME_T mf = {
        .type      = frame->is_i_frame ? E_VIDEO_I_FRAME : E_VIDEO_PB_FRAME,
        .p_buf     = frame->data,
        .size      = frame->length,
        .pts       = tuya_p2p_misc_get_current_time_ms(),
        .timestamp = tuya_p2p_misc_get_current_time_ms(),
    };
    tuya_p2p_sdk_put_video_frame(&mf);
}

int resample_to_8k_fixed(const int16_t *in, size_t in_frames, int in_rate, int channels,
                         int16_t *out_buf_out, size_t *out_frames_out);
// UCHAR_T tmp_pcm[800] = {0};
// UCHAR_T g711_buf[400] = {0};
UCHAR_T tmp_pcm[1600] = {0};
UCHAR_T g711_buf[1600] = {0};
INT_T tuya_ipc_app_audio_frame_put(VOID *data, INT_T len)
{
    if(tuya_ipc_get_client_online_num() <= 0)
    {
        TAL_PR_ERR("No p2p client");
        return ;
    }
    
    // Audio buffer size calculation:
    // PCM @ 16kHz, 16bit -> input ~40ms frame = 640 samples = 1280 bytes
    // After resample to 8kHz: 320 samples = 640 bytes (with 20% margin = 800 bytes)
    // After G.711 encode: 320 bytes (with 20% margin = 400 bytes)
    
    MEDIA_FRAME_T media_frame = {0};
    media_frame.type = E_AUDIO_FRAME;
    media_frame.p_buf = (BYTE_T *)data;
    media_frame.size = (UINT_T)len;
    media_frame.pts = tuya_p2p_misc_get_current_time_ms();
    media_frame.timestamp = tuya_p2p_misc_get_current_time_ms();

    // 先对输入PCM进行重采样到8k（假设输入为16位PCM，采样率16k）
    size_t out_frames = 0;
    size_t in_frames = media_frame.size / 2; // 16位PCM，每样本2字节
    int ret = resample_to_8k_fixed((const int16_t *)media_frame.p_buf, in_frames, 16000, 1,
                                   (int16_t *)tmp_pcm, &out_frames);
    if (ret != 0) {
        TAL_PR_ERR("resample_to_8k_fixed failed, %d", ret);
        return OPRT_COM_ERROR;
    }

    // 重采样后的PCM字节数
    size_t pcm_bytes = out_frames * 2; // 16位PCM

    // 然后进行G.711 μ-law编码
    size_t g711_bytes = 0;
    ret = tuya_g711_encode(TUYA_G711_MU_LAW, tmp_pcm, pcm_bytes, g711_buf, &g711_bytes);
    if (ret != 0) {
        TAL_PR_ERR("g711 encode failed, %d", ret);
        return OPRT_COM_ERROR;
    }
    media_frame.p_buf = g711_buf;
    media_frame.size = g711_bytes; // G.711为8位，每样本1字节

    int rt = tuya_p2p_sdk_put_audio_frame(&media_frame);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("put audio failed: %d", rt);
    }
    else{
        TAL_PR_DEBUG("put audio success: data size %d ", len);
    }
}

STATIC VOID __tuya_ipc_app_rev_video_cb(IN INT_T device, IN INT_T channel, IN CONST MEDIA_VIDEO_FRAME_T *p_video_frame)
{
    PR_INFO("Rev video. size:[%u] video_codec:[%d] video_frame_type:[%d]\n", p_video_frame->buf_len, p_video_frame->video_codec, p_video_frame->video_frame_type);
    return;
}

STATIC INT_T __tuya_ipc_p2p_event_cb(IN CONST INT_T device, IN CONST INT_T channel, IN CONST MEDIA_STREAM_EVENT_E event, IN PVOID_T args)
{
    OPERATE_RET rt = 0;
    PR_DEBUG("p2p rev event cb=[%d] ", event);
    MEDIA_STREAM_EVENT_E msg = event;
    switch (event)
    {
       case MEDIA_STREAM_LIVE_VIDEO_START:
        {
            C2C_TRANS_CTRL_VIDEO_START * parm = (C2C_TRANS_CTRL_VIDEO_START *)args;
            PR_DEBUG("chn[%u] video start",parm->channel);
            wukong_audio_player_stop(AI_PLAYER_ALL);
            wukong_ai_agent_chat_break(NULL);
            wukong_ai_device_mode_switch(AI_DEVICE_MODE_P2P);
            tuya_ai_toy_camera_switch_mode(CAM_MODE_H264_YUV);
            tuya_ai_toy_camera_subscribe(CAM_STREAM_H264, CAM_CONSUMER_P2P, __p2p_h264_cb, NULL);
            break;
        }
        case MEDIA_STREAM_LIVE_VIDEO_STOP:
        {
            C2C_TRANS_CTRL_VIDEO_STOP * parm = (C2C_TRANS_CTRL_VIDEO_STOP *)args;
            PR_DEBUG("chn[%u] video stop",parm->channel);
            wukong_audio_player_stop(AI_PLAYER_ALL);
            wukong_ai_agent_chat_break(NULL);
            wukong_ai_device_mode_switch(AI_DEVICE_MODE_CHAT);
            tuya_ai_toy_camera_unsubscribe(CAM_STREAM_H264, CAM_CONSUMER_P2P);
            tuya_ai_toy_camera_switch_mode(CAM_MODE_JPEG_YUV);
            break;
        }
        case MEDIA_STREAM_LIVE_AUDIO_START:
        {
            C2C_TRANS_CTRL_AUDIO_START * parm = (C2C_TRANS_CTRL_AUDIO_START *)args;
            PR_DEBUG("chn[%u] audio start",parm->channel);
            wukong_audio_player_stop(AI_PLAYER_ALL);
            tuya_ai_agent_event(AI_EVENT_CHAT_BREAK, 0);
            wukong_ai_device_mode_switch(AI_DEVICE_MODE_P2P);
            TUYA_CALL_ERR_LOG(ty_publish_event(TUYA_IPC_CALL, &msg));
            break;
        }
        case MEDIA_STREAM_LIVE_AUDIO_STOP:
        {
            C2C_TRANS_CTRL_AUDIO_STOP * parm = (C2C_TRANS_CTRL_AUDIO_STOP *)args;
            PR_DEBUG("chn[%u] audio stop",parm->channel);
            wukong_audio_player_stop(AI_PLAYER_ALL);
            tuya_ai_agent_event(AI_EVENT_CHAT_BREAK, 0);
            wukong_ai_device_mode_switch(AI_DEVICE_MODE_CHAT);
            TUYA_CALL_ERR_LOG(ty_publish_event(TUYA_IPC_CALL, &msg));
            break;
        }
        case MEDIA_STREAM_SPEAKER_START:
        {
            PR_DEBUG("enbale audio speaker");
            //TUYA_APP_Enable_Speaker_CB(TRUE);
            break;
        }
        case MEDIA_STREAM_SPEAKER_STOP:
        {
            PR_DEBUG("disable audio speaker");
            //TUYA_APP_Enable_Speaker_CB(FALSE);
            break;
        }
    }
    return rt;
}

int resample_to_16k_fixed(const int16_t *in, size_t in_frames, int in_rate, int channels,
                         int16_t *out_buf_out, size_t *out_frames_out);
STATIC VOID __tuya_ipc_app_rev_audio_cb(IN INT_T device, IN INT_T channel, IN CONST MEDIA_AUDIO_FRAME_T *p_audio_frame)
{
    // Audio buffer size calculation:
    // G.711 @ 8kHz, 25fps -> each frame = 40ms -> 320 bytes G.711 data
    // After decode to PCM (16bit): 320 * 2 = 640 bytes (with 20% margin = 800 bytes)
    // After resample to 16kHz: 640 * 2 = 1280 bytes (with 20% margin = 1600 bytes)
    UCHAR_T rev_pcm[800] = {0};
    UCHAR_T rev_pcm_16k[1600] = {0};
    
    // 1. 先将G.711解码为8K 16bit PCM
    size_t decode_size = 0;
    int rt = tuya_g711_decode(TUYA_G711_MU_LAW, p_audio_frame->p_audio_buf, p_audio_frame->buf_len, rev_pcm, &decode_size);
    if (rt != 0) {
        PR_ERR("g711 decode failed, %d", rt);
        return;
    }

    PR_DEBUG("g711 decode success, src data size %d decode size %d sample %d codec %d", p_audio_frame->buf_len, decode_size, p_audio_frame->audio_sample, p_audio_frame->audio_codec);

    // 2. 将8K 16bit PCM重采样为16K 16bit PCM
    size_t in_frames = decode_size / 2; // 16bit PCM，每样本2字节
    size_t out_frames = 0;
    rt = resample_to_16k_fixed((const int16_t *)rev_pcm, in_frames, 8000, 1,
                               (int16_t *)rev_pcm_16k, &out_frames);
    if (rt != 0) {
        PR_ERR("resample_to_16k_fixed failed, %d", rt);
        return;
    }

    size_t out_bytes = out_frames * 2; // 16位PCM

    // 3. 将16K PCM送入播放接口
    TKL_AUDIO_FRAME_INFO_T frame = {0};
    frame.pbuf = (CHAR_T *)rev_pcm_16k;
    frame.used_size = out_bytes;
    int ret = tkl_ao_put_frame(0, 0, NULL, &frame);
    if(ret != OPRT_OK) {
        PR_ERR("tkl_ao_put_frame failed, ret=%d", ret);
        return 0;
    }

    return;
}
OPERATE_RET tuya_ipc_app_start()
{
    OPERATE_RET ret = OPRT_OK;

    TUYA_IPC_SDK_VAR_S sdkVar = {0};

    //初始化Adapter中间件
    sdkVar.media_var.on_recv_video_cb = __tuya_ipc_app_rev_video_cb;
    sdkVar.media_var.on_recv_audio_cb = __tuya_ipc_app_rev_audio_cb;

    sdkVar.media_info.stream_enable[E_IPC_STREAM_VIDEO_MAIN] = TRUE;    /* Whether to enable local HD video streaming */
    sdkVar.media_info.video_fps[E_IPC_STREAM_VIDEO_MAIN] = 25;  /* FPS */
    sdkVar.media_info.video_gop[E_IPC_STREAM_VIDEO_MAIN] = 25;  /* GOP */
    sdkVar.media_info.video_bitrate[E_IPC_STREAM_VIDEO_MAIN] = TUYA_VIDEO_BITRATE_1M; /* Rate limit */
    sdkVar.media_info.video_width[E_IPC_STREAM_VIDEO_MAIN] = 480; /* Single frame resolution of width*/
    sdkVar.media_info.video_height[E_IPC_STREAM_VIDEO_MAIN] = 480;/* Single frame resolution of height */
    sdkVar.media_info.video_freq[E_IPC_STREAM_VIDEO_MAIN] = 90000; /* Clock frequency */
    sdkVar.media_info.video_codec[E_IPC_STREAM_VIDEO_MAIN] = TUYA_CODEC_VIDEO_H264; /* Encoding format */
    /* Audio stream configuration.
    Note: The internal P2P preview, cloud storage, and local storage of the SDK are all use E_IPC_STREAM_AUDIO_MAIN data. */
    sdkVar.media_info.stream_enable[E_IPC_STREAM_AUDIO_MAIN] = TRUE;         /* Whether to enable local sound collection */
    sdkVar.media_info.audio_codec[E_IPC_STREAM_AUDIO_MAIN] = TUYA_CODEC_AUDIO_G711U;/* Encoding format */
    sdkVar.media_info.audio_sample [E_IPC_STREAM_AUDIO_MAIN]= TUYA_AUDIO_SAMPLE_8K;/* Sampling Rate */
    /*本地上传音频的参数*/
    sdkVar.media_info.audio_databits [E_IPC_STREAM_AUDIO_MAIN]= TUYA_AUDIO_DATABITS_16;/* Bit width */
    sdkVar.media_info.audio_channel[E_IPC_STREAM_AUDIO_MAIN]= TUYA_AUDIO_CHANNEL_MONO;/* channel */
    sdkVar.media_info.audio_fps[E_IPC_STREAM_AUDIO_MAIN] = 25;/* Fragments per second */

    //初始化P2P组件
    sdkVar.media_adatper_info.on_event_cb = __tuya_ipc_p2p_event_cb;
    sdkVar.media_adatper_info.on_ai_result_cb = 0;
    sdkVar.media_adatper_info.low_power = 1;
    sdkVar.media_adatper_info.max_client_num = 1;
    sdkVar.media_adatper_info.def_live_mode = TRANS_DEFAULT_STANDARD;
    sdkVar.media_adatper_info.recv_buffer_size = 16*1024;

    tuya_p2p_sdk_init(&sdkVar);

    return 0;
}

STATIC VOID_T tuya_ipc_thread(VOID_T *arg)
{
    TAL_PR_NOTICE("tuya_ipc_thread start");
    tuya_ipc_app_start();
    tal_thread_delete(ty_ipc_thread);
    ty_ipc_thread = NULL;
}

OPERATE_RET __ipc_start_cb(VOID *data)
{
    if (ty_ipc_thread != NULL) {
        return OPRT_OK;
    }
    THREAD_CFG_T thrd_param = {8192, THREAD_PRIO_2, "tuya_ipc_thread"};
    tal_thread_create_and_start(&ty_ipc_thread, NULL, NULL, tuya_ipc_thread, NULL, &thrd_param);
    return OPRT_OK;
}

STATIC BOOL_T s_event_active_done = FALSE;
STATIC OPERATE_RET __event_active_cb(VOID *data)
{
    if (s_event_active_done) {
        return OPRT_OK;
    }
    s_event_active_done = TRUE;

    TAL_PR_NOTICE("__event_active_cb start");
    //完成p2p passwd 更新
    extern BOOL_T iot_permit_mqtt_connect_cb(VOID);
    while(TRUE != iot_permit_mqtt_connect_cb()) {
        // usleep(500*1000);
        tal_system_sleep(500);
    }

    return OPRT_OK;
}
OPERATE_RET tuya_p2p_app_start(VOID)
{
    ty_subscribe_event(EVENT_POST_ACTIVATE, "ipc_start", __event_active_cb, SUBSCRIBE_TYPE_ONETIME);
    OPERATE_RET rt = ty_subscribe_event(EVENT_LINK_UP, "ipc_start", __ipc_start_cb, SUBSCRIBE_TYPE_NORMAL);

    /* 补偿时序竞争：如果订阅时设备已激活，EVENT_POST_ACTIVATE 不会再触发，
     * 需要手动调用 __event_active_cb 完成 p2p passwd 更新 */
    extern BOOL_T iot_permit_mqtt_connect_cb(VOID);
    if (TRUE == iot_permit_mqtt_connect_cb()) {
        TAL_PR_NOTICE("[p2p] already activated, trigger event_active directly");
        __event_active_cb(NULL);
    }

    /* 补偿时序竞争：如果订阅时网络已经 up，EVENT_LINK_UP 不会再触发，
     * 需要手动调用 __ipc_start_cb 启动 P2P 线程 */
    if (tuya_svc_netmgr_linkage_is_up(LINKAGE_TYPE_DEFAULT)) {
        TAL_PR_NOTICE("[p2p] link already up, trigger ipc start directly");
        __ipc_start_cb(NULL);
    }

    return rt;
}

#endif