#include "tuya_p2p_sdk.h"
#include <stdio.h>
#include "uni_log.h"
#include "base_event_info.h"
#include "tuya_ring_buffer.h"
#include "tuya_ipc_skill.h"
#include "tuya_ipc_mqtt_subscribe.h"
#include "tuya_devos_utils.h"
#include "tuya_media_service_rtc.h"


#define MAX_IPC_CHANNEL_NUM  1
RING_BUFFER_USER_HANDLE_T g_a_handle[MAX_IPC_CHANNEL_NUM] = {NULL};
RING_BUFFER_USER_HANDLE_T g_v_handle[MAX_IPC_CHANNEL_NUM] = {NULL};
STATIC BOOL_T s_ring_buffer_inited[MAX_IPC_CHANNEL_NUM] = {FALSE};

OPERATE_RET TUYA_APP_Put_Frame(RING_BUFFER_USER_HANDLE_T handle, IN CONST MEDIA_FRAME_T *p_frame);

STATIC bool __ipc_media_adapter_init(TUYA_IPC_MEDIA_ADAPTER_VAR_T *pMediaVar, IPC_MEDIA_INFO_T *pMediaInfo)
{
    TUYA_IPC_MEDIA_ADAPTER_VAR_T media_var = {0};
    memcpy(&media_var, pMediaVar, sizeof(TUYA_IPC_MEDIA_ADAPTER_VAR_T));
    if (tuya_ipc_media_adapter_init(&media_var) != OPRT_OK) 
    {
        return false;
    }

    DEVICE_MEDIA_INFO_T device_media_info = {0};
    memcpy(&device_media_info.av_encode_info, pMediaInfo, sizeof(IPC_MEDIA_INFO_T));
    
    device_media_info.audio_decode_info.enable = 1;
    device_media_info.audio_decode_info.audio_codec = TUYA_CODEC_AUDIO_MP3;
    device_media_info.audio_decode_info.audio_sample = TUYA_AUDIO_SAMPLE_8K;
    /*下发音频参数指定*/
    device_media_info.audio_decode_info.audio_databits = TUYA_AUDIO_DATABITS_16;
    device_media_info.audio_decode_info.audio_channel = TUYA_AUDIO_CHANNEL_MONO;
    device_media_info.max_pic_len = 100;

#ifdef VIDEO_DECODE //上报视频解码能力，用于双向视频功能
    TUYA_DECODER_T video_decoder = {0};
    video_decoder.codec_id = TUYA_CODEC_VIDEO_H264;
    video_decoder.decoder_desc.v_decoder.height = 320;
    video_decoder.decoder_desc.v_decoder.width = 240;
    video_decoder.decoder_desc.v_decoder.profile = VIDEO_AVC_PROFILE_BASE_LINE;
    device_media_info.decoder_cnt = 1;
    device_media_info.decoders = &video_decoder;
#endif

    if (tuya_ipc_media_adapter_set_media_info(0, 0, device_media_info) != OPRT_OK) 
    {
        return false;
    }

    return true;
}

STATIC OPERATE_RET __init_ring_buffer(CONST IPC_MEDIA_INFO_T *pMediaInfo, INT_T channel)
{
    if(NULL == pMediaInfo)
	{
		PR_DEBUG("create ring buffer para is NULL\n");
		return -1;
	}
	OPERATE_RET ret = OPRT_OK;

    if(s_ring_buffer_inited[channel] == TRUE)
    {
        PR_DEBUG("The Ring Buffer Is Already Inited");
        return OPRT_OK;
    }

    IPC_STREAM_E ringbuffer_stream_type;
   // CHANNEL_E channel;
    RING_BUFFER_INIT_PARAM_T param={0};
    for( ringbuffer_stream_type = E_IPC_STREAM_VIDEO_MAIN; ringbuffer_stream_type < E_IPC_STREAM_MAX; ringbuffer_stream_type++ )
    {
        PR_DEBUG("init ring buffer Channel:%d Stream:%d Enable:%d", channel, ringbuffer_stream_type, pMediaInfo->stream_enable[ringbuffer_stream_type]);
        if(pMediaInfo->stream_enable[ringbuffer_stream_type] == TRUE)
        {
            if(ringbuffer_stream_type == E_IPC_STREAM_AUDIO_MAIN)
            {
                param.bitrate = pMediaInfo->audio_sample[E_IPC_STREAM_AUDIO_MAIN]*pMediaInfo->audio_databits[E_IPC_STREAM_AUDIO_MAIN]/1024;
                param.fps = pMediaInfo->audio_fps[E_IPC_STREAM_AUDIO_MAIN];
                param.max_buffer_seconds = 0;
                param.request_key_frame_cb = NULL;
                PR_DEBUG("audio_sample %d, audio_databits %d, audio_fps %d",pMediaInfo->audio_sample[E_IPC_STREAM_AUDIO_MAIN],pMediaInfo->audio_databits[E_IPC_STREAM_AUDIO_MAIN],pMediaInfo->audio_fps[E_IPC_STREAM_AUDIO_MAIN]);
                ret = tuya_ipc_ring_buffer_init(0, channel, ringbuffer_stream_type,&param);
            }
            else
            {
                param.bitrate = pMediaInfo->video_bitrate[ringbuffer_stream_type];
                param.fps = pMediaInfo->video_fps[ringbuffer_stream_type];
                param.max_buffer_seconds = 0;
                param.request_key_frame_cb = NULL;
                PR_DEBUG("video_bitrate %d, video_fps %d",pMediaInfo->video_bitrate[ringbuffer_stream_type],pMediaInfo->video_fps[ringbuffer_stream_type]);
                ret = tuya_ipc_ring_buffer_init(0, channel, ringbuffer_stream_type, &param);
            }
            if(ret != 0)
            {
                PR_ERR("init ring buffer fails. %d %d", ringbuffer_stream_type, ret);
                return OPRT_MALLOC_FAILED;
            }
            PR_DEBUG("init ring buffer success. channel:%d", ringbuffer_stream_type);
        }
    }

    s_ring_buffer_inited[channel] = TRUE;

    return 0;
}

STATIC OPERATE_RET __open_ring_buffer(INT_T channel)
{
    if (g_v_handle[0] != NULL) 
    {
        OPRT_COM_ERROR;
    }
    g_v_handle[0] = tuya_ipc_ring_buffer_open(0, channel, E_IPC_STREAM_VIDEO_MAIN, E_RBUF_WRITE);
    g_a_handle[0] = tuya_ipc_ring_buffer_open(0, channel, E_IPC_STREAM_AUDIO_MAIN, E_RBUF_WRITE);;
    return OPRT_OK;
}

STATIC OPERATE_RET __p2p_put_frame(RING_BUFFER_USER_HANDLE_T handle, IN CONST MEDIA_FRAME_T *p_frame)
{
    PR_TRACE("Put Frame. type:%d size:%u pts:%llu ts:%llu",
             p_frame->type, p_frame->size, p_frame->pts, p_frame->timestamp);

    OPERATE_RET ret = tuya_ipc_ring_buffer_append_data(handle,p_frame->p_buf, p_frame->size,p_frame->type,p_frame->pts);

    if(ret != OPRT_OK)
    {
        PR_ERR("Put Frame Fail.%d  type:%d size:%u pts:%llu ts:%llu",ret,
                  p_frame->type, p_frame->size, p_frame->pts, p_frame->timestamp);
    }
    else{
        PR_TRACE("Put Frame Success. type:%d size:%u pts:%llu ts:%llu",
                  p_frame->type, p_frame->size, p_frame->pts, p_frame->timestamp);
    }
    return ret;
}

OPERATE_RET tuya_p2p_sdk_put_video_frame(IN CONST MEDIA_FRAME_T *p_frame)
{
    return __p2p_put_frame(g_v_handle[0], p_frame);
}

OPERATE_RET tuya_p2p_sdk_put_audio_frame(IN CONST MEDIA_FRAME_T *p_frame)
{
    return __p2p_put_frame(g_a_handle[0], p_frame);
}

STATIC OPERATE_RET __OnIotInited(void *data)
{
#if defined(ENABLE_CLOUD_OPERATION) && (ENABLE_CLOUD_OPERATION==1)
    cloud_operation_init();
    upload_notification_init();
#endif

    //支持DP NULL 查询
    //sf_dp_set_delete_null_dp(FALSE);
    //mqtt extra init cb
    tuya_ipc_mqtt_register_cb_init();
    //使能skill
    TUYA_IPC_SKILL_PARAM_U skill_param = {.value = 1};
    tuya_ipc_skill_enable(TUYA_IPC_SKILL_LOWPOWER, &skill_param);

    //设置激活的skill参数
    CHAR_T *ipc_skills = NULL;
#if defined(HARDWARE_INFO_CHECK) && (HARDWARE_INFO_CHECK==1) 
    int len = 4096;
    ipc_skills = (CHAR_T *)Malloc(len);
#else    
    int len = 256;
    ipc_skills = (CHAR_T *)Malloc(len);
#endif
    memset(ipc_skills, 0, len);
    if(ipc_skills)
    {
        strcpy(ipc_skills, "\"skillParam\":\"");
        snprintf(ipc_skills+strlen(ipc_skills),len-strlen(ipc_skills),"{\\\"type\\\":%d,\\\"skill\\\":",TUYA_P2P); //p2p类型
        tuya_ipc_http_fill_skills_cb(ipc_skills);
#if defined(HARDWARE_INFO_CHECK) && (HARDWARE_INFO_CHECK==1)   
        TUYA_IPC_SENSOR_INFO_T sensor_info = {0};    
        tuya_ipc_hardware_info_fill(ipc_skills, &sensor_info);
#endif        
        strcat(ipc_skills,"}\"");
    }
    gw_active_set_ext_param(ipc_skills);

#if defined(ENABLE_IPC_4G)&&(ENABLE_IPC_4G==1)
    tuya_ipc_dev_manager_init();
#endif

    TAL_PR_NOTICE("OnIotInited success");
    return 0;
}

OPERATE_RET tuya_p2p_sdk_subscribe_iot_init_event(VOID)
{
    return ty_subscribe_event(EVENT_INIT, "iot inited", __OnIotInited, 0);
}

OPERATE_RET tuya_ipc_album_panorama_start(IN OUT C2C_CMD_IO_CTRL_ALBUM_DOWNLOAD_START *parm)
{
    return 0;
}

OPERATE_RET tuya_ipc_album_panorama_cancel(IN C2C_ALBUM_DOWNLOAD_CANCEL *parm)
{
    return 0;
}

OPERATE_RET tuya_ipc_album_panorama_query(IN C2C_QUERY_ALBUM_REQ* parm)
{
    return 0;
}

OPERATE_RET tuya_p2p_sdk_init(TUYA_IPC_SDK_VAR_S *sdk_var)
{
    OPERATE_RET ret = OPRT_OK;
    //初始化Adapter中间件
    TUYA_IPC_MEDIA_ADAPTER_VAR_T media_var = {0};
    IPC_MEDIA_INFO_T media_info = {0};
    memcpy(&media_var, &sdk_var->media_var, sizeof(TUYA_IPC_MEDIA_ADAPTER_VAR_T));
    memcpy(&media_info, &sdk_var->media_info, sizeof(IPC_MEDIA_INFO_T));
    if (!__ipc_media_adapter_init(&media_var, &media_info) ) 
    {
        TAL_PR_NOTICE("media adapter init is error\n");
        return OPRT_INVALID_PARM;
    }
    //初始化P2P组件
    ret = tuya_ipc_media_stream_init(&sdk_var->media_adatper_info);
    if (ret != OPRT_OK) {
        TAL_PR_ERR("media stream init failed: %d", ret);
        return ret;
    }
    
    //初始化RingBuffer
    ret = __init_ring_buffer(&media_info, 0);
    if(OPRT_OK != ret)
    {
        TAL_PR_NOTICE("create ring buffer is error\n");
        return ret;
    }
    ret = __open_ring_buffer(0);
    tuya_ipc_ring_buffer_adapter_register_media_source();
    //设置激活的skill参数并上报
    TUYA_IPC_SKILL_PARAM_U skill_param = {.value = 0};
    skill_param.value = tuya_p2p_rtc_get_skill(); 
    tuya_ipc_skill_enable(TUYA_IPC_SKILL_P2P, &skill_param);
    skill_param.value = 1;
    tuya_ipc_skill_enable(TUYA_IPC_SKILL_LOWPOWER, &skill_param);
    skill_param.value = 1;
    tuya_ipc_skill_enable(TUYA_IPC_SKILL_PX, &skill_param);
    tuya_ipc_upload_skills();
    //返回结果
    TAL_PR_NOTICE("tuya_p2p_sdk_init success\n");
    return ret;
}

OPERATE_RET tuya_p2p_sdk_end(VOID)
{
    tuya_ipc_media_stream_deinit();
    return 0;
}
