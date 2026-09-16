/**
 * @file tal_comm_audio.h
 * @brief
 * @version 0.1
 * @date 2025-08-20
 *
 * @copyright Copyright (c) 2023 Tuya Inc. All Rights Reserved.
 *
 * Permission is hereby granted, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), Under the premise of complying
 * with the license of the third-party open source software contained in the software,
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software.
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 */

#ifndef __TDD_COMM_AUDIO_H__
#define __TDD_COMM_AUDIO_H__

#include "base_event.h"
#include "tuya_cloud_types.h"
#include "tuya_cloud_com_defs.h"
#include "gfw_core/gfw_core_cmd.h"
#include "gfw_core/gfw_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GFW_EVENT_AUDIO_INIT          "gfw.evt.init"
#define GFW_EVENT_AUDIO_RESET         "gfw.evt.reset"
#define GFW_EVENT_AUDIO_WAKE_UP       "gfw.evt.wakeup"
#define GFW_EVENT_AUDIO_VERSION       "gfw.audio.ver"
#define GFW_EVENT_AUDIO_EVENT_NOTIFY  "gfw.audio.evt"

// 音频帧格式
typedef UINT8_T TDD_COMM_AUDIO_FRAME_FORMAT_E;
#define TDD_COMM_AUDIO_FRAME_FORMAT_PCM         0
#define TDD_COMM_AUDIO_FRAME_FORMAT_SPEEX       1
#define TDD_COMM_AUDIO_FRAME_FORMAT_OPUS        2
#define TDD_COMM_AUDIO_FRAME_FORMAT_MP3         3
#define TDD_COMM_AUDIO_FRAME_FORMAT_FAST_PCM    4

// 音频采样状态
typedef UINT8_T TDD_COMM_AUDIO_STATUS_E;
#define TDD_COMM_AUDIO_STATUS_VAD_START      0
#define TDD_COMM_AUDIO_STATUS_RECEIVING      1
#define TDD_COMM_AUDIO_STATUS_VAD_END        2

// 音频上传格式配置
#define TDD_COMM_AUDIO_UPLOAD_MODE_SPEEX 0     // SPEEX上传
#define TDD_COMM_AUDIO_UPLOAD_MODE_OPUS  1     // OPUS上传
#define TDD_COMM_AUDIO_UPLOAD_MODE          TDD_COMM_AUDIO_UPLOAD_MODE_OPUS

// 音频播放格式配置
#define TDD_COMM_AUDIO_CODEC_TO_MCU_MP3 1      // MP3播放
#define TDD_COMM_AUDIO_CODEC_TO_MCU_PCM 2      // MP3解码PCM播放
#define TDD_COMM_AUDIO_CODEC_TO_MCU_TYPE    TDD_COMM_AUDIO_CODEC_TO_MCU_PCM

#pragma pack(1)
typedef struct {
    UINT_T  sps;    // sample rate(16000)
    UCHAR_T bit;    // bit width(16bit)
    UCHAR_T channel; // channel(1)
    UCHAR_T gain;   // 20(0-32db)
    UCHAR_T type;   // 2(0: PCM, 1: speex, 2: opus)
    UINT_T  recv_max_len; // 1024 (max length of recv data)
} TDD_COMM_AUDIO_MIC_CFG_T;

typedef struct {
    UINT_T  sps;    // sample rate(16000)
    UCHAR_T bit;    // bit width(16bit)
    UCHAR_T volume; // volume(0-100)
    UCHAR_T type;   // 2(0: PCM, 1: speex, 2: opus, 3: mp3)
    UINT_T  send_max_len; // 1024 (max length of send data)
} TDD_COMM_AUDIO_SPK_CFG_T;
#pragma pack()

typedef enum {
    MIC_CMD_TYPE = 0x01,
    SPK_CMD_TYPE = 0x02,
    WAKE_CMD_TYPE = 0x03,
    VAD_CMD_TYPE = 0x04,
    WAKE_NOTIFY_CMD_TYPE = 0x05,
    SILENT_CMD_TYPE = 0X06,
    SILENCE_TIME_SET = 0x07,
    MIC_TIME_SET = 0x08,
    VAD_SENSITY_SET = 0x09,
    SPK_MUTE_SET = 0x0E,
    DUPLEX_SET = 0x12,
    EXTENDED_WAKEUP_ENABLE = 0x14
} TDD_COMM_AUDIO_CFG_TYPE_E;

typedef enum {
    SPK_CMD_START = 0x00,
    SPK_CMD_DATA  = 0x01,
    SPK_CMD_STOP  = 0x02,
} TDD_COMM_AUDIO_SPK_CMD_E;

typedef enum {
    TDD_COMM_AUDIO_DUPLEX_UNSET = 0x00,
    TDD_COMM_AUDIO_DUPLEX_HALF = 0x01,
    TDD_COMM_AUDIO_DUPLEX_FULL = 0x02
} TDD_COMM_AUDIO_DUPLEX_E;

typedef struct {
    TDD_COMM_AUDIO_MIC_CFG_T *mic_cfg;

    TDD_COMM_AUDIO_SPK_CFG_T *spk_cfg;

    TUYA_UART_NUM_E port;
} TDD_COMM_AUDIO_CFG_T;


typedef struct {
    UINT32_T semantic_id; // 为了向后兼容性，semantic_id必须是第一个元素
    BOOL_T energy_available;
    BOOL_T confidence_available;
    BOOL_T has_doa_angle;
    BOOL_T has_text;
    INT_T energy;
    INT_T confidence;
    INT_T doa_angle;
    CONST CHAR_T * text;
} TDD_COMM_AUDIO_WAKEUP_INFO_T;


typedef enum {
    TDD_COMM_AUDIO_SELF_LEARNING_CMD_TYPE_INVALID = -0x01,

    TDD_COMM_AUDIO_SELF_LEARNING_CMD_TYPE_ENTER = 0x01,
    TDD_COMM_AUDIO_SELF_LEARNING_CMD_TYPE_INTERRUPT = 0x02,
    TDD_COMM_AUDIO_SELF_LEARNING_CMD_TYPE_RECORD_REQUEST = 0x03,
    TDD_COMM_AUDIO_SELF_LEARNING_CMD_TYPE_RECORD_RESULT = 0x04,
    TDD_COMM_AUDIO_SELF_LEARNING_CMD_TYPE_RESULT = 0x05,
    TDD_COMM_AUDIO_SELF_LEARNING_CMD_TYPE_DELETE = 0x06,
    TDD_COMM_AUDIO_SELF_LEARNING_CMD_TYPE_QUERY = 0x07,
    TDD_COMM_AUDIO_SELF_LEARNING_CMD_TYPE_DELETE_ALL = 0x08,
    TDD_COMM_AUDIO_SELF_LEARNING_CMD_TYPE_RECORD_START = 0x09
} TDD_COMM_AUDIO_SELF_LEARNING_CMD_TYPE_E;


typedef VOID (*TDD_COMM_AUDIO_SELF_LEARNING_EVENT_CALLBACK_T)(TDD_COMM_AUDIO_SELF_LEARNING_CMD_TYPE_E type, CONST BYTE_T * payload, SIZE_T payload_size);

#define TDD_COMM_AUDIO_MAX_SUPPORTED_FORMAT_COUNT 8

typedef struct {
    TDD_COMM_AUDIO_FRAME_FORMAT_E supported_downstream_format[TDD_COMM_AUDIO_MAX_SUPPORTED_FORMAT_COUNT];
    SIZE_T supported_downstream_format_count;
    TDD_COMM_AUDIO_FRAME_FORMAT_E supported_upstream_format[TDD_COMM_AUDIO_MAX_SUPPORTED_FORMAT_COUNT];
    SIZE_T supported_upstream_format_count;
    BOOL_T supports_play_with_format;
    BOOL_T supports_spk_data_prefetch;
    BOOL_T supports_self_learning;
    BOOL_T supports_extended_wakeup;
    BOOL_T supports_doa;
} TDD_COMM_AUDIO_ABILITY_T;

/**
 * @brief 语音模组mic接收的语音数据输出接口样例，需要外部按要求实现
 *
 * @param[in] status 语音输出过程状态
 * @param[in] data 语音数据接收数组指针
 * @param[in] len 语音数据长度
 *
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
typedef VOID (*TDD_COMM_AUDIO_MIC_UPLOAD_CB)(UCHAR_T status,UCHAR_T *data, UINT_T len);

/**
 * @brief 语音组件初始化
 *
 * @param[in] cfg 配置参数
 * @param[in] timeout 模块初始化超时时间，默认15s
 * 
 * 初始化的同时，请调用tdd_comm_audio_mic_upload_regiter注册接口
 * 
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdd_comm_audio_init(TDD_COMM_AUDIO_CFG_T *cfg, UINT_T timeout);
OPERATE_RET tdd_comm_audio_init_no_wait(TDD_COMM_AUDIO_CFG_T *cfg);
OPERATE_RET tdd_comm_audio_wait_init(UINT_T max_delay_ms);
/**
 * @brief 语音组件反初始化
 *
 * 主要为低功耗进入时调用，内部会将线程空闲，关闭影响功耗的外设；
 * 退出低功耗后，直接调用tdd_comm_audio_init即可恢复
 * 
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdd_comm_audio_deinit(VOID);

/**
 * @brief 向语音传输语音数据
 *
 * @param[in] type 流程控制类型 开始传输 传输数据 结束传输
 * @param[in] data 语音数据指针
 * @param[in] len 数据长度
 * 
 * @return UINT_T spk语音数据最大值
 */
OPERATE_RET tdd_comm_audio_spk_audio_data_send(UCHAR_T cmd, UCHAR_T type, UCHAR_T *data, UINT_T len);

/**
 * @brief 语音模组spk音量参数初始化
 *
 * @return volume 音量值，范围0-100
 */
VOID tdd_comm_audio_spk_volume_init(UCHAR_T volume);

/**
 * @brief 语音模组spk音量参数设置
 *
 * @param[in] volume 音量值，范围0-100
 *
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdd_comm_audio_spk_volume_set(UCHAR_T volume);

/**
 * @brief 语音模组spk音量参数获取
 *
 * @return UCHAR_T 音量值，范围0-100
 */
UCHAR_T tdd_comm_audio_spk_volume_get(VOID);

/**
 * @brief 语音组件MIC语音数据输出接口注册
 *
 * @param[in] cb 回调接口
 * 
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdd_comm_audio_mic_upload_regiter(TDD_COMM_AUDIO_MIC_UPLOAD_CB cb);

/**
 * @brief 语音模组mic灵敏度参数设置
 *
 * @param[in] gain 灵敏度值，范围0-32db
 *
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdd_comm_audio_mic_gain_set(UCHAR_T gain);

/**
 * @brief 语音模组mic灵敏度参数获取
 *
 * @return UCHAR_T 灵敏度值，范围0-32db
 */
UCHAR_T tdd_comm_audio_mic_gain_get(VOID);

/**
 * @brief 语音模组离线语音参数设置
 *
 * @param[in] type 设置参数类型，见TDD_COMM_AUDIO_CFG_TYPE_E
 * @param[in] data 参数内容，大体为开关，时间长度等
 *
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdd_comm_audio_voice_cfg_set(TDD_COMM_AUDIO_CFG_TYPE_E type, UCHAR_T data);

/**
 * @brief 语音模组录音播音测试接口
 *
 * @param[in] time 录音时间
 * 
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdd_comm_audio_recording_and_play_test(UCHAR_T time);

/**
 * @brief 语音模组固件版本获取
 *
 * @return UCHAR_T 版本字符串
 */
CHAR_T* tdd_comm_audio_sw_ver_get(VOID);

/**
 * @brief 语音模组OTA_TP设置
 *
 * @param[in] tp OTA传输协议类型
 */
VOID_T tdd_comm_audio_ota_tp_set(DEV_TYPE_T tp);

/**
 * @brief 语音模组OTA_TP获取
 *
 * @return DEV_TYPE_T
 */
DEV_TYPE_T tdd_comm_audio_ota_tp_get(VOID);

/**
 * @brief 获取语音模组是否已经初始化
 * @param  
 * @return 
 */
BOOL_T tdd_comm_audio_is_inited(VOID);

OPERATE_RET tdd_comm_audio_init_audio_test(VOID);

/**
 * @brief 获取最后一次音频测试成功与否
 * @param  
 * @return 

 */
BOOL_T tdd_comm_audio_is_last_audio_test_success(VOID);


OPERATE_RET tdd_comm_audio_self_learning_send(TDD_COMM_AUDIO_SELF_LEARNING_CMD_TYPE_E type, CONST BYTE_T *request_payload, SIZE_T request_size);
OPERATE_RET tdd_comm_audio_self_learning_send_and_wait_response(TDD_COMM_AUDIO_SELF_LEARNING_CMD_TYPE_E type, CONST BYTE_T *request_payload, SIZE_T request_size, BYTE_T **response_payload, SIZE_T *response_size);
OPERATE_RET tdd_comm_audio_self_learning_event_register(TDD_COMM_AUDIO_SELF_LEARNING_EVENT_CALLBACK_T callback);


CONST TDD_COMM_AUDIO_ABILITY_T * tdd_comm_audio_get_ability(VOID);

#ifdef __cplusplus
}
#endif

#endif // __TAL_COMM_AUDIO_H__
      