/**
 * @file wukong_audio_output_board.c
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
#include "wukong_audio_output.h"

STATIC VOID *s_audio_output_handle = NULL;

OPERATE_RET wukong_audio_board_output_init(WUKONG_AUDIO_OUTPUT_CFG_T *cfg)
{
    TKL_AUDIO_CONFIG_T audio_output_cfg = {0};
    audio_output_cfg.codectype = TKL_CODEC_AUDIO_PCM;
    audio_output_cfg.sample = cfg->sample_rate;
    audio_output_cfg.datebits = cfg->sample_bits;
    audio_output_cfg.channel = cfg->channel;
    return tkl_ao_init(&audio_output_cfg, 1, &s_audio_output_handle);
}

OPERATE_RET wukong_audio_board_output_deinit(VOID)
{
    return tkl_ao_uninit(s_audio_output_handle);
}

OPERATE_RET wukong_audio_board_output_start(VOID)
{
    return tkl_ao_start(TKL_AUDIO_TYPE_BOARD, 0, s_audio_output_handle);
}

OPERATE_RET wukong_audio_board_output_write(UINT8_T *data, UINT16_T datalen)
{
    TKL_AUDIO_FRAME_INFO_T frame = {0};
    frame.pbuf = (CHAR_T *)data;
    frame.used_size = datalen;
    frame.type = TKL_AUDIO_FRAME;
    frame.codectype = TKL_CODEC_AUDIO_PCM;
    frame.sample = TKL_AUDIO_SAMPLE_16K;
    frame.datebits = TKL_AUDIO_DATABITS_16;
    frame.channel = TKL_AUDIO_CHANNEL_MONO;
    return tkl_ao_put_frame(TKL_AUDIO_TYPE_BOARD, 0, s_audio_output_handle, &frame);
}

OPERATE_RET wukong_audio_board_output_stop(VOID)
{
    return tkl_ao_stop(TKL_AUDIO_TYPE_BOARD, 0, s_audio_output_handle);
}

OPERATE_RET wukong_audio_board_output_set_vol(INT32_T volume)
{
    return tkl_ao_set_vol(TKL_AUDIO_TYPE_BOARD, 0, s_audio_output_handle, volume);
}

WUKONG_AUDIO_OUTPUT_CONSUMER_T g_audio_output_consumer = {
    .init = wukong_audio_board_output_init,
    .deinit = wukong_audio_board_output_deinit,
    .start = wukong_audio_board_output_start,
    .write = wukong_audio_board_output_write,
    .stop = wukong_audio_board_output_stop,
    .set_vol = wukong_audio_board_output_set_vol,
};
