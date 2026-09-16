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
#include "tuya_device_cfg.h"
#include "svc_ai_player.h"

#if defined(AI_PLAYER_SUPPORT_DEFAULT_CONSUMER) && (AI_PLAYER_SUPPORT_DEFAULT_CONSUMER == 1)
#include "tal_audio_output.h"

typedef struct {
    TAL_AUDIO_OUTPUT_HANDLE handle;
    UINT32_T sample_rate;
    UINT8_T sample_bits;
    UINT8_T channel;
} SPEAKER_CTX_T;

STATIC SPEAKER_CTX_T s_speaker_ctx = {0};
#endif

OPERATE_RET consumer_speaker_open(UINT32_T sample_rate, UINT8_T sample_bits, UINT8_T channel)
{
#if defined(AI_PLAYER_SUPPORT_DEFAULT_CONSUMER) && (AI_PLAYER_SUPPORT_DEFAULT_CONSUMER == 1)
    UINT32_T frame_time_ms = 20;

    s_speaker_ctx.sample_rate = sample_rate;
    s_speaker_ctx.sample_bits = sample_bits;
    s_speaker_ctx.channel = channel;

    TAL_AUDIO_OUTPUT_CFG_T config = {0};
    config.type = TAL_AUDIO_OUTPUT_DAC;
    config.dev.dac_config.port = 0;
    config.dev.dac_config.spk_num = 1;
    config.dev.dac_config.pa_gpio = TUYA_AI_TOY_SPK_EN_PIN;
    config.dev.dac_config.pa_active_level = TUYA_GPIO_LEVEL_HIGH;
    config.sample_bits = sample_bits;
    config.sample_rate = sample_rate;
    config.frame_time_ms = frame_time_ms;

    TAL_PR_NOTICE("consumer_speaker_open open rate=%u bits=%d ch=%u pa=%d active=%d frame_ms=%u",
        sample_rate, sample_bits, channel, TUYA_AI_TOY_SPK_EN_PIN,
        TUYA_GPIO_LEVEL_HIGH, frame_time_ms);

    s_speaker_ctx.handle = tal_audio_output_init(&config);
    return (s_speaker_ctx.handle != NULL) ? OPRT_OK : OPRT_COM_ERROR;
#else
    return OPRT_OK;
#endif
}

OPERATE_RET consumer_speaker_close(VOID)
{
#if defined(AI_PLAYER_SUPPORT_DEFAULT_CONSUMER) && (AI_PLAYER_SUPPORT_DEFAULT_CONSUMER == 1)
    if (s_speaker_ctx.handle != NULL) {
        tal_audio_output_deinit(s_speaker_ctx.handle);
        s_speaker_ctx.handle = NULL;
    }
    return OPRT_OK;
#else
    return OPRT_OK;
#endif
}

OPERATE_RET consumer_speaker_start(AI_PLAYER_HANDLE handle)
{
#if defined(AI_PLAYER_SUPPORT_DEFAULT_CONSUMER) && (AI_PLAYER_SUPPORT_DEFAULT_CONSUMER == 1)
    return tal_audio_output_start(s_speaker_ctx.handle);
#else
    return OPRT_OK;
#endif
}

OPERATE_RET consumer_speaker_write(AI_PLAYER_HANDLE handle, CONST VOID *buf, UINT_T len)
{
#if defined(AI_PLAYER_SUPPORT_DEFAULT_CONSUMER) && (AI_PLAYER_SUPPORT_DEFAULT_CONSUMER == 1)
    return tal_audio_output_write(s_speaker_ctx.handle, (UINT8_T *)buf, (UINT32_T)len);
#else
    return OPRT_OK;
#endif
}

OPERATE_RET consumer_speaker_stop(AI_PLAYER_HANDLE handle)
{
#if defined(AI_PLAYER_SUPPORT_DEFAULT_CONSUMER) && (AI_PLAYER_SUPPORT_DEFAULT_CONSUMER == 1)
    return tal_audio_output_stop(s_speaker_ctx.handle);
#else
    return OPRT_OK;
#endif
}

OPERATE_RET consumer_speaker_set_volume(UINT_T volume)
{
#if defined(AI_PLAYER_SUPPORT_DEFAULT_CONSUMER) && (AI_PLAYER_SUPPORT_DEFAULT_CONSUMER == 1)
    return tal_audio_output_set_volume(s_speaker_ctx.handle, (UINT8_T)volume);
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
