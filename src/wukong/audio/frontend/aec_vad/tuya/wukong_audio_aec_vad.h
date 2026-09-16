/**
 * @file wukong_audio_aec_vad.h
 * @brief Backward-compatible AEC/VAD API header
 *
 * Includes wukong_audio_frontend.h for type definitions and declares the
 * legacy wukong_aec_vad_* functions (now thin wrappers around the frontend
 * dispatcher in wukong_audio_frontend.c).
 *
 * Also exports g_tuya_frontend_ops for board-level registration.
 *
 * @copyright Copyright (c) Tuya Inc.
 */

#ifndef __WUKONG_AUDIO_AEC_VAD_H__
#define __WUKONG_AUDIO_AEC_VAD_H__

#include "wukong_audio_frontend.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET wukong_aec_vad_init(UINT32_T min_speech_len_ms, UINT32_T max_speech_interval_ms, UINT32_T frame_size);
OPERATE_RET wukong_aec_vad_deinit(VOID);
OPERATE_RET wukong_aec_vad_process(INT16_T *mic_data, INT16_T *ref_data, INT16_T *out_data);
OPERATE_RET wukong_vad_set_threshold(WUKONG_AUDIO_VAD_THRESHOLD_E level);
OPERATE_RET wukong_vad_start(VOID);
OPERATE_RET wukong_vad_stop(VOID);
INT_T wukong_vad_get_flag(VOID);

extern WUKONG_AUDIO_FRONTEND_OPS_T g_tuya_frontend_ops;

#ifdef __cplusplus
}
#endif

#endif /* __WUKONG_AUDIO_AEC_VAD_H__ */
