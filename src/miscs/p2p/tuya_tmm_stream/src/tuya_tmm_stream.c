/**
 * @file tuya_tmm_stream.c
 * @author baijue.huang@tuya.com
 * @brief tuya tmm stream
 * @version 0.1
 * @date 2022-8-31
 * 
 * @copyright Copyright (c) 2021
 * 
 */

/***********************************************************************
 ** INCLUDE                                                           **
 **********************************************************************/
#include <stdio.h>

#include "uni_log.h"
#include "tal_thread.h"
#include "tal_time_service.h"
#include "tuya_ring_buffer.h"
#include "tuya_tmm_link.h"
#include "tuya_ipc_mqtt_subscribe.h"
#include "tuya_ipc_moto_mqtt.h"
#include "tuya_ipc_p2p.h"

#include "base_event.h"
#include "tuya_g711_utils.h"
#include "gw_intf.h"
#include "tuya_iot_internal_api.h"
#include "mqc_app.h"             /* get_mqc_conn_stat — 按需启动的 MQTT 已连补偿 */

#include "tuya_tmm_stream_link.h"

/***********************************************************************
 ** CONSTANT ( MACRO AND ENUM )                                       **
 **********************************************************************/

#define TUYA_TMM_STREAM_DELAY_MAX_MS       500
#define GET_DEVICE_TMM_FUNC "tuya.device.ipc.p2p.config.get"
/***********************************************************************
 ** STRUCT                                                            **
 **********************************************************************/

typedef struct {
    BOOL_T                           inited;
    DEVICE_MEDIA_INFO_T              media_info;
    RING_BUFFER_USER_HANDLE_T        audio_write_handle;
    RING_BUFFER_USER_HANDLE_T        video_write_handle;
    TUYA_TMM_STREAM_AUDIO_OUTPUT_CB  audio_output_cb;
    TUYA_TMM_STREAM_VIDEO_OUTPUT_CB  video_output_cb;
    TUYA_TMM_STREAM_INIT_S           init_param;
    BOOL_T                           is_first_frame;
    THREAD_HANDLE                    thread_handle;
} TUYA_TMM_STREAM_INS_S;

/***********************************************************************
 ** VARIABLE                                                          **
 **********************************************************************/

STATIC TUYA_TMM_STREAM_INS_S          g_tmm_stream_ins = {0};

/***********************************************************************
 ** FUNCTON                                                           **
 **********************************************************************/

STATIC OPERATE_RET __tmm_ipc_media_adapter_init(TUYA_TMM_STREAM_INIT_S *init_params);
STATIC OPERATE_RET __tmm_ipc_media_adapter_set_media_info(TUYA_TMM_STREAM_INIT_S *init_params);
STATIC OPERATE_RET __tmm_ipc_media_stream_init(TUYA_TMM_STREAM_INIT_S *init_params);
STATIC OPERATE_RET __tmm_link_init(TUYA_TMM_STREAM_INIT_S *init_params);
STATIC OPERATE_RET __tmm_ringbuf_init(TUYA_TMM_STREAM_INIT_S *init_params);

extern VOID tuya_ipc_upload_skills();

#if defined(TUYA_CHECK_TMM_ABILITY)
#define TUYA_P2P    4
STATIC BOOL_T __get_device_has_tmm_func(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    CHAR_T post_data[256] = {0};
    ty_cJSON *p_result = NULL, *type = NULL;
    BOOL_T is_has_tmm = FALSE;

    sprintf(post_data, "{\"type\": %d}", TUYA_P2P);
    PR_INFO("post data: [%s]", post_data);
    rt = iot_httpc_common_post_simple(GET_DEVICE_TMM_FUNC, "2.0", post_data, NULL, &p_result);
    if (OPRT_OK != rt) {
        PR_ERR("post %s result failed", GET_DEVICE_TMM_FUNC);
        if (p_result) ty_cJSON_Delete(p_result);
        return is_has_tmm;
    }

    if ((OPRT_OK == rt) && (NULL != p_result)) {
        type = ty_cJSON_GetObjectItem(p_result, "type");
        if (type && type->valueint == TUYA_P2P) {
            is_has_tmm = TRUE;
        }
        ty_cJSON_Delete(p_result);
    }

    PR_DEBUG("device tmm function %d", is_has_tmm);

    return is_has_tmm;
}
#endif

/**
 * @brief Subscribe moto MQTT topic for P2P ICE signaling
 * @param[in] data unused event payload
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __tmm_moto_mqtt_book_topic(VOID *data)
{
    TUYA_IPC_MOTO_MQTT_T *mqtthdl = NULL;

    (VOID)data;
    mqtthdl = tuya_ipc_get_moto_mqtt_instance();
    if (mqtthdl == NULL || mqtthdl->book_topic == NULL) {
        PR_ERR("moto mqtt instance unavailable");
        return OPRT_COM_ERROR;
    }

    if (mqtthdl->book_topic() != OPRT_OK) {
        PR_ERR("moto mqtt book_topic failed");
        return OPRT_COM_ERROR;
    }

    PR_INFO("moto mqtt book_topic ok");
    return OPRT_OK;
}

OPERATE_RET __tuya_tmm_stream_subscribe_init(VOID *data)
{
    if (g_tmm_stream_ins.inited) {
        PR_WARN("already inited.");
        return OPRT_OK;
    }

    if (__tmm_moto_mqtt_book_topic(data) != OPRT_OK) {
        PR_ERR("__tmm_moto_mqtt_book_topic failed.");
        return OPRT_COM_ERROR;
    }

    #if defined(TUYA_CHECK_TMM_ABILITY)
    if (FALSE == __get_device_has_tmm_func()) {
        PR_ERR("__get_device_has_tmm_func failed.");
        return OPRT_COM_ERROR;
    }
    #endif

    TUYA_TMM_STREAM_INIT_S *init_params = &g_tmm_stream_ins.init_param;

    if (__tmm_ipc_media_adapter_init(init_params) != OPRT_OK) {
        PR_ERR("__tmm_ipc_media_adapter_init failed.");
        return OPRT_COM_ERROR;
    }

    if (__tmm_ipc_media_adapter_set_media_info(init_params) != OPRT_OK) {
        PR_ERR("__tmm_ipc_media_adapter_set_media_info failed.");
        return OPRT_COM_ERROR;
    }

    if (__tmm_ipc_media_stream_init(init_params) != OPRT_OK) {
        PR_ERR("__tmm_ipc_media_stream_init failed.");
        return OPRT_COM_ERROR;
    }

    if (__tmm_link_init(init_params) != OPRT_OK) {
        PR_ERR("__tmm_link_init failed.");
        return OPRT_COM_ERROR;
    }

    tuya_ipc_upload_skills();
    PR_DEBUG("tuya_ipc_upload_skills SUCCESSFULLY.");

    if (__tmm_ringbuf_init(init_params) != OPRT_OK) {
        PR_ERR("__tmm_ringbuf_init failed.");
        return OPRT_COM_ERROR;
    }

    g_tmm_stream_ins.audio_output_cb = init_params->audio_output_cb;
    g_tmm_stream_ins.video_output_cb = init_params->video_output_cb;
    g_tmm_stream_ins.inited = TRUE;

    PR_DEBUG("tuya tmm stream SUCCESSFULLY.");
    ty_publish_event(EVENT_TMM_STREAM_READY, NULL);

    return OPRT_OK;
}

BOOL_T tuya_tmm_stream_is_ready(VOID)
{
    return g_tmm_stream_ins.inited;
}

STATIC VOID __tuya_tmm_stream_subscribe_init_thrd(VOID *arg)
{
    __tuya_tmm_stream_subscribe_init(NULL);
    if (g_tmm_stream_ins.thread_handle != NULL) {
        tal_thread_delete(g_tmm_stream_ins.thread_handle);
    }
}

OPERATE_RET tuya_tmm_stream_subscribe_init(VOID *data)
{
    /* 幂等:EVENT_MQTT_CONNECTED 事件与「MQTT 已连」补偿路径(见 stream_init)
     * 可能双触发,只允许拉起一次 init 线程。 */
    STATIC BOOL_T s_init_started = FALSE;
    OPERATE_RET rt;

    (VOID)data;
    if (s_init_started || g_tmm_stream_ins.inited) {
        return OPRT_OK;
    }
    s_init_started = TRUE;

    THREAD_CFG_T thrd_param = {1024 * 10, THREAD_PRIO_1, "tuya_tmm_stream"};
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    thrd_param.psram_mode = 1;
#endif
    rt = tal_thread_create_and_start(&g_tmm_stream_ins.thread_handle, NULL, NULL,
                                     __tuya_tmm_stream_subscribe_init_thrd, NULL, &thrd_param);
    if (rt != OPRT_OK) {
        s_init_started = FALSE;
    }
    return rt;
}

OPERATE_RET tuya_tmm_stream_init(TUYA_TMM_STREAM_INIT_S *init_params)
{
    if (g_tmm_stream_ins.inited) {
        PR_WARN("already inited.");
        return OPRT_OK;
    }

    if (init_params == NULL) {
        return OPRT_INVALID_PARM;
    }

    gw_active_set_ext_param((CHAR_T *)"\"skillParam\":{\"type\":4}");
    tuya_ipc_mqtt_register_cb_init();
    if (tuya_ipc_moto_mqtt_init() != OPRT_OK) {
        PR_ERR("tuya_ipc_moto_mqtt_init failed.");
        return OPRT_COM_ERROR;
    }

    memcpy(&g_tmm_stream_ins.init_param, init_params, sizeof(TUYA_TMM_STREAM_INIT_S));

    ty_subscribe_event(EVENT_MQTT_CONNECTED, "media_skills", tuya_tmm_stream_subscribe_init, SUBSCRIBE_TYPE_ONETIME);
    ty_subscribe_event(EVENT_MQTT_CONNECTED, "moto_mqtt", __tmm_moto_mqtt_book_topic, SUBSCRIBE_TYPE_NORMAL);

    /* 按需启动补偿(docs/adr/0008):P2P 开关后开时 MQTT 早已连接,上面的 ONETIME
     * 事件不会再触发,这里直接补跑一次(subscribe_init 幂等,与事件双触发安全)。
     * 对应原生 tuya_p2p_app_start 的「补偿时序竞争」做法。 */
    if (get_mqc_conn_stat()) {
        tuya_tmm_stream_subscribe_init(NULL);
    }

    return OPRT_OK;
}

OPERATE_RET tuya_tmm_stream_deinit(VOID)
{
    return OPRT_OK;
}

OPERATE_RET tuya_tmm_stream_connect(TUYA_TMM_STREAM_CONF_S *conf)
{
    if (conf == NULL) {
        return OPRT_INVALID_PARM;
    }

    UINT_T t0 = (UINT_T)tal_time_get_posix_ms();
    PR_NOTICE("[tmm_diag] stream_connect ENTER dev_id %s local_key %s", conf->dev_id, conf->local_key);

    if (tuya_tmm_stream_link_connect(conf) != OPRT_OK) {
        PR_ERR("[tmm_diag] stream_connect link_connect FAILED dev_id %s %ums",
               conf->dev_id, (UINT_T)(tal_time_get_posix_ms() - t0));
        return OPRT_COM_ERROR;
    }
    PR_NOTICE("[tmm_diag] stream_connect link_connect OK %ums", (UINT_T)(tal_time_get_posix_ms() - t0));

    if (tuya_tmm_stream_link_control(conf, 10 * 1000) != OPRT_OK) {
        PR_ERR("[tmm_diag] stream_connect link_control FAILED dev_id %s %ums",
               conf->dev_id, (UINT_T)(tal_time_get_posix_ms() - t0));
        tuya_tmm_stream_link_disconnect(conf);
        return OPRT_COM_ERROR;
    }
    PR_NOTICE("[tmm_diag] stream_connect link_control OK total %ums", (UINT_T)(tal_time_get_posix_ms() - t0));

    return OPRT_OK;
}

OPERATE_RET tuya_tmm_stream_disconnect(TUYA_TMM_STREAM_CONF_S *conf)
{
    if (conf == NULL) {
        return OPRT_INVALID_PARM;
    }

    PR_INFO("tuya_tmm_stream_disconnect: dev_id %s.", conf->dev_id);

    // before disconnect, clear the flag to help to notify the opposite end.
    if (tuya_tmm_stream_link_control(conf, 10 * 1000) != OPRT_OK) {
        PR_ERR("tuya_tmm_stream_link_control: dev_id %s.", conf->dev_id);
    }

    PR_INFO("tuya_tmm_stream_disconnect SUCCESSFULLY.");
    
    if (tuya_tmm_stream_link_disconnect(conf) != OPRT_OK) {
        PR_ERR("tuya_tmm_stream_link_disconnect FAILED: dev_id %s.", conf->dev_id);
    }
    PR_INFO("tuya_tmm_stream_disconnect SUCCESSFULLY.");

    return OPRT_OK;
}

OPERATE_RET tuya_tmm_stream_ctrl(TUYA_TMM_STREAM_CONF_S *conf)
{
    if (conf == NULL) {
        return OPRT_INVALID_PARM;
    }

    PR_DEBUG("tuya_tmm_stream_ctrl: channel_ctrl called: dev_id %s.", conf->dev_id);

    if (tuya_tmm_stream_link_control(conf, 20 * 1000) != OPRT_OK) {
        PR_ERR("tuya_tmm_stream_link_control FAILED: dev_id %s.", conf->dev_id);
        return OPRT_COM_ERROR;
    }
    PR_DEBUG("tuya_tmm_stream_ctrl SUCCESSFULLY.");
    
    return OPRT_OK;
}

OPERATE_RET tuya_tmm_stream_audio_send(TUYA_TMM_STREAM_AUDIO_DATA_S *data)
{
    RING_BUFFER_USER_HANDLE_T write_handle = g_tmm_stream_ins.audio_write_handle;

    if (write_handle == NULL) {
        PR_DEBUG("write handle is NULL");
        return OPRT_NOT_SUPPORTED;
    }

    if (data == NULL) {
        PR_DEBUG("the param is NULL");
        return OPRT_INVALID_PARM;
    }

    if (tuya_ipc_ring_buffer_append_data(write_handle, data->p_data, data->data_len, E_AUDIO_FRAME, data->user_data.timestamp) != OPRT_OK) {
        PR_ERR("tuya_ipc_ring_buffer_append_data FAILED: addr %p, size %u, type %d, pts %llu.",
        data->p_data, data->data_len, E_AUDIO_FRAME, data->user_data.timestamp);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

OPERATE_RET tuya_tmm_stream_audio_uplink_reset(VOID)
{
    RING_BUFFER_USER_HANDLE_T write_handle = g_tmm_stream_ins.audio_write_handle;

    if (write_handle == NULL) {
        return OPRT_OK;
    }

    tuya_ipc_ring_buffer_clean_user_state_and_buffer(write_handle);
    tuya_ipc_ring_buffer_correct_timestamp(0, 0, E_IPC_STREAM_AUDIO_MAIN, 40, TRUE);
    return OPRT_OK;
}

OPERATE_RET tuya_tmm_stream_video_send(TUYA_TMM_STREAM_VIDEO_DATA_S *data, BOOL_T is_i_frame)
{
    RING_BUFFER_USER_HANDLE_T write_handle = g_tmm_stream_ins.video_write_handle;
    MEDIA_FRAME_TYPE_E        frame_type;

    if (write_handle == NULL) {
        PR_DEBUG("video write handle is NULL (is_support_video off?)");
        return OPRT_NOT_SUPPORTED;
    }

    if (data == NULL || data->p_data == NULL || data->data_len == 0) {
        PR_DEBUG("the param is NULL");
        return OPRT_INVALID_PARM;
    }

    frame_type = is_i_frame ? E_VIDEO_I_FRAME : E_VIDEO_PB_FRAME;

    if (tuya_ipc_ring_buffer_append_data(write_handle, data->p_data, data->data_len, frame_type, data->user_data.timestamp) != OPRT_OK) {
        PR_ERR("tuya_ipc_ring_buffer_append_data(video) FAILED: addr %p, size %u, type %d, pts %llu.",
        data->p_data, data->data_len, frame_type, data->user_data.timestamp);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

STATIC UINT_T __tmm_g711u_to_pcm16(CONST UCHAR_T *g711, UINT_T g711_len, CHAR_T *pcm_buf, UINT_T pcm_buf_sz)
{
    size_t out_bytes = 0;

    if (g711 == NULL || pcm_buf == NULL || g711_len == 0 || pcm_buf_sz == 0) {
        return 0;
    }

    if (tuya_g711_decode(TUYA_G711_MU_LAW, (UCHAR_T *)g711, g711_len, (UCHAR_T *)pcm_buf, &out_bytes) != 0) {
        return 0;
    }

    return (UINT_T)out_bytes;
}

STATIC UINT_T __tmm_g711a_to_pcm16(CONST UCHAR_T *g711, UINT_T g711_len, CHAR_T *pcm_buf, UINT_T pcm_buf_sz)
{
    size_t out_bytes = 0;

    if (g711 == NULL || pcm_buf == NULL || g711_len == 0 || pcm_buf_sz == 0) {
        return 0;
    }

    if (tuya_g711_decode(TUYA_G711_A_LAW, (UCHAR_T *)g711, g711_len, (UCHAR_T *)pcm_buf, &out_bytes) != 0) {
        return 0;
    }

    return (UINT_T)out_bytes;
}

STATIC VOID __tmm_media_recv_video_cb(IN INT_T device, IN INT_T channel, IN CONST MEDIA_VIDEO_FRAME_T *media_video_frame)
{
    return ;
}

STATIC VOID __tmm_media_recv_audio_cb(IN INT_T device, IN INT_T channel, IN CONST MEDIA_AUDIO_FRAME_T *media_audio_frame)
{
    static CHAR_T  audio_buf[2048] = {0};    //缓存G711解码出来的PCM数据
    static INT64_T adjusting_timestamp = 0;

    INT64_T timestamp = tal_time_get_posix_ms();

    if (g_tmm_stream_ins.is_first_frame == TRUE) {
        g_tmm_stream_ins.is_first_frame = FALSE;
        adjusting_timestamp = timestamp - media_audio_frame->timestamp;
    }

    INT64_T intval = timestamp - adjusting_timestamp - media_audio_frame->timestamp;

    if (intval < -100) {
        adjusting_timestamp = timestamp - media_audio_frame->timestamp;
    }

    if (g_tmm_stream_ins.audio_output_cb) {
        TUYA_TMM_STREAM_AUDIO_DATA_S audio_data = {0};

        switch(media_audio_frame->audio_codec) {
        case TUYA_CODEC_AUDIO_PCM:
            audio_data.p_data = media_audio_frame->p_audio_buf;
            audio_data.data_len = media_audio_frame->buf_len;
            snprintf(audio_data.user_data.audio_codec, sizeof(audio_data.user_data.audio_codec), "pcm");
            break;
        case TUYA_CODEC_AUDIO_G711U:
            /* G711 解码输出 = 输入 × 2(PCM 16-bit),必须按输出长度约束,否则
             * buf_len 在 1024~2047 时输出 2048~4094 字节,写爆 audio_buf(2048B) */
            if (media_audio_frame->buf_len > 0 &&
                media_audio_frame->buf_len * 2 <= sizeof(audio_buf)) {
                audio_data.p_data = (VOID *)audio_buf;
                audio_data.data_len = __tmm_g711u_to_pcm16(media_audio_frame->p_audio_buf,
                                                             media_audio_frame->buf_len,
                                                             audio_buf, sizeof(audio_buf));
            }
            snprintf(audio_data.user_data.audio_codec, sizeof(audio_data.user_data.audio_codec), "pcm");
            break;
        case TUYA_CODEC_AUDIO_G711A:
            /* 同上:按解码输出长度(输入×2)守卫,防止栈缓冲区溢出 */
            if (media_audio_frame->buf_len > 0 &&
                media_audio_frame->buf_len * 2 <= sizeof(audio_buf)) {
                audio_data.p_data = (VOID *)audio_buf;
                audio_data.data_len = __tmm_g711a_to_pcm16(media_audio_frame->p_audio_buf,
                                                             media_audio_frame->buf_len,
                                                             audio_buf, sizeof(audio_buf));
            }
            snprintf(audio_data.user_data.audio_codec, sizeof(audio_data.user_data.audio_codec), "pcm");
            break;
        default:
            audio_data.p_data = media_audio_frame->p_audio_buf;
            audio_data.data_len = media_audio_frame->buf_len;
            snprintf(audio_data.user_data.audio_codec, sizeof(audio_data.user_data.audio_codec), "unkown");
            break;
        }
        audio_data.user_data.audio_sample = media_audio_frame->audio_sample;
        audio_data.user_data.audio_databits = media_audio_frame->audio_databits;
        audio_data.user_data.audio_channel = media_audio_frame->audio_channel;
        audio_data.user_data.timestamp = media_audio_frame->timestamp;

        g_tmm_stream_ins.audio_output_cb(&audio_data);
    }
}

STATIC OPERATE_RET __tmm_ipc_media_adapter_init(TUYA_TMM_STREAM_INIT_S *init_params)
{
    TUYA_IPC_MEDIA_ADAPTER_VAR_T media_var = {0};

    media_var.get_snapshot_cb = NULL;
    media_var.on_recv_video_cb = __tmm_media_recv_video_cb;
    media_var.on_recv_audio_cb = __tmm_media_recv_audio_cb;

    if (tuya_ipc_media_adapter_init(&media_var) != OPRT_OK) {
        PR_ERR("tuya_ipc_media_adapter_init FAILED.");
        return OPRT_COM_ERROR;
    }

    PR_DEBUG("tuya_ipc_media_adapter_init SUCCESSFULLY.");
    
    return OPRT_OK;
}

STATIC OPERATE_RET __tmm_ipc_media_adapter_set_media_info(TUYA_TMM_STREAM_INIT_S *init_params)
{
    if (init_params->is_support_audio) {
        g_tmm_stream_ins.media_info.av_encode_info.stream_enable[E_IPC_STREAM_AUDIO_MAIN] = TRUE;
        /* 上行发送编码与原方案(tuya_p2p_app.c)一致:G.711 μ-law(8-bit,~8KB/s),
         * 上层喂入前已 G711 编码;相比 PCM(16-bit,16KB/s)减半上行带宽、缓解拥塞延迟。 */
        g_tmm_stream_ins.media_info.av_encode_info.audio_codec[E_IPC_STREAM_AUDIO_MAIN]    = TUYA_CODEC_AUDIO_G711U;  //Encoding format
        g_tmm_stream_ins.media_info.av_encode_info.audio_sample[E_IPC_STREAM_AUDIO_MAIN]   = TUYA_AUDIO_SAMPLE_8K;  //Sampling Rate
        g_tmm_stream_ins.media_info.av_encode_info.audio_databits[E_IPC_STREAM_AUDIO_MAIN] = TUYA_AUDIO_DATABITS_16;  //Bit width
        g_tmm_stream_ins.media_info.av_encode_info.audio_channel[E_IPC_STREAM_AUDIO_MAIN]  = TUYA_AUDIO_CHANNEL_MONO;  //channel
        g_tmm_stream_ins.media_info.av_encode_info.audio_fps[E_IPC_STREAM_AUDIO_MAIN]      = 25;  //Fragments per second
        g_tmm_stream_ins.media_info.audio_decode_info.enable         = TRUE;
        /* 下行接收声明与原方案(tuya_p2p_sdk.c)一致:G.711 μ-law@8k(64kbps)。
         * 移植初版声明 PCM@16k(256kbps),APP 据此下发 4 倍带宽的裸流,在 relay
         * 单 TCP 双向复用下挤压上行吞吐/ACK,对讲(APP说话)时上行 KCP 即拥塞。
         * 收侧 G711U 解码(__tmm_media_recv_audio_cb)与 8k→16k 重采样播放已就绪。 */
        g_tmm_stream_ins.media_info.audio_decode_info.audio_codec    = TUYA_CODEC_AUDIO_G711U;
        g_tmm_stream_ins.media_info.audio_decode_info.audio_sample   = TUYA_AUDIO_SAMPLE_8K;
        g_tmm_stream_ins.media_info.audio_decode_info.audio_databits = TUYA_AUDIO_DATABITS_16;
        g_tmm_stream_ins.media_info.audio_decode_info.audio_channel  = TUYA_AUDIO_CHANNEL_MONO;
    }

    /* 视频发送(设备→对端,H264)。声明本机作为媒体源的视频编码能力,
     * 对端(APP)据此拉流;实际编码帧由上层相机 H264 源经 tuya_tmm_stream_video_send 喂入。
     * 参数对齐原生 tuya_p2p_app.c 已验证配置(480x480 / H264 / 25fps / 1Mbps)。 */
    if (init_params->is_support_video) {
        g_tmm_stream_ins.media_info.av_encode_info.stream_enable[E_IPC_STREAM_VIDEO_MAIN] = TRUE;
        g_tmm_stream_ins.media_info.av_encode_info.video_codec[E_IPC_STREAM_VIDEO_MAIN]   = TUYA_CODEC_VIDEO_H264;
        g_tmm_stream_ins.media_info.av_encode_info.video_fps[E_IPC_STREAM_VIDEO_MAIN]     = 25;
        g_tmm_stream_ins.media_info.av_encode_info.video_gop[E_IPC_STREAM_VIDEO_MAIN]     = 25;
        g_tmm_stream_ins.media_info.av_encode_info.video_bitrate[E_IPC_STREAM_VIDEO_MAIN] = TUYA_VIDEO_BITRATE_1M;
        g_tmm_stream_ins.media_info.av_encode_info.video_width[E_IPC_STREAM_VIDEO_MAIN]   = 480;
        g_tmm_stream_ins.media_info.av_encode_info.video_height[E_IPC_STREAM_VIDEO_MAIN]  = 480;
        g_tmm_stream_ins.media_info.av_encode_info.video_freq[E_IPC_STREAM_VIDEO_MAIN]    = 90000;
    }

    if (tuya_ipc_media_adapter_set_media_info(0, 0, g_tmm_stream_ins.media_info) != OPRT_OK) {
        PR_ERR("tuya_ipc_media_adapter_set_media_info FAILED.");
        return OPRT_COM_ERROR;
    }

    PR_DEBUG("tuya_ipc_media_adapter_set_media_info SUCCESSFULLY.");

    return OPRT_OK;
}

STATIC OPERATE_RET __tmm_stream_event_cb(TUYA_TMM_STREAM_EVENT_E event)
{
    if (g_tmm_stream_ins.init_param.event_cb) {
       return g_tmm_stream_ins.init_param.event_cb(event);
    }

    return OPRT_OK;
}

STATIC INT_T __tmm_media_stream_event_cb(IN CONST INT_T device, IN CONST INT_T channel, IN CONST MEDIA_STREAM_EVENT_E event, IN PVOID_T args)
{
    switch (event) {
        case MEDIA_STREAM_SPEAKER_START:
            PR_NOTICE("[tmm_diag] MEDIA_STREAM_SPEAKER_START dev=%d ch=%d", device, channel);
            g_tmm_stream_ins.is_first_frame = TRUE;
            __tmm_stream_event_cb(TUYA_TMM_STREAM_EVT_SPEAKER_START);
            PR_DEBUG("TUYA_TMM_STREAM_EVT_SPEAKER_START");
            break;

        case MEDIA_STREAM_SPEAKER_STOP:
            __tmm_stream_event_cb(TUYA_TMM_STREAM_EVT_SPEAKER_STOP);
            PR_DEBUG("TUYA_TMM_STREAM_EVT_SPEAKER_STOP");
            break;

        case MEDIA_STREAM_LIVE_VIDEO_START:
            __tmm_stream_event_cb(TUYA_TMM_STREAM_EVT_VIDEO_START);
            PR_DEBUG("TUYA_TMM_STREAM_EVT_VIDEO_START");
            break;

        case MEDIA_STREAM_LIVE_VIDEO_STOP:
            __tmm_stream_event_cb(TUYA_TMM_STREAM_EVT_VIDEO_STOP);
            PR_DEBUG("TUYA_TMM_STREAM_EVT_VIDEO_STOP");
            break;
    
        case MEDIA_STREAM_LIVE_AUDIO_START:
            PR_NOTICE("[tmm_diag] MEDIA_STREAM_LIVE_AUDIO_START dev=%d ch=%d (对讲主事件:开麦克风上行)", device, channel);
            g_tmm_stream_ins.is_first_frame = TRUE;
            __tmm_stream_event_cb(TUYA_TMM_STREAM_EVT_AUDIO_START);
            PR_DEBUG("TUYA_TMM_STREAM_EVT_AUDIO_START");
            break;

        case MEDIA_STREAM_LIVE_AUDIO_STOP:
            __tmm_stream_event_cb(TUYA_TMM_STREAM_EVT_AUDIO_STOP);
            PR_DEBUG("TUYA_TMM_STREAM_EVT_AUDIO_STOP");
            break;

        case MEDIA_STREAM_LIVE_VIDEO_SEND_START:
            __tmm_stream_event_cb(TUYA_TMM_STREAM_EVT_VIDEO_SEND_START);
            PR_DEBUG("TUYA_TMM_STREAM_EVT_VIDEO_SEND_START");
            break;

        case MEDIA_STREAM_LIVE_VIDEO_SEND_STOP:
            __tmm_stream_event_cb(TUYA_TMM_STREAM_EVT_VIDEO_SEND_STOP);
            PR_DEBUG("TUYA_TMM_STREAM_EVT_VIDEO_SEND_STOP");
            break;

        case MEDIA_STREAM_LIVE_AUDIO_SEND_START:
            __tmm_stream_event_cb(TUYA_TMM_STREAM_EVT_AUDIO_SEND_START);
            PR_DEBUG("TUYA_TMM_STREAM_EVT_AUDIO_SEND_START");
            break;

        case MEDIA_STREAM_LIVE_AUDIO_SEND_STOP:
            __tmm_stream_event_cb(TUYA_TMM_STREAM_EVT_AUDIO_SEND_STOP);
            PR_DEBUG("TUYA_TMM_STREAM_EVT_AUDIO_SEND_STOP");
            break;
        
        default:
            PR_DEBUG("event not support: %d.", event);
            break;
    }

    return 0;
}

STATIC OPERATE_RET __tmm_ipc_media_stream_init(TUYA_TMM_STREAM_INIT_S *init_params)
{
    MEDIA_STREAM_VAR_T stream_var = {0};

    stream_var.on_event_cb = __tmm_media_stream_event_cb;
    stream_var.max_client_num = 1;
    stream_var.def_live_mode = 0;
    /* 对齐原生 tuya_p2p_app.c 的 media_adatper_info.low_power=1(低功耗设备声明,
     * 影响 P2P 保活/唤醒策略);移植初版误置 FALSE。 */
    stream_var.low_power = TRUE;

    if (tuya_ipc_media_stream_init(&stream_var) != OPRT_OK) {
        PR_ERR("tuya_ipc_media_stream_init FAILED.");
        return OPRT_COM_ERROR;
    }

    PR_DEBUG("tuya_ipc_media_stream_init SUCCESSFULLY.");

    return OPRT_OK;
}

STATIC VOID __tmm_paired_dev_list_change_cb(LIST_HEAD* head)
{
    return ;
}

STATIC OPERATE_RET __tmm_link_init(TUYA_TMM_STREAM_INIT_S *init_params)
{
    if (tuya_tmm_link_init() != OPRT_OK) {
        PR_ERR("tuya_tmm_link_init FAILED.");
        return OPRT_COM_ERROR;
    }
    PR_DEBUG("tuya_tmm_link_init SUCCESSFULLY.");

    INT_T            decoder_num = 0;
    TUYA_DECODER_T   decoder_info[5] = {{0}};

    INT_T            encoder_num = 0;
    IPC_MEDIA_INFO_T encoder_info[5] = {0};

    decoder_info[0].codec_id = TUYA_CODEC_AUDIO_PCM;
    decoder_info[0].decoder_desc.a_decoder.sample   = TUYA_AUDIO_SAMPLE_8K;
    decoder_info[0].decoder_desc.a_decoder.databits = TUYA_AUDIO_DATABITS_16;
    decoder_info[1].codec_id = TUYA_CODEC_AUDIO_G711U;
    decoder_info[1].decoder_desc.a_decoder.sample   = TUYA_AUDIO_SAMPLE_8K;
    decoder_info[1].decoder_desc.a_decoder.databits = TUYA_AUDIO_DATABITS_16;
    decoder_info[2].codec_id = TUYA_CODEC_AUDIO_G711A;
    decoder_info[2].decoder_desc.a_decoder.sample   = TUYA_AUDIO_SAMPLE_8K;
    decoder_info[2].decoder_desc.a_decoder.databits = TUYA_AUDIO_DATABITS_16;
    decoder_num                                     = 3;

    if (init_params->is_support_video) {
        decoder_info[3].codec_id                        = TUYA_CODEC_VIDEO_H264;
        decoder_info[3].decoder_desc.v_decoder.height   = 360;
        decoder_info[3].decoder_desc.v_decoder.width    = 640;
        decoder_info[3].decoder_desc.v_decoder.profile  = VIDEO_AVC_PROFILE_BASE_LINE;
        decoder_info[4].codec_id                        = TUYA_CODEC_VIDEO_MJPEG;
        decoder_info[4].decoder_desc.v_decoder.height   = 480;
        decoder_info[4].decoder_desc.v_decoder.width    = 640;
        decoder_info[4].decoder_desc.v_decoder.profile  = VIDEO_AVC_PROFILE_BASE_LINE;
        decoder_num                                     = 5;
    }

    // add audio encoder
    encoder_info[0].stream_enable[E_IPC_STREAM_AUDIO_MAIN] = TRUE;
    encoder_info[0].audio_codec[E_IPC_STREAM_AUDIO_MAIN]    = TUYA_CODEC_AUDIO_PCM;
    encoder_info[0].audio_databits[E_IPC_STREAM_AUDIO_MAIN] = TUYA_AUDIO_DATABITS_16;
    encoder_info[0].audio_channel[E_IPC_STREAM_AUDIO_MAIN]  = TUYA_AUDIO_CHANNEL_MONO;
    encoder_info[0].audio_sample[E_IPC_STREAM_AUDIO_MAIN]   = TUYA_AUDIO_SAMPLE_16K;
    encoder_info[0].audio_fps[E_IPC_STREAM_AUDIO_MAIN]      = 25;
    encoder_info[1].stream_enable[E_IPC_STREAM_AUDIO_MAIN] = TRUE;
    encoder_info[1].audio_codec[E_IPC_STREAM_AUDIO_MAIN]    = TUYA_CODEC_AUDIO_G711A;
    encoder_info[1].audio_databits[E_IPC_STREAM_AUDIO_MAIN] = TUYA_AUDIO_DATABITS_8;
    encoder_info[1].audio_channel[E_IPC_STREAM_AUDIO_MAIN]  = TUYA_AUDIO_CHANNEL_MONO;
    encoder_info[1].audio_sample[E_IPC_STREAM_AUDIO_MAIN]   = TUYA_AUDIO_SAMPLE_8K;
    encoder_info[1].audio_fps[E_IPC_STREAM_AUDIO_MAIN]      = 25;
    encoder_info[2].audio_codec[E_IPC_STREAM_AUDIO_MAIN]    = TUYA_CODEC_AUDIO_G711U;
    encoder_info[2].audio_databits[E_IPC_STREAM_AUDIO_MAIN] = TUYA_AUDIO_DATABITS_8;
    encoder_info[2].audio_channel[E_IPC_STREAM_AUDIO_MAIN]  = TUYA_AUDIO_CHANNEL_MONO;
    encoder_info[2].audio_sample[E_IPC_STREAM_AUDIO_MAIN]   = TUYA_AUDIO_SAMPLE_8K;
    encoder_info[2].audio_fps[E_IPC_STREAM_AUDIO_MAIN]      = 25;
    encoder_num                                             = 3;

    // add video encoder (device→peer, H264) —— 与 media_info av_encode_info 视频配置一致
    if (init_params->is_support_video) {
        encoder_info[encoder_num].stream_enable[E_IPC_STREAM_VIDEO_MAIN] = TRUE;
        encoder_info[encoder_num].video_codec[E_IPC_STREAM_VIDEO_MAIN]   = TUYA_CODEC_VIDEO_H264;
        encoder_info[encoder_num].video_fps[E_IPC_STREAM_VIDEO_MAIN]     = 25;
        encoder_info[encoder_num].video_gop[E_IPC_STREAM_VIDEO_MAIN]     = 25;
        encoder_info[encoder_num].video_bitrate[E_IPC_STREAM_VIDEO_MAIN] = TUYA_VIDEO_BITRATE_1M;
        encoder_info[encoder_num].video_width[E_IPC_STREAM_VIDEO_MAIN]   = 480;
        encoder_info[encoder_num].video_height[E_IPC_STREAM_VIDEO_MAIN]  = 480;
        encoder_info[encoder_num].video_freq[E_IPC_STREAM_VIDEO_MAIN]    = 90000;
        encoder_num++;
    }

    for (UINT_T i = 0; i < decoder_num; ++i) {
        if (tuya_tmm_link_add_decoder(decoder_info[i]) != OPRT_OK) {
            PR_ERR("tuya_tmm_link_add_decoder FAILED, i %u.", i);
        }
        PR_DEBUG("tuya_tmm_link_add_decoder SUCCESSFULLY: index %u, codec_id %d.", i, decoder_info[i].codec_id);
    }
    PR_DEBUG("tuya_tmm_link_add_decoder SUCCESSFULLY.");

    for (UINT_T i = 0; i < encoder_num; ++i) {
        if (tuya_tmm_link_add_encoder(encoder_info[i]) != OPRT_OK) {
            PR_ERR("tuya_tmm_link_add_decoder FAILED, i %u.", i);
        }
        PR_DEBUG("tuya_tmm_link_add_encoder SUCCESSFULLY: index %u.", i);
    }
    PR_DEBUG("tuya_tmm_link_add_encoder SUCCESSFULLY.");

    TMM_START_PARAM_T tmm_start_param = {0};
    tmm_start_param.list_change_cb = __tmm_paired_dev_list_change_cb;

    if (tuya_tmm_link_start(&tmm_start_param) != OPRT_OK) {
        PR_ERR("tuya_tmm_link_start FAILED.");
        return OPRT_COM_ERROR;
    }
    PR_DEBUG("tuya_tmm_link_start SUCCESSFULLY.");

    return OPRT_OK;
}

STATIC OPERATE_RET __tmm_ringbuf_init(TUYA_TMM_STREAM_INIT_S *init_params)
{
    BOOL_T audio_en = g_tmm_stream_ins.media_info.audio_decode_info.enable;
    BOOL_T video_en = (init_params->is_support_video &&
                       g_tmm_stream_ins.media_info.av_encode_info.stream_enable[E_IPC_STREAM_VIDEO_MAIN]);

    /* 先 init 所有启用的流的环形缓冲,再一次性 register_media_source(登记全部已建缓冲),
     * 最后 open 各自的 WRITE 句柄——顺序对齐原生 tuya_p2p_sdk 的 __init_ring_buffer/__open_ring_buffer。 */
    if (audio_en) {
        RING_BUFFER_INIT_PARAM_T ringbuf_param = {0};
        ringbuf_param.bitrate = g_tmm_stream_ins.media_info.audio_decode_info.audio_sample * g_tmm_stream_ins.media_info.audio_decode_info.audio_databits / 1024;
        ringbuf_param.fps = 25;
        /* 上行音频缓冲与原方案一致:max_buffer_seconds=0(不做多秒缓冲)。
         * 原为 4s——拥塞时会积压到数秒音频延迟;改 0 后拥塞即丢旧帧、控住延迟。 */
        ringbuf_param.max_buffer_seconds = 0;
        ringbuf_param.request_key_frame_cb = NULL;

        if (tuya_ipc_ring_buffer_init(0, 0, E_IPC_STREAM_AUDIO_MAIN, &ringbuf_param) != OPRT_OK) {
            PR_ERR("tuya_ipc_ring_buffer_init FAILED: stream %d, bitrate %u, fps %u, max_buffer_seconds %u.",
            E_IPC_STREAM_AUDIO_MAIN, ringbuf_param.bitrate, ringbuf_param.fps, ringbuf_param.max_buffer_seconds);
            return OPRT_COM_ERROR;
        }
    }

    if (video_en) {
        RING_BUFFER_INIT_PARAM_T v_param = {0};
        v_param.bitrate = g_tmm_stream_ins.media_info.av_encode_info.video_bitrate[E_IPC_STREAM_VIDEO_MAIN];
        v_param.fps = g_tmm_stream_ins.media_info.av_encode_info.video_fps[E_IPC_STREAM_VIDEO_MAIN];
        v_param.max_buffer_seconds = 0;
        v_param.request_key_frame_cb = NULL;

        if (tuya_ipc_ring_buffer_init(0, 0, E_IPC_STREAM_VIDEO_MAIN, &v_param) != OPRT_OK) {
            PR_ERR("tuya_ipc_ring_buffer_init FAILED: stream %d, bitrate %u, fps %u.",
            E_IPC_STREAM_VIDEO_MAIN, v_param.bitrate, v_param.fps);
            return OPRT_COM_ERROR;
        }
    }

    if (audio_en || video_en) {
        tuya_ipc_ring_buffer_adapter_register_media_source();
    }

    if (audio_en) {
        g_tmm_stream_ins.audio_write_handle = tuya_ipc_ring_buffer_open(0, 0, E_IPC_STREAM_AUDIO_MAIN, E_RBUF_WRITE);
        if (g_tmm_stream_ins.audio_write_handle == NULL) {
            PR_ERR("tuya_ipc_ring_buffer_open WRITE FAILED: stream %d.", E_IPC_STREAM_AUDIO_MAIN);
            return OPRT_COM_ERROR;
        }
        PR_DEBUG("tuya_ipc_ring_buffer_open SUCCESSFULLY stream:%d.", E_IPC_STREAM_AUDIO_MAIN);
    }

    if (video_en) {
        g_tmm_stream_ins.video_write_handle = tuya_ipc_ring_buffer_open(0, 0, E_IPC_STREAM_VIDEO_MAIN, E_RBUF_WRITE);
        if (g_tmm_stream_ins.video_write_handle == NULL) {
            PR_ERR("tuya_ipc_ring_buffer_open WRITE FAILED: stream %d.", E_IPC_STREAM_VIDEO_MAIN);
            return OPRT_COM_ERROR;
        }
        PR_DEBUG("tuya_ipc_ring_buffer_open SUCCESSFULLY stream:%d.", E_IPC_STREAM_VIDEO_MAIN);
    }

    return OPRT_OK;
}
