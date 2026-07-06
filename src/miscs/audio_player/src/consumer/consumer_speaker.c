/**
 * @file consumer_speaker.c
 * @brief 
 * @version 0.1
 * @date 2025-09-24
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

#include "uni_log.h"
#include "tkl_audio.h"
#include "svc_ai_player.h"

typedef struct {
    VOID *handle;
    TKL_AUDIO_SAMPLE_E sample;
    TKL_AUDIO_DATABITS_E datebits;
    TKL_AUDIO_CHANNEL_E channel;
} SPEAKER_CTX_T;

#if defined(AI_PLAYER_SUPPORT_DEFAULT_CONSUMER) && (AI_PLAYER_SUPPORT_DEFAULT_CONSUMER == 1)
STATIC SPEAKER_CTX_T s_speaker_ctx = {0};
#endif

OPERATE_RET consumer_speaker_open(TKL_AUDIO_SAMPLE_E sample, TKL_AUDIO_DATABITS_E datebits, TKL_AUDIO_CHANNEL_E channel)
{
#if defined(AI_PLAYER_SUPPORT_DEFAULT_CONSUMER) && (AI_PLAYER_SUPPORT_DEFAULT_CONSUMER == 1)
    s_speaker_ctx.sample = sample;
    s_speaker_ctx.datebits = datebits;
    s_speaker_ctx.channel = channel;

    TKL_AUDIO_CONFIG_T audio_output_cfg = {0};
    audio_output_cfg.codectype = TKL_CODEC_AUDIO_PCM;
    audio_output_cfg.sample = sample;
    audio_output_cfg.datebits = datebits;
    audio_output_cfg.channel = channel;
        
    return tkl_ao_init(&audio_output_cfg, 1, &s_speaker_ctx.handle);
#else
    return OPRT_OK;
#endif
}

OPERATE_RET consumer_speaker_close(VOID)
{
#if defined(AI_PLAYER_SUPPORT_DEFAULT_CONSUMER) && (AI_PLAYER_SUPPORT_DEFAULT_CONSUMER == 1)
    return tkl_ao_uninit(s_speaker_ctx.handle);
#else
    return OPRT_OK;
#endif
}

OPERATE_RET consumer_speaker_start(AI_PLAYER_HANDLE handle)
{
#if defined(AI_PLAYER_SUPPORT_DEFAULT_CONSUMER) && (AI_PLAYER_SUPPORT_DEFAULT_CONSUMER == 1)
    return tkl_ao_start(TKL_AUDIO_TYPE_BOARD, 0, s_speaker_ctx.handle);
#else
    return OPRT_OK;
#endif
}

OPERATE_RET consumer_speaker_write(AI_PLAYER_HANDLE handle, CONST VOID *buf, UINT_T len)
{
#if defined(AI_PLAYER_SUPPORT_DEFAULT_CONSUMER) && (AI_PLAYER_SUPPORT_DEFAULT_CONSUMER == 1)
    TKL_AUDIO_FRAME_INFO_T frame = {0};

    frame.type = TKL_AUDIO_FRAME;
    frame.codectype = TKL_CODEC_AUDIO_PCM;
    frame.sample = s_speaker_ctx.sample;
    frame.datebits = s_speaker_ctx.datebits;
    frame.channel = s_speaker_ctx.channel;
    frame.pbuf = (CHAR_T *)buf;
    frame.used_size = len;
    return tkl_ao_put_frame(0, 0, s_speaker_ctx.handle, &frame);
#else
    return OPRT_OK;
#endif
}

OPERATE_RET consumer_speaker_stop(AI_PLAYER_HANDLE handle)
{
#if defined(AI_PLAYER_SUPPORT_DEFAULT_CONSUMER) && (AI_PLAYER_SUPPORT_DEFAULT_CONSUMER == 1)
    return tkl_ao_stop(TKL_AUDIO_TYPE_BOARD, 0, s_speaker_ctx.handle);
#else
    return OPRT_OK;
#endif
}

OPERATE_RET consumer_speaker_set_volume(UINT_T volume)
{
#if defined(AI_PLAYER_SUPPORT_DEFAULT_CONSUMER) && (AI_PLAYER_SUPPORT_DEFAULT_CONSUMER == 1)
    return tkl_ao_set_vol(TKL_AUDIO_TYPE_BOARD, 0, s_speaker_ctx.handle, volume);
#else
    return OPRT_OK;
#endif
}

AI_PLAYER_CONSUMER_T g_consumer_speaker = {
    .open = consumer_speaker_open,
    .close = consumer_speaker_close,
    .start = consumer_speaker_start,
    .write = consumer_speaker_write,
    .stop = consumer_speaker_stop,
    .set_volume = consumer_speaker_set_volume
};
