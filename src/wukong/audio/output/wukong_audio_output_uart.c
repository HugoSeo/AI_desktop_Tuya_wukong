/**
 * @file wukong_audio_output_uart.c
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
#include "tdl_comm_audio.h"
#include "wukong_audio_output.h"

STATIC VOID *s_audio_output_handle = NULL;

OPERATE_RET wukong_audio_uart_output_init(WUKONG_AUDIO_OUTPUT_CFG_T *cfg)
{
    return OPRT_OK;
}

OPERATE_RET wukong_audio_uart_output_deinit(VOID)
{
    return OPRT_OK;
}

OPERATE_RET wukong_audio_uart_output_start(VOID)
{
    return tdl_comm_audio_spk_write_stream_start();
}

OPERATE_RET wukong_audio_uart_output_write(UINT8_T *data, UINT16_T datalen)
{
    return tdl_comm_audio_spk_write_stream_data((UCHAR_T *)data, datalen);
}

OPERATE_RET wukong_audio_uart_output_stop(VOID)
{
    return tdl_comm_audio_spk_write_stream_stop();
}

OPERATE_RET wukong_audio_uart_output_set_vol(INT32_T volume)
{
    return tdl_comm_audio_spk_volume_set(volume);
}

WUKONG_AUDIO_OUTPUT_CONSUMER_T g_audio_output_consumer = {
    .init = wukong_audio_uart_output_init,
    .deinit = wukong_audio_uart_output_deinit,
    .start = wukong_audio_uart_output_start,
    .write = wukong_audio_uart_output_write,
    .stop = wukong_audio_uart_output_stop,
    .set_vol = wukong_audio_uart_output_set_vol,
};
