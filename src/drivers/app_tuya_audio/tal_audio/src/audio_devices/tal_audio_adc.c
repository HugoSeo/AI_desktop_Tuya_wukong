/**
 * @file tal_audio_adc.c
 * @brief TAL audio ADC device adapter
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

#include <stdint.h>
#include <string.h>

#include "tal_audio_dev.h"
#include "tkl_aud_adc.h"

#ifdef AUDIO_INPUT_DEVICE_ADC

/* Includes ------------------------------------------------------------------*/

/* Private function prototypes -----------------------------------------------*/

/* Public variables ----------------------------------------------------------*/

/* Private functions ---------------------------------------------------------*/

/**
 * @brief Initialize the onboard ADC microphone device
 * @param[in] ctrl Audio input control context
 * @param[in] cfg Public audio input configuration
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_adc_init(TAL_AI_CTRL_T *ctrl, TAL_AUDIO_INPUT_CFG_T *cfg)
{
    OPERATE_RET ret = OPRT_OK;
    TKL_AUD_ADC_CFG_T onboard_mic_config;

    if ((ctrl == NULL) || (cfg == NULL)) {
        return OPRT_INVALID_PARM;
    }

    if (cfg->dev.ai_adc_conf.port >= TUYA_AUDIO_ADC_PORT_MAX) {
        return OPRT_INVALID_PARM;
    }
    onboard_mic_config.chan = ctrl->dev.ai_adc_conf.chan;
    onboard_mic_config.vol = 0;
    onboard_mic_config.sample_bits = cfg->sample_bits;
    onboard_mic_config.sample_rate = cfg->sample_rate;
    onboard_mic_config.frame_time_ms = cfg->frame_time_ms;
    onboard_mic_config.upper_cb = tal_audio_input_data_cb;
    onboard_mic_config.args = ctrl;

    ret = tkl_aud_adc_init(ctrl->dev.ai_adc_conf.port, &onboard_mic_config);
    if (ret != OPRT_OK) {
        return ret;
    }

    return ret;
}

/**
 * @brief Deinitialize the onboard ADC microphone device
 * @param[in] ctrl Audio input control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_adc_deinit(TAL_AI_CTRL_T *ctrl)
{
    OPERATE_RET ret = OPRT_OK;

    if ((ctrl == NULL) || (ctrl->dev.ai_adc_conf.port >= TUYA_AUDIO_ADC_PORT_MAX)) {
        return OPRT_INVALID_PARM;
    }

    ret = tal_audio_input_adc_stop(ctrl);
    if ((ret != OPRT_OK) && (ret != OPRT_INVALID_PARM)) {
        return ret;
    }

    tkl_aud_adc_deinit(ctrl->dev.ai_adc_conf.port);

    return OPRT_OK;
}

/**
 * @brief Set onboard ADC microphone volume
 * @param[in] ctrl Audio input control context
 * @param[in] volume Input volume in range 0 to 100
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_adc_set_volume(TAL_AI_CTRL_T *ctrl, UINT32_T volume)
{
    OPERATE_RET ret = OPRT_OK;

    if ((ctrl == NULL) || (ctrl->dev.ai_adc_conf.port >= TUYA_AUDIO_ADC_PORT_MAX)) {
        return OPRT_INVALID_PARM;
    }

    ret = tkl_aud_adc_set_vol(ctrl->dev.ai_adc_conf.port, volume);

    return ret;
}

/**
 * @brief Start onboard ADC microphone capture
 * @param[in] ctrl Audio input control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_adc_start(TAL_AI_CTRL_T *ctrl)
{
    OPERATE_RET ret = OPRT_OK;

    if ((ctrl == NULL) || (ctrl->dev.ai_adc_conf.port >= TUYA_AUDIO_ADC_PORT_MAX)) {
        return OPRT_INVALID_PARM;
    }

    ret = tkl_aud_adc_start(ctrl->dev.ai_adc_conf.port);
    if (ret != OPRT_OK) {
        return ret;
    }

    return OPRT_OK;
}

/**
 * @brief Stop onboard ADC microphone capture
 * @param[in] ctrl Audio input control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_adc_stop(TAL_AI_CTRL_T *ctrl)
{
    OPERATE_RET ret = OPRT_OK;

    if ((ctrl == NULL) || (ctrl->dev.ai_adc_conf.port >= TUYA_AUDIO_ADC_PORT_MAX)) {
        return OPRT_INVALID_PARM;
    }

    ret = tkl_aud_adc_stop(ctrl->dev.ai_adc_conf.port);
    if (ret != OPRT_OK) {
        return ret;
    }

    return OPRT_OK;
}

#endif // AUDIO_INPUT_DEVICE_ADC

