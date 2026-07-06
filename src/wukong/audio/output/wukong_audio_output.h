/**
 * @file wukong_audio_output.h
 * @brief Audio output abstraction layer
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

#ifndef __WUKONG_AUDIO_OUTPUT_H__
#define __WUKONG_AUDIO_OUTPUT_H__

#include "tuya_cloud_types.h"
#include "tkl_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct  {
    TKL_AUDIO_SAMPLE_E          sample_rate;       // audio sample rate
    TKL_AUDIO_DATABITS_E        sample_bits;       // audio sample bits
    TKL_AUDIO_CHANNEL_E         channel;           // audio channel
} WUKONG_AUDIO_OUTPUT_CFG_T;

/**
 * @brief Audio output consumer operations table
 *
 * Platform-specific backends (board or UART) implement this interface.
 */
typedef struct {
    /** @brief Initialize the audio output consumer
     *  @param[in] cfg  Audio output configuration
     *  @return OPRT_OK on success */
    OPERATE_RET (*init)(WUKONG_AUDIO_OUTPUT_CFG_T *cfg);

    /** @brief Deinitialize the audio output consumer and release resources
     *  @return OPRT_OK on success */
    OPERATE_RET (*deinit)(VOID);

    /** @brief Start audio playback pipeline
     *  @return OPRT_OK on success */
    OPERATE_RET (*start)(VOID);

    /** @brief Stop audio playback
     *  @return OPRT_OK on success */
    OPERATE_RET (*stop)(VOID);

    /** @brief Write PCM audio data to the output
     *  @param[in] data     Pointer to the audio data buffer
     *  @param[in] datalen  Length of the audio data in bytes
     *  @return OPRT_OK on success */
    OPERATE_RET (*write)(UINT8_T *data, UINT16_T datalen);

    /** @brief Set audio output volume
     *  @param[in] volume  Volume level (range depends on platform)
     *  @return OPRT_OK on success */
    OPERATE_RET (*set_vol)(INT32_T volume);
} WUKONG_AUDIO_OUTPUT_CONSUMER_T;
extern WUKONG_AUDIO_OUTPUT_CONSUMER_T g_audio_output_consumer;

/** @brief Initialize audio output subsystem
 *  @param[in] cfg  Configuration struct (output type, sample rate, etc.)
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_output_init(WUKONG_AUDIO_OUTPUT_CFG_T *cfg);

/** @brief Deinitialize audio output subsystem and release resources
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_output_deinit(VOID);

/** @brief Start audio playback pipeline
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_output_start(VOID);

/** @brief Write PCM audio data to the output
 *  @param[in] data     Pointer to the audio data buffer
 *  @param[in] datalen  Length of the audio data in bytes
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_output_write(UINT8_T *data, UINT16_T datalen);

/** @brief Stop audio playback
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_output_stop(VOID);

/** @brief Set audio output volume
 *  @param[in] volume  Volume level (range depends on platform)
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_output_set_vol(INT32_T volume);


#ifdef __cplusplus
}
#endif
#endif /* __WUKONG_AUDIO_OUTPUT_H__ */
