/**
 * @file wukong_audio_frontend.c
 * @brief Audio frontend dispatcher
 *
 * Delegates to the registered ops implementation and feeds KWS after
 * frontend processing completes.
 *
 * @copyright Copyright (c) Tuya Inc.
 */

#include "wukong_audio_frontend.h"
#include "wukong_kws.h"
#include "tal_log.h"

STATIC WUKONG_AUDIO_FRONTEND_OPS_T *s_frontend_ops = NULL;
STATIC UINT32_T s_frame_size = 0;

OPERATE_RET wukong_audio_frontend_register(WUKONG_AUDIO_FRONTEND_OPS_T *ops)
{
    TUYA_CHECK_NULL_RETURN(ops, OPRT_INVALID_PARM);
    s_frontend_ops = ops;
    return OPRT_OK;
}

OPERATE_RET wukong_audio_frontend_init(UINT32_T min_speech_len_ms, UINT32_T max_speech_interval_ms, UINT32_T frame_size)
{
    if (!s_frontend_ops || !s_frontend_ops->init) {
        return OPRT_OK;
    }

    s_frame_size = frame_size;
    return s_frontend_ops->init(min_speech_len_ms, max_speech_interval_ms, frame_size);
}

OPERATE_RET wukong_audio_frontend_deinit(VOID)
{
    if (!s_frontend_ops || !s_frontend_ops->deinit) {
        return OPRT_OK;
    }

    OPERATE_RET rt = s_frontend_ops->deinit();
    s_frame_size = 0;
    return rt;
}

OPERATE_RET wukong_audio_frontend_process(INT16_T *mic_data, INT16_T *ref_data, INT16_T *out_data)
{
    TUYA_CHECK_NULL_RETURN(mic_data, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(ref_data, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(out_data, OPRT_INVALID_PARM);

    if (!s_frontend_ops || !s_frontend_ops->process) {
        return OPRT_OK;
    }

    OPERATE_RET rt = s_frontend_ops->process(mic_data, ref_data, out_data);
    if (rt != OPRT_OK) {
        return rt;
    }

    INT16_T *kws_data = out_data;
    UINT32_T kws_len = s_frame_size;

    if (s_frontend_ops->get_kws_output) {
        s_frontend_ops->get_kws_output(&kws_data, &kws_len);
    }

    INT_T vad_flag = wukong_audio_frontend_vad_get_flag();
    wukong_kws_feed_with_vad((UINT8_T *)kws_data, kws_len,
                             vad_flag == WUKONG_AUDIO_VAD_START ? 1 : 0);

    return OPRT_OK;
}

OPERATE_RET wukong_audio_frontend_vad_start(VOID)
{
    if (!s_frontend_ops || !s_frontend_ops->vad_start) {
        return OPRT_OK;
    }
    return s_frontend_ops->vad_start();
}

OPERATE_RET wukong_audio_frontend_vad_stop(VOID)
{
    if (!s_frontend_ops || !s_frontend_ops->vad_stop) {
        return OPRT_OK;
    }
    return s_frontend_ops->vad_stop();
}

OPERATE_RET wukong_audio_frontend_vad_set_threshold(WUKONG_AUDIO_VAD_THRESHOLD_E level)
{
    if (!s_frontend_ops || !s_frontend_ops->vad_set_threshold) {
        return OPRT_OK;
    }
    return s_frontend_ops->vad_set_threshold(level);
}

INT_T wukong_audio_frontend_vad_get_flag(VOID)
{
    if (!s_frontend_ops || !s_frontend_ops->vad_get_flag) {
        return WUKONG_AUDIO_VAD_STOP;
    }
    return s_frontend_ops->vad_get_flag();
}

/*
 * Backward-compatible wrappers for the old wukong_aec_vad_* API.
 * These delegate to the frontend dispatcher so that any code still
 * including wukong_audio_aec_vad.h continues to compile and work.
 */
OPERATE_RET wukong_aec_vad_init(UINT32_T min_speech_len_ms, UINT32_T max_speech_interval_ms, UINT32_T frame_size)
{
    return wukong_audio_frontend_init(min_speech_len_ms, max_speech_interval_ms, frame_size);
}

OPERATE_RET wukong_aec_vad_deinit(VOID)
{
    return wukong_audio_frontend_deinit();
}

OPERATE_RET wukong_aec_vad_process(INT16_T *mic_data, INT16_T *ref_data, INT16_T *out_data)
{
    return wukong_audio_frontend_process(mic_data, ref_data, out_data);
}

OPERATE_RET wukong_vad_set_threshold(WUKONG_AUDIO_VAD_THRESHOLD_E level)
{
    return wukong_audio_frontend_vad_set_threshold(level);
}

OPERATE_RET wukong_vad_start(VOID)
{
    return wukong_audio_frontend_vad_start();
}

OPERATE_RET wukong_vad_stop(VOID)
{
    return wukong_audio_frontend_vad_stop();
}

INT_T wukong_vad_get_flag(VOID)
{
    return wukong_audio_frontend_vad_get_flag();
}
