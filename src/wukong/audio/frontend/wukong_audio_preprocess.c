/**
 * @file wukong_audio_preprocess.c
 * @brief Common audio preprocessing helpers before frontend algorithms.
 */

#include "wukong_audio_preprocess.h"

#define WUKONG_AUDIO_HPF_PI 3.14159265358979323846f

#if WUKONG_AUDIO_HPF_ENABLE
STATIC INT16_T __clip_s16(float v)
{
    if (v > 32767.0f) {
        return 32767;
    }
    if (v < -32768.0f) {
        return -32768;
    }
    return (INT16_T)v;
}
#endif

VOID_T wukong_audio_hpf_init(WUKONG_AUDIO_HPF_T *st, float fs, float fc)
{
    if (st == NULL) {
        return;
    }

    st->alpha = 0.0f;
    st->prev_x = 0.0f;
    st->prev_y = 0.0f;
    st->inited = 0;

#if WUKONG_AUDIO_HPF_ENABLE
    if (fs < 1.0f) {
        fs = 16000.0f;
    }
    if (fc < 1.0f) {
        fc = 1.0f;
    }
    if (fc > (fs * 0.5f)) {
        fc = fs * 0.5f;
    }

    float rc = 1.0f / (2.0f * WUKONG_AUDIO_HPF_PI * fc);
    float dt = 1.0f / fs;
    st->alpha = rc / (rc + dt);
    st->inited = 1;
#endif
}

VOID_T wukong_audio_hpf_reset(WUKONG_AUDIO_HPF_T *st)
{
    if (st == NULL) {
        return;
    }

    st->prev_x = 0.0f;
    st->prev_y = 0.0f;
}

INT16_T wukong_audio_hpf_process_sample(WUKONG_AUDIO_HPF_T *st, INT16_T x)
{
#if WUKONG_AUDIO_HPF_ENABLE
    if (st == NULL || !st->inited) {
        return x;
    }

    float xf = (float)x;
    float y = st->alpha * (st->prev_y + xf - st->prev_x);
    st->prev_x = xf;
    st->prev_y = y;

    return __clip_s16(y);
#else
    (VOID)st;
    return x;
#endif
}

VOID_T wukong_audio_hpf_process_mono(WUKONG_AUDIO_HPF_T *st, INT16_T *pcm, UINT32_T samples)
{
    if (pcm == NULL || st == NULL) {
        return;
    }

    for (UINT32_T i = 0; i < samples; i++) {
        pcm[i] = wukong_audio_hpf_process_sample(st, pcm[i]);
    }
}

VOID_T wukong_audio_hpf_process_interleaved(WUKONG_AUDIO_HPF_T *st, INT16_T *pcm,
                                            UINT32_T samples, UINT32_T channels)
{
    if (pcm == NULL || st == NULL || channels == 0) {
        return;
    }

    for (UINT32_T i = 0; i < samples; i++) {
        for (UINT32_T ch = 0; ch < channels; ch++) {
            UINT32_T idx = i * channels + ch;
            pcm[idx] = wukong_audio_hpf_process_sample(&st[ch], pcm[idx]);
        }
    }
}
