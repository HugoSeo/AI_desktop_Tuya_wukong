/**
 * @file wukong_audio_preprocess.h
 * @brief Common audio preprocessing helpers before frontend algorithms.
 */

#ifndef __WUKONG_AUDIO_PREPROCESS_H__
#define __WUKONG_AUDIO_PREPROCESS_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WUKONG_AUDIO_HPF_ENABLE
#define WUKONG_AUDIO_HPF_ENABLE       1
#endif

#ifndef WUKONG_AUDIO_HPF_CUTOFF_HZ
#define WUKONG_AUDIO_HPF_CUTOFF_HZ    60
#endif

#ifndef WUKONG_AUDIO_HPF_FILTER_REF
#define WUKONG_AUDIO_HPF_FILTER_REF   0
#endif

typedef struct {
    float alpha;
    float prev_x;
    float prev_y;
    int   inited;
} WUKONG_AUDIO_HPF_T;

VOID_T wukong_audio_hpf_init(WUKONG_AUDIO_HPF_T *st, float fs, float fc);
VOID_T wukong_audio_hpf_reset(WUKONG_AUDIO_HPF_T *st);
INT16_T wukong_audio_hpf_process_sample(WUKONG_AUDIO_HPF_T *st, INT16_T x);
VOID_T wukong_audio_hpf_process_mono(WUKONG_AUDIO_HPF_T *st, INT16_T *pcm, UINT32_T samples);
VOID_T wukong_audio_hpf_process_interleaved(WUKONG_AUDIO_HPF_T *st, INT16_T *pcm,
                                            UINT32_T samples, UINT32_T channels);

#ifdef __cplusplus
}
#endif

#endif /* __WUKONG_AUDIO_PREPROCESS_H__ */
