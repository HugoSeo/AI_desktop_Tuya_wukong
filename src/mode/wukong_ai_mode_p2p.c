/**
 * @file wukong_ai_mode_p2p.c
 * @brief P2P mode implementation.
 *
 * Implements the P2P interaction mode for direct peer-to-peer
 * audio/video communication via mobile app.
 *
 * @copyright Copyright (c) 2023 Tuya Inc. All Rights Reserved.
 */

#include "wukong_ai_mode.h"
#include "tuya_ai_toy.h"
#include "wukong_kws.h"
#include "wukong_tm_internal.h"

#if defined(ENABLE_AI_MODE_P2P) && (ENABLE_AI_MODE_P2P == 1)
#include "tuya_p2p_app.h"
#include "tuya_p2p_tmm_voip.h"

STATIC AI_CHAT_MODE_HANDLE_T s_ai_p2p_cb = {0};
STATIC AI_CHAT_MODE_PARAM_T s_ai_p2p = {0};

/**
 * @brief Initialize P2P mode.
 *
 * Sets up manual VAD and disables KWS, switching to IDLE state for P2P connection.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_p2p_int_cb(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_p2p] init");
    OPERATE_RET rt = OPRT_OK;

    tuya_ai_toy_led_on();

    //set vad mode
    wukong_audio_input_wakeup_mode_set(WUKONG_AUDIO_VAD_MANUAL);

    //disenable kws
    wukong_kws_disable();

    DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_P2P, s_ai_p2p.state, AI_CHAT_IDLE);

    s_ai_p2p.wakeup_stat = TRUE;

    wukong_audio_input_wakeup_set(TRUE);
// #ifdef ENABLE_TUYA_UI
//     AI_DEVICE_MODE_E trigger_mode = AI_DEVICE_MODE_P2P;
//     tuya_ai_display_msg(&trigger_mode, 1, TY_DISPLAY_TP_CHAT_MODE);
// #endif

    return rt;
}

/**
 * @brief De-initialize P2P mode.
 *
 * Stops audio input paths to safely switch back to normal AI modes.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_p2p_deint_cb(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_p2p] deinit");
    OPERATE_RET rt = OPRT_OK;

    /*
     * Stop the manual wakeup/audio path before switching back to
     * normal AI modes, otherwise in-flight mic frames may fall
     * through to the default AI input path before a new session exists.
     */
    s_ai_p2p.wakeup_stat = FALSE;
    TUYA_CALL_ERR_LOG(wukong_audio_input_wakeup_set(FALSE));
    TUYA_CALL_ERR_LOG(wukong_audio_input_reset());
    wukong_ai_agent_input_stop();
    return rt;
}

/**
 * @brief Periodic task callback for P2P mode.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_p2p_task_cb(VOID *data, INT_T len)
{
    /* 通话期保证麦克风 wakeup 常开(自愈,ADR 0009)。
     * __voip_enter_call 在 TMM 事件线程切 P2P 模式并置 wakeup=1,而它先调的
     * wukong_ai_agent_chat_break() 会让主调度线程(本 on_task 所在线程)异步跑
     * 上一模式 HOLD 的 __ai_hold_idle_cb → wukong_audio_input_wakeup_set(FALSE)。
     * 两线程无共同锁,HOLD idle 回调偶尔晚于 P2P on_init 落地,把 wakeup 覆盖回 0,
     * 手动 VAD 下麦克风帧不再上送(设备→对端偶现无声)。on_task 跑在调度线程,
     * 检测到通话中 wakeup 被关即重置——只在被覆盖时置位一次,不刷屏。 */
    /* ADR 0009 自愈守卫:仅当 P2P 自身仍认为 wakeup 应为开时才补置。
     * deinit 第一行即设 wakeup_stat=FALSE,因此 deinit 后到达的残留 on_task
     * 会在此被拦截,不会误将 wakeup 重新打开。
     *
     * 若不加此检查: wukong_ai_device_mode_switch 在 DEINIT 后、更新
     * s_mode_mgr.active 前存在竞态窗口——本 on_task 可能在 P2P 已 deinit 后
     * 被调度线程分发,此时 is_incall() 仍为真(TMM 异步断开中),wakeup 已被
     * deinit 清零,将误置 wakeup=TRUE,导致通话结束后音频 pipeline 泄漏,
     * 麦克风数据持续刷入 agent,表现为日志刷屏/屏幕闪烁。 */
    if (s_ai_p2p.wakeup_stat &&
        tuya_toy_tmm_voip_is_incall() &&
        !wukong_audio_input_wakeup_get()) {
        TAL_PR_WARN("[====ai_p2p] wakeup cleared during call, re-assert");
        s_ai_p2p.wakeup_stat = TRUE;
        wukong_audio_input_wakeup_set(TRUE);
    }
    return OPRT_OK;
}

/**
 * @brief Event callback for P2P mode.
 *
 * @param[in] data Pointer to WUKONG_AI_EVENT_T structure.
 * @param[in] len  Length of the data (unused).
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_p2p_event_cb(VOID *data, INT_T len)
{
    WUKONG_AI_EVENT_T *event = (WUKONG_AI_EVENT_T *)data;

    if (!event) {
        return OPRT_OK;
    }
    if (event->type == WUKONG_AI_EVENT_CLOCK_MCP_COUNTDOWN_TIMER && event->data &&
        ((UINT8_T *)event->data)[4] == (UINT8_T)WUKONG_TM_TIMER_OPR_FINISH) {
        wukong_audio_player_alert(AI_TOY_ALERT_TYPE_WAKEUP, FALSE);
    } else if (event->type == WUKONG_AI_EVENT_CLOCK_MCP_ALARM && event->data &&
               ((UINT8_T *)event->data)[4] == (UINT8_T)WUKONG_TM_TIMER_OPR_START) {
        wukong_audio_player_alert(AI_TOY_ALERT_TYPE_WAKEUP, FALSE);
    }
    return OPRT_OK;
}

/**
 * @brief Handle KWS (Keyword Spotting) wakeup in P2P mode.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_p2p_wakeup(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_p2p] wakeup"); 
    OPERATE_RET rt = OPRT_OK;
    return rt;
}

/**
 * @brief Handle VAD (Voice Activity Detection) event in P2P mode.
 *
 * @param[in] data Pointer to WUKONG_AUDIO_VAD_FLAG_E containing VAD flag.
 * @param[in] len  Length of the data.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_p2p_vad(VOID *data, INT_T len)
{
    return OPRT_OK;
}

/**
 * @brief Client run callback for P2P mode.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_p2p_client_run(VOID_T *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_p2p] client run");
    return OPRT_OK;
}

/**
 * @brief Handle key press event in P2P mode.
 *
 * @param[in] data Pointer to PUSH_KEY_TYPE_E containing the key event.
 * @param[in] len  Length of the data.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_p2p_key_cb(VOID *data, INT_T len)
{
    OPERATE_RET rt = OPRT_OK;
    PUSH_KEY_TYPE_E event = *(PUSH_KEY_TYPE_E *)data;
    TAL_PR_DEBUG("[====ai_p2p] key: %d", event);
    switch (event) 
    {        
        case NORMAL_KEY:
        {
            wukong_audio_player_stop(AI_PLAYER_ALL);
            wukong_audio_input_reset();
            wukong_ai_agent_chat_break(NULL);
            DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_P2P, s_ai_p2p.state, AI_CHAT_IDLE);
            s_ai_p2p.wakeup_stat = FALSE;

        } 
        break;  

        case SEQ_KEY:
        {
            ;
        }
        break;

        case LONG_KEY: 
        {
            DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_P2P, s_ai_p2p.state, AI_CHAT_LISTEN);
            s_ai_p2p.wakeup_stat = TRUE;
        }
        break;   

        case RELEASE_KEY: 
        {
            DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_P2P, s_ai_p2p.state, AI_CHAT_UPLOAD);
        }
        break;

        default:
        break;
    }
    
    return rt;
}

/**
 * @brief Handle idle notification in P2P mode.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_p2p_notify_idle_cb(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_p2p] notify idle");
    return OPRT_OK;
}

/**
 * @brief Handle audio input routing for P2P mode.
 *
 * Re-routes captured audio data directly to the P2P stream instead of the default AI agent.
 *
 * @param[in] data Pointer to audio payload.
 * @param[in] len  Length of the audio payload.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_p2p_handle_audio_input(VOID *data, INT_T len)
{
    /* TMM 通话进行中:麦克风上行走 TMM(ADR 0002 统一通话模型);
     * 否则回退原生 C2C 推流路径(媒体复用,见 ADR 0002)。 */
    if (tuya_toy_tmm_voip_is_incall()) {
        return tuya_toy_tmm_voip_audio_feed(data, len);
    }
    return tuya_ipc_app_audio_frame_put(data, len);
}

/**
 * @brief Register the P2P mode.
 *
 * Assigns all relevant callbacks to the provided mode handle structure.
 *
 * @param[out] cb Pointer to the mode handle pointer.
 * @return OPRT_OK on success.
 */
OPERATE_RET ai_p2p_register(AI_CHAT_MODE_HANDLE_T **cb)
{
    OPERATE_RET rt = OPRT_OK;

    s_ai_p2p_cb.on_init         = wukong_ai_p2p_int_cb;
    s_ai_p2p_cb.on_deinit       = wukong_ai_p2p_deint_cb;
    s_ai_p2p_cb.on_key          = wukong_ai_p2p_key_cb;
    s_ai_p2p_cb.on_task         = wukong_ai_p2p_task_cb;
    s_ai_p2p_cb.on_event        = wukong_ai_p2p_event_cb;
    s_ai_p2p_cb.on_wakeup       = wukong_ai_p2p_wakeup;
    s_ai_p2p_cb.on_vad          = wukong_ai_p2p_vad;
    s_ai_p2p_cb.on_client       = wukong_ai_p2p_client_run;
    s_ai_p2p_cb.on_notify_idle  = wukong_ai_p2p_notify_idle_cb;
    s_ai_p2p_cb.on_audio_input  = wukong_ai_p2p_handle_audio_input;
    *cb = &s_ai_p2p_cb;
    return rt;
}
#endif