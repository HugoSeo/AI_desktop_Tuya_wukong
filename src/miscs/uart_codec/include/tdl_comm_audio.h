/**
 * @file tdl_comm_audio.h
 * @brief
 * @version 0.1
 * @date 2025-11-05
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

#ifndef __TDL_COMM_AUDIO_H__
#define __TDL_COMM_AUDIO_H__

#include "tuya_cloud_types.h"
#include "tdd_comm_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TDL_COMM_AUDIO_MIC_STATUS_IDLE  0
#define TDL_COMM_AUDIO_MIC_STATUS_START 1
#define TDL_COMM_AUDIO_MIC_STATUS_RECV  2
#define TDL_COMM_AUDIO_MIC_STATUS_STOP  3

#define TDL_COMM_AUDIO_SPK_STATUS_IDLE    0
#define TDL_COMM_AUDIO_SPK_STATUS_START   1
#define TDL_COMM_AUDIO_SPK_STATUS_PLAYING 2
#define TDL_COMM_AUDIO_SPK_STATUS_STOP    3

typedef INT_T (*TDL_COMM_MIC_UPLOAD_CB)(UCHAR_T *data, UINT_T len);
typedef VOID_T (*TDL_COMM_SILENCE_TIMEOUT_CB)(VOID_T *arg);
typedef VOID_T (*TDL_COMM_WAKE_UP_CB)(VOID_T *arg);
typedef VOID_T (*TDL_COMM_RESET_CB)(VOID_T *arg);
typedef VOID_T (*TDL_COMM_MIC_STATUS_CB)(UINT8_T status);
typedef VOID_T (*TDL_COMM_SPK_STATUS_CB)(UINT8_T status);

typedef enum {
    AUDIO_MODULE_8006 = 0,    // 国芯微GX_8006
    AUDIO_MODULE_1302 = 1,    // 启英泰伦CI_1302
} TDL_COMM_AUDIO_MODULE_E;

typedef struct {
    UCHAR_T                 mic_time;           //MIC最大拾音时间
    UCHAR_T                 silnece_timeout;    //静默超时时间
}TDL_COMM_AUDIO_PARAM_CFG_T;

typedef struct {
    TUYA_GPIO_NUM_E         spk_flow_io;        // 串口流控 IO
    TUYA_GPIO_LEVEL_E       spk_flow_io_level;  // 串口流控有效电平
    TUYA_GPIO_NUM_E         power_io;           // 语音芯片供电控制，高电平工作
    TUYA_GPIO_NUM_E         boot_io;            // 语音芯片BOOT(国芯微GX_8006使用)
    TUYA_GPIO_NUM_E         mute_io_level;      // mute有效电平
}TDL_COMM_AUDIO_IO_CFG_T;

typedef struct {
    TDL_COMM_AUDIO_PARAM_CFG_T  *cfg_param;         //基础参数

    TDL_COMM_AUDIO_IO_CFG_T     *cfg_io;            //基础控制IO

    TDD_COMM_AUDIO_CFG_T        *cfg_commum;        //通讯相关

    TDL_COMM_MIC_UPLOAD_CB      mic_upload;         //上层mic语音数据输入接口

    TDL_COMM_AUDIO_MODULE_E    module_type;         //语音芯片类型
}TDL_COMM_AUDIO_CFG_T;

typedef enum {
    TDL_COMM_AUDIO_TEST_CHANNEL_MIC = 0x00,
    TDL_COMM_AUDIO_TEST_CHANNEL_REF = 0x01
} TDL_COMM_AUDIO_TEST_CHANNEL;

/**
 * @brief audio模组上电初始化
 *
 * @param[in] cfg 配置参数
 * @param[in] timeout 模块初始化超时时间，默认15s
 * 
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdl_comm_audio_power_on(TUYA_GPIO_NUM_E boot_io,TUYA_GPIO_NUM_E power_io);

/**
 * @brief audio codec组件初始化
 *
 * @param[in] cfg 配置参数,其中cfg_io，mic_upload必填，其余有参数有默认值
 * @param[in] timeout 模块初始化超时时间，默认15s
 * 
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdl_comm_audio_init(TDL_COMM_AUDIO_CFG_T *cfg, UINT_T timeout);

/**
 * @brief 向SPK写语音数据-开始阶段
 *
 * @return OPERATE_RET 操作结果，返回成功或失败的状态码。
 */
OPERATE_RET tdl_comm_audio_spk_write_stream_start(VOID_T);

/**
 * @brief 向SPK写语音数据-写数据阶段
 *
 * @param[in] data 语音数据来源指针
 * @param[in] len 长度
 *
 * @return OPERATE_RET 操作结果，返回成功或失败的状态码。
 */
OPERATE_RET tdl_comm_audio_spk_write_stream_data(UCHAR_T *data, UINT_T len);

/**
 * @brief 向SPK写语音数据-结束阶段
 *
 * @return OPERATE_RET 操作结果，返回成功或失败的状态码。
 */
OPERATE_RET tdl_comm_audio_spk_write_stream_stop(VOID_T);

/**
 * @brief audio模组spk音量参数设置
 *
 * @param[in] volume 音量值，范围0-100
 *
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdl_comm_audio_spk_volume_set(UCHAR_T volume);

/**
 * @brief audio模组spk音量参数获取
 *
 * @return UCHAR_T 音量值，范围0-100
 */
UCHAR_T tdl_comm_audio_spk_volume_get(VOID_T);

/**
 * @brief audio模组mic灵敏度参数设置
 *
 * @param[in] gain 灵敏度值，范围0-32db
 *
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdl_comm_audio_mic_gain_set(UCHAR_T gain);

/**
 * @brief audio模组mic灵敏度参数获取
 *
 * @return UCHAR_T 灵敏度值，范围0-32db
 */
UCHAR_T tdl_comm_audio_mic_gain_get(VOID_T);

/**
 * @brief audio模组离线语音参数设置-MIC开关
 *
 * @param[in] sw 功能开关
 *
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdl_comm_audio_voice_cfg_set_mic(BOOL_T sw);

/**
 * @brief audio模组离线语音参数设置-SPK开关
 *
 * @param[in] sw 功能开关
 *
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdl_comm_audio_voice_cfg_set_spk(BOOL_T sw);

/**
 * @brief audio模组离线语音参数设置-mute有效电平
 *
 * @param[in] level 有效电平
 *
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdl_comm_audio_voice_cfg_set_mute_level(UCHAR_T level);

/**
 * @brief audio模组离线语音参数设置-唤醒词开关
 *
 * @param[in] sw 功能开关
 *
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdl_comm_audio_voice_cfg_set_wake(BOOL_T sw);

/**
 * @brief audio模组离线语音参数设置-唤醒通知
 * 
 * 用于模组通知语音芯片设备被唤醒
 *
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdl_comm_audio_voice_cfg_set_wake_notify(VOID_T);

/**
 * @brief audio模组离线语音参数设置-vad控制
 *
 * @param[in] cfg 0x00-关闭 0x01-开启，使用固件里默认的vad timeout 其余-多少帧静默会触发timeout(1帧10ms)
 *
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdl_comm_audio_voice_cfg_set_vad(UCHAR_T cfg);

/**
 * @brief audio模组离线语音参数设置-静默
 *
 *
 * 设置唤醒超时，立即进入静默状态
 * 
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdl_comm_audio_voice_cfg_set_silent(VOID_T);

/**
 * @brief audio模组离线语音参数设置-静默超时时间
 *
 * @param[in] timeout 超时时间（单位s），范围：30-180，默认30
 *
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdl_comm_audio_voice_cfg_set_silence_timeout(CHAR_T timeout);


/**
 * @brief audio模组离线语音参数获取-静默超时时间
 *
 * @return CHAR_T 参数结果
 */
CHAR_T tdl_comm_audio_voice_cfg_get_silence_timeout(VOID_T);

/**
 * @brief audio模组启动静默超时定时器
 *
 * 模块内部会准备一个定时器用于静默超时定时
 * 
 * @return OPERATE_RET 操作结果
 */
OPERATE_RET tdl_comm_audio_silence_timeout_start(VOID_T);

/**
 * @brief audio模组启动静默超时回调
 *
 * 模块内部静默超时定时器超时后调用，供应用实现功能
 * 
 */
VOID_T tdl_comm_audio_silence_timeout_cb_regiter(TDL_COMM_SILENCE_TIMEOUT_CB cb);

/**
 * @brief audio模组离线语音参数设置-最大拾音时间
 *
 * @param[in] time 单位s，范围：10-60，默认30
 *
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdl_comm_audio_voice_cfg_set_mic_time(CHAR_T time);

/**
 * @brief audio模组离线语音参数获取-最大拾音时间
 *
 * @return CHAR_T 参数结果
 */
CHAR_T tdl_comm_audio_voice_cfg_get_mic_time(VOID_T);

/**
 * @brief audio模组被语音唤醒后回调
 *
 * 模块内部被语音唤醒后后调用，供应用实现功能
 * 
 */
VOID_T tdl_comm_audio_wake_up_cb_regiter(TDL_COMM_WAKE_UP_CB cb);

/**
 * @brief audio模组启动复位回调
 *
 * 模块内部复位后调用，供应用实现功能，复位触发与语音芯片版本上报绑定，
 * 语音芯片每次上电会主动上报版本，主要用于语音芯片OTA或异常复位之后的通知
 * 
 */
VOID_T tdl_comm_audio_reset_cb_regiter(TDL_COMM_RESET_CB cb);

/**
 * @brief 语音模组录音播音测试接口
 *
 * @param[in] time 录音时间
 * 
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功
 */
OPERATE_RET tdl_comm_audio_recording_and_play_test(UCHAR_T time);

/**
 * @brief 语音模组固件OTA_TP获取
 *
 * @return DEV_TYPE_T tp号
 */
DEV_TYPE_T tdl_comm_audio_ota_tp_get(VOID);

/**
 * @brief 语音模组固件版本获取
 *
 * @return UCHAR_T 版本字符串
 */
CHAR_T* tdl_comm_audio_sw_ver_get(VOID);

BOOL_T tdl_comm_audio_init_flag_get(VOID);

/**
 * @brief audio模组MIC状态回调
 *
 * 为外部提供MIC收音阶段的变化提示
 * 
 */
VOID_T tdl_comm_audio_mic_status_cb_regiter(TDL_COMM_MIC_STATUS_CB cb);

/**
 * @brief audio模组SPK状态回调
 *
 * 为外部提供SPK放音阶段的变化提示
 * 
 */
VOID_T tdl_comm_audio_spk_status_cb_regiter(TDL_COMM_SPK_STATUS_CB cb);

/**
 * @brief MCU OTA 结束回调
 * 
 * @param tp 通道号
 * @param result ota结果
 * @return 执行结果,返回 OPRT_OK 表示成功，返回 其他 则表示失败 
 */
OPERATE_RET tdl_comm_audio_mcu_ota_notify_cb(GW_PERMIT_DEV_TP_T tp,INT_T result);

/**
 * @brief MCU OTA 启动回调
 * @param[in] fw 文件信息
 * @return 执行结果,返回 OPRT_OK 表示成功，返回 其他 则表示失败
 */
OPERATE_RET tdl_comm_audio_mcu_ota_inform_cb(FW_UG_S *fw);

/**
 * @brief 获取SPK发送最大长度
 * @param[in] none
 * @return UINT_T spk发送最大长度
 */
UINT_T tdd_comm_audio_spk_send_max_len_get(VOID);

BOOL_T tdl_comm_audio_is_inited(VOID);

OPERATE_RET tdl_comm_audio_test_start(TDL_COMM_AUDIO_TEST_CHANNEL channel, BOOL_T *is_success, UINT_T timeout_ms);
OPERATE_RET tdl_comm_audio_test_end(BOOL_T *is_success, UINT_T timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // __TUYA_AUDIO_H__
