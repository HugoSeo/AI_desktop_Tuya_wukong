#include "resample_fixed.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Fixed-point linear interpolation:
   - use 16.16 style for position: integer part in high 32-? but we use 32-bit with 16 frac (Q16)
   - step = (in_rate << 16) / OUT_SR
   - pos starts at 0
   - interpolation: s = ((s0*(1-frac) + s1*frac) >> 16)
*/
#define OUT_SR_16K 16000
#define OUT_SR_8K  8000

/* Saturate a 32-bit value to the signed 16-bit range.
 * Uses the single ARM SSAT instruction when the DSP extension is available
 * (Cortex-M33 with __ARM_FEATURE_DSP); otherwise falls back to a branchless
 * clamp. Replaces the previous clip16_from_i64() two-branch helper. The Q16
 * interpolation result fed here already fits in [-32768, 32767] for valid
 * int16 inputs, so this guard is defensive. */
static inline int16_t clamp_s16(int32_t x)
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

int resample_to_16k_fixed(const int16_t *in, size_t in_frames, int in_rate, int channels,
                          int16_t *out_buf_out, size_t *out_frames_out)
{
    if (!in || !out_buf_out || !out_frames_out || in_frames==0 || in_rate<=0 || channels<=0) return -1;
    /* compute output frames in pure integer arithmetic. The previous (double)
     * cast pulled in soft-float helpers (__aeabi_dmul/__aeabi_ddiv/
     * __aeabi_d2uz): Cortex-M33's FPv5-SP only has single-precision hardware,
     * so double is software-emulated. The integer form may differ from the
     * double version by one tail frame due to rounding, which is immaterial
     * for resampling. */
    size_t out_frames = (size_t)(((uint64_t)in_frames * OUT_SR_16K) / (uint64_t)in_rate);
    if (out_frames == 0) { *out_frames_out = 0; return 0; }
    int16_t *out = out_buf_out;
    if (!out) return -2;

    uint32_t step = (uint32_t)(((uint64_t)in_rate << 16) / (uint32_t)OUT_SR_16K);
    uint32_t pos = 0; /* Q16 fractional position in input samples, pos integer part = pos>>16 */

    for (size_t j=0;j<out_frames;++j){
        uint32_t idx = pos >> 16; /* index in input frames */
        uint32_t frac = pos & 0xFFFF; /* 0..65535 */

        /* read s0 and s1 as mono int32. Accumulate channels in int32: a handful
         * of int16 samples can never overflow int32, and keeping the sum 32-bit
         * makes sum/channels a hardware SDIV instead of __aeabi_ldivmod. */
        int32_t s0 = 0, s1 = 0;
        if ((size_t)idx < in_frames) {
            int32_t sum = 0;
            for (int c=0;c<channels;++c) sum += in[idx * channels + c];
            s0 = sum / channels;
        }
        if ((size_t)(idx + 1) < in_frames) {
            int32_t sum = 0;
            for (int c=0;c<channels;++c) sum += in[(idx + 1) * channels + c];
            s1 = sum / channels;
        }

        /* linear interpolation in integer: s = (s0*(65536-frac) + s1*frac) >> 16.
         * Products are kept in int64 (|s0|*(1<<16) reaches ~2.1e9, near
         * INT32_MAX) so GCC lowers them to SMULL with an SMLAL accumulate on
         * ARMv7-M/v8-M. Numerically identical to the previous form. */
        int64_t v = (int64_t)s0 * (int64_t)(0x10000 - frac) + (int64_t)s1 * (int64_t)frac;
        out[j] = clamp_s16((int32_t)(v >> 16));

        pos += step;
        /* if pos beyond last sample, clamp to last (prevents reading out of range) */
        if ((pos >> 16) >= in_frames) break;
    }

    *out_frames_out = out_frames;
    return 0;
}

int resample_to_8k_fixed(const int16_t *in, size_t in_frames, int in_rate, int channels,
                         int16_t *out_buf_out, size_t *out_frames_out)
{
    if (!in || !out_buf_out || !out_frames_out || in_frames==0 || in_rate<=0 || channels<=0) return -1;
    /* compute output frames (see the 16k variant for the integer-form rationale) */
    size_t out_frames = (size_t)(((uint64_t)in_frames * OUT_SR_8K) / (uint64_t)in_rate);
    if (out_frames == 0) { *out_frames_out = 0; return 0; }
    int16_t *out = out_buf_out;
    if (!out) return -2;

    uint32_t step = (uint32_t)(((uint64_t)in_rate << 16) / (uint32_t)OUT_SR_8K);
    uint32_t pos = 0; /* Q16 fractional position in input samples, pos integer part = pos>>16 */

    for (size_t j=0;j<out_frames;++j){
        uint32_t idx = pos >> 16; /* index in input frames */
        uint32_t frac = pos & 0xFFFF; /* 0..65535 */

        /* read s0 and s1 as mono int32 */
        int32_t s0 = 0, s1 = 0;
        if ((size_t)idx < in_frames) {
            int32_t sum = 0;
            for (int c=0;c<channels;++c) sum += in[idx * channels + c];
            s0 = sum / channels;
        }
        if ((size_t)(idx + 1) < in_frames) {
            int32_t sum = 0;
            for (int c=0;c<channels;++c) sum += in[(idx + 1) * channels + c];
            s1 = sum / channels;
        }

        /* linear interpolation in integer: s = (s0*(65536-frac) + s1*frac) >> 16 */
        int64_t v = (int64_t)s0 * (int64_t)(0x10000 - frac) + (int64_t)s1 * (int64_t)frac;
        out[j] = clamp_s16((int32_t)(v >> 16));

        pos += step;
        /* if pos beyond last sample, clamp to last (prevents reading out of range) */
        if ((pos >> 16) >= in_frames) break;
    }

    *out_frames_out = out_frames;
    return 0;
}
