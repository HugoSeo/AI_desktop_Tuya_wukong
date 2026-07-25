/**
 * @file wukong_audio_frontend.h
 * @brief Audio frontend abstraction layer
 *
 * Provides a platform-agnostic operations table for audio frontend processing
 * (AEC, VAD, KWS). Platforms register their own ops implementation to decouple
 * the audio pipeline from specific AEC/VAD backends.
 *
 * @copyright Copyright (c) Tuya Inc.
 */

#ifndef __WUKONG_AUDIO_FRONTEND_H__
#define __WUKONG_AUDIO_FRONTEND_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WUKONG_AUDIO_VAD_START = 1,
    WUKONG_AUDIO_VAD_STOP,
} WUKONG_AUDIO_VAD_FLAG_E;

typedef enum {
    WUKONG_AUDIO_VAD_HIGH,
    WUKONG_AUDIO_VAD_MID,
    WUKONG_AUDIO_VAD_LOW,
} WUKONG_AUDIO_VAD_THRESHOLD_E;

/**
 * @brief Audio frontend operations table
 *
 * Each platform provides an implementation of this table. The dispatcher
 * calls ops->process, then feeds the output to KWS automatically.
 */
typedef struct {
    /** @brief Initialize the frontend (AEC, VAD, etc.)
     *  @param[in] min_speech_len_ms       Minimum speech length in ms to trigger VAD start
     *  @param[in] max_speech_interval_ms  Max silence gap in ms before VAD stop
     *  @param[in] frame_size              Audio frame size in bytes
     *  @return OPRT_OK on success */
    OPERATE_RET (*init)(UINT32_T min_speech_len_ms, UINT32_T max_speech_interval_ms, UINT32_T frame_size);

    /** @brief Deinitialize the frontend and release resources
     *  @return OPRT_OK on success */
    OPERATE_RET (*deinit)(VOID);

    /**
     * @brief Process one audio frame through AEC and VAD
     *
     * @param[in]  mic_data  Microphone input PCM
     * @param[in]  ref_data  Reference (speaker) input PCM
     * @param[out] out_data  AEC-processed output PCM (returned to caller/driver)
     * @return OPRT_OK on success
     */
    OPERATE_RET (*process)(INT16_T *mic_data, INT16_T *ref_data, INT16_T *out_data);

    /** @brief Start VAD detection
     *  @return OPRT_OK on success */
    OPERATE_RET (*vad_start)(VOID);

    /** @brief Stop VAD detection
     *  @return OPRT_OK on success */
    OPERATE_RET (*vad_stop)(VOID);

    /** @brief Set VAD sensitivity threshold
     *  @param[in] level  Threshold level (HIGH / MID / LOW)
     *  @return OPRT_OK on success */
    OPERATE_RET (*vad_set_threshold)(WUKONG_AUDIO_VAD_THRESHOLD_E level);

    /** @brief Get current VAD flag
     *  @return WUKONG_AUDIO_VAD_START if speech detected, WUKONG_AUDIO_VAD_STOP otherwise */
    INT_T       (*vad_get_flag)(VOID);

    /**
     * @brief Get KWS-specific output data (optional)
     *
     * Some backends (e.g. Speex) produce a separate linear AEC output that is
     * better suited for KWS than the full preprocessed out_data. If this
     * callback is NULL, the dispatcher feeds out_data to KWS instead.
     *
     * @param data  [out] Pointer to KWS-ready PCM buffer (owned by backend)
     * @param len   [out] Data length in bytes
     * @return OPRT_OK on success
     */
    OPERATE_RET (*get_kws_output)(INT16_T **data, UINT32_T *len);
} WUKONG_AUDIO_FRONTEND_OPS_T;

/**
 * @brief Register a frontend ops implementation
 *
 * Must be called before wukong_audio_frontend_init(). Typically called
 * from board-specific init code.
 *
 * @param ops  Operations table; must remain valid for the lifetime of the frontend
 * @return OPRT_OK on success
 */
OPERATE_RET wukong_audio_frontend_register(WUKONG_AUDIO_FRONTEND_OPS_T *ops);

/** @brief Initialize the audio frontend via the registered ops
 *  @param[in] min_speech_len_ms       Minimum speech length in ms
 *  @param[in] max_speech_interval_ms  Maximum silence gap in ms before VAD triggers stop
 *  @param[in] frame_size              Audio frame size in bytes
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_frontend_init(UINT32_T min_speech_len_ms, UINT32_T max_speech_interval_ms, UINT32_T frame_size);

/** @brief Deinitialize the audio frontend and release resources
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_frontend_deinit(VOID);

/**
 * @brief Process one audio frame and feed KWS
 *
 * Calls ops->process, then automatically feeds the result (or get_kws_output
 * if provided) to wukong_kws_feed_with_vad.
 *
 * Signature matches the T5 tkl_ai_set_vad_aec_algorithm callback type,
 * so it can be registered directly.
 */
OPERATE_RET wukong_audio_frontend_process(INT16_T *mic_data, INT16_T *ref_data, INT16_T *out_data);

/** @brief Start VAD detection
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_frontend_vad_start(VOID);

/** @brief Stop VAD detection
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_frontend_vad_stop(VOID);

/** @brief Set VAD sensitivity threshold
 *  @param[in] level  Threshold level (WUKONG_AUDIO_VAD_LOW / MID / HIGH)
 *  @return OPRT_OK on success */
OPERATE_RET wukong_audio_frontend_vad_set_threshold(WUKONG_AUDIO_VAD_THRESHOLD_E level);

/** @brief Get current VAD detection flag
 *  @return WUKONG_AUDIO_VAD_START if speech detected, WUKONG_AUDIO_VAD_STOP otherwise */
INT_T       wukong_audio_frontend_vad_get_flag(VOID);

#ifdef __cplusplus
}
#endif

#endif /* __WUKONG_AUDIO_FRONTEND_H__ */
