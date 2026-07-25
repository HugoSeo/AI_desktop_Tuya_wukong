/**
 * @file wukong_audio_input_uart.c
 * @brief 
 * @version 0.1
 * @date 2025-12-10
 * 
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
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

#include "tal_log.h"
#include "wukong_audio_input.h"
#include "tdl_comm_audio.h"
#include "base_event.h"
#include "wukong_audio_aec_vad.h"
#include "tuya_device_cfg.h"

STATIC WUKONG_AUDIO_INPUT_CFG_T s_audio_cfg;

STATIC INT_T __mic_upload_cb(UCHAR_T *data, UINT_T len)
{
    return s_audio_cfg.uart.mic_upload(data, len);
}

VOID_T __SILENCE_TIMEOUT_CB(VOID_T *arg)
{
    TAL_PR_DEBUG("========= uart input silence timeout cb");
}

VOID_T __RESET_CB(VOID_T *arg)
{
    TAL_PR_DEBUG("========= uart input reset cb");
    tdl_comm_audio_voice_cfg_set_vad(TRUE);
    tdl_comm_audio_voice_cfg_set_wake(TRUE);
    tdl_comm_audio_voice_cfg_set_mic(FALSE);
}

VOID_T __SPK_STATUS_CB(UINT8_T status)
{
    TAL_PR_DEBUG("========= uart input spk status cb: %d", status);
}

STATIC UINT8_T s_vad_status = TDL_COMM_AUDIO_MIC_STATUS_IDLE;
STATIC VOID_T __MIC_STATUS_CB(UINT8_T status)
{
    TAL_PR_DEBUG("========= uart input mic status cb: %d->%d", s_vad_status, status);

    if((status == TDL_COMM_AUDIO_MIC_STATUS_START) && (s_vad_status != TDL_COMM_AUDIO_MIC_STATUS_START)) {
        s_vad_status = status;
        ty_publish_event(EVENT_AUDIO_VAD, (VOID*)WUKONG_AUDIO_VAD_START);
    } else if((status == TDL_COMM_AUDIO_MIC_STATUS_STOP) && (s_vad_status != TDL_COMM_AUDIO_MIC_STATUS_STOP)) {
        s_vad_status = status;
        ty_publish_event(EVENT_AUDIO_VAD, (VOID*)WUKONG_AUDIO_VAD_STOP);
    } else {
        s_vad_status = status;
    }
}

OPERATE_RET wukong_audio_uart_input_init(WUKONG_AUDIO_INPUT_CFG_T *cfg)
{
    TAL_PR_DEBUG("========= uart input init");

    OPERATE_RET rt = OPRT_OK;
    TDL_COMM_AUDIO_CFG_T tdl_config = {0};
    TDL_COMM_AUDIO_PARAM_CFG_T param_cfg = {0};
    TDL_COMM_AUDIO_IO_CFG_T io_cfg = {0};
    TDD_COMM_AUDIO_CFG_T comm_cfg = {0};
    TDD_COMM_AUDIO_MIC_CFG_T mic_cfg = {0};
    TDD_COMM_AUDIO_SPK_CFG_T spk_cfg = {0};

    param_cfg.mic_time = 60; // 60s
    param_cfg.silnece_timeout = 30; // 30s

    io_cfg.boot_io = UART_CODEC_BOOT_IO;
    io_cfg.power_io = UART_CODEC_POWER_IO;
    io_cfg.spk_flow_io = UART_CODEC_SPK_FLOWCTL_IO;
    io_cfg.spk_flow_io_level = UART_CODEC_SPK_FLOWCTL_IO_LEVEL;
    io_cfg.mute_io_level = UART_CODEC_MUTE_IO_LEVEL;

    mic_cfg.sps = 16000;
    mic_cfg.bit = 16;
    mic_cfg.channel = 1;
    mic_cfg.gain = 20;
#if defined(UART_CODEC_UPLOAD_FORMAT) && (UART_CODEC_UPLOAD_FORMAT == 1)
    mic_cfg.type = 1; // SPEEX
#else
    mic_cfg.type = 2; // OPUS
#endif
    mic_cfg.recv_max_len = 1024;

    spk_cfg.sps = 16000;
    spk_cfg.bit = 16;
    spk_cfg.volume = 80;
    spk_cfg.type = UART_CODEC_DOWNLOAD_FORMAT;
    spk_cfg.send_max_len = 1152;

    memcpy(&s_audio_cfg, cfg, sizeof(WUKONG_AUDIO_INPUT_CFG_T));
    comm_cfg.port = UART_CODEC_UART_PORT;
    comm_cfg.mic_cfg = &mic_cfg;
    comm_cfg.spk_cfg = &spk_cfg;
    tdl_config.cfg_commum = &comm_cfg;
    tdl_config.cfg_io = &io_cfg;
    tdl_config.cfg_param = &param_cfg;
    tdl_config.mic_upload = __mic_upload_cb;
    tdl_config.module_type = UART_CODEC_VENDOR_ID ? AUDIO_MODULE_1302 : AUDIO_MODULE_8006;
    rt = tdl_comm_audio_init(&tdl_config, 0);

    tdl_comm_audio_reset_cb_regiter(__RESET_CB);
    tdl_comm_audio_spk_status_cb_regiter(__SPK_STATUS_CB);
    tdl_comm_audio_silence_timeout_cb_regiter(__SILENCE_TIMEOUT_CB);
    tdl_comm_audio_mic_status_cb_regiter(__MIC_STATUS_CB);

    return rt;
}

OPERATE_RET wukong_audio_uart_input_deinit(VOID)
{
    TAL_PR_DEBUG("========= uart input deinit");
    return OPRT_OK;
}

OPERATE_RET wukong_audio_uart_input_start(VOID)
{
    s_vad_status = TDL_COMM_AUDIO_MIC_STATUS_IDLE;
    TAL_PR_DEBUG("========= uart input start");
    return OPRT_OK;
}

OPERATE_RET wukong_audio_uart_input_stop(VOID)
{
    s_vad_status = TDL_COMM_AUDIO_MIC_STATUS_IDLE;
    TAL_PR_DEBUG("========= uart input stop");
    return OPRT_OK;
}

OPERATE_RET wukong_audio_uart_input_wakeup_mode_set(WUKONG_AUDIO_VAD_MODE_E mode)
{
    TAL_PR_DEBUG("========= uart input set wakeup mode: %d", mode);
    return OPRT_OK;
}

OPERATE_RET wukong_audio_uart_input_reset(VOID)
{
    TAL_PR_DEBUG("========= uart input reset");
    tdl_comm_audio_voice_cfg_set_mic(TRUE);

    return OPRT_OK;
}

OPERATE_RET wukong_audio_uart_input_wakeup_set(BOOL_T is_wakeup)
{
    TAL_PR_DEBUG("========= uart input wakeup set: %d", is_wakeup);

    if(is_wakeup) {
        tdl_comm_audio_voice_cfg_set_mic(TRUE);
    } else {
        tdl_comm_audio_voice_cfg_set_mic(FALSE);
        tdl_comm_audio_voice_cfg_set_silent();
    }

    return OPRT_OK;
}

WUKONG_AUDIO_INPUT_PRODUCER_T g_audio_input_producer = {
    .init = wukong_audio_uart_input_init,
    .deinit = wukong_audio_uart_input_deinit,
    .start = wukong_audio_uart_input_start,
    .stop = wukong_audio_uart_input_stop,
    .reset = wukong_audio_uart_input_reset,
    .set_wakeup = wukong_audio_uart_input_wakeup_set,
    .set_vad_mode = wukong_audio_uart_input_wakeup_mode_set,
}; 
