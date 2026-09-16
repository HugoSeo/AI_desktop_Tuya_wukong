/**
 * @file tal_audio_output.h
 * @brief TAL audio output public API
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

#ifndef __TAL_AUDIO_OUTPUT_H__
#define __TAL_AUDIO_OUTPUT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "tuya_iot_config.h"

typedef VOID_T * TAL_AUDIO_OUTPUT_HANDLE;

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef enum {
    TAL_AUDIO_OUTPUT_DAC = 0,
    TAL_AUDIO_OUTPUT_UAV,
    TAL_AUDIO_OUTPUT_I2S,
    TAL_AUDIO_OUTPUT_MAX,
} TAL_AUDIO_OUTPUT_TYPE_E;

typedef struct {
    TUYA_AUDIO_DAC_PORT_E   port;
    UINT32_T                spk_num;
    TUYA_GPIO_NUM_E         pa_gpio;
    TUYA_GPIO_LEVEL_E       pa_active_level;
} TAL_AUDIO_DAC_OUTPUT_CHAN_T;

typedef struct {
    TUYA_I2S_NUM_E              port;
    TUYA_I2S_MODE_E             mode;
    TUYA_I2S_COMM_FORMAT_E      i2s_protocol;
    TUYA_I2S_CHANNEL_FMT_E      data_format;
} TAL_AUDIO_OUTPUT_I2S_T;

typedef struct {
    TAL_AUDIO_OUTPUT_TYPE_E type;
    union {
        TAL_AUDIO_DAC_OUTPUT_CHAN_T   dac_config;
        TAL_AUDIO_OUTPUT_I2S_T        i2s_config;
    } dev;
    TUYA_AUDIO_SAMPLE_BITS_E        sample_bits;
    UINT32_T                        sample_rate;
    UINT32_T                        frame_time_ms;
} TAL_AUDIO_OUTPUT_CFG_T;

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */
/**
 * @brief Initialize TAL audio output
 * @param[in] config output configuration
 * @return output handle on success, NULL on failure
 * @note The handle owns the worker thread and device resources used by the
 *       output stream.
 */
TAL_AUDIO_OUTPUT_HANDLE tal_audio_output_init(TAL_AUDIO_OUTPUT_CFG_T *config);

/**
 * @brief Deinitialize TAL audio output
 * @param[in] handle output handle
 * @return OPRT_OK on success, error code on failure
 * @note Stops playback, releases the worker thread, and frees all resources
 *       associated with the handle.
 */
OPERATE_RET tal_audio_output_deinit(TAL_AUDIO_OUTPUT_HANDLE handle);

/**
 * @brief Start audio output playback
 * @param[in] handle output handle
 * @return OPRT_OK on success, error code on failure
 * @note Playback must be started before calling `tal_audio_output_write()`.
 */
OPERATE_RET tal_audio_output_start(TAL_AUDIO_OUTPUT_HANDLE handle);

/**
 * @brief Stop audio output playback
 * @param[in] handle output handle
 * @return OPRT_OK on success, error code on failure
 * @note Stops the device and aborts any write request that is still waiting
 *       for ring buffer space.
 */
OPERATE_RET tal_audio_output_stop(TAL_AUDIO_OUTPUT_HANDLE handle);

/**
 * @brief Set output volume
 * @param[in] handle output handle
 * @param[in] volume volume level in range 0 to 100
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_set_volume(TAL_AUDIO_OUTPUT_HANDLE handle, UINT8_T volume);

/**
 * @brief Write PCM data for playback
 * @param[in] handle output handle
 * @param[in] buf user PCM buffer
 * @param[in] len buffer length in bytes
 * @return OPRT_OK on success, error code on failure
 * @note This function blocks until all data has been drained into the internal
 *       ring buffer, or until playback is stopped or deinitialized.
 */
OPERATE_RET tal_audio_output_write(TAL_AUDIO_OUTPUT_HANDLE handle, UINT8_T *buf, UINT32_T len);

#ifdef __cplusplus
}
#endif

#endif  // __TAL_AUDIO_OUTPUT_H__
