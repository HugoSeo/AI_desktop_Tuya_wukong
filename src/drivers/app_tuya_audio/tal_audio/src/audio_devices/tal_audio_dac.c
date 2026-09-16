/**
 * @file tal_audio_dac.c
 * @brief TAL audio DAC device adapter
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

/* Includes ------------------------------------------------------------------*/
#include <string.h>

#include "tal_audio_dev.h"
#include "tkl_aud_dac.h"
#include "tkl_gpio.h"
#include "tkl_memory.h"

#include "uni_log.h"

/* Public macros -------------------------------------------------------------*/

/* Public typedefs -----------------------------------------------------------*/

/* Public variables ----------------------------------------------------------*/

/* function prototypes ------------------------------------------------*/

/**
 * @brief Get the inactive PA GPIO level from the active level
 * @param[in] active_level Power amplifier active level
 * @return Inactive GPIO level
 */
STATIC TUYA_GPIO_LEVEL_E __dac_pa_inactive_level(TUYA_GPIO_LEVEL_E active_level)
{
    return (active_level == TUYA_GPIO_LEVEL_LOW)? TUYA_GPIO_LEVEL_HIGH: TUYA_GPIO_LEVEL_LOW;
}

/**
 * @brief Check whether a valid PA control GPIO is configured
 * @param[in] pin PA GPIO number
 * @return TRUE when the PA pin is valid, otherwise FALSE
 */
STATIC BOOL_T tal_audio_output_dac_has_pa(TUYA_GPIO_NUM_E pin)
{
    return (pin < TUYA_GPIO_NUM_MAX);
}

/**
 * @brief Initialize the optional PA control GPIO
 * @param[in] dac_config DAC channel configuration
 * @return OPRT_OK on success, error code on failure
 */
STATIC OPERATE_RET __dac_pa_init(TAL_AUDIO_DAC_OUTPUT_CHAN_T *dac_config)
{
    TUYA_GPIO_BASE_CFG_T gpio_cfg;

    if (dac_config == NULL) {
        return OPRT_INVALID_PARM;
    }

    TUYA_GPIO_NUM_E   pa_gpio = dac_config->pa_gpio;
    TUYA_GPIO_LEVEL_E pa_active_level = dac_config->pa_active_level;

    TUYA_GPIO_LEVEL_E cfg_level = (pa_active_level == TUYA_GPIO_LEVEL_LOW)?
                                TUYA_GPIO_LEVEL_HIGH: TUYA_GPIO_LEVEL_LOW;

    memset(&gpio_cfg, 0, sizeof(TUYA_GPIO_BASE_CFG_T));
    gpio_cfg.direct = TUYA_GPIO_OUTPUT;
    gpio_cfg.mode = TUYA_GPIO_PUSH_PULL;
    gpio_cfg.level = cfg_level;

    return tkl_gpio_init(pa_gpio, &gpio_cfg);
}

/**
 * @brief Deinitialize the optional PA control GPIO
 * @param[in] dac_config DAC channel configuration
 * @return OPRT_OK on success, error code on failure
 */
STATIC OPERATE_RET tal_audio_output_dac_pa_deinit(TAL_AUDIO_DAC_OUTPUT_CHAN_T *dac_config)
{
    if (dac_config == NULL) {
        return OPRT_INVALID_PARM;
    }

    TUYA_GPIO_NUM_E pin = dac_config->pa_gpio;
    if (!tal_audio_output_dac_has_pa(pin)) {
        return OPRT_OK;
    }

    return tkl_gpio_deinit(pin);
}

/**
 * @brief Set the PA output state
 * @param[in] dac_config DAC channel configuration
 * @param[in] enable TRUE to enable the PA, FALSE to disable it
 * @return OPRT_OK on success, error code on failure
 */
STATIC OPERATE_RET tal_audio_output_dac_pa_set(TAL_AUDIO_DAC_OUTPUT_CHAN_T *dac_config, BOOL_T enable)
{
    TUYA_GPIO_LEVEL_E level;

    if (dac_config == NULL) {
        return OPRT_INVALID_PARM;
    }

    TUYA_GPIO_NUM_E pin = dac_config->pa_gpio;
    TUYA_GPIO_LEVEL_E active_level = dac_config->pa_active_level;
    TUYA_GPIO_LEVEL_E unactive_level = (active_level == TUYA_GPIO_LEVEL_LOW)?
                                TUYA_GPIO_LEVEL_HIGH: TUYA_GPIO_LEVEL_LOW;

    if (!tal_audio_output_dac_has_pa(pin)) {
        return OPRT_OK;
    }

    level = enable ? active_level: unactive_level;

    return tkl_gpio_write(pin, level);
}


/**
 * @brief Initialize the DAC output device
 * @param[in] ctrl Audio output control context
 * @param[in] cfg Public audio output configuration
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_dac_init(TAL_AO_CTRL_T *ctrl, TAL_AUDIO_OUTPUT_CFG_T *cfg)
{
    OPERATE_RET ret = OPRT_OK;
    TKL_AUD_DAC_CFG_T dac_cfg;

    if ((ctrl == NULL) || (cfg == NULL)) {
        return OPRT_INVALID_PARM;
    }

    memset(&dac_cfg, 0, sizeof(TKL_AUD_DAC_CFG_T));

    dac_cfg.chan_num = cfg->dev.dac_config.spk_num;
    dac_cfg.volume = 0;
    dac_cfg.sample_bits = cfg->sample_bits;
    dac_cfg.sample_rate = cfg->sample_rate;
    dac_cfg.frame_time_ms = cfg->frame_time_ms;
    dac_cfg.frame_cb = tal_audio_output_frame_cb;
    dac_cfg.args = ctrl;

    ret = tkl_aud_dac_init(cfg->dev.dac_config.port, &dac_cfg);
    if (ret != OPRT_OK) {
        return ret;
    }

    // init pa
    __dac_pa_init(&cfg->dev.dac_config);

    return ret;
}

/**
 * @brief Deinitialize the DAC output device
 * @param[in] ctrl Audio output control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_dac_deinit(TAL_AO_CTRL_T *ctrl)
{
    OPERATE_RET ret = OPRT_OK;
    if (ctrl == NULL) {
        return OPRT_INVALID_PARM;
    }

    // PA
    tal_audio_output_dac_pa_set(&ctrl->dev.dac_config, FALSE);
    tal_audio_output_dac_pa_deinit(&ctrl->dev.dac_config);

    ret = tkl_aud_dac_deinit(ctrl->dev.dac_config.port);

    return ret;
}

/**
 * @brief Start DAC playback
 * @param[in] ctrl Audio output control context
 * @return OPRT_OK on success, error code on failure
 * @note The PA is enabled only after the DAC starts successfully.
 */
OPERATE_RET tal_audio_output_dac_start(TAL_AO_CTRL_T *ctrl)
{
    OPERATE_RET ret = OPRT_OK;
    if (ctrl == NULL) {
        return OPRT_INVALID_PARM;
    }

    ret = tkl_aud_dac_start(ctrl->dev.dac_config.port);
    if (ret != OPRT_OK) {
        tkl_aud_dac_stop(ctrl->dev.dac_config.port);
        tal_audio_output_dac_pa_set(&ctrl->dev.dac_config, FALSE);
        return ret;
    }
    tal_audio_output_dac_pa_set(&ctrl->dev.dac_config, TRUE);

    return OPRT_OK;
}

/**
 * @brief Stop DAC playback
 * @param[in] ctrl Audio output control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_dac_stop(TAL_AO_CTRL_T *ctrl)
{
    OPERATE_RET ret = OPRT_OK;
    if (ctrl == NULL) {
        return OPRT_INVALID_PARM;
    }

    tal_audio_output_dac_pa_set(&ctrl->dev.dac_config, FALSE);

    ret = tkl_aud_dac_stop(ctrl->dev.dac_config.port);
    if (ret != OPRT_OK) {
        return ret;
    }

    return OPRT_OK;
}

/**
 * @brief Set DAC output volume
 * @param[in] ctrl Audio output control context
 * @param[in] volume Output volume in range 0 to 100
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_dac_set_volume(TAL_AO_CTRL_T *ctrl, UINT32_T volume)
{
    if (ctrl == NULL) {
        return OPRT_INVALID_PARM;
    }
    return tkl_aud_dac_set_volume(ctrl->dev.dac_config.port, volume);
}

/**
 * @brief Write PCM data to the DAC driver
 * @param[in] ctrl Audio output control context
 * @param[in] buffer PCM data buffer
 * @param[in] len Buffer length in bytes
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_dac_write(TAL_AO_CTRL_T *ctrl, UINT8_T *buffer, UINT32_T len)
{
    if (ctrl == NULL) {
        return OPRT_INVALID_PARM;
    }
    return tkl_aud_dac_write(ctrl->dev.dac_config.port, buffer, len);
}
