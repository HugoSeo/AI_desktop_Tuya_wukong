#include "wukong_audio_input.h"
#include "tal_log.h"

OPERATE_RET wukong_audio_input_init(WUKONG_AUDIO_INPUT_CFG_T *cfg)
{    
    TUYA_CHECK_NULL_RETURN(g_audio_input_producer.init, OPRT_RESOURCE_NOT_READY);
    return g_audio_input_producer.init(cfg);
}

OPERATE_RET wukong_audio_input_start(VOID)
{
    TUYA_CHECK_NULL_RETURN(g_audio_input_producer.start, OPRT_RESOURCE_NOT_READY);
    return g_audio_input_producer.start();
}

OPERATE_RET wukong_audio_input_stop(VOID)
{
    TUYA_CHECK_NULL_RETURN(g_audio_input_producer.stop, OPRT_RESOURCE_NOT_READY);
    return g_audio_input_producer.stop();
}

OPERATE_RET wukong_audio_input_deinit(VOID)
{
    TUYA_CHECK_NULL_RETURN(g_audio_input_producer.deinit, OPRT_RESOURCE_NOT_READY);
    return g_audio_input_producer.deinit();
}

OPERATE_RET wukong_audio_input_wakeup_mode_set(WUKONG_AUDIO_VAD_MODE_E mode)
{
    TUYA_CHECK_NULL_RETURN(g_audio_input_producer.set_vad_mode, OPRT_RESOURCE_NOT_READY);
    return g_audio_input_producer.set_vad_mode(mode);
}

OPERATE_RET wukong_audio_input_reset(VOID)
{
    TUYA_CHECK_NULL_RETURN(g_audio_input_producer.reset, OPRT_RESOURCE_NOT_READY);
    return g_audio_input_producer.reset();
}

OPERATE_RET wukong_audio_input_wakeup_set(BOOL_T is_wakeup)
{
    TUYA_CHECK_NULL_RETURN(g_audio_input_producer.set_wakeup, OPRT_RESOURCE_NOT_READY);
    return g_audio_input_producer.set_wakeup(is_wakeup);
}