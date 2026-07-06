/**
 * @file ifly_audio_aec_vad.c
 * @brief iFlytek dual-mic AEC + Tuya RNN VAD frontend implementation
 *
 * Provides a WUKONG_AUDIO_FRONTEND_OPS_T backend using the iFlytek
 * libifly_aec_wakeup.a library for echo cancellation and Tuya RNN VAD
 * for voice activity detection. Register g_ifly_frontend_ops via
 * wukong_audio_frontend_register() from board init code.
 *
 * Two process paths are supported, selected at compile time:
 *
 *   USE_TKL_AUDIO_INPUT=0 (default, tkl_audio.c pipeline):
 *     mic [L,R interleaved, 16kHz direct] + ref [mono, 16kHz]
 *     -> (optional) FIR LPF/decimation (ENABLE_DMIC_48K_DECIM)
 *     -> HPF DC removal (AUDIO_HP_ENABLE)
 *     -> gain (MIC_AUDIO_FIXGAIN)
 *     -> ifly_aec_process -> mono output
 *     -> Tuya RNN VAD
 *
 *   USE_TKL_AUDIO_INPUT=1 (ifly_audio.c direct driver):
 *     Bottom layer already done: 48k->FIR->16k + HPF + [L,R,Ref] split.
 *     This path skips HPF and LPF to avoid double-filtering, only does:
 *     gain -> ifly_aec_process -> mono output -> Tuya RNN VAD
 *
 * @version 1.0
 * @date 2026-03-25
 * @copyright Copyright (c) Tuya Inc.
 */

#include "ifly_audio_aec_vad.h"
#include "ifly_aec.h"
#include "vtn_auth_adapter.h"
#include "speexdsp/audio_subsys_rnn_vad.h"
#include "audio_dump.h"
#include "tal_memory.h"
#include "tal_log.h"
#include "tuya_app_config.h"
#include <math.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#ifndef USE_TKL_AUDIO_INPUT
#define USE_TKL_AUDIO_INPUT 1
#endif


/** ifly_aec_process fixed frame size in samples (8 ms @ 16 kHz) */
#define AEC_FRAME_SIZE        128

/** DMIC software gain compensation, consistent with ifly demo MIC_AUDIO_FIXGAIN */
#define MIC_AUDIO_FIXGAIN     6

/**
 * Gain applied to AEC output before VAD and KWS.
 * Matches WAKEUP_AUDIO_FIXGAIN in ifly_asr_common.h.
 * ifly_kws_detect must NOT apply additional gain on the same buffer.
 */
#define WAKEUP_AUDIO_FIXGAIN  5

/*
 * Enable DMIC 48k sampling + FIR low-pass decimation to 16k
 *   1: 48kHz capture -> FIR LPF (fc=8kHz) -> 3:1 decimation -> 16kHz output
 *   0: keep 16kHz direct capture (default)
 *
 * @note Ignored when USE_TKL_AUDIO_INPUT=1; the driver already handles
 *       48k->16k decimation before calling the process callback.
 */
#ifndef ENABLE_DMIC_48K_DECIM
#define ENABLE_DMIC_48K_DECIM  0
#endif

#if ENABLE_DMIC_48K_DECIM
#define DMIC_RAW_FS       48000
#define DMIC_DECIM_FACTOR 3
#define DMIC_FIR_TAPS     81
#else
#define DMIC_DECIM_FACTOR 1
#endif

/*
 * High-pass filter configuration (DC / low-frequency noise removal)
 *
 * AUDIO_HP_ENABLE       : 1 enable / 0 disable (default enabled)
 * AUDIO_HP_CUTOFF_HZ    : cutoff frequency, default 60 Hz
 * AUDIO_HP_FILTER_REF   : whether to also filter the ref channel (default 0)
 * AUDIO_HP_USE_BIQUAD   : 1 = 2nd-order Butterworth; 0 = 1st-order (default)
 * AUDIO_HP_BIQUAD_Q1000 : biquad Q * 1000, default 707 (≈ 0.707 Butterworth)
 *
 * @note Ignored when USE_TKL_AUDIO_INPUT=1; HPF is done in the driver.
 */
#ifndef AUDIO_HP_ENABLE
#define AUDIO_HP_ENABLE       1
#endif
#ifndef AUDIO_HP_CUTOFF_HZ
#define AUDIO_HP_CUTOFF_HZ    60
#endif
#ifndef AUDIO_HP_FILTER_REF
#define AUDIO_HP_FILTER_REF   0
#endif
#ifndef AUDIO_HP_USE_BIQUAD
#define AUDIO_HP_USE_BIQUAD   0
#endif
#ifndef AUDIO_HP_BIQUAD_Q1000
#define AUDIO_HP_BIQUAD_Q1000 707
#endif

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */

/** First-order high-pass filter state */
typedef struct {
    float alpha;
    float prev_x;
    float prev_y;
    int   inited;
} audio_hpf1_t;

/** Second-order biquad high-pass filter state */
typedef struct {
    float b0, b1, b2;
    float a1, a2;
    float x1, x2;
    float y1, y2;
    int   inited;
} audio_hpf2_t;

#if ENABLE_DMIC_48K_DECIM
/** Dual-channel (LR interleaved) FIR low-pass + decimator state */
typedef struct {
    float coeffs[DMIC_FIR_TAPS];
    float delayL[DMIC_FIR_TAPS];
    float delayR[DMIC_FIR_TAPS];
    int   wr;
} lpf_decimator_t;
#endif

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */

STATIC VOID                      *s_rnn_vad_handle = NULL;
STATIC INT16_T                   *s_aec_out         = NULL;
STATIC UINT32_T                   s_frame_size       = 0;
STATIC WUKONG_AUDIO_VAD_FLAG_E    s_vad_flag         = WUKONG_AUDIO_VAD_STOP;

#if !defined(USE_TKL_AUDIO_INPUT) || (USE_TKL_AUDIO_INPUT == 0)
STATIC audio_hpf1_t s_hpf1_l, s_hpf1_r;
STATIC audio_hpf2_t s_hpf2_l, s_hpf2_r;
#if AUDIO_HP_FILTER_REF
STATIC audio_hpf1_t s_hpf1_ref;
STATIC audio_hpf2_t s_hpf2_ref;
#endif
#if ENABLE_DMIC_48K_DECIM
STATIC lpf_decimator_t s_lpf_decim;
#endif
#endif /* !USE_TKL_AUDIO_INPUT */

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */

STATIC OPERATE_RET __ifly_init(UINT32_T min_speech_len_ms, UINT32_T max_speech_interval_ms, UINT32_T frame_size);
STATIC OPERATE_RET __ifly_deinit(VOID);

/* ---------------------------------------------------------------------------
 * Filter helpers (only compiled for the non-direct path)
 * --------------------------------------------------------------------------- */

#if !defined(USE_TKL_AUDIO_INPUT) || (USE_TKL_AUDIO_INPUT == 0)

/**
 * @brief Initialize first-order high-pass filter
 * @param[out] st  Filter state
 * @param[in]  fs  Sample rate in Hz
 * @param[in]  fc  Cutoff frequency in Hz
 * @return none
 */
STATIC VOID audio_hpf1_init(audio_hpf1_t *st, float fs, float fc)
{
    if (!st) {
        return;
    }
    if (fc < 1.0f) {
        fc = 1.0f;
    }
    float RC   = 1.0f / (2.0f * (float)M_PI * fc);
    float dt   = 1.0f / fs;
    st->alpha  = RC / (RC + dt);
    st->prev_x = 0.0f;
    st->prev_y = 0.0f;
    st->inited = 1;
}

/**
 * @brief Process one sample through first-order HPF
 * @param[in] st  Filter state
 * @param[in] x   Input sample
 * @return Filtered sample
 */
STATIC INT16_T audio_hpf1_process(audio_hpf1_t *st, INT16_T x)
{
    if (!st || !st->inited) {
        return x;
    }
    float xf = (float)x;
    float y  = st->alpha * (st->prev_y + xf - st->prev_x);
    st->prev_x = xf;
    st->prev_y = y;
    if (y >  32767.0f)  { y =  32767.0f; }
    else if (y < -32768.0f) { y = -32768.0f; }
    return (INT16_T)y;
}

/**
 * @brief Initialize second-order biquad high-pass filter (Butterworth)
 * @param[out] st  Filter state
 * @param[in]  fs  Sample rate in Hz
 * @param[in]  fc  Cutoff frequency in Hz
 * @param[in]  Q   Quality factor (0.707 = Butterworth)
 * @return none
 */
STATIC VOID audio_hpf2_init(audio_hpf2_t *st, float fs, float fc, float Q)
{
    if (!st) {
        return;
    }
    if (fc < 1.0f) { fc = 1.0f; }
    if (Q  < 0.1f) { Q  = 0.1f; }
    float w0     = 2.0f * (float)M_PI * fc / fs;
    float cos_w0 = cosf(w0);
    float sin_w0 = sinf(w0);
    float alpha  = sin_w0 / (2.0f * Q);
    float b0     =  (1.0f + cos_w0) / 2.0f;
    float b1     = -(1.0f + cos_w0);
    float b2     =  (1.0f + cos_w0) / 2.0f;
    float a0     =   1.0f + alpha;
    float a1     =  -2.0f * cos_w0;
    float a2     =   1.0f - alpha;
    st->b0 = b0 / a0;  st->b1 = b1 / a0;  st->b2 = b2 / a0;
    st->a1 = a1 / a0;  st->a2 = a2 / a0;
    st->x1 = st->x2 = 0.0f;
    st->y1 = st->y2 = 0.0f;
    st->inited = 1;
}

/**
 * @brief Process one sample through second-order biquad HPF
 * @param[in] st  Filter state
 * @param[in] x   Input sample
 * @return Filtered sample
 */
STATIC INT16_T audio_hpf2_process(audio_hpf2_t *st, INT16_T x)
{
    if (!st || !st->inited) {
        return x;
    }
    float xf = (float)x;
    float y  = st->b0 * xf + st->b1 * st->x1 + st->b2 * st->x2
               - st->a1 * st->y1 - st->a2 * st->y2;
    st->x2 = st->x1;  st->x1 = xf;
    st->y2 = st->y1;  st->y1 = y;
    if (y >  32767.0f)  { y =  32767.0f; }
    else if (y < -32768.0f) { y = -32768.0f; }
    return (INT16_T)y;
}

/**
 * @brief Route one sample through the selected HPF order
 * @param[in] sample  Input sample
 * @param[in] st1     First-order state  (AUDIO_HP_USE_BIQUAD == 0)
 * @param[in] st2     Second-order state (AUDIO_HP_USE_BIQUAD == 1)
 * @return Filtered sample
 */
STATIC inline INT16_T audio_hp_process(INT16_T sample, audio_hpf1_t *st1, audio_hpf2_t *st2)
{
#if AUDIO_HP_USE_BIQUAD
    return audio_hpf2_process(st2, sample);
#else
    (VOID)st2;
    return audio_hpf1_process(st1, sample);
#endif
}

#if ENABLE_DMIC_48K_DECIM
/**
 * @brief Design FIR low-pass coefficients (Hamming-windowed sinc)
 * @param[out] h     Coefficient array of length taps
 * @param[in]  taps  Number of taps
 * @param[in]  fs    Sample rate
 * @param[in]  fc    Cutoff frequency
 * @return none
 */
STATIC VOID design_fir_lowpass(float *h, int taps, float fs, float fc)
{
    int   M       = taps - 1;
    float norm_fc = fc / fs;
    for (int n = 0; n <= M; ++n) {
        int   k    = n - M / 2;
        float sinc = (k == 0) ? 2.0f * (float)M_PI * norm_fc
                               : sinf(2.0f * (float)M_PI * norm_fc * k) / (float)k;
        float w    = 0.54f - 0.46f * cosf(2.0f * (float)M_PI * n / M);
        h[n] = (sinc / (float)M_PI) * w;
    }
    float sum = 0.0f;
    for (int i = 0; i < taps; ++i) { sum += h[i]; }
    if (sum != 0.0f) {
        float inv = 1.0f / sum;
        for (int i = 0; i < taps; ++i) { h[i] *= inv; }
    }
}

/**
 * @brief Initialize FIR low-pass decimator state
 * @param[out] st  Decimator state
 * @return none
 */
STATIC VOID lpf_decimator_init(lpf_decimator_t *st)
{
    memset(st, 0, sizeof(*st));
    design_fir_lowpass(st->coeffs, DMIC_FIR_TAPS, (float)DMIC_RAW_FS, 8000.0f);
}

/**
 * @brief Apply FIR LPF + 3:1 decimation on LR-interleaved input
 * @param[in]  st          Decimator state
 * @param[in]  in          Input LR-interleaved samples at DMIC_RAW_FS
 * @param[in]  in_frames   Input frame count (DMIC_DECIM_FACTOR × AEC_FRAME_SIZE)
 * @param[out] out         Output LR-interleaved at 16kHz (AEC_FRAME_SIZE frames)
 * @return Number of output frames written
 */
STATIC int lpf_decimator_process(lpf_decimator_t *st, const INT16_T *in, int in_frames, INT16_T *out)
{
    const int taps  = DMIC_FIR_TAPS;
    const int decim = DMIC_DECIM_FACTOR;
    int out_frames  = 0;
    for (int n = 0; n < in_frames; ++n) {
        st->delayL[st->wr] = (float)in[2 * n];
        st->delayR[st->wr] = (float)in[2 * n + 1];
        if (++st->wr == taps) { st->wr = 0; }
        if ((n % decim) == (decim - 1)) {
            float accL = 0.0f, accR = 0.0f;
            int   idx  = st->wr;
            for (int k = 0; k < taps; ++k) {
                if (--idx < 0) { idx = taps - 1; }
                accL += st->coeffs[k] * st->delayL[idx];
                accR += st->coeffs[k] * st->delayR[idx];
            }
            if (accL >  32767.0f)  { accL =  32767.0f; }
            else if (accL < -32768.0f) { accL = -32768.0f; }
            if (accR >  32767.0f)  { accR =  32767.0f; }
            else if (accR < -32768.0f) { accR = -32768.0f; }
            out[2 * out_frames]     = (INT16_T)accL;
            out[2 * out_frames + 1] = (INT16_T)accR;
            out_frames++;
        }
    }
    return out_frames;
}
#endif /* ENABLE_DMIC_48K_DECIM */

#endif /* !USE_TKL_AUDIO_INPUT */

/* ---------------------------------------------------------------------------
 * Frontend ops implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Initialize ifly AEC + Tuya RNN VAD frontend
 * @param[in] min_speech_len_ms      Minimum speech length for RNN VAD (ms)
 * @param[in] max_speech_interval_ms Maximum silence gap before VAD stop (ms)
 * @param[in] frame_size             Audio frame size in bytes
 * @return OPRT_OK on success, error code on failure
 */
STATIC OPERATE_RET __ifly_init(UINT32_T min_speech_len_ms, UINT32_T max_speech_interval_ms, UINT32_T frame_size)
{
    extern void ifly_auth_init(void);
    ifly_auth_init();

    ifly_aec_set_mic_number(2);
    if (ifly_aec_init() != 0) {
        TAL_PR_ERR("ifly_aec_init failed");
        return OPRT_COM_ERROR;
    }

    if (s_aec_out == NULL) {
#ifdef ENABLE_EXT_RAM
        s_aec_out = (INT16_T *)tal_psram_malloc(AEC_FRAME_SIZE * sizeof(INT16_T));
#else
        s_aec_out = (INT16_T *)tal_malloc(AEC_FRAME_SIZE * sizeof(INT16_T));
#endif
        if (s_aec_out == NULL) {
            ifly_aec_uninit();
            return OPRT_MALLOC_FAILED;
        }
    }

#if !defined(USE_TKL_AUDIO_INPUT) || (USE_TKL_AUDIO_INPUT == 0)
#if AUDIO_HP_ENABLE
    {
        float fs = 16000.0f;
        float fc = (float)AUDIO_HP_CUTOFF_HZ;
#if AUDIO_HP_USE_BIQUAD
        float Q = ((float)AUDIO_HP_BIQUAD_Q1000) / 1000.0f;
        audio_hpf2_init(&s_hpf2_l,   fs, fc, Q);
        audio_hpf2_init(&s_hpf2_r,   fs, fc, Q);
#if AUDIO_HP_FILTER_REF
        audio_hpf2_init(&s_hpf2_ref, fs, fc, Q);
#endif
#else
        audio_hpf1_init(&s_hpf1_l,   fs, fc);
        audio_hpf1_init(&s_hpf1_r,   fs, fc);
#if AUDIO_HP_FILTER_REF
        audio_hpf1_init(&s_hpf1_ref, fs, fc);
#endif
#endif /* AUDIO_HP_USE_BIQUAD */
        TAL_PR_DEBUG("ifly HPF enabled fc=%dHz", AUDIO_HP_CUTOFF_HZ);
    }
#endif /* AUDIO_HP_ENABLE */
#if ENABLE_DMIC_48K_DECIM
    lpf_decimator_init(&s_lpf_decim);
    TAL_PR_DEBUG("ifly LPF decimation enabled: %d->16kHz taps=%d", DMIC_RAW_FS, DMIC_FIR_TAPS);
#else
    TAL_PR_DEBUG("ifly LPF decimation disabled, 16kHz direct capture");
#endif
#endif /* !USE_TKL_AUDIO_INPUT */

    if (s_rnn_vad_handle == NULL) {
        s_rnn_vad_handle = rnn_vad_create();
        if (s_rnn_vad_handle == NULL) {
            __ifly_deinit();
            return OPRT_MALLOC_FAILED;
        }
        struct _rnn_vad_param_in param = {0};
        param.min_speech_len      = min_speech_len_ms;
        param.max_speech_interval = max_speech_interval_ms;
        rnn_vad_init(&param, s_rnn_vad_handle);
        rnn_vad_set_callback(s_rnn_vad_handle, -50); /* default MID threshold */
    }

    s_frame_size = frame_size;
#if defined(USE_TKL_AUDIO_INPUT) && (USE_TKL_AUDIO_INPUT == 1)
    TAL_PR_DEBUG("ifly audio aec_vad -> init (direct path, HPF/decim done in driver), "
                 "frame_size=%d aec_out=%p vad=%p", s_frame_size, s_aec_out, s_rnn_vad_handle);
#else
    TAL_PR_DEBUG("ifly audio aec_vad -> init, frame_size=%d aec_out=%p vad=%p",
                 s_frame_size, s_aec_out, s_rnn_vad_handle);
#endif
    return OPRT_OK;
}

/**
 * @brief Deinitialize ifly AEC frontend and release all resources
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __ifly_deinit(VOID)
{
    if (s_aec_out) {
        tal_free(s_aec_out);
        s_aec_out = NULL;
    }
    ifly_aec_uninit();
    if (s_rnn_vad_handle) {
        rnn_vad_destroy(s_rnn_vad_handle);
        s_rnn_vad_handle = NULL;
    }
    s_frame_size = 0;
    return OPRT_OK;
}

/**
 * @brief Process one audio frame through ifly AEC and Tuya RNN VAD
 *
 * Two paths based on USE_TKL_AUDIO_INPUT:
 *   0 (default): (decim ->) HPF -> gain -> ifly AEC -> VAD
 *   1 (direct) : HPF/decim already done by driver; only gain -> ifly AEC -> VAD
 *
 * @param[in]  mic_data  LR-interleaved dual-mic PCM (16kHz, or 48kHz when ENABLE_DMIC_48K_DECIM=1)
 * @param[in]  ref_data  Reference (speaker) PCM, mono 16kHz
 * @param[out] out_data  AEC-processed mono output, AEC_FRAME_SIZE samples
 * @return OPRT_OK on success
 *
 * @note KWS feeding is handled by the frontend dispatcher (wukong_audio_frontend.c).
 */
STATIC OPERATE_RET __ifly_process(INT16_T *mic_data, INT16_T *ref_data, INT16_T *out_data)
{
    TUYA_CHECK_NULL_RETURN(mic_data, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(ref_data, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(out_data, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(s_aec_out, OPRT_RESOURCE_NOT_READY);

    INT16_T aec_in[AEC_FRAME_SIZE * 3];

#if defined(USE_TKL_AUDIO_INPUT) && (USE_TKL_AUDIO_INPUT == 1)
    /*
     * Direct path (USE_TKL_AUDIO_INPUT=1):
     * HPF and 48k->16k FIR decimation are already done in ifly_audio.c
     * (audio_input_read). Only apply gain and assemble the 3-channel buffer.
     */
    for (INT32_T i = 0; i < AEC_FRAME_SIZE; i++) {
        aec_in[i * 3]     = mic_data[i * 2]     * MIC_AUDIO_FIXGAIN;  /* L   × FIXGAIN */
        aec_in[i * 3 + 1] = mic_data[i * 2 + 1] * MIC_AUDIO_FIXGAIN;  /* R   × FIXGAIN */
        aec_in[i * 3 + 2] = ref_data[i]         * MIC_AUDIO_FIXGAIN;  /* Ref × FIXGAIN */
    }
#else
    /*
     * Standard path (USE_TKL_AUDIO_INPUT=0 / tkl_audio.c pipeline):
     * Apply optional FIR LPF/decimation then HPF before gain.
     */
#if ENABLE_DMIC_48K_DECIM
    INT16_T mic_16k[AEC_FRAME_SIZE * 2];
    lpf_decimator_process(&s_lpf_decim, mic_data, AEC_FRAME_SIZE * DMIC_DECIM_FACTOR, mic_16k);
    INT16_T *mic_ptr = mic_16k;
#else
    INT16_T *mic_ptr = mic_data;
#endif

    for (INT32_T i = 0; i < AEC_FRAME_SIZE; i++) {
#if AUDIO_HP_ENABLE
        INT16_T l   = audio_hp_process(mic_ptr[i * 2],     &s_hpf1_l, &s_hpf2_l);
        INT16_T r   = audio_hp_process(mic_ptr[i * 2 + 1], &s_hpf1_r, &s_hpf2_r);
#if AUDIO_HP_FILTER_REF
        INT16_T ref = audio_hp_process(ref_data[i], &s_hpf1_ref, &s_hpf2_ref);
#else
        INT16_T ref = ref_data[i];
#endif
#else
        INT16_T l   = mic_ptr[i * 2];
        INT16_T r   = mic_ptr[i * 2 + 1];
        INT16_T ref = ref_data[i];
#endif /* AUDIO_HP_ENABLE */
        aec_in[i * 3]     = l   * MIC_AUDIO_FIXGAIN;
        aec_in[i * 3 + 1] = r   * MIC_AUDIO_FIXGAIN;
        aec_in[i * 3 + 2] = ref * MIC_AUDIO_FIXGAIN;
    }
#endif /* USE_TKL_AUDIO_INPUT */

    if (ifly_aec_process(aec_in, s_aec_out, AEC_FRAME_SIZE) < 0) {
        TAL_PR_ERR("ifly_aec_process failed");
        return OPRT_COM_ERROR;
    }

    /* Apply wakeup gain to AEC output in-place.
     * This gain matches WAKEUP_AUDIO_FIXGAIN in ifly_asr_common.h and
     * the original IFLY_IVW_FIXGAIN in ifly_kws.c. The amplified buffer
     * is used for both VAD detection and KWS feeding, so ifly_kws_detect
     * skips gain. */
    for (INT32_T i = 0; i < AEC_FRAME_SIZE; i++) {
        s_aec_out[i] = (int16_t)(s_aec_out[i] * WAKEUP_AUDIO_FIXGAIN);
    }

    memcpy(out_data, s_aec_out, AEC_FRAME_SIZE * sizeof(INT16_T));
    audio_dump_write(AUDIO_DUMP_MIC, aec_in,   AEC_FRAME_SIZE * 3 * sizeof(INT16_T));
    audio_dump_write(AUDIO_DUMP_AEC, s_aec_out, AEC_FRAME_SIZE     * sizeof(INT16_T));

    if (s_rnn_vad_handle) {
        BOOL_T has_vad = rnn_vad_process(s_rnn_vad_handle, (short *)s_aec_out);
        if (has_vad && s_vad_flag != WUKONG_AUDIO_VAD_START) {
            TAL_PR_DEBUG("################ [vad start] ################");
            s_vad_flag = WUKONG_AUDIO_VAD_START;
        } else if (!has_vad && s_vad_flag != WUKONG_AUDIO_VAD_STOP) {
            TAL_PR_DEBUG("################ [vad stop] ################");
            s_vad_flag = WUKONG_AUDIO_VAD_STOP;
        }
    }

    return OPRT_OK;
}

/**
 * @brief Start VAD detection; resets VAD flag to STOP
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __ifly_vad_start(VOID)
{
    s_vad_flag = WUKONG_AUDIO_VAD_STOP;
    if (s_rnn_vad_handle) {
        rnn_vad_start(s_rnn_vad_handle);
    }
    return OPRT_OK;
}

/**
 * @brief Stop VAD detection; resets VAD flag to STOP
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __ifly_vad_stop(VOID)
{
    s_vad_flag = WUKONG_AUDIO_VAD_STOP;
    if (s_rnn_vad_handle) {
        rnn_vad_stop(s_rnn_vad_handle);
    }
    return OPRT_OK;
}

/**
 * @brief Set VAD sensitivity threshold
 * @param[in] level  WUKONG_AUDIO_VAD_HIGH / MID / LOW
 * @return OPRT_OK on success, OPRT_RESOURCE_NOT_READY if VAD not initialized
 */
STATIC OPERATE_RET __ifly_vad_set_threshold(WUKONG_AUDIO_VAD_THRESHOLD_E level)
{
    if (s_rnn_vad_handle == NULL) {
        return OPRT_RESOURCE_NOT_READY;
    }
    switch (level) {
    case WUKONG_AUDIO_VAD_HIGH:
        rnn_vad_set_callback(s_rnn_vad_handle, -40);
        break;
    case WUKONG_AUDIO_VAD_MID:
        rnn_vad_set_callback(s_rnn_vad_handle, -50);
        break;
    case WUKONG_AUDIO_VAD_LOW:
        rnn_vad_set_callback(s_rnn_vad_handle, -60);
        break;
    default:
        break;
    }
    return OPRT_OK;
}

/**
 * @brief Get current VAD flag
 * @return WUKONG_AUDIO_VAD_START if speech detected, WUKONG_AUDIO_VAD_STOP otherwise
 */
STATIC INT_T __ifly_vad_get_flag(VOID)
{
    return (INT_T)s_vad_flag;
}

/**
 * @brief Get KWS-ready output buffer (AEC mono output)
 * @param[out] data  Pointer to mono PCM buffer
 * @param[out] len   Buffer length in bytes (AEC_FRAME_SIZE * sizeof(INT16_T))
 * @return OPRT_OK on success, OPRT_RESOURCE_NOT_READY if not initialized
 */
STATIC OPERATE_RET __ifly_get_kws_output(INT16_T **data, UINT32_T *len)
{
    if (s_aec_out == NULL) {
        return OPRT_RESOURCE_NOT_READY;
    }
    *data = s_aec_out;
    *len  = AEC_FRAME_SIZE * sizeof(INT16_T);
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Public ops table
 * --------------------------------------------------------------------------- */

WUKONG_AUDIO_FRONTEND_OPS_T g_ifly_frontend_ops = {
    .init              = __ifly_init,
    .deinit            = __ifly_deinit,
    .process           = __ifly_process,
    .vad_start         = __ifly_vad_start,
    .vad_stop          = __ifly_vad_stop,
    .vad_set_threshold = __ifly_vad_set_threshold,
    .vad_get_flag      = __ifly_vad_get_flag,
    .get_kws_output    = __ifly_get_kws_output,
};
