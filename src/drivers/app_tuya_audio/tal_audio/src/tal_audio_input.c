/**
 * @file tal_audio_input.c
 * @brief TAL audio input implementation
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

#include "tal_audio_input.h"
#include "audio_devices/tal_audio_dev.h"
#include "tkl_mutex.h"
#include "tkl_system.h"
#include "tkl_memory.h"

#include "uni_log.h"

// sr: sample rate, sb: sample bits, ch: channel num, t: frame time in ms
#define ADC_FRAME_SIZE(sr, sb, t, ch)  (((sr) * ((sb) / 8) / 1000) * (ch) * (t))

#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
#define AI_MALLOC(s)    tkl_system_psram_malloc(s)
#define AI_FREE(p)      tkl_system_psram_free(p)
#else
#define AI_MALLOC(s)    tkl_system_malloc(s)
#define AI_FREE(p)      tkl_system_free(p)
#endif

/**
 * @brief Convert ADC channel enum to actual channel count
 * @param[in] chan ADC channel enum
 * @return Channel count, or 0 when unsupported
 */
STATIC UINT32_T __ai_adc_channel_count(TUYA_AUDIO_ADC_CHAN_E chan)
{
    switch (chan) {
    case TUYA_AUDIO_ADC_CHANNEL_L:
    case TUYA_AUDIO_ADC_CHANNEL_R:
        return 1;
    case TUYA_AUDIO_ADC_CHANNEL_LR:
        return 2;
    default:
        return 0;
    }
}

/**
 * @brief Convert DMIC channel enum to actual channel count
 * @param[in] chan DMIC channel enum
 * @return Channel count, or 0 when unsupported
 */
STATIC UINT32_T __ai_dmic_channel_count(TUYA_AUDIO_DMIC_CHAN_E chan)
{
    switch (chan) {
    case TUYA_AUDIO_DMIC_CHANNEL_L:
    case TUYA_AUDIO_DMIC_CHANNEL_R:
        return 1;
    case TUYA_AUDIO_DMIC_CHANNEL_LR:
        return 2;
    default:
        return 0;
    }
}

/**
 * @brief Convert I2S channel format to actual channel count
 * @param[in] fmt I2S channel format enum
 * @return Channel count (1 or 2), or 0 when unsupported
 */
STATIC UINT32_T __ai_i2s_channel_count(TUYA_I2S_CHANNEL_FMT_E fmt)
{
    switch (fmt) {
    case TUYA_I2S_CHANNEL_FMT_RIGHT_LEFT:
    case TUYA_I2S_CHANNEL_FMT_ALL_RIGHT:
    case TUYA_I2S_CHANNEL_FMT_ALL_LEFT:
        return 2;
    case TUYA_I2S_CHANNEL_FMT_ONLY_RIGHT:
    case TUYA_I2S_CHANNEL_FMT_ONLY_LEFT:
        return 1;
    default:
        return 0;
    }
}


/**
 * @brief Validate audio input initialization parameters
 * @param[in] config Audio input configuration
 * @return OPRT_OK on success, error code on invalid parameter
 */
STATIC OPERATE_RET __ai_parameter_check(CONST TAL_AUDIO_INPUT_CFG_T *config)
{
    if (config == NULL || config->audio_input_cb == NULL ||
        config->frame_time_ms == 0 || config->sample_rate == 0) {
        return OPRT_INVALID_PARM;
    }

    if (config->type >= TAL_AUDIO_INPUT_MAX) {
        PR_ERR("audio input error, not support type %d", config->type);
        return OPRT_INVALID_PARM;
    }

    if ((config->sample_bits != TUYA_AUDIO_SAMPLE_BITS_8) &&
        (config->sample_bits != TUYA_AUDIO_SAMPLE_BITS_16) &&
        (config->sample_bits != TUYA_AUDIO_SAMPLE_BITS_24) &&
        (config->sample_bits != TUYA_AUDIO_SAMPLE_BITS_32)) {
        PR_ERR("audio input error, not support sample bits %d", config->sample_bits);
        return OPRT_INVALID_PARM;
    }

    return OPRT_OK;
}

/**
 * @brief Forward audio frame data to the registered upper-layer callback
 * @param[in] event Audio frame event type (ADC or DMIC)
 * @param[in] buf PCM frame buffer
 * @param[in] buf_len PCM frame length in bytes
 * @param[in] args Input control context
 * @return none
 * @note The callback is ignored when capture has not started.
 */
VOID_T tal_audio_input_data_cb(TUYA_AUDIO_FRAME_EVT_E event, uint8_t *buf, uint32_t buf_len, VOID_T *args)
{
    TAL_AI_CTRL_T *ctrl = (TAL_AI_CTRL_T *)args;
    if ((ctrl == NULL) || (!ctrl->started)) {
        return;
    }

    if (ctrl->audio_input_cb) {
        TAL_AUDIO_FRAME_T frame;
        frame.event      = event;
        frame.buf        = buf;
        frame.len        = buf_len;
        frame.seq_no     = ctrl->seq_no++;
        frame.time_stamp = tkl_system_get_millisecond();
        ctrl->audio_input_cb(&frame, ctrl->audio_input_args);
    }
}

/**
 * @brief Initialize TAL audio input
 * @param[in] config Audio input configuration
 * @return Audio input handle on success, NULL on failure
 */
TAL_AUDIO_INPUT_HANDLE tal_audio_input_init(TAL_AUDIO_INPUT_CFG_T *config)
{
    OPERATE_RET ret = OPRT_OK;
    TAL_AI_CTRL_T *ctrl = NULL;

    if (config == NULL) {
        PR_ERR("audio input error, %p", config);
        return NULL;
    }

    ret = __ai_parameter_check(config);
    if (ret != OPRT_OK) {
        return NULL;
    }

    ctrl = (TAL_AI_CTRL_T *)AI_MALLOC(sizeof(TAL_AI_CTRL_T));
    if (ctrl == NULL) {
        PR_ERR("audio input malloc ctrl failed");
        return NULL;
    }

    memset(ctrl, 0, sizeof(TAL_AI_CTRL_T));

    ret = tkl_mutex_create_init(&ctrl->mutex);
    if (ret != OPRT_OK) {
        PR_ERR("audio input create mutex failed");
        AI_FREE(ctrl);
        return NULL;
    }

    ctrl->type = config->type;
    memcpy(&ctrl->dev, &config->dev, sizeof(ctrl->dev));
    ctrl->sample_bits = config->sample_bits;
    ctrl->audio_input_cb = config->audio_input_cb;
    ctrl->audio_input_args = config->args;

    switch (config->type) {
#ifdef AUDIO_INPUT_DEVICE_ADC
        case TAL_AUDIO_INPUT_ADC:
            ctrl->frame_size = ADC_FRAME_SIZE(config->sample_rate, config->sample_bits,
                                              config->frame_time_ms,
                                              __ai_adc_channel_count(ctrl->dev.ai_adc_conf.chan));
            ret = tal_audio_input_adc_init(ctrl, config);
            break;
#endif

#ifdef AUDIO_INPUT_DEVICE_DMIC
        case TAL_AUDIO_INPUT_DMIC:
            ctrl->frame_size = ADC_FRAME_SIZE(config->sample_rate, config->sample_bits,
                                              config->frame_time_ms,
                                              __ai_dmic_channel_count(ctrl->dev.ai_dmic_conf.chan));
            ret = tal_audio_input_digital_init(ctrl, config);
            break;
#endif

#ifdef AUDIO_INPUT_DEVICE_UAV
        case TAL_AUDIO_INPUT_UAV:
            ret = tal_audio_input_uav_init(ctrl);
            break;
#endif

#ifdef AUDIO_INPUT_DEVICE_I2S
        case TAL_AUDIO_INPUT_I2S:
            ctrl->frame_size = ADC_FRAME_SIZE(config->sample_rate, config->sample_bits,
                                              config->frame_time_ms,
                                              __ai_i2s_channel_count(ctrl->dev.ai_i2s_conf.data_format));
            ret = tal_audio_input_i2s_init(ctrl, config);
            break;
#endif

        default:
            PR_ERR("audio input error, not support type %d", config->type);
            ret = OPRT_NOT_SUPPORTED;
            break;
    }

    if (ret != OPRT_OK) {
        tkl_mutex_release(ctrl->mutex);
        AI_FREE(ctrl);
        PR_ERR("init input %d error", config->type);
        return NULL;
    }

    return (TAL_AUDIO_INPUT_HANDLE)ctrl;
}

/**
 * @brief Deinitialize TAL audio input
 * @param[in] handle Audio input handle
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_deinit(TAL_AUDIO_INPUT_HANDLE handle)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    TAL_AI_CTRL_T *ctrl = (TAL_AI_CTRL_T *)handle;
    TKL_MUTEX_HANDLE mutex;
    TAL_AUDIO_INPUT_TYPE_E type;

    if (ctrl == NULL) {
        return OPRT_INVALID_PARM;
    }

    tkl_mutex_lock(ctrl->mutex);

    ctrl->started = FALSE;
    type = ctrl->type;

    switch (ctrl->type) {
#ifdef AUDIO_INPUT_DEVICE_ADC
        case TAL_AUDIO_INPUT_ADC:
            ret = tal_audio_input_adc_deinit(ctrl);
            break;
#endif

#ifdef AUDIO_INPUT_DEVICE_DMIC
        case TAL_AUDIO_INPUT_DMIC:
            ret = tal_audio_input_digital_deinit(ctrl);
            break;
#endif

#ifdef AUDIO_INPUT_DEVICE_UAV
        case TAL_AUDIO_INPUT_UAV:
            ret = tal_audio_input_uav_deinit(ctrl);
            break;
#endif

#ifdef AUDIO_INPUT_DEVICE_I2S
        case TAL_AUDIO_INPUT_I2S:
            ret = tal_audio_input_i2s_deinit(ctrl);
            break;
#endif

        default:
            PR_ERR("audio input error, not support type %d", ctrl->type);
            tkl_mutex_unlock(ctrl->mutex);
            return OPRT_NOT_SUPPORTED;
    }

    if (ret != OPRT_OK) {
        tkl_mutex_unlock(ctrl->mutex);
        PR_ERR("deinit input %d error", type);
        return ret;
    }

    mutex = ctrl->mutex;
    memset(ctrl, 0, sizeof(TAL_AI_CTRL_T));
    AI_FREE(ctrl);

    tkl_mutex_unlock(mutex);
    tkl_mutex_release(mutex);

    return OPRT_OK;
}

/**
 * @brief Set audio input volume
 * @param[in] handle Audio input handle
 * @param[in] volume Input volume in range 0 to 100
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_set_volume(TAL_AUDIO_INPUT_HANDLE handle, UINT8_T volume)
{
    TAL_AI_CTRL_T *ctrl = (TAL_AI_CTRL_T *)handle;
    OPERATE_RET ret;

    if (ctrl == NULL) {
        return OPRT_INVALID_PARM;
    }

    if (volume > 100) {
        PR_ERR("audio input error, volume %d out of range", volume);
        return OPRT_INVALID_PARM;
    }

    tkl_mutex_lock(ctrl->mutex);

    switch (ctrl->type) {
#ifdef AUDIO_INPUT_DEVICE_ADC
        case TAL_AUDIO_INPUT_ADC:
            ret = tal_audio_input_adc_set_volume(ctrl, volume);
            break;
#endif

#ifdef AUDIO_INPUT_DEVICE_DMIC
        case TAL_AUDIO_INPUT_DMIC:
            ret = tal_audio_input_digital_set_volume(ctrl, volume);
            break;
#endif

#ifdef AUDIO_INPUT_DEVICE_UAV
        case TAL_AUDIO_INPUT_UAV:
            ret = tal_audio_input_uav_set_volume(ctrl, volume);
            break;
#endif

#ifdef AUDIO_INPUT_DEVICE_I2S
        case TAL_AUDIO_INPUT_I2S:
            ret = tal_audio_input_i2s_set_volume(ctrl, volume);
            break;
#endif

        default:
            PR_ERR("audio input error, not support type %d", ctrl->type);
            ret = OPRT_NOT_SUPPORTED;
            break;
    }

    tkl_mutex_unlock(ctrl->mutex);
    return ret;
}

/**
 * @brief Start audio input capture
 * @param[in] handle Audio input handle
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_start(TAL_AUDIO_INPUT_HANDLE handle)
{
    TAL_AI_CTRL_T *ctrl = (TAL_AI_CTRL_T *)handle;
    OPERATE_RET ret = OPRT_OK;

    if (ctrl == NULL) {
        PR_ERR("audio input start failed, parameter error");
        return OPRT_INVALID_PARM;
    }

    tkl_mutex_lock(ctrl->mutex);

    switch (ctrl->type) {
#ifdef AUDIO_INPUT_DEVICE_ADC
        case TAL_AUDIO_INPUT_ADC:
            ret = tal_audio_input_adc_start(ctrl);
            break;
#endif

#ifdef AUDIO_INPUT_DEVICE_DMIC
        case TAL_AUDIO_INPUT_DMIC:
            ret = tal_audio_input_digital_start(ctrl);
            break;
#endif

#ifdef AUDIO_INPUT_DEVICE_UAV
        case TAL_AUDIO_INPUT_UAV:
            ret = tal_audio_input_uav_start(ctrl);
            break;
#endif

#ifdef AUDIO_INPUT_DEVICE_I2S
        case TAL_AUDIO_INPUT_I2S:
            ret = tal_audio_input_i2s_start(ctrl);
            break;
#endif

        default:
            PR_ERR("audio input error, not support type %d", ctrl->type);
            tkl_mutex_unlock(ctrl->mutex);
            return OPRT_NOT_SUPPORTED;
    }

    if (ret == OPRT_OK) {
        ctrl->started = TRUE;
    }

    tkl_mutex_unlock(ctrl->mutex);
    return ret;
}

/**
 * @brief Stop audio input capture
 * @param[in] handle Audio input handle
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_stop(TAL_AUDIO_INPUT_HANDLE handle)
{
    TAL_AI_CTRL_T *ctrl = (TAL_AI_CTRL_T *)handle;
    OPERATE_RET ret = OPRT_OK;

    if (ctrl == NULL) {
        return OPRT_INVALID_PARM;
    }

    tkl_mutex_lock(ctrl->mutex);

    ctrl->started = FALSE;

    switch (ctrl->type) {
#ifdef AUDIO_INPUT_DEVICE_ADC
        case TAL_AUDIO_INPUT_ADC:
            ret = tal_audio_input_adc_stop(ctrl);
            break;
#endif

#ifdef AUDIO_INPUT_DEVICE_DMIC
        case TAL_AUDIO_INPUT_DMIC:
            ret = tal_audio_input_digital_stop(ctrl);
            break;
#endif

#ifdef AUDIO_INPUT_DEVICE_UAV
        case TAL_AUDIO_INPUT_UAV:
            ret = tal_audio_input_uav_stop(ctrl);
            break;
#endif

#ifdef AUDIO_INPUT_DEVICE_I2S
        case TAL_AUDIO_INPUT_I2S:
            ret = tal_audio_input_i2s_stop(ctrl);
            break;
#endif

        default:
            PR_ERR("audio input error, not support type %d", ctrl->type);
            tkl_mutex_unlock(ctrl->mutex);
            return OPRT_NOT_SUPPORTED;
    }

    tkl_mutex_unlock(ctrl->mutex);
    return ret;
}

