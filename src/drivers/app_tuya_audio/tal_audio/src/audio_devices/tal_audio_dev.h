/**
 * @file tal_audio_dev.h
 * @brief Internal control structures for audio input/output
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

#ifndef __TAL_AUDIO_INNER_H__
#define __TAL_AUDIO_INNER_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Includes
 * --------------------------------------------------------------------------- */
#include "tuya_cloud_types.h"
#include "tuya_iot_config.h"
#include "tuya_ringbuf.h"
#include "tal_audio_input.h"
#include "tal_audio_output.h"
#include "tkl_semaphore.h"
#include "tkl_mutex.h"
#include "tkl_thread.h"
#include "tkl_queue.h"

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#ifndef TAL_AO_RINGBUF_SIZE
#define TAL_AO_RINGBUF_SIZE     4096
#endif

#define AUDIO_INPUT_DEVICE_ADC      1
#define AUDIO_INPUT_DEVICE_DMIC     1
#define AUDIO_INPUT_DEVICE_I2S      1
#define AUDIO_OUTPUT_DEVICE_DAC     1
#define AUDIO_OUTPUT_DEVICE_I2S     1

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    TAL_AUDIO_INPUT_TYPE_E type;
    union {
        TAL_AUDIO_INPUT_ANALOG_T    ai_adc_conf;
        TAL_AUDIO_INPUT_I2S_T       ai_i2s_conf;
        TAL_AUDIO_INPUT_UAV_T       ai_uav_conf;
        TAL_AUDIO_INPUT_DMIC_T      ai_dmic_conf;
    } dev;
    UINT32_T            frame_size;
    TUYA_AUDIO_SAMPLE_BITS_E sample_bits;
    TAL_AUDIO_INPUT_CB  audio_input_cb;
    VOID_T             *audio_input_args;
    volatile BOOL_T     started;
    TKL_MUTEX_HANDLE    mutex;
    UINT32_T            seq_no;
} TAL_AI_CTRL_T;

typedef enum {
    TAL_AO_MSG_WRITE,
    TAL_AO_MSG_FRAME_DONE,
    TAL_AO_MSG_DEINIT,
} TAL_AO_MSG_TYPE_E;

typedef struct {
    UINT8_T         *buf;
    UINT32_T        len;
    UINT32_T        offset;
    OPERATE_RET     result;
    TKL_SEM_HANDLE  done_sem;
} TAL_AO_WRITE_REQ_T;

typedef struct {
    UINT8_T         volume;
    OPERATE_RET     result;
    TKL_SEM_HANDLE  done_sem;
} TAL_AO_VOLUME_REQ_T;

typedef struct {
    TAL_AO_MSG_TYPE_E type;
    VOID_T           *payload;
} TAL_AO_MSG_T;

typedef struct {
    TAL_AUDIO_OUTPUT_TYPE_E type;
    union {
        TAL_AUDIO_DAC_OUTPUT_CHAN_T   dac_config;
        TAL_AUDIO_OUTPUT_I2S_T        i2s_config;
    } dev;
    UINT32_T                frame_size;
    UINT32_T                frame_feed_retry_timeout;
    TUYA_RINGBUFF_T         ringbuf;
    TKL_QUEUE_HANDLE        msg_queue;
    UINT32_T                queue_timeout;
    TKL_SEM_HANDLE          write_done_sem;
    TKL_SEM_HANDLE          thread_exit_sem;
    TKL_THREAD_HANDLE       worker_thread;
    TAL_AO_WRITE_REQ_T     *pending_write;
    UINT8_T                *frame_buf;
    BOOL_T                  started;
    BOOL_T                  running;
} TAL_AO_CTRL_T;

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */

/**
 * @brief Forward audio frame data to the registered upper-layer callback
 * @param[in] event Audio frame event type (ADC or DMIC)
 * @param[in] buf PCM frame buffer
 * @param[in] buf_len PCM frame length in bytes
 * @param[in] args Input control context
 * @return none
 */
VOID_T tal_audio_input_data_cb(TUYA_AUDIO_FRAME_EVT_E event, uint8_t *buf, uint32_t buf_len, VOID_T *args);

/**
 * @brief Initialize the ADC input device
 * @param[in] ctrl Input control context
 * @param[in] cfg Public audio input configuration
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_adc_init(TAL_AI_CTRL_T *ctrl, TAL_AUDIO_INPUT_CFG_T *cfg);

/**
 * @brief Deinitialize the ADC input device
 * @param[in] ctrl Input control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_adc_deinit(TAL_AI_CTRL_T *ctrl);

/**
 * @brief Set ADC input volume
 * @param[in] ctrl Input control context
 * @param[in] volume Input volume in range 0 to 100
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_adc_set_volume(TAL_AI_CTRL_T *ctrl, UINT32_T volume);

/**
 * @brief Stop ADC input capture
 * @param[in] ctrl Input control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_adc_stop(TAL_AI_CTRL_T *ctrl);

/**
 * @brief Start ADC input capture
 * @param[in] ctrl Input control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_adc_start(TAL_AI_CTRL_T *ctrl);

/**
 * @brief Initialize the digital (DMIC) input device
 * @param[in] ctrl Input control context
 * @param[in] cfg Public audio input configuration
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_digital_init(TAL_AI_CTRL_T *ctrl, TAL_AUDIO_INPUT_CFG_T *cfg);

/**
 * @brief Deinitialize the digital (DMIC) input device
 * @param[in] ctrl Input control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_digital_deinit(TAL_AI_CTRL_T *ctrl);

/**
 * @brief Set digital (DMIC) input volume
 * @param[in] ctrl Input control context
 * @param[in] volume Input volume in range 0 to 100
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_digital_set_volume(TAL_AI_CTRL_T *ctrl, UINT32_T volume);

/**
 * @brief Stop digital (DMIC) input capture
 * @param[in] ctrl Input control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_digital_stop(TAL_AI_CTRL_T *ctrl);

/**
 * @brief Start digital (DMIC) input capture
 * @param[in] ctrl Input control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_digital_start(TAL_AI_CTRL_T *ctrl);

/**
 * @brief Post a frame-done event from the DAC driver
 * @param[in] event DAC frame event type
 * @param[in] arg Output control context
 * @return none
 */
VOID_T tal_audio_output_frame_cb(TUYA_AUDIO_DAC_FRAME_EVT_E event, VOID_T *arg);

/**
 * @brief Initialize the DAC output device
 * @param[in] ctrl Output control context
 * @param[in] cfg Public audio output configuration
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_dac_init(TAL_AO_CTRL_T *ctrl, TAL_AUDIO_OUTPUT_CFG_T *cfg);

/**
 * @brief Deinitialize the DAC output device
 * @param[in] ctrl Output control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_dac_deinit(TAL_AO_CTRL_T *ctrl);

/**
 * @brief Start DAC playback
 * @param[in] ctrl Output control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_dac_start(TAL_AO_CTRL_T *ctrl);

/**
 * @brief Stop DAC playback
 * @param[in] ctrl Output control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_dac_stop(TAL_AO_CTRL_T *ctrl);

/**
 * @brief Set DAC output volume
 * @param[in] ctrl Output control context
 * @param[in] volume Output volume in range 0 to 100
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_dac_set_volume(TAL_AO_CTRL_T *ctrl, UINT32_T volume);

/**
 * @brief Write one PCM frame to the DAC driver
 * @param[in] ctrl Output control context
 * @param[in] buffer PCM data buffer
 * @param[in] len Buffer length in bytes
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_dac_write(TAL_AO_CTRL_T *ctrl, UINT8_T *buffer, UINT32_T len);

/**
 * @brief Initialize the I2S input device
 * @param[in] ctrl Input control context
 * @param[in] cfg Public audio input configuration
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_i2s_init(TAL_AI_CTRL_T *ctrl, TAL_AUDIO_INPUT_CFG_T *cfg);

/**
 * @brief Deinitialize the I2S input device
 * @param[in] ctrl Input control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_i2s_deinit(TAL_AI_CTRL_T *ctrl);

/**
 * @brief Set I2S input volume
 * @param[in] ctrl Input control context
 * @param[in] volume Input volume in range 0 to 100
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_i2s_set_volume(TAL_AI_CTRL_T *ctrl, UINT32_T volume);

/**
 * @brief Start I2S input capture
 * @param[in] ctrl Input control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_i2s_start(TAL_AI_CTRL_T *ctrl);

/**
 * @brief Stop I2S input capture
 * @param[in] ctrl Input control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_input_i2s_stop(TAL_AI_CTRL_T *ctrl);

/**
 * @brief Initialize the I2S output device
 * @param[in] ctrl Output control context
 * @param[in] cfg Public audio output configuration
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_i2s_init(TAL_AO_CTRL_T *ctrl, TAL_AUDIO_OUTPUT_CFG_T *cfg);

/**
 * @brief Deinitialize the I2S output device
 * @param[in] ctrl Output control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_i2s_deinit(TAL_AO_CTRL_T *ctrl);

/**
 * @brief Start I2S output playback
 * @param[in] ctrl Output control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_i2s_start(TAL_AO_CTRL_T *ctrl);

/**
 * @brief Stop I2S output playback
 * @param[in] ctrl Output control context
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_i2s_stop(TAL_AO_CTRL_T *ctrl);

/**
 * @brief Set I2S output volume
 * @param[in] ctrl Output control context
 * @param[in] volume Output volume in range 0 to 100
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tal_audio_output_i2s_set_volume(TAL_AO_CTRL_T *ctrl, UINT32_T volume);

/**
 * @brief Write PCM data to I2S output
 * @param[in] ctrl Output control context
 * @param[in] buffer PCM data buffer
 * @param[in] len Buffer length in bytes
 * @return OPRT_OK on success, OPRT_OS_ADAPTER_DAC_BUSY when ring buffer full
 */
OPERATE_RET tal_audio_output_i2s_write(TAL_AO_CTRL_T *ctrl, UINT8_T *buffer, UINT32_T len);

#ifdef __cplusplus
}
#endif

#endif /*__TAL_AUDIO_INNER_H__ */
