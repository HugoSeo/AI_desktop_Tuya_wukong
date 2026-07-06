/**
 * @file wukong_audio_aec_vad.c
 * @brief Speex AEC + RNN VAD frontend implementation
 *
 * Provides a WUKONG_AUDIO_FRONTEND_OPS_T backend using Speex echo cancellation
 * and RNN-based voice activity detection. Register g_tuya_frontend_ops
 * via wukong_audio_frontend_register() from board init code.
 *
 * @copyright Copyright (c) Tuya Inc.
 */

#include "tal_memory.h"
#include "tal_system.h"
#include "tal_log.h"
#include "tuya_app_config.h"
#include "audio_dump.h"
#include "wukong_audio_frontend.h"
#include "speexdsp/audio_subsys_speexdsp_wrap2.h"
#include "speexdsp/audio_subsys_rnn_vad.h"

#define TUYA_AEC_VAD_DEBUG 1

STATIC VOID *__s_speex_aec_handle = NULL;
STATIC VOID *__s_rnn_vad_handle = NULL;
STATIC UINT16_T *__s_linearaec = NULL;
STATIC UINT32_T __s_frame_size = 0;
STATIC WUKONG_AUDIO_VAD_FLAG_E __s_aec_vad_flag = WUKONG_AUDIO_VAD_STOP;

STATIC OPERATE_RET __speex_rnn_vad_set_threshold(WUKONG_AUDIO_VAD_THRESHOLD_E level);
STATIC OPERATE_RET __speex_rnn_deinit(VOID);

STATIC OPERATE_RET __speex_rnn_init(UINT32_T min_speech_len_ms, UINT32_T max_speech_interval_ms, UINT32_T frame_size)
{
    OPERATE_RET rt = OPRT_OK;

    if (__s_speex_aec_handle == NULL) {
        __s_speex_aec_handle = speex_aes_create(frame_size / 2);
        speex_aes_set_param(__s_speex_aec_handle, 5);
        speex_ns_set_param(__s_speex_aec_handle, 8, 10);
        TUYA_CHECK_NULL_RETURN(__s_speex_aec_handle, OPRT_COM_ERROR);
    }

    if (__s_rnn_vad_handle == NULL) {
        __s_rnn_vad_handle = rnn_vad_create();
        struct _rnn_vad_param_in param = {0};
        param.min_speech_len = min_speech_len_ms;
        param.max_speech_interval = max_speech_interval_ms;
        rnn_vad_init(&param, __s_rnn_vad_handle);
        __speex_rnn_vad_set_threshold(WUKONG_AUDIO_VAD_MID);
        TUYA_CHECK_NULL_GOTO(__s_rnn_vad_handle, __err_exit);
    }

    if (__s_linearaec == NULL) {
#ifdef ENABLE_EXT_RAM
        __s_linearaec = tal_psram_malloc(frame_size * 2);
        TUYA_CHECK_NULL_GOTO(__s_linearaec, __err_exit);
#else
        __s_linearaec = tal_malloc(frame_size * 2);
        TUYA_CHECK_NULL_RETURN(__s_linearaec, OPRT_MALLOC_FAILED);
#endif
    }
    __s_frame_size = frame_size;
    TAL_PR_DEBUG("wukong aec -> init, frame size %d, aec %p, vad %p, lineraraec %p",
                 __s_frame_size, __s_speex_aec_handle, __s_rnn_vad_handle, __s_linearaec);
    return OPRT_OK;

__err_exit:
    __speex_rnn_deinit();
    return rt;
}

STATIC OPERATE_RET __speex_rnn_deinit(VOID)
{
    if (__s_linearaec) {
        tal_free(__s_linearaec);
        __s_linearaec = NULL;
    }

    if (__s_rnn_vad_handle) {
        rnn_vad_destroy(__s_rnn_vad_handle);
        __s_rnn_vad_handle = NULL;
    }

    if (__s_speex_aec_handle) {
        speex_aes_destory(__s_speex_aec_handle);
        __s_speex_aec_handle = NULL;
    }

    __s_frame_size = 0;
    return OPRT_OK;
}

STATIC OPERATE_RET __speex_rnn_process(INT16_T *mic_data, INT16_T *ref_data, INT16_T *out_data)
{
    TUYA_CHECK_NULL_RETURN(mic_data, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(ref_data, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(out_data, OPRT_INVALID_PARM);

    if (__s_speex_aec_handle) {
        UINT32_T begin = tal_system_get_millisecond();
        speex_aes_process(__s_speex_aec_handle, (short *)mic_data, (short *)ref_data, (short *)out_data);

        audio_dump_write(AUDIO_DUMP_MIC, mic_data, __s_frame_size);
        audio_dump_write(AUDIO_DUMP_REF, ref_data, __s_frame_size);
        audio_dump_write(AUDIO_DUMP_AEC, out_data, __s_frame_size);

        UINT32_T end = tal_system_get_millisecond();
#if defined(TUYA_AEC_VAD_DEBUG) && (TUYA_AEC_VAD_DEBUG == 1)
        STATIC INT_T cnt = 0;
        if (cnt++ % 500 == 0) {
            TAL_PR_DEBUG("speexdsp process time: aec->%d(ms), count = %d\r\n", end - begin, cnt);
        }
#endif
    }

    if (__s_speex_aec_handle && __s_rnn_vad_handle && __s_linearaec) {
        UINT32_T begin = tal_system_get_millisecond();
        BOOL_T has_vad = rnn_vad_process(__s_rnn_vad_handle, (short *)out_data);
        if (has_vad && __s_aec_vad_flag != WUKONG_AUDIO_VAD_START) {
            TAL_PR_DEBUG("################ [vad start] ################ \r\n");
            __s_aec_vad_flag = WUKONG_AUDIO_VAD_START;
        } else if (!has_vad && __s_aec_vad_flag != WUKONG_AUDIO_VAD_STOP) {
            TAL_PR_DEBUG("################ [vad stop] ################ \r\n");
            __s_aec_vad_flag = WUKONG_AUDIO_VAD_STOP;
        }
        UINT32_T end = tal_system_get_millisecond();

#if defined(TUYA_AEC_VAD_DEBUG) && (TUYA_AEC_VAD_DEBUG == 1)
        STATIC INT_T cnt = 0;
        if (cnt++ % 500 == 0) {
            TAL_PR_DEBUG("rnn_date_version: 25082101 \r\n");
            TAL_PR_DEBUG("rnn vad process time: rnn vad->%d(ms), flag=%d, count = %d\r\n", end - begin, __s_aec_vad_flag, cnt);
        }
#endif

        speex_get_param(__s_speex_aec_handle, NULL, (short *)__s_linearaec);
    }

    return OPRT_OK;
}

STATIC OPERATE_RET __speex_rnn_vad_set_threshold(WUKONG_AUDIO_VAD_THRESHOLD_E level)
{
    if (__s_rnn_vad_handle) {
        switch (level) {
        case WUKONG_AUDIO_VAD_HIGH:
            rnn_vad_set_callback(__s_rnn_vad_handle, -40);
            break;
        case WUKONG_AUDIO_VAD_MID:
            rnn_vad_set_callback(__s_rnn_vad_handle, -50);
            break;
        case WUKONG_AUDIO_VAD_LOW:
            rnn_vad_set_callback(__s_rnn_vad_handle, -60);
            break;
        default:
            break;
        }
        return OPRT_OK;
    }
    return OPRT_RESOURCE_NOT_READY;
}

STATIC OPERATE_RET __speex_rnn_vad_start(VOID)
{
    __s_aec_vad_flag = WUKONG_AUDIO_VAD_STOP;
    if (__s_rnn_vad_handle) {
        rnn_vad_start(__s_rnn_vad_handle);
    }
    return OPRT_OK;
}

STATIC OPERATE_RET __speex_rnn_vad_stop(VOID)
{
    __s_aec_vad_flag = WUKONG_AUDIO_VAD_STOP;
    if (__s_rnn_vad_handle) {
        rnn_vad_stop(__s_rnn_vad_handle);
    }
    return OPRT_OK;
}

STATIC INT_T __speex_rnn_vad_get_flag(VOID)
{
    return __s_aec_vad_flag;
}

STATIC OPERATE_RET __speex_rnn_get_kws_output(INT16_T **data, UINT32_T *len)
{
    if (__s_linearaec && __s_frame_size > 0) {
        *data = (INT16_T *)__s_linearaec;
        *len = __s_frame_size;
        return OPRT_OK;
    }
    return OPRT_RESOURCE_NOT_READY;
}

WUKONG_AUDIO_FRONTEND_OPS_T g_tuya_frontend_ops = {
    .init              = __speex_rnn_init,
    .deinit            = __speex_rnn_deinit,
    .process           = __speex_rnn_process,
    .vad_start         = __speex_rnn_vad_start,
    .vad_stop          = __speex_rnn_vad_stop,
    .vad_set_threshold = __speex_rnn_vad_set_threshold,
    .vad_get_flag      = __speex_rnn_vad_get_flag,
    .get_kws_output    = __speex_rnn_get_kws_output,
};
