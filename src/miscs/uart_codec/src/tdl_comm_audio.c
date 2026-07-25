/**
 * @file tuya_audio.c
 * @brief
 * @version 0.1
 * @date 2025-04-05
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

#include <stdio.h>
#include "uni_log.h"

#include "tdl_comm_audio.h"
#include "gfw_mcu_ota/gfw_mcu_ota.h"

#include "tkl_gpio.h"
#include "tal_memory.h"
#include "tal_sw_timer.h"
#include "tdd_comm_audio.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define UART_SPK_START  (0x00)
#define UART_SPK_DATA   (0x01)
#define UART_SPK_STOP   (0x02)

#define MIC_TIME_DEF            (30)
#define SILENCE_TIMEOUT_DEF     (30)

#define SW_ON   (1)
#define SW_OFF  (0)

#ifndef UART_CODEC_SPK_FLOWCTL_YIELD_MS
#define UART_CODEC_SPK_FLOWCTL_YIELD_MS    (10) // ms
#endif

typedef UINT8_T ADUIO_MIC_STATUS_T;

typedef UINT8_T AUDIO_SPK_STATUS_T;

#define MIC_STAT_CHANGE(new_stat)                                                        \
    do {                                                                                 \
        if (sg_audio_ctx.mic_status != new_stat) {                                       \
            TAL_PR_DEBUG("mic stat changed: %d->%d", sg_audio_ctx.mic_status, new_stat); \
        }                                                                                \
        sg_audio_ctx.mic_status = new_stat;                                              \
    } while (0)

#define SPK_STAT_CHANGE(new_stat)                                                        \
    do {                                                                                 \
        if (sg_audio_ctx.spk_status != new_stat) {                                       \
            TAL_PR_DEBUG("spk stat changed: %d->%d", sg_audio_ctx.spk_status, new_stat); \
        }                                                                                \
        sg_audio_ctx.spk_status = new_stat;                                              \
    } while (0)

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct {
    // param
    UCHAR_T                         mic_time;          //最大拾音时间
    UCHAR_T                         silnece_timeout;   //静默超时时间
    TIMER_ID                        silnece_tid;       //静默超时定时器

    // mic
    TDL_COMM_MIC_UPLOAD_CB          mic_upload;
    ADUIO_MIC_STATUS_T              mic_status;
    TDL_COMM_MIC_STATUS_CB          mic_status_cb;

    // spk
    AUDIO_SPK_STATUS_T              spk_status;
    TDL_COMM_SPK_STATUS_CB          spk_status_cb;

    // io
    TDL_COMM_AUDIO_MODULE_E         type;               // 语音芯片类型
    TDL_COMM_AUDIO_IO_CFG_T         audio_io_cfg;       // 语音芯片IO配置

    // ctl
    BOOL_T                          is_init;
    TDL_COMM_SILENCE_TIMEOUT_CB     silence_timeout_cb; // 静默超时回调
    TDL_COMM_WAKE_UP_CB             wake_up_cb;         // 语音唤醒回调
    TDL_COMM_SILENCE_TIMEOUT_CB     reset_cb;           // 语音芯片复位回调
} TDL_COMM_AUDIO_CONTEXT_T;


/***********************************************************
********************function declaration********************
***********************************************************/
STATIC VOID_T __audio_power_reset(VOID_T);

STATIC OPERATE_RET __uart_send_spk_packet(UCHAR_T type, UCHAR_T *data, UINT_T len);


/***********************************************************
***********************variable define**********************
***********************************************************/
STATIC TDL_COMM_AUDIO_CONTEXT_T sg_audio_ctx = {
    .mic_time           = MIC_TIME_DEF,
    .silnece_timeout    = SILENCE_TIMEOUT_DEF,
};

/***********************************************************
***********************function define**********************
***********************************************************/
// 语音芯片复位
STATIC VOID_T __audio_power_reset(VOID_T)
{
    tkl_gpio_write(sg_audio_ctx.audio_io_cfg.power_io, TUYA_GPIO_LEVEL_LOW);
    tal_system_sleep(250);
    tkl_gpio_write(sg_audio_ctx.audio_io_cfg.power_io, TUYA_GPIO_LEVEL_HIGH);
    return ;    
}

STATIC VOID_T __audio_power_off(VOID_T)
{
    tkl_gpio_write(sg_audio_ctx.audio_io_cfg.power_io, TUYA_GPIO_LEVEL_LOW);
}

/**
 * @brief 检查 UART 是否忙碌
 * 
 * 此函数通过读取指定 GPIO 引脚的电平状态来判断 UART 是否忙碌。
 * 如果读取 GPIO 电平失败，则认为 UART 忙碌。
 * 根据读取到的电平与预设的流控有效电平进行比较，从而确定 UART 的忙碌状态。
 * spk_flow_io_level 有效电平
 * 0：低电平有效
 * IO拉低表示可以发送，IO拉高表示不能发送；
 * 1：高电平有效
 * IO拉高表示可以发送，IO拉低表示不能发送；
 * 
 * @return BOOL_T 返回 TRUE 表示 UART 忙碌，返回 FALSE 表示 UART 不忙碌
 */
STATIC BOOL_T _is_uart_busy(void)
{
    #if 1
    OPERATE_RET ret = OPRT_OK;
    TUYA_GPIO_LEVEL_E read_level = TUYA_GPIO_LEVEL_LOW;
    ret = tkl_gpio_read(sg_audio_ctx.audio_io_cfg.spk_flow_io, &read_level);
    if (ret != OPRT_OK) {
        PR_ERR("Failed to read GPIO level op_ret: %d", ret);
        return TRUE;
    }

    // 和有效电平相同，可以发送
    if (read_level == sg_audio_ctx.audio_io_cfg.spk_flow_io_level) {
        return FALSE;
    }else {
        return TRUE;
    }
    
    #else
    return FALSE;
    #endif
}

STATIC VOID_T __audio_silence_timeout_cb(TIMER_ID timer_id, VOID_T *arg)
{
    TAL_PR_DEBUG("%s", __func__);

    tdl_comm_audio_voice_cfg_set_mic(SW_OFF);

    if (sg_audio_ctx.silence_timeout_cb) {
        sg_audio_ctx.silence_timeout_cb(arg);
    } else{
        TAL_PR_ERR("silence_timeout_cb is NULL");
    }
    
    
    return;
}

STATIC VOID_T __mic_staus_detect(UCHAR_T type)
{
    struct
    {
        ADUIO_MIC_STATUS_T  last_status;
        ADUIO_MIC_STATUS_T  change_status;
    }change_list[3] =
    {
        {TDL_COMM_AUDIO_MIC_STATUS_IDLE,TDL_COMM_AUDIO_MIC_STATUS_START},
        {TDL_COMM_AUDIO_MIC_STATUS_START,TDL_COMM_AUDIO_MIC_STATUS_RECV},
        {TDL_COMM_AUDIO_MIC_STATUS_RECV,TDL_COMM_AUDIO_MIC_STATUS_STOP},
    };

    if((change_list[type].last_status == sg_audio_ctx.mic_status) || (change_list[type].last_status == TDL_COMM_AUDIO_MIC_STATUS_START) || (type == TDL_COMM_AUDIO_MIC_STATUS_RECV))
    {
        MIC_STAT_CHANGE(change_list[type].change_status);

        if (sg_audio_ctx.mic_status_cb) {
            sg_audio_ctx.mic_status_cb(sg_audio_ctx.mic_status);
        } else {
            TAL_PR_DEBUG("mic_status_cb is NULL");
        }
        
        if(TDL_COMM_AUDIO_MIC_STATUS_STOP == sg_audio_ctx.mic_status)
        {
            MIC_STAT_CHANGE(TDL_COMM_AUDIO_MIC_STATUS_IDLE);
        }
    }
}

/**
 * @brief 处理麦克风音频语音上传相关操作
 * 
 * 该函数根据底层8006传入的音频状态，对外应用进行数据传输操作以及状态指示操作
 * 
 * @param status 音频状态，取值为 TDD_COMM_AUDIO_STATUS_VAD_START、TDD_COMM_AUDIO_STATUS_VAD_END 等
 * @param data 音频数据指针
 * @param len 音频数据的长度
 */
STATIC VOID_T __audio_mic_upload(UCHAR_T status, UCHAR_T *data, UINT_T len)
{
    if (!sg_audio_ctx.is_init) {
        return;
    }

    if (NULL == sg_audio_ctx.mic_upload) {
        return;
    }

    TAL_PR_TRACE("audio status:%d, len:%d", status, len);

    __mic_staus_detect(status);

    switch (status) {
        case TDD_COMM_AUDIO_STATUS_VAD_START: {
            TAL_PR_DEBUG("%s: VAD start", __func__);
        } break;

        case TDD_COMM_AUDIO_STATUS_VAD_END: {
            TAL_PR_DEBUG("%s: VAD end", __func__);
        } break;

        case TDD_COMM_AUDIO_STATUS_RECEIVING: {
            //数据传输
            sg_audio_ctx.mic_upload(data,len);
            PR_TRACE("slilence_timeout reflash:%d",sg_audio_ctx.silnece_timeout);
            tdl_comm_audio_silence_timeout_start();
        } break;

        default: {
            TAL_PR_DEBUG("Unknow type");
        } break;
    }

    return;
}


STATIC VOID_T __spk_staus_detect(UCHAR_T type)
{
    struct
    {
        AUDIO_SPK_STATUS_T  last_status;
        AUDIO_SPK_STATUS_T  change_status;
    }change_list[3] =
    {
        {TDL_COMM_AUDIO_SPK_STATUS_IDLE,TDL_COMM_AUDIO_SPK_STATUS_START},
        {TDL_COMM_AUDIO_SPK_STATUS_START,TDL_COMM_AUDIO_SPK_STATUS_PLAYING},
        {TDL_COMM_AUDIO_SPK_STATUS_PLAYING,TDL_COMM_AUDIO_SPK_STATUS_STOP},
    };

    if((change_list[type].last_status == sg_audio_ctx.spk_status))
    {
        SPK_STAT_CHANGE(change_list[type].change_status);
        if(sg_audio_ctx.spk_status_cb)
        {
            sg_audio_ctx.spk_status_cb(sg_audio_ctx.spk_status);
        }
        
        if(TDL_COMM_AUDIO_SPK_STATUS_STOP == sg_audio_ctx.spk_status)
        {
            SPK_STAT_CHANGE(TDL_COMM_AUDIO_SPK_STATUS_IDLE);
        }
    }
}
/**
 * @brief 发送扬声器数据包到UART
 *
 * 此函数用于将扬声器数据包发送到UART。它支持发送控制命令（如开始和停止）和数据帧。
 * 如果是控制命令，直接发送命令；如果是数据，将数据分帧发送。
 *
 * @param type 数据包类型，可以是UART_SPK_START、UART_SPK_STOP或UART_SPK_DATA
 * @param data 要发送的数据指针
 * @param len 要发送的数据长度
 * @return OPERATE_RET 操作结果，OPRT_OK表示成功，其他值表示失败
 */
STATIC OPERATE_RET __uart_send_spk_packet(UCHAR_T type, UCHAR_T *data, UINT_T len)
{
    UINT32_T offset = 0;
    UINT32_T send_len = 0;
    OPERATE_RET rt = OPRT_OK;
    STATIC UINT_T busy_cnt = 0;
    STATIC UINT_T send_cnt = 0;

    if(!sg_audio_ctx.is_init){
        return OPRT_OK;
    }

    // MCU升级中
    if (gfw_is_in_mcu_ota_upgrade()) {
        return OPRT_OK;
    }

    // MCU升级完，重启中
    if (gfw_is_in_mcu_ota_reset()) {
        return OPRT_OK;
    }

    // PR_DEBUG("uart send spk packet, type:%d, len:%d", type, len);
    __spk_staus_detect(type);

    // PR_DEBUG("slilence_timeout reflash:%d",sg_audio_ctx.silnece_timeout);
    tdl_comm_audio_silence_timeout_start();

    if (type == UART_SPK_START) {
        send_cnt = 0;
    }

    if (type == UART_SPK_DATA) {
        send_cnt += len;
    }

    if (type == UART_SPK_STOP) {
        TAL_PR_DEBUG("uart send total:%d", send_cnt);
    }

    if (type == UART_SPK_START || type == UART_SPK_STOP) {
        rt = tdd_comm_audio_spk_audio_data_send(type, NULL, 0);
        if (rt != OPRT_OK) {
            PR_ERR("spk send failed, rt:%d\n", rt);
            return rt;
        }
        tal_system_sleep(10);
        return OPRT_OK;
    }

    if (type != UART_SPK_DATA) {
        PR_ERR("[ERROR] Invalid type\n");
        return OPRT_INVALID_PARM;
    }

    // 数据分帧发送
    UINT_T send_buf_size = tdd_comm_audio_spk_send_max_len_get();
    // PR_DEBUG("send buf size:%d, send len:%d", send_buf_size, len);
    extern BOOL_T is_playing_alert;
    while (offset < len) {
        while (_is_uart_busy()) {
            tal_system_sleep(5);
            busy_cnt++;
            // 流控10秒超时，语音芯片重启，中断本次播放
            if (busy_cnt > ((6*1000)/5)) {
                PR_NOTICE("flow control timeout, restart voice chip");
                __audio_power_reset();
                busy_cnt = 0;
                return OPRT_OK;;
            }
        }
        
        busy_cnt = 0;
        send_len = (len - offset > send_buf_size) ? send_buf_size : (len - offset);

        // 发送数据
        // PR_DEBUG("send frame, type:%d, offset:%d, send_len:%d", type, offset, send_len);
        rt = tdd_comm_audio_spk_audio_data_send(type, data + offset, send_len);
        if (rt != OPRT_OK) {
            PR_ERR("[ERROR] Send frame failed (offset=%u)\n", offset);
            return rt;
        }
        offset += send_len;
        tal_system_sleep(UART_CODEC_SPK_FLOWCTL_YIELD_MS);
    }

    return OPRT_OK;
}

STATIC OPERATE_RET __audio_wake_up_cb(VOID_T *data)
{
    TAL_PR_DEBUG("audio wake up");

    if(sg_audio_ctx.wake_up_cb)
    {
        sg_audio_ctx.wake_up_cb(data);
    }

    return OPRT_OK;
}

STATIC OPERATE_RET __audio_init_cb(VOID_T *data)
{
    TAL_PR_DEBUG("audio init");

    tdd_comm_audio_voice_cfg_set(SILENCE_TIME_SET,sg_audio_ctx.silnece_timeout);
    tdd_comm_audio_voice_cfg_set(MIC_TIME_SET,sg_audio_ctx.mic_time);

    if (sg_audio_ctx.type == AUDIO_MODULE_1302) {
        tdd_comm_audio_voice_cfg_set(SPK_MUTE_SET, sg_audio_ctx.audio_io_cfg.mute_io_level);
    }

    return OPRT_OK;
}

STATIC OPERATE_RET __audio_reset_cb(VOID_T *data)
{
    TAL_PR_DEBUG("audio reset");

    tdl_comm_audio_voice_cfg_set_silence_timeout(sg_audio_ctx.silnece_timeout);
    tdl_comm_audio_voice_cfg_set_mic_time(sg_audio_ctx.mic_time);

    if (sg_audio_ctx.type == AUDIO_MODULE_1302) {
        tdd_comm_audio_voice_cfg_set(SPK_MUTE_SET, sg_audio_ctx.audio_io_cfg.mute_io_level);
    }

    if(sg_audio_ctx.reset_cb)
    {
        sg_audio_ctx.reset_cb(data);
    }

    return OPRT_OK;
}


/***********************************************************
*******************eternal function define******************
***********************************************************/
OPERATE_RET tdl_comm_audio_power_on(TUYA_GPIO_NUM_E boot_io,TUYA_GPIO_NUM_E power_io)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_GPIO_BASE_CFG_T boot_cfg = {
        .direct = TUYA_GPIO_OUTPUT,
        .level = TUYA_GPIO_LEVEL_HIGH,
        .mode = TUYA_GPIO_PUSH_PULL,
    };

    if (sg_audio_ctx.type == AUDIO_MODULE_8006) {
        TUYA_CALL_ERR_RETURN(tkl_gpio_init(boot_io, &boot_cfg));
        TUYA_CALL_ERR_RETURN(tkl_gpio_write(boot_io, TUYA_GPIO_LEVEL_HIGH));
    }
    
    TUYA_GPIO_BASE_CFG_T power_cfg = {
        .direct = TUYA_GPIO_OUTPUT,
        .level = TUYA_GPIO_LEVEL_LOW,
        .mode = TUYA_GPIO_PUSH_PULL,
    };

    TUYA_CALL_ERR_RETURN(tkl_gpio_init(power_io, &power_cfg));
    TUYA_CALL_ERR_RETURN(tkl_gpio_write(power_io, TUYA_GPIO_LEVEL_LOW));
    tal_system_sleep(25);
    TUYA_CALL_ERR_RETURN(tkl_gpio_write(power_io, TUYA_GPIO_LEVEL_HIGH));

    return OPRT_OK;
}

OPERATE_RET tdl_comm_audio_init(TDL_COMM_AUDIO_CFG_T *cfg, UINT_T timeout)
{
    OPERATE_RET rt = OPRT_COM_ERROR;

    PR_DEBUG("audio init start");
    
    if((NULL == &cfg->cfg_io))
    {
        return OPRT_INVALID_PARM;
    }

    if(cfg->cfg_param)
    {
        sg_audio_ctx.mic_time = cfg->cfg_param->mic_time;
        sg_audio_ctx.silnece_timeout = cfg->cfg_param->silnece_timeout;
    }

    sg_audio_ctx.type = cfg->module_type;
    memcpy(&sg_audio_ctx.audio_io_cfg, cfg->cfg_io, SIZEOF(TDL_COMM_AUDIO_IO_CFG_T));
    sg_audio_ctx.mic_upload = cfg->mic_upload;

    tdl_comm_audio_power_on(sg_audio_ctx.audio_io_cfg.boot_io,sg_audio_ctx.audio_io_cfg.power_io);

    //串口流控IO初始化
    TUYA_GPIO_BASE_CFG_T base_cfg = {
        .direct = TUYA_GPIO_INPUT,
        .level = TUYA_GPIO_LEVEL_LOW,
        .mode = TUYA_GPIO_FLOATING,
    };

    PR_DEBUG("flow io:%d level:%d", sg_audio_ctx.audio_io_cfg.spk_flow_io, sg_audio_ctx.audio_io_cfg.spk_flow_io_level);
    TUYA_CALL_ERR_RETURN(tkl_gpio_init(sg_audio_ctx.audio_io_cfg.spk_flow_io, &base_cfg));

    if (!sg_audio_ctx.is_init) {
        tal_sw_timer_create(__audio_silence_timeout_cb, NULL, &sg_audio_ctx.silnece_tid);

        TUYA_CALL_ERR_RETURN(ty_subscribe_event(GFW_EVENT_AUDIO_WAKE_UP, "audio_wake_up", __audio_wake_up_cb, SUBSCRIBE_TYPE_NORMAL));
        TUYA_CALL_ERR_RETURN(ty_subscribe_event(GFW_EVENT_AUDIO_INIT, "audio_init", __audio_init_cb, SUBSCRIBE_TYPE_NORMAL));
        TUYA_CALL_ERR_RETURN(ty_subscribe_event(GFW_EVENT_AUDIO_RESET, "audio_reset", __audio_reset_cb, SUBSCRIBE_TYPE_NORMAL));
    }
    else//低功耗重入
    {
        PR_DEBUG("audio init again");
    }

    tdd_comm_audio_mic_upload_regiter(__audio_mic_upload);
    rt = tdd_comm_audio_init(cfg->cfg_commum,timeout);

    if(OPRT_OK == rt)
    {
        sg_audio_ctx.is_init = TRUE;
        PR_DEBUG("audio init success!");
    }
    else
    {
        PR_DEBUG("audio init timeout!");
    }

    return rt;
}

OPERATE_RET tdl_comm_audio_deinit()
{
    __audio_power_off();

    tkl_gpio_deinit(sg_audio_ctx.audio_io_cfg.power_io);
    tkl_gpio_deinit(sg_audio_ctx.audio_io_cfg.spk_flow_io);
    if (sg_audio_ctx.type == AUDIO_MODULE_8006) {
        tkl_gpio_deinit(sg_audio_ctx.audio_io_cfg.boot_io);
    }

    return tdd_comm_audio_deinit();
}

OPERATE_RET tdl_comm_audio_spk_write_stream_start(VOID_T)
{
    if (!sg_audio_ctx.is_init) 
    {
        return OPRT_OK;
    }

    return __uart_send_spk_packet(UART_SPK_START,NULL,0);
}

OPERATE_RET tdl_comm_audio_spk_write_stream_data(UCHAR_T *data, UINT_T len)
{
    if (!sg_audio_ctx.is_init) 
    {
        return OPRT_OK;
    }

    return __uart_send_spk_packet(UART_SPK_DATA,data,len);
}

OPERATE_RET tdl_comm_audio_spk_write_stream_stop(VOID_T)
{
    if (!sg_audio_ctx.is_init) 
    {
        return OPRT_OK;
    }

    return __uart_send_spk_packet(UART_SPK_STOP,NULL,0);
}

OPERATE_RET tdl_comm_audio_spk_volume_set(UCHAR_T volume)
{
    if (!sg_audio_ctx.is_init) 
    {
        return OPRT_OK;
    }

    return tdd_comm_audio_spk_volume_set(volume);
}

UCHAR_T tdl_comm_audio_spk_volume_get(VOID_T)
{
    return tdd_comm_audio_spk_volume_get();
}

OPERATE_RET tdl_comm_audio_mic_gain_set(UCHAR_T gain)
{
    if (!sg_audio_ctx.is_init) 
    {
        return OPRT_OK;
    }

    return tdd_comm_audio_mic_gain_set(gain);
}

UCHAR_T tdl_comm_audio_mic_gain_get(VOID_T)
{
    return tdd_comm_audio_mic_gain_get();
}

OPERATE_RET tdl_comm_audio_voice_cfg_set_mic(BOOL_T sw)
{
    if (!sg_audio_ctx.is_init) 
    {
        return OPRT_OK;
    }
	
    return tdd_comm_audio_voice_cfg_set(MIC_CMD_TYPE,sw);
}

OPERATE_RET tdl_comm_audio_voice_cfg_set_spk(BOOL_T sw)
{
    if (!sg_audio_ctx.is_init) 
    {
        return OPRT_OK;
    }

    return tdd_comm_audio_voice_cfg_set(SPK_CMD_TYPE,sw);
}

OPERATE_RET tdl_comm_audio_voice_cfg_set_wake(BOOL_T sw)
{
    if (!sg_audio_ctx.is_init) 
    {
        return OPRT_OK;
    }

    return tdd_comm_audio_voice_cfg_set(WAKE_CMD_TYPE,sw);
}

OPERATE_RET tdl_comm_audio_voice_cfg_set_wake_notify(VOID_T)
{
    if (!sg_audio_ctx.is_init) 
    {
        return OPRT_OK;
    }

    return tdd_comm_audio_voice_cfg_set(WAKE_NOTIFY_CMD_TYPE,0X01);
}

OPERATE_RET tdl_comm_audio_voice_cfg_set_vad(UCHAR_T cfg)
{
    if (!sg_audio_ctx.is_init) 
    {
        return OPRT_OK;
    }

    return tdd_comm_audio_voice_cfg_set(VAD_CMD_TYPE,cfg);
}

OPERATE_RET tdl_comm_audio_voice_cfg_set_silent(VOID_T)
{
    if (!sg_audio_ctx.is_init) 
    {
        return OPRT_OK;
    }

    return tdd_comm_audio_voice_cfg_set(SILENT_CMD_TYPE,0X01);
}

OPERATE_RET tdl_comm_audio_voice_cfg_set_silence_timeout(CHAR_T timeout)
{
    if (!sg_audio_ctx.is_init) 
    {
        return OPRT_OK;
    }

    OPERATE_RET rt = OPRT_OK;

    rt =  tdd_comm_audio_voice_cfg_set(SILENCE_TIME_SET,timeout);
    if(OPRT_OK == rt)
    {
        sg_audio_ctx.silnece_timeout = timeout;
    }
    
    return rt;
}

CHAR_T tdl_comm_audio_voice_cfg_get_silence_timeout(VOID_T)
{
    return sg_audio_ctx.silnece_timeout;
}

OPERATE_RET tdl_comm_audio_silence_timeout_start(VOID_T)
{
    return tal_sw_timer_start(sg_audio_ctx.silnece_tid, sg_audio_ctx.silnece_timeout*1000, TAL_TIMER_ONCE);
}

VOID_T tdl_comm_audio_silence_timeout_cb_regiter(TDL_COMM_SILENCE_TIMEOUT_CB cb)
{
    sg_audio_ctx.silence_timeout_cb = cb;
    return;
}

OPERATE_RET tdl_comm_audio_voice_cfg_set_mic_time(CHAR_T time)
{
    if (!sg_audio_ctx.is_init) 
    {
        return OPRT_OK;
    }

    OPERATE_RET rt = OPRT_OK;

    rt =  tdd_comm_audio_voice_cfg_set(MIC_TIME_SET,time);
    if(OPRT_OK == rt)
    {
        sg_audio_ctx.mic_time = time;
    }

    return rt;
}

CHAR_T tdl_comm_audio_voice_cfg_get_mic_time(VOID_T)
{
    return sg_audio_ctx.mic_time;
}

OPERATE_RET tdl_comm_audio_voice_cfg_set_mute_level(UCHAR_T level)
{
    if (!sg_audio_ctx.is_init) 
    {
        return OPRT_OK;
    }

    OPERATE_RET rt = OPRT_OK;

    return tdd_comm_audio_voice_cfg_set(SPK_MUTE_SET, level);
}

VOID_T tdl_comm_audio_wake_up_cb_regiter(TDL_COMM_WAKE_UP_CB cb)
{
    if (!sg_audio_ctx.is_init) 
    {
        return;
    }

    sg_audio_ctx.wake_up_cb = cb;
    return;
}

VOID_T tdl_comm_audio_reset_cb_regiter(TDL_COMM_RESET_CB cb)
{
    if (!sg_audio_ctx.is_init) 
    {
        return;
    }

    sg_audio_ctx.reset_cb = cb;
    return;
}

OPERATE_RET tdl_comm_audio_recording_and_play_test(UCHAR_T time)
{
    if (!sg_audio_ctx.is_init) 
    {
        return OPRT_OK;
    }

    return tdd_comm_audio_recording_and_play_test(time);
}

DEV_TYPE_T tdl_comm_audio_ota_tp_get(VOID)
{
    return tdd_comm_audio_ota_tp_get();
}

CHAR_T* tdl_comm_audio_sw_ver_get(VOID)
{
    return tdd_comm_audio_sw_ver_get();
}

BOOL_T tdl_comm_audio_init_flag_get(VOID)
{
    return sg_audio_ctx.is_init;
}

VOID_T tdl_comm_audio_mic_status_cb_regiter(TDL_COMM_MIC_STATUS_CB cb)
{
    if (!sg_audio_ctx.is_init) 
    {
        return;
    }

    sg_audio_ctx.mic_status_cb = cb;

    return;   
}

VOID_T tdl_comm_audio_spk_status_cb_regiter(TDL_COMM_SPK_STATUS_CB cb)
{
    if (!sg_audio_ctx.is_init) 
    {
        return;
    }

    sg_audio_ctx.spk_status_cb = cb;

    return;
}

OPERATE_RET tdl_comm_audio_mcu_ota_notify_cb(GW_PERMIT_DEV_TP_T tp,INT_T result)
{
    if (!sg_audio_ctx.is_init) 
    {
        return OPRT_OK;
    }

    if (tdd_comm_audio_ota_tp_get() != tp)
    {
        return OPRT_INVALID_PARM;
    }
    else
    {
        return gfw_mcu_ota_dev_ug_notify_cb(result);
    }
}

OPERATE_RET tdl_comm_audio_mcu_ota_inform_cb(FW_UG_S *fw)
{
    if (!sg_audio_ctx.is_init) 
    {
        return OPRT_OK;
    }
    
    if(tdd_comm_audio_ota_tp_get() != fw->tp)
    {
        return OPRT_INVALID_PARM;
    }
    else
    {
        return gfw_mcu_ota_ug_inform_cb(fw);
    }
}

OPERATE_RET tdl_comm_audio_test_start(TDL_COMM_AUDIO_TEST_CHANNEL channel, BOOL_T* is_success, UINT_T timeout_ms) {
    OPERATE_RET rt;
    rt = tdd_comm_audio_init_audio_test();
    if (rt != OPRT_OK) {
        PR_ERR("Failed to init audio test with err code %d", rt);
        return rt;
    }
    const UINT8_T cmd_data[] = {GFW_0x92_AUDIO_TEST, channel};
    rt = gfw_core_cmd_send_with_params(
        AUDIO_GFW_CMD_TYPE_SYNC, 
        GFW_CMD_VOICE_SERV, cmd_data, 
        sizeof(cmd_data), 
        timeout_ms,
        NULL,
        0);
    if (rt != OPRT_OK){
        PR_ERR("Audio test start command send failed, rt: %d", rt);
        *is_success = FALSE;
        return rt;
    }
    *is_success = tdd_comm_audio_is_last_audio_test_success();
    return OPRT_OK;
}


OPERATE_RET tdl_comm_audio_test_end(BOOL_T* is_success, UINT_T timeout_ms) {
    OPERATE_RET rt;
    rt = tdd_comm_audio_init_audio_test();
    if (rt != OPRT_OK) {
        PR_ERR("Failed to init audio test with err code %d", rt);
        return rt;
    }
    const cmd_data[] = {GFW_0x92_AUDIO_TEST, 0xFF};
    rt = gfw_core_cmd_send_with_params(
        AUDIO_GFW_CMD_TYPE_SYNC, 
        GFW_CMD_VOICE_SERV, 
        cmd_data, 
        sizeof(cmd_data), 
        timeout_ms,
        NULL,
        0);
    if (rt != OPRT_OK){
        PR_ERR("Audio test start command send failed, rt: %d", rt);
        *is_success = FALSE;
        return rt;
    }
    *is_success = tdd_comm_audio_is_last_audio_test_success();
    return OPRT_OK;
    
}