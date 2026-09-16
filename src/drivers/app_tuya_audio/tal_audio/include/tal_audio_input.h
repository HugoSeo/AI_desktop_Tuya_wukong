/**
 * @file tal_audio_input.h
 * @brief TAL audio input public API
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

#ifndef __TAL_AUDIO_INPUT_H__
#define __TAL_AUDIO_INPUT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_iot_config.h"
#include "tuya_cloud_types.h"

typedef enum {
    TAL_AUDIO_INPUT_ADC = 0,         // onboard analog mic
    TAL_AUDIO_INPUT_DMIC,         // onboard digital mic
    TAL_AUDIO_INPUT_UAV,             // usb audio
    TAL_AUDIO_INPUT_I2S,             // extenal codec
    TAL_AUDIO_INPUT_MAX,
} TAL_AUDIO_INPUT_TYPE_E;

typedef VOID_T *TAL_AUDIO_INPUT_HANDLE;

typedef struct {
    TUYA_AUDIO_FRAME_EVT_E event;
    UINT8_T    *buf;
    UINT32_T    len;
    UINT32_T    seq_no;
    SYS_TIME_T  time_stamp;
} TAL_AUDIO_FRAME_T;

/**
 * @brief Audio input frame callback
 * @param[in] frame Audio frame descriptor with PCM data, sequence number and timestamp
 * @param[in] args User private data passed in `TAL_AUDIO_INPUT_CFG_T`
 * @return none
 * @note The callback may run in hardware interrupt context, so it should finish
 *       quickly and must not block.
 */
typedef VOID_T (*TAL_AUDIO_INPUT_CB)(TAL_AUDIO_FRAME_T *frame, VOID_T *args);

typedef struct {
    TUYA_AUDIO_ADC_PORT_E port;
    TUYA_AUDIO_ADC_CHAN_E chan;
} TAL_AUDIO_INPUT_ANALOG_T;

typedef struct {
    TUYA_AUDIO_DMIC_PORT_E port;
    TUYA_AUDIO_DMIC_CHAN_E chan;
} TAL_AUDIO_INPUT_DMIC_T;

typedef struct {
    UINT32_T port;
} TAL_AUDIO_INPUT_UAV_T;

typedef struct {
    TUYA_I2S_NUM_E              port;
    TUYA_I2S_MODE_E             mode;
    TUYA_I2S_COMM_FORMAT_E      i2s_protocol;
    TUYA_I2S_CHANNEL_FMT_E      data_format;
    BOOL_T                      use_dma;
} TAL_AUDIO_INPUT_I2S_T;

typedef struct {
    TAL_AUDIO_INPUT_TYPE_E type;
    union {
        TAL_AUDIO_INPUT_ANALOG_T    ai_adc_conf;
        TAL_AUDIO_INPUT_I2S_T       ai_i2s_conf;
        TAL_AUDIO_INPUT_UAV_T       ai_uav_conf;
        TAL_AUDIO_INPUT_DMIC_T      ai_dmic_conf;
    } dev;

    TUYA_AUDIO_SAMPLE_BITS_E sample_bits;       // T5 SMP, support 16bits only
    UINT32_T sample_rate;
    UINT32_T frame_time_ms;

    TAL_AUDIO_INPUT_CB audio_input_cb;
    VOID_T *args;
} TAL_AUDIO_INPUT_CFG_T;

/**
 * @brief Initialize TAL audio input
 * @param[in] config Audio input configuration
 * @return Audio input handle on success, NULL on failure
 */
TAL_AUDIO_INPUT_HANDLE tal_audio_input_init(TAL_AUDIO_INPUT_CFG_T *config);

/**
 * @brief Deinitialize TAL audio input
 * @param[in] handle Audio input handle
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_deinit(TAL_AUDIO_INPUT_HANDLE handle);

/**
 * @brief Set audio input volume
 * @param[in] handle Audio input handle
 * @param[in] volume Input volume in range 0 to 100
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_set_volume(TAL_AUDIO_INPUT_HANDLE handle, UINT8_T volume);

/**
 * @brief Start audio input capture
 * @param[in] handle Audio input handle
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_start(TAL_AUDIO_INPUT_HANDLE handle);

/**
 * @brief Stop audio input capture
 * @param[in] handle Audio input handle
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_stop(TAL_AUDIO_INPUT_HANDLE handle);


#ifdef __cplusplus
}
#endif

#endif  // __TAL_AUDIO_INPUT_H__
