/**
 * @file tal_audio_digital.c
 * @brief TAL audio digital (DMIC) device adapter
 * @version 1.0
 * @date 2025-03-25
 * @copyright Copyright (c) Tuya Inc.
 */

#include <stdint.h>
#include <string.h>

#include "tal_audio_dev.h"
#include "tkl_aud_dmic.h"

#ifdef AUDIO_INPUT_DEVICE_DMIC

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Initialize the onboard digital (DMIC) device
 * @param[in] ctrl Audio input control context
 * @param[in] cfg Public audio input configuration
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_digital_init(TAL_AI_CTRL_T *ctrl, TAL_AUDIO_INPUT_CFG_T *cfg)
{
    OPERATE_RET ret = OPRT_OK;
    TKL_AUD_DMIC_CFG_T dmic_config;

    if ((ctrl == NULL) || (cfg == NULL)) {
        return OPRT_INVALID_PARM;
    }

    if (cfg->dev.ai_dmic_conf.port >= TUYA_AUDIO_DMIC_PORT_MAX) {
        return OPRT_INVALID_PARM;
    }

    dmic_config.chan = ctrl->dev.ai_dmic_conf.chan;
    dmic_config.vol = 0;
    dmic_config.sample_bits = cfg->sample_bits;
    dmic_config.sample_rate = cfg->sample_rate;
    dmic_config.frame_time_ms = cfg->frame_time_ms;
    dmic_config.upper_cb = tal_audio_input_data_cb;
    dmic_config.args = ctrl;

    ret = tkl_aud_dmic_init(ctrl->dev.ai_dmic_conf.port, &dmic_config);
    if (ret != OPRT_OK) {
        return ret;
    }

    return ret;
}

/**
 * @brief Deinitialize the onboard digital (DMIC) device
 * @param[in] ctrl Audio input control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_digital_deinit(TAL_AI_CTRL_T *ctrl)
{
    OPERATE_RET ret = OPRT_OK;

    if ((ctrl == NULL) || (ctrl->dev.ai_dmic_conf.port >= TUYA_AUDIO_DMIC_PORT_MAX)) {
        return OPRT_INVALID_PARM;
    }

    ret = tal_audio_input_digital_stop(ctrl);
    if ((ret != OPRT_OK) && (ret != OPRT_INVALID_PARM)) {
        return ret;
    }

    tkl_aud_dmic_deinit(ctrl->dev.ai_dmic_conf.port);

    return OPRT_OK;
}

/**
 * @brief Set onboard digital (DMIC) volume
 * @param[in] ctrl Audio input control context
 * @param[in] volume Input volume in range 0 to 100
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_digital_set_volume(TAL_AI_CTRL_T *ctrl, UINT32_T volume)
{
    OPERATE_RET ret = OPRT_OK;

    if ((ctrl == NULL) || (ctrl->dev.ai_dmic_conf.port >= TUYA_AUDIO_DMIC_PORT_MAX)) {
        return OPRT_INVALID_PARM;
    }

    ret = tkl_aud_dmic_set_vol(ctrl->dev.ai_dmic_conf.port, volume);

    return ret;
}

/**
 * @brief Start onboard digital (DMIC) capture
 * @param[in] ctrl Audio input control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_digital_start(TAL_AI_CTRL_T *ctrl)
{
    OPERATE_RET ret = OPRT_OK;

    if ((ctrl == NULL) || (ctrl->dev.ai_dmic_conf.port >= TUYA_AUDIO_DMIC_PORT_MAX)) {
        return OPRT_INVALID_PARM;
    }

    ret = tkl_aud_dmic_start(ctrl->dev.ai_dmic_conf.port);
    if (ret != OPRT_OK) {
        return ret;
    }

    return OPRT_OK;
}

/**
 * @brief Stop onboard digital (DMIC) capture
 * @param[in] ctrl Audio input control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_digital_stop(TAL_AI_CTRL_T *ctrl)
{
    OPERATE_RET ret = OPRT_OK;

    if ((ctrl == NULL) || (ctrl->dev.ai_dmic_conf.port >= TUYA_AUDIO_DMIC_PORT_MAX)) {
        return OPRT_INVALID_PARM;
    }

    ret = tkl_aud_dmic_stop(ctrl->dev.ai_dmic_conf.port);
    if (ret != OPRT_OK) {
        return ret;
    }

    return OPRT_OK;
}

#endif // AUDIO_INPUT_DEVICE_DMIC
