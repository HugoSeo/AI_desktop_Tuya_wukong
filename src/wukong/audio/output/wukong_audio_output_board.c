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
#include "tal_audio_output.h"
#include "wukong_audio_output.h"

STATIC TAL_AUDIO_OUTPUT_HANDLE s_audio_output_handle = NULL;

OPERATE_RET wukong_audio_board_output_init(WUKONG_AUDIO_OUTPUT_CFG_T *cfg)
{
    OPERATE_RET rt = OPRT_OK;
    TAL_AUDIO_OUTPUT_CFG_T config = {0};

    if (cfg == NULL) {
        TAL_PR_ERR("board output init failed: cfg is NULL");
        return OPRT_INVALID_PARM;
    }

    TAL_PR_NOTICE("board output init: rate=%u bits=%d ch=%u pa=%d active=%d frame=%u",
                  cfg->sample_rate, cfg->sample_bits, cfg->channel,
                  cfg->pa_gpio, cfg->pa_active_level, cfg->frame_time_ms);

    config.type = TAL_AUDIO_OUTPUT_DAC;
    config.dev.dac_config.port = 0;
    config.dev.dac_config.spk_num = 1;
    config.dev.dac_config.pa_gpio = cfg->pa_gpio;
    config.dev.dac_config.pa_active_level = cfg->pa_active_level;
    config.sample_bits = cfg->sample_bits;
    config.sample_rate = cfg->sample_rate;
    config.frame_time_ms = cfg->frame_time_ms;

    s_audio_output_handle = tal_audio_output_init(&config);
    if (s_audio_output_handle == NULL) {
        TAL_PR_ERR("board output init failed");
        return OPRT_COM_ERROR;
    }

    TAL_PR_NOTICE("board output init done: handle=%p ret=%d", s_audio_output_handle, rt);
    return rt;
}

OPERATE_RET wukong_audio_board_output_deinit(VOID)
{
    OPERATE_RET rt;

    TAL_PR_NOTICE("board output deinit: handle=%p", s_audio_output_handle);
    rt = tal_audio_output_deinit(s_audio_output_handle);
    TAL_PR_NOTICE("board output deinit done: handle=%p ret=%d", s_audio_output_handle, rt);
    s_audio_output_handle = NULL;
    return rt;
}

OPERATE_RET wukong_audio_board_output_start(VOID)
{
    OPERATE_RET rt;

    rt = tal_audio_output_start(s_audio_output_handle);
    TAL_PR_NOTICE("board output start: handle=%p ret=%d", s_audio_output_handle, rt);
    return rt;
}

OPERATE_RET wukong_audio_board_output_write(UINT8_T *data, UINT_T datalen)
{
    OPERATE_RET rt;

    rt = tal_audio_output_write(s_audio_output_handle, data, (UINT32_T)datalen);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("board output write failed: handle=%p len=%u ret=%d",
                   s_audio_output_handle, datalen, rt);
    }
    return rt;
}

OPERATE_RET wukong_audio_board_output_stop(VOID)
{
    OPERATE_RET rt;

    rt = tal_audio_output_stop(s_audio_output_handle);
    TAL_PR_NOTICE("board output stop: handle=%p ret=%d", s_audio_output_handle, rt);
    return rt;
}

OPERATE_RET wukong_audio_board_output_set_vol(INT32_T volume)
{
    OPERATE_RET rt;

    rt = tal_audio_output_set_volume(s_audio_output_handle, volume);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("board output set volume failed: handle=%p volume=%d ret=%d",
                   s_audio_output_handle, volume, rt);
    }
    return rt;
}

WUKONG_AUDIO_OUTPUT_CONSUMER_T g_audio_output_consumer = {
    .init = wukong_audio_board_output_init,
    .deinit = wukong_audio_board_output_deinit,
    .start = wukong_audio_board_output_start,
    .write = wukong_audio_board_output_write,
    .stop = wukong_audio_board_output_stop,
    .set_vol = wukong_audio_board_output_set_vol,
};
