/**
 * @file tuya_tmm_stream.h
 * @author baijue.huang@tuya.com
 * @brief tuya tmm stream
 * @version 0.1
 * @date 2022-8-31
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#ifndef __TUYA_TMM_STREAM_H__
#define __TUYA_TMM_STREAM_H__

/***********************************************************************
 ** INCLUDE                                                           **
 **********************************************************************/
#include "tuya_cloud_types.h"
#include "tuya_cloud_com_defs.h"

#include "tuya_ipc_media_adapter.h"
#include "tuya_ipc_media_stream.h"
#include "tuya_ipc_media.h"
#include "tuya_tmm_link.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EVENT_TMM_STREAM_READY          "tmm.stream.ready"

/***********************************************************************
 ** CONSTANT ( MACRO AND ENUM )                                       **
 **********************************************************************/

/***********************************************************************
 ** STRUCT                                                            **
 **********************************************************************/

typedef struct {
    CHAR_T                             audio_codec[16];
    INT_T                              audio_sample;
    INT_T                              audio_databits;
    INT_T                              audio_channel;

    UINT64_T                           timestamp; //in milliseconds
    unsigned long long                 seq;
} TUYA_TMM_STREAM_AUDIO_USER_DATA_S;

// 推拉流中的音频帧报文；
typedef struct {
    VOID                              *p_data;
    UINT_T                             data_len;
    TUYA_TMM_STREAM_AUDIO_USER_DATA_S  user_data;
} TUYA_TMM_STREAM_AUDIO_DATA_S;

typedef struct {
    TUYA_CODEC_ID_E                    video_codec;
    MEIDA_RECV_VIDEO_FRAME_TYPE_E      video_frame_type;
    UINT_T                             width;
    UINT_T                             height;
    UINT64_T                           timestamp; //in milliseconds
    unsigned long long                 seq;
} TUYA_TMM_STREAM_VIDEO_UESR_DATA_S;

// 推拉流中的视频帧报文；
typedef struct {
    VOID                              *p_data;
    UINT_T                             data_len;
    TUYA_TMM_STREAM_VIDEO_UESR_DATA_S  user_data;
} TUYA_TMM_STREAM_VIDEO_DATA_S;

typedef enum {
    TUYA_TMM_STREAM_EVT_SPEAKER_START,            //开始播放
    TUYA_TMM_STREAM_EVT_SPEAKER_STOP,             //停止播放
    TUYA_TMM_STREAM_EVT_VIDEO_START,              //开始推视频流
    TUYA_TMM_STREAM_EVT_VIDEO_STOP,               //停止推视频流
    TUYA_TMM_STREAM_EVT_AUDIO_START,              //开始推音频流
    TUYA_TMM_STREAM_EVT_AUDIO_STOP,               //停止推音频流

    TUYA_TMM_STREAM_EVT_VIDEO_SEND_START,         //对端请求被拉视频流
    TUYA_TMM_STREAM_EVT_VIDEO_SEND_STOP,          //对端请求停止被拉视频流
    TUYA_TMM_STREAM_EVT_AUDIO_SEND_START,         //对端请求被拉音频流
    TUYA_TMM_STREAM_EVT_AUDIO_SEND_STOP,          //对端请求停止被拉音频流
} TUYA_TMM_STREAM_EVENT_E;

typedef OPERATE_RET(*TUYA_TMM_STREAM_EVENT_CB)(TUYA_TMM_STREAM_EVENT_E event);
typedef OPERATE_RET (*TUYA_TMM_STREAM_AUDIO_OUTPUT_CB)(TUYA_TMM_STREAM_AUDIO_DATA_S *in);
typedef OPERATE_RET (*TUYA_TMM_STREAM_VIDEO_OUTPUT_CB)(TUYA_TMM_STREAM_VIDEO_DATA_S *in);
 
typedef struct {
	BOOL_T                             is_support_audio;
	TUYA_TMM_STREAM_AUDIO_OUTPUT_CB    audio_output_cb;
	
	BOOL_T                             is_support_video;
	TUYA_TMM_STREAM_VIDEO_OUTPUT_CB    video_output_cb;

    TUYA_TMM_STREAM_EVENT_CB           event_cb;
} TUYA_TMM_STREAM_INIT_S;

typedef struct {
    CHAR_T                             dev_id[DEV_ID_LEN + 1];
    CHAR_T                             local_key[LOCAL_KEY_LEN + 1];

    BOOL_T                             enable_send_audio;
    BOOL_T                             enable_send_video;
    BOOL_T                             enable_recv_audio;
    BOOL_T                             enable_recv_video;
} TUYA_TMM_STREAM_CONF_S;

/***********************************************************************
 ** VARIABLE                                                          **
 **********************************************************************/

/***********************************************************************
 ** FUNCTON                                                           **
 **********************************************************************/

/*****************************************************************************
 * @brief      tuya tmm stream 初始化
 *
 * @param      init_params: 初始化参数
 * @return
 *     - 0， success
 *     - 非0 error
*****************************************************************************/
OPERATE_RET tuya_tmm_stream_init(TUYA_TMM_STREAM_INIT_S *init_params);

/**
 * @brief Whether TMM stream init (including skill upload) has completed
 * @return TRUE if stream stack is ready for VoIP media
 */
BOOL_T tuya_tmm_stream_is_ready(VOID);

/*****************************************************************************
 * @brief      tuya tmm stream 注销
 *
 * @return
 *     - 0， success
 *     - 非0 error
*****************************************************************************/
OPERATE_RET tuya_tmm_stream_deinit(VOID);

/*****************************************************************************
 * @brief      tuya tmm stream p2p建立连接
 *
 * @param      conf: 连接配置参数
 * @return
 *     - 0， success
 *     - 非0 error
*****************************************************************************/
OPERATE_RET tuya_tmm_stream_connect(TUYA_TMM_STREAM_CONF_S *conf);

/*****************************************************************************
 * @brief      tuya tmm stream p2p断开连接
 *
 * @param      conf: 断开连接配置参数
 * @return
 *     - 0， success
 *     - 非0 error
*****************************************************************************/
OPERATE_RET tuya_tmm_stream_disconnect(TUYA_TMM_STREAM_CONF_S *conf);

/*****************************************************************************
 * @brief      tuya tmm stream 控制
 *
 * @param      conf: 控制配置参数
 * @return
 *     - 0， success
 *     - 非0 error
*****************************************************************************/
OPERATE_RET tuya_tmm_stream_ctrl(TUYA_TMM_STREAM_CONF_S *conf);

/*****************************************************************************
 * @brief      tuya tmm stream 发送音频数据
 *
 * @param      data: 音频数据结构
 * @return
 *     - 0， success
 *     - 非0 error
*****************************************************************************/
OPERATE_RET tuya_tmm_stream_audio_send(TUYA_TMM_STREAM_AUDIO_DATA_S *data);

/**
 * @brief Reset uplink audio ring buffer state for a new call session
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_tmm_stream_audio_uplink_reset(VOID);

/*****************************************************************************
 * @brief      tuya tmm stream 发送视频数据(设备→对端,已编码 H264 帧)
 *
 * @param      data:       视频帧(p_data/data_len/user_data.timestamp 有效)
 * @param      is_i_frame: TRUE=I 帧(关键帧),FALSE=P/B 帧
 * @return
 *     - 0， success
 *     - 非0 error
 * @note       仅在 init 时 is_support_video=TRUE(视频发送环形缓冲已建立)时有效;
 *             否则返回 OPRT_NOT_SUPPORTED。视频帧节拍由上游相机编码器决定,
 *             不经 jitter 缓冲,直接写入 E_IPC_STREAM_VIDEO_MAIN 环形缓冲。
*****************************************************************************/
OPERATE_RET tuya_tmm_stream_video_send(TUYA_TMM_STREAM_VIDEO_DATA_S *data, BOOL_T is_i_frame);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_TMM_STREAM_H__ */
