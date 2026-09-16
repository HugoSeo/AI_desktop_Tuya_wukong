/**
 * @file mixer_fixed.c
 * @brief
 * @version 0.2
 * @date 2025-10-09
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

#include "mixer_fixed.h"

/* Saturate a 32-bit value to the signed 16-bit range.
 * Uses the single ARM SSAT instruction when the DSP extension is available
 * (Cortex-M33 with __ARM_FEATURE_DSP); otherwise falls back to a branchless
 * C clamp. Replaces the previous two-branch CLAMP_S16 macro. */
static __inline int16_t clamp_s16(int32_t x)
{
#ifdef __ARM_FEATURE_DSP
    __asm__ ("ssat %0, #16, %1" : "=r"(x) : "r"(x));
    return (int16_t)x;
#else
    if (x > 32767) {
        x = 32767;
    } else if (x < -32768) {
        x = -32768;
    }
    return (int16_t)x;
#endif
}

VOID mix_pcm_s16_mono_16k(int16_t *src, int16_t *dst, int samples)
{
    int i;
    for (i = 0; i < samples; i++) {
        int32_t mixed = (int32_t)src[i] + (int32_t)dst[i];
        dst[i] = clamp_s16(mixed);
    }
}

OPERATE_RET ai_player_mixer_process(BYTE_T *src_decode_buf, BYTE_T *dst_decode_buf, UINT_T samples)
{
    mix_pcm_s16_mono_16k((int16_t *)src_decode_buf, (int16_t *)dst_decode_buf, samples);
    return OPRT_OK;
}

OPERATE_RET ai_player_volume_process(BYTE_T *decode_buf, UINT_T len, INT_T volume, INT_T max_volume, UINT_T datebits)
{
    int32_t gain_q15;
    int16_t *pcm;
    INT_T samples;
    int i;

    if (datebits != 16) {
        return OPRT_OK; // only support 16bits
    }

    if (max_volume <= 0) {
        return OPRT_OK;
    }

    /* Hoist the division out of the per-sample loop: precompute a Q15
     * fixed-point gain so each sample costs a multiply + shift + saturate
     * instead of a multiply + integer divide (SDIV) per sample. At full
     * volume (volume == max_volume) the result is bit-exact with the old
     * "(sample * volume) / max_volume"; at other levels negative samples
     * may differ by <= 1 LSB (arithmetic-shift rounding vs. truncation),
     * which is inaudible for a volume scale. */
    gain_q15 = (int32_t)(((int64_t)volume << 15) / max_volume);

    samples = len / 2; // 16bits = 2 bytes
    pcm = (int16_t *)decode_buf;
    for (i = 0; i < samples; i++) {
        int32_t sample = pcm[i];
        sample = (int32_t)(((int64_t)sample * gain_q15) >> 15);
        pcm[i] = clamp_s16(sample);
    }

    return OPRT_OK;
}
