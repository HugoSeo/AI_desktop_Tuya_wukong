/**
 * @file uart.c
 * @brief 
 * @version 0.1
 * @date 2025-12-11
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
#include "wukong_kws.h"
#include "uart.h"
#include "wukong_ai_mode.h"
#include "tdl_comm_audio.h"

STATIC VOID_T __WAKE_UP_CB(VOID_T *arg)
{
    TAL_PR_DEBUG("=========wake up cb=========");
    wukong_kws_event(WUKONG_KWS_XIAOZHITONGXUE);
}

OPERATE_RET wukong_kws_uart_init(VOID)
{
    tdl_comm_audio_wake_up_cb_regiter(__WAKE_UP_CB);
    return OPRT_OK;
}

OPERATE_RET wukong_kws_uart_deinit(VOID)
{
    return OPRT_OK;
}

OPERATE_RET wukong_kws_uart_start(VOID)
{
    TAL_PR_DEBUG("=========kws uart start %d=========", tuya_ai_toy_trigger_mode_get());

    // 随意对话模式, 打开唤醒词，打开VAD；说话过程中，唤醒词和说话都可以打断（默认支持)
    if(AI_CHAT_SUB_FREE == tuya_ai_toy_trigger_mode_get()) {
        tdl_comm_audio_voice_cfg_set_mic(FALSE);
        tdl_comm_audio_voice_cfg_set_wake(TRUE);
        tdl_comm_audio_voice_cfg_set_vad(TRUE);
        tdl_comm_audio_voice_cfg_set_silent();
    } else {
        // 唤醒对话模式, 打开唤醒词，打开VAD，（语音播放过程中，只能唤醒词打断，或者按键打断），打断以后开MIC；
        // 音频播放期间, MIC不拾音（关闭MIC），可接收唤醒通知
        tdl_comm_audio_voice_cfg_set_mic(FALSE);
        tdl_comm_audio_voice_cfg_set_wake(TRUE);
        tdl_comm_audio_voice_cfg_set_vad(TRUE);
        tdl_comm_audio_voice_cfg_set_silent();
    }

    return OPRT_OK;
}

OPERATE_RET wukong_kws_uart_stop(VOID)
{
    TAL_PR_DEBUG("=========kws uart stop %d=========", tuya_ai_toy_trigger_mode_get());

    if(AI_CHAT_SUB_ONESHOT == tuya_ai_toy_trigger_mode_get()) {
        // 按键对话模式，关闭唤醒词，打开VAD；
        // 按键按下打开MIC(并打断当前播放)，启动VAD，30s超时（无说话或者播放）；超时之后需要重新按键开MIC，来启动VAD；
        // 音频播放期间，MIC不拾音（关闭MIC）
        tdl_comm_audio_voice_cfg_set_mic(FALSE);
        tdl_comm_audio_voice_cfg_set_wake(FALSE);
        tdl_comm_audio_voice_cfg_set_vad(TRUE);
        tdl_comm_audio_voice_cfg_set_silent();
    } else {
        // 长按对话模式，关闭唤醒词，关闭VAD；
        // 按键按下打开MIC(并打断当前播放)，按键松开关闭MIC；
        tdl_comm_audio_voice_cfg_set_mic(FALSE);
        tdl_comm_audio_voice_cfg_set_wake(FALSE);
        tdl_comm_audio_voice_cfg_set_vad(FALSE);
        wukong_ai_agent_server_vad_ctrl(FALSE);//关闭云端VAD
    }

    return OPRT_OK;
}
