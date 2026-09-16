#include "wukong_audio_input.h"
#include "tal_log.h"

/* 缓存最近一次经本层 set 的 wakeup 状态,供 wukong_audio_input_wakeup_get 读取。
 * 所有 wakeup 变更都经 wukong_audio_input_wakeup_set 这层入口,故缓存与实际一致
 * (手动 VAD 模式下无板级内部自动切换)。用于 P2P 通话期自愈重置 wakeup。 */
STATIC BOOL_T s_wakeup_state = FALSE;

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
    OPERATE_RET rt = g_audio_input_producer.set_wakeup(is_wakeup);
    if (rt == OPRT_OK) {
        s_wakeup_state = is_wakeup;
    }
    return rt;
}

BOOL_T wukong_audio_input_wakeup_get(VOID)
{
    return s_wakeup_state;
}

OPERATE_RET wukong_audio_input_set_vol(UINT8_T volume)
{
    TUYA_CHECK_NULL_RETURN(g_audio_input_producer.set_vol, OPRT_RESOURCE_NOT_READY);
    return g_audio_input_producer.set_vol(volume);
}
