/**
 * @file tal_audio_i2s.c
 * @brief TAL audio I2S device adapter (input RX + output TX)
 * @version 2.0
 * @date 2026-04-12
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

#include <stdint.h>
#include <string.h>

#include "tal_audio_dev.h"
#include "tkl_i2s.h"

#ifdef AUDIO_INPUT_DEVICE_I2S

/* ---------------------------------------------------------------------------
 * Input (RX) functions
 * --------------------------------------------------------------------------- */

/**
 * @brief Initialize I2S input device
 * @param[in] ctrl Input control context
 * @param[in] cfg Public audio input configuration
 * @return OPRT_OK on success, error code on failure
 * @note Wires tal_audio_input_data_cb as the RX callback into the TKL layer,
 *       matching the ADC/DMIC upper_cb pattern.
 */
OPERATE_RET tal_audio_input_i2s_init(TAL_AI_CTRL_T *ctrl, TAL_AUDIO_INPUT_CFG_T *cfg)
{
    TUYA_I2S_BASE_CFG_T i2s_config;

    if ((ctrl == NULL) || (cfg == NULL)) {
        return OPRT_INVALID_PARM;
    }

    if (cfg->dev.ai_i2s_conf.port >= TUYA_I2S_NUM_MAX) {
        return OPRT_INVALID_PARM;
    }

    memset(&i2s_config, 0, sizeof(TUYA_I2S_BASE_CFG_T));
    i2s_config.mode = cfg->dev.ai_i2s_conf.mode;
    i2s_config.sample_rate = cfg->sample_rate;
    i2s_config.bits_per_sample = cfg->sample_bits;
    i2s_config.channel_format = cfg->dev.ai_i2s_conf.data_format;
    i2s_config.communication_format = cfg->dev.ai_i2s_conf.i2s_protocol;
    i2s_config.i2s_dma_flags = cfg->dev.ai_i2s_conf.use_dma;
    i2s_config.upper_rx_cb = (VOID_T *)tal_audio_input_data_cb;
    i2s_config.rx_args = ctrl;

    return tkl_i2s_init(cfg->dev.ai_i2s_conf.port, &i2s_config);
}

/**
 * @brief Deinitialize I2S input device
 * @param[in] ctrl Input control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_i2s_deinit(TAL_AI_CTRL_T *ctrl)
{
    if (ctrl == NULL || ctrl->dev.ai_i2s_conf.port >= TUYA_I2S_NUM_MAX) {
        return OPRT_INVALID_PARM;
    }

    tkl_i2s_recv_stop(ctrl->dev.ai_i2s_conf.port);

    return tkl_i2s_deinit(ctrl->dev.ai_i2s_conf.port);
}

/**
 * @brief Set I2S input volume
 * @param[in] ctrl Input control context
 * @param[in] volume Input volume
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_i2s_set_volume(TAL_AI_CTRL_T *ctrl, UINT32_T volume)
{
    if (ctrl == NULL || ctrl->dev.ai_i2s_conf.port >= TUYA_I2S_NUM_MAX) {
        return OPRT_INVALID_PARM;
    }

    return tkl_i2s_set_vol(ctrl->dev.ai_i2s_conf.port, volume);
}

/**
 * @brief Start I2S input capture
 * @param[in] ctrl Input control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_i2s_start(TAL_AI_CTRL_T *ctrl)
{
    if (ctrl == NULL || ctrl->dev.ai_i2s_conf.port >= TUYA_I2S_NUM_MAX) {
        return OPRT_INVALID_PARM;
    }

    return tkl_i2s_start(ctrl->dev.ai_i2s_conf.port);
}

/**
 * @brief Stop I2S input capture
 * @param[in] ctrl Input control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_i2s_stop(TAL_AI_CTRL_T *ctrl)
{
    if (ctrl == NULL || ctrl->dev.ai_i2s_conf.port >= TUYA_I2S_NUM_MAX) {
        return OPRT_INVALID_PARM;
    }

    return tkl_i2s_recv_stop(ctrl->dev.ai_i2s_conf.port);
}

/* ---------------------------------------------------------------------------
 * Output (TX) functions
 * --------------------------------------------------------------------------- */

/**
 * @brief Initialize I2S output device
 * @param[in] ctrl Output control context
 * @param[in] cfg Public audio output configuration
 * @return OPRT_OK on success, error code on failure
 * @note Wires tal_audio_output_frame_cb as the TX frame-done callback,
 *       matching the DAC frame_cb pattern.
 */
OPERATE_RET tal_audio_output_i2s_init(TAL_AO_CTRL_T *ctrl, TAL_AUDIO_OUTPUT_CFG_T *cfg)
{
    TUYA_I2S_BASE_CFG_T i2s_config;

    if ((ctrl == NULL) || (cfg == NULL)) {
        return OPRT_INVALID_PARM;
    }

    if (cfg->dev.i2s_config.port >= TUYA_I2S_NUM_MAX) {
        return OPRT_INVALID_PARM;
    }

    memset(&i2s_config, 0, sizeof(TUYA_I2S_BASE_CFG_T));
    i2s_config.mode = cfg->dev.i2s_config.mode;
    i2s_config.sample_rate = cfg->sample_rate;
    i2s_config.bits_per_sample = cfg->sample_bits;
    i2s_config.channel_format = cfg->dev.i2s_config.data_format;
    i2s_config.communication_format = cfg->dev.i2s_config.i2s_protocol;
    i2s_config.i2s_dma_flags = 1;
    i2s_config.upper_tx_cb = (VOID_T *)tal_audio_output_frame_cb;
    i2s_config.tx_args = ctrl;

    return tkl_i2s_init(cfg->dev.i2s_config.port, &i2s_config);
}

/**
 * @brief Deinitialize I2S output device
 * @param[in] ctrl Output control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_i2s_deinit(TAL_AO_CTRL_T *ctrl)
{
    if (ctrl == NULL || ctrl->dev.i2s_config.port >= TUYA_I2S_NUM_MAX) {
        return OPRT_INVALID_PARM;
    }

    tkl_i2s_send_stop(ctrl->dev.i2s_config.port);

    return tkl_i2s_deinit(ctrl->dev.i2s_config.port);
}

/**
 * @brief Start I2S output playback
 * @param[in] ctrl Output control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_i2s_start(TAL_AO_CTRL_T *ctrl)
{
    if (ctrl == NULL || ctrl->dev.i2s_config.port >= TUYA_I2S_NUM_MAX) {
        return OPRT_INVALID_PARM;
    }

    return tkl_i2s_start(ctrl->dev.i2s_config.port);
}

/**
 * @brief Stop I2S output playback
 * @param[in] ctrl Output control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_i2s_stop(TAL_AO_CTRL_T *ctrl)
{
    if (ctrl == NULL || ctrl->dev.i2s_config.port >= TUYA_I2S_NUM_MAX) {
        return OPRT_INVALID_PARM;
    }

    return tkl_i2s_send_stop(ctrl->dev.i2s_config.port);
}

/**
 * @brief Set I2S output volume
 * @param[in] ctrl Output control context
 * @param[in] volume Output volume
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_i2s_set_volume(TAL_AO_CTRL_T *ctrl, UINT32_T volume)
{
    if (ctrl == NULL || ctrl->dev.i2s_config.port >= TUYA_I2S_NUM_MAX) {
        return OPRT_INVALID_PARM;
    }

    return tkl_i2s_set_vol(ctrl->dev.i2s_config.port, volume);
}

/**
 * @brief Write PCM data to I2S output
 * @param[in] ctrl Output control context
 * @param[in] buffer PCM data buffer
 * @param[in] len Buffer length in bytes
 * @return OPRT_OK on success, OPRT_OS_ADAPTER_DAC_BUSY when ring buffer full
 */
OPERATE_RET tal_audio_output_i2s_write(TAL_AO_CTRL_T *ctrl, UINT8_T *buffer, UINT32_T len)
{
    if (ctrl == NULL || ctrl->dev.i2s_config.port >= TUYA_I2S_NUM_MAX) {
        return OPRT_INVALID_PARM;
    }

    return tkl_i2s_send(ctrl->dev.i2s_config.port, buffer, len);
}

#endif // AUDIO_INPUT_DEVICE_I2S
