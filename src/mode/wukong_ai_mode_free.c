/**
 * @file wukong_ai_mode_free.c
 * @brief Free mode implementation.
 *
 * Implements the free interaction mode, allowing continuous conversation
 * with automatic VAD and KWS.
 *
 * @copyright Copyright (c) 2023 Tuya Inc. All Rights Reserved.
 */

#include "wukong_ai_mode.h"
#include "wukong_kws.h"
#include "tuya_ai_toy.h"
#include "wukong_tm_internal.h"

#if defined(ENABLE_AI_MODE_FREE) && (ENABLE_AI_MODE_FREE == 1)

STATIC AI_CHAT_MODE_HANDLE_T s_ai_free_cb = {0};
STATIC AI_CHAT_MODE_PARAM_T s_ai_free = {0};
STATIC AI_CHAT_STATE_E s_ai_cur_state = AI_CHAT_INVALID;

/**
 * @brief Handle ASR result event in free mode.
 *
 * @param[in] type AI event type.
 * @param[in] data Event payload (ASR text data).
 */
STATIC VOID __ai_free_asr_result(WUKONG_AI_EVENT_TYPE_E type, UCHAR_T *data)
{
    if (NULL == data) {
        return;
    }

    WUKONG_AI_TEXT_T *text = (WUKONG_AI_TEXT_T *)data;
    //TAL_PR_NOTICE("ai toy -> recv wukong asr result: %s", text->data);
#ifdef ENABLE_TUYA_UI
    tuya_ai_display_msg((UINT8_T*)text->data, text->datalen, TY_DISPLAY_TP_HUMAN_CHAT);
#endif
    return;    
}

/**
 * @brief Handle text stream event in free mode.
 *
 * Updates UI based on text stream start, data and stop events.
 *
 * @param[in] type AI event type.
 * @param[in] data Event payload (text data).
 */
STATIC VOID __ai_free_text_stream(WUKONG_AI_EVENT_TYPE_E type, UCHAR_T *data)
{
    TUYA_CHECK_NULL_RETURN(data, OPRT_INVALID_PARM);

    WUKONG_AI_TEXT_T *text = (WUKONG_AI_TEXT_T *)data;
    TAL_PR_NOTICE("ai toy -> recv wukong text result: %s", text->data);

    switch (type)
    {
    case WUKONG_AI_EVENT_TEXT_STREAM_START:
        #ifdef ENABLE_TUYA_UI
        tuya_ai_display_msg((UINT8_T*)text->data, text->datalen, TY_DISPLAY_TP_AI_CHAT_START);
        #endif
        break;
    case WUKONG_AI_EVENT_TEXT_STREAM_DATA:
        #ifdef ENABLE_TUYA_UI
        tuya_ai_display_msg((UINT8_T*)text->data, text->datalen, TY_DISPLAY_TP_AI_CHAT_DATA);
        #endif
        /* code */
        break;
    case WUKONG_AI_EVENT_TEXT_STREAM_STOP:
    case WUKONG_AI_EVENT_TEXT_STREAM_ABORT:
        #ifdef ENABLE_TUYA_UI
        tuya_ai_display_msg(NULL, 0, TY_DISPLAY_TP_AI_CHAT_STOP);
        #endif
        break;
    default:
        break;
    }
}

/**
 * @brief Handle emotion event in free mode.
 *
 * Dispatches the received emotion to the display and emotion skill handler.
 *
 * @param[in] type AI event type.
 * @param[in] data Event payload (emotion data).
 */
STATIC VOID __ai_free_emition(WUKONG_AI_EVENT_TYPE_E type, UCHAR_T *data)
{
    TUYA_CHECK_NULL_RETURN(data, OPRT_INVALID_PARM);

    WUKONG_AI_EMO_T *emo = (WUKONG_AI_EMO_T *)data;
    TAL_PR_NOTICE("ai toy -> recv wukong emotion result: %s", emo->name);
#ifdef ENABLE_TUYA_UI
    tuya_ai_display_msg((UINT8_T*)emo->name, strlen(emo->name), TY_DISPLAY_TP_EMOJI);
#endif 
    return;
}

/**
 * @brief Enter IDLE state in free mode.
 *
 * Turns off recording LED, starts idle timers, and clears wakeup status.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __ai_free_idle_cb(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_free] idle");
    OPERATE_RET rt = OPRT_OK;

    tuya_ai_toy_led_off();

    //close idle timer
    tuya_ai_toy_idle_timer_ctrl(FALSE);

    //open low power timer
    tuya_ai_toy_lowpower_timer_ctrl(TRUE);

    //disable wakeup
    wukong_audio_input_wakeup_set(FALSE);
    s_ai_free.wakeup_stat = FALSE;

#if defined(USING_BOARD_AUDIO_INPUT) && (USING_BOARD_AUDIO_INPUT == 1)
    //decrease the threshold of vad
    wukong_vad_set_threshold(WUKONG_AUDIO_VAD_LOW);
#endif

    return rt;
}

/**
 * @brief Enter LISTEN state in free mode.
 *
 * Turns on recording LED, plays wakeup sound, and interrupts previous TTS/skills.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __ai_free_listen_cb(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_free] listen");
    OPERATE_RET rt = OPRT_OK;

    tuya_ai_toy_led_flash(500);

    //open idle timer
    tuya_ai_toy_idle_timer_ctrl(TRUE);

    //close low power timer
    tuya_ai_toy_lowpower_timer_ctrl(FALSE);

    //wakeup audio input
    s_ai_free.wakeup_stat = TRUE;
    wukong_audio_input_wakeup_set(TRUE);

    return rt;
}

/**
 * @brief Enter UPLOAD state in free mode.
 *
 * Flashes LED quickly to indicate audio data is being uploaded.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __ai_free_upload_cb(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_free] upload");
    OPERATE_RET rt = OPRT_OK;
    return rt;
}

/**
 * @brief Enter THINK state in free mode.
 *
 * Flashes LED slowly to indicate the AI is processing the input.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __ai_free_think_cb(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_free] think");
    OPERATE_RET rt = OPRT_OK;

    tuya_ai_toy_led_flash(2000);

    tuya_ai_toy_idle_timer_ctrl(TRUE);

    wukong_audio_input_wakeup_set(TRUE);

    s_ai_free.wakeup_stat = TRUE;

#if defined(USING_BOARD_AUDIO_INPUT) && (USING_BOARD_AUDIO_INPUT == 1)
    //decrease the threshold of vad
    wukong_vad_set_threshold(WUKONG_AUDIO_VAD_MID);
#endif

    return rt;
}

/**
 * @brief Enter SPEAK state in free mode.
 *
 * Controls the LED breathing effect during TTS playback.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __ai_free_speak_cb(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_free] speak");
    OPERATE_RET rt = OPRT_OK;

    tuya_ai_toy_led_on();
    
    tuya_ai_toy_idle_timer_ctrl(FALSE);

    return rt;
}

/**
 * @brief Initialize free mode.
 *
 * Configures the VAD to automatic mode, enables keyword spotting (KWS),
 * and transitions to the IDLE state.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_free_int_cb(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_free] init");
    OPERATE_RET rt = OPRT_OK;

    tuya_ai_toy_led_on();

    //set vad mode
    wukong_audio_input_wakeup_mode_set(WUKONG_AUDIO_VAD_AUTO);

    //disenable kws
    wukong_kws_enable();

    // s_ai_free.state = AI_CHAT_IDLE;
    CHAT_SUB_STATE_CHANGE(AI_CHAT_SUB_FREE, s_ai_free.state, AI_CHAT_IDLE);

    s_ai_free.wakeup_stat = FALSE;

#ifdef ENABLE_TUYA_UI
    AI_CHAT_SUB_MODE_E trigger_mode = AI_CHAT_SUB_FREE;
    tuya_ai_display_msg(&trigger_mode, 1, TY_DISPLAY_TP_CHAT_MODE);
#endif


    return rt;
}

/**
 * @brief De-initialize free mode.
 *
 * Disables KWS and ensures the VAD is stopped to clean up resources.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_free_deint_cb(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_free] deinit");
    OPERATE_RET rt = OPRT_OK;
    s_ai_cur_state = AI_CHAT_INVALID;
    wukong_ai_agent_input_stop();
    s_ai_cur_state = AI_CHAT_INVALID;
    return rt;
}

/**
 * @brief Periodic task callback for free mode.
 *
 * Handles state transitions based on the current logical state of the AI interaction.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_free_task_cb(VOID *data, INT_T len)
{
    if(s_ai_cur_state == s_ai_free.state)
    {
        return OPRT_OK;
    }

    switch(s_ai_free.state)
    {
        case AI_CHAT_INIT:
        {
            ;
        }
        break;

        case AI_CHAT_IDLE:
        {
            __ai_free_idle_cb(NULL, 0);
        }
        break;

        case AI_CHAT_LISTEN:
        {
            __ai_free_listen_cb(NULL, 0);
        }
        break;

        case AI_CHAT_UPLOAD:
        {
            __ai_free_upload_cb(NULL, 0);
        }
        break;

        case AI_CHAT_THINK:
        {
            __ai_free_think_cb(NULL, 0);
        }
        break;

        case AI_CHAT_SPEAK:
        {
            __ai_free_speak_cb(NULL, 0);
        }
        break;

        default:
        break;
    }

    s_ai_cur_state = s_ai_free.state;

#ifdef ENABLE_TUYA_UI   
    tuya_ai_display_msg(&s_ai_free.state, 1, TY_DISPLAY_TP_CHAT_STAT);
#endif  

    return OPRT_OK;
}

/**
 * @brief Event callback for free mode.
 *
 * Processes various AI system events (e.g., ASR/TTS status, skills)
 * and drives the state machine accordingly.
 *
 * @param[in] data Pointer to WUKONG_AI_EVENT_T structure.
 * @param[in] len  Length of the data (unused).
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_free_event_cb(VOID *data, INT_T len)
{
    OPERATE_RET rt = OPRT_OK;
    TUYA_CHECK_NULL_RETURN(data, OPRT_INVALID_PARM);
    WUKONG_AI_EVENT_T *event = (WUKONG_AI_EVENT_T *)data;

    // STATIC WUKONG_AI_EVENT_TYPE_E cur = WUKONG_AI_EVENT_IDLE;
    // if(cur == event->type)
    // {
    //     return OPRT_OK;
    // }
    // cur = event->type;

    TAL_PR_DEBUG("[====ai_free] event type: %d", event->type);
    switch (event->type) 
    {
        case WUKONG_AI_EVENT_ASR_EMPTY:
        case WUKONG_AI_EVENT_ASR_ERROR:
        {
            // s_ai_free.state = AI_CHAT_LISTEN;
            CHAT_SUB_STATE_CHANGE(AI_CHAT_SUB_FREE, s_ai_free.state, AI_CHAT_LISTEN);
        }
        break;

        case WUKONG_AI_EVENT_ASR_OK:
        {
            // s_ai_free.state = AI_CHAT_THINK;
            CHAT_SUB_STATE_CHANGE(AI_CHAT_SUB_FREE, s_ai_free.state, AI_CHAT_THINK);
            __ai_free_asr_result(event->type, event->data);
        }
        break;
            
        case WUKONG_AI_EVENT_TTS_PRE:
        {
            // s_ai_free.state = AI_CHAT_SPEAK;
            CHAT_SUB_STATE_CHANGE(AI_CHAT_SUB_FREE, s_ai_free.state, AI_CHAT_SPEAK);
        }
        break;

        case WUKONG_AI_EVENT_TTS_START:
        case WUKONG_AI_EVENT_TTS_DATA:   
        {

        }
        break;   

        case WUKONG_AI_EVENT_TTS_STOP:
        case WUKONG_AI_EVENT_TTS_ABORT:
        case WUKONG_AI_EVENT_TTS_ERROR:
        {

        }
        break;

        case WUKONG_AI_EVENT_VAD_TIMEOUT:
        {

        }
        break;

        case WUKONG_AI_EVENT_TEXT_STREAM_START:
        case WUKONG_AI_EVENT_TEXT_STREAM_DATA:
        case WUKONG_AI_EVENT_TEXT_STREAM_STOP:
        case WUKONG_AI_EVENT_TEXT_STREAM_ABORT:
        {
            __ai_free_text_stream(event->type, event->data);
        }
        break;

        case WUKONG_AI_EVENT_EMOTION:
        case WUKONG_AI_EVENT_LLM_EMOTION:
        {
            __ai_free_emition(event->type, event->data);
        }
        break;

        case WUKONG_AI_EVENT_SKILL:
        {

        }
        break;

        case WUKONG_AI_EVENT_CHAT_BREAK:
        case WUKONG_AI_EVENT_SERVER_VAD:
        {

        }
        break;
        case WUKONG_AI_EVENT_EXIT:
            CHAT_SUB_STATE_CHANGE(AI_CHAT_SUB_FREE, s_ai_free.state, AI_CHAT_IDLE);
        break;

        case WUKONG_AI_EVENT_PLAY_CTL_PLAY:
        break;
        case WUKONG_AI_EVENT_PLAY_CTL_RESUME:
        {
            wukong_audio_player_resume();
        }
        break;

        case WUKONG_AI_EVENT_PLAY_CTL_PAUSE:
        {
            wukong_audio_player_pause();
        }
        break;

        case WUKONG_AI_EVENT_PLAY_CTL_REPLAY:
        {
            wukong_audio_player_replay();
        }
        break;

        case WUKONG_AI_EVENT_PLAY_CTL_END:
        case WUKONG_AI_EVENT_PLAY_END:
        {
            if(s_ai_free.wakeup_stat)
            {
                // s_ai_free.state = AI_CHAT_LISTEN;
                CHAT_SUB_STATE_CHANGE(AI_CHAT_SUB_FREE, s_ai_free.state, AI_CHAT_LISTEN);
                
            }
            else
            {
                // s_ai_free.state = AI_CHAT_IDLE;
                CHAT_SUB_STATE_CHANGE(AI_CHAT_SUB_FREE, s_ai_free.state, AI_CHAT_IDLE);
            }
        }
        break;        

        case WUKONG_AI_EVENT_PLAY_ALERT:
        {
            wukong_audio_player_alert((TY_AI_TOY_ALERT_TYPE_E)event->data, TRUE);
        }
        break;

        case WUKONG_AI_EVENT_CLOCK_MCP_COUNTDOWN_TIMER:
        {
            if (event->data &&
                ((UINT8_T *)event->data)[4] == (UINT8_T)WUKONG_TM_TIMER_OPR_FINISH) {
                wukong_audio_player_alert(AI_TOY_ALERT_TYPE_WAKEUP, FALSE);
            }
        }
        break;

        case WUKONG_AI_EVENT_CLOCK_MCP_ALARM:
        {
            if (event->data &&
                ((UINT8_T *)event->data)[4] == (UINT8_T)WUKONG_TM_TIMER_OPR_START) {
                wukong_audio_player_alert(AI_TOY_ALERT_TYPE_WAKEUP, FALSE);
            }
        }
        break;

        case WUKONG_AI_EVENT_PLAY_CTL_PREV:
        case WUKONG_AI_EVENT_PLAY_CTL_NEXT:
        case WUKONG_AI_EVENT_PLAY_CTL_SEQUENTIAL:
        case WUKONG_AI_EVENT_PLAY_CTL_SEQUENTIAL_LOOP:
        case WUKONG_AI_EVENT_PLAY_CTL_SINGLE_LOOP:
        {

        }
        break;

        default:
        break;
    }

    return rt;
}

/**
 * @brief Handle KWS (Keyword Spotting) wakeup in free mode.
 *
 * Transitions the mode to LISTEN state to start receiving audio input.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_free_wakeup(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_free] wakeup"); 
    OPERATE_RET rt = OPRT_OK;

    wukong_ai_agent_output_stop(TRUE);
    wukong_audio_player_stop(AI_PLAYER_ALL);
    wukong_audio_input_reset();
    wukong_ai_agent_chat_break(NULL);

    wukong_audio_player_alert(AI_TOY_ALERT_TYPE_WAKEUP, FALSE);
    // s_ai_free.state = AI_CHAT_LISTEN;
    CHAT_SUB_STATE_CHANGE(AI_CHAT_SUB_FREE, s_ai_free.state, AI_CHAT_LISTEN);
    s_ai_free.wakeup_stat = TRUE;

    return rt;
}

/**
 * @brief Handle VAD (Voice Activity Detection) event in free mode.
 *
 * Reacts to voice start and stop events to transition between LISTEN, UPLOAD, and THINK states.
 *
 * @param[in] data Pointer to WUKONG_AUDIO_VAD_FLAG_E containing VAD flag.
 * @param[in] len  Length of the data.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_free_vad(VOID *data, INT_T len)
{
    if(!s_ai_free.wakeup_stat)
    {
        TAL_PR_ERR("[====ai_free] vad ignored when not wakeup");
        return OPRT_OK;
    }

    TUYA_CHECK_NULL_RETURN(data, OPRT_INVALID_PARM);
    OPERATE_RET rt = OPRT_OK;

    WUKONG_AUDIO_VAD_FLAG_E vad_flag = (len >= (INT_T)sizeof(WUKONG_AUDIO_VAD_FLAG_E))
        ? *(WUKONG_AUDIO_VAD_FLAG_E *)data : (WUKONG_AUDIO_VAD_FLAG_E)0;
    TAL_PR_DEBUG("[====ai_free] vad: [%d]", vad_flag); 
    if (WUKONG_AUDIO_VAD_START == vad_flag) 
    {
        if (!wukong_ai_agent_is_ready()) {
            TAL_PR_DEBUG("ai agent is not ready, ignore audio input");
            return OPRT_RESOURCE_NOT_READY;
        }
        wukong_ai_agent_set_scene(AI_AGENT_SCODE_CHAT);
        wukong_ai_agent_input_start(FALSE);
        tuya_ai_toy_idle_timer_ctrl(FALSE);
    } 
    else 
    {
        wukong_ai_agent_input_stop();
        tuya_ai_toy_idle_timer_ctrl(TRUE);
        // s_ai_free.state = AI_CHAT_UPLOAD;
    }

    return rt;
}

/**
 * @brief Client run callback for free mode.
 *
 * Triggered periodically by the main application to handle client-side logic.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_free_client_run(VOID_T *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_free] client run");
    // s_ai_free.state = AI_CHAT_IDLE;
    CHAT_SUB_STATE_CHANGE(AI_CHAT_SUB_FREE, s_ai_free.state, AI_CHAT_IDLE);    
    return OPRT_OK;
}

/**
 * @brief Handle key press event in free mode.
 *
 * Processes physical button presses (e.g., exiting or toggling state).
 *
 * @param[in] data Pointer to PUSH_KEY_TYPE_E containing the key event.
 * @param[in] len  Length of the data.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_free_key_cb(VOID *data, INT_T len)
{
    OPERATE_RET rt = OPRT_OK;
    PUSH_KEY_TYPE_E event = *(PUSH_KEY_TYPE_E *)data;
    TAL_PR_DEBUG("[====ai_free] key: %d", event);
    switch (event) 
    {        
        case NORMAL_KEY:
        {
            wukong_ai_agent_output_stop(TRUE);
            wukong_audio_player_stop(AI_PLAYER_ALL);
            wukong_audio_input_reset();
            wukong_ai_agent_chat_break(NULL);

            wukong_audio_player_alert(AI_TOY_ALERT_TYPE_WAKEUP, FALSE);
            // s_ai_free.state = AI_CHAT_LISTEN;
            CHAT_SUB_STATE_CHANGE(AI_CHAT_SUB_FREE, s_ai_free.state, AI_CHAT_LISTEN);
            s_ai_free.wakeup_stat = TRUE;

        } 
        break;  

        case SEQ_KEY:
        {
            ;
        }
        break;

        case LONG_KEY: 
        {
            ;
        }
        break;   

        case RELEASE_KEY: 
        {
            ;
        }
        break;

        default:
        break;
    }
    
    return rt;
}

/**
 * @brief Handle idle notification in free mode.
 *
 * Called when the system detects inactivity, causing the mode to reset to IDLE.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_free_notify_idle_cb(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_free] client run");
    OPERATE_RET rt = OPRT_OK;

    // s_ai_free.state = AI_CHAT_IDLE;
    CHAT_SUB_STATE_CHANGE(AI_CHAT_SUB_FREE, s_ai_free.state, AI_CHAT_IDLE);

    return rt;
}

/**
 * @brief Register the free mode.
 *
 * Assigns all relevant callbacks to the provided mode handle structure.
 *
 * @param[out] cb Pointer to the mode handle pointer.
 * @return OPRT_OK on success.
 */
OPERATE_RET ai_free_register(AI_CHAT_MODE_HANDLE_T **cb)
{
    OPERATE_RET rt = OPRT_OK;

    s_ai_free_cb.on_init       = wukong_ai_free_int_cb;
    s_ai_free_cb.on_deinit     = wukong_ai_free_deint_cb;
    s_ai_free_cb.on_key        = wukong_ai_free_key_cb;
    s_ai_free_cb.on_task       = wukong_ai_free_task_cb;
    s_ai_free_cb.on_event      = wukong_ai_free_event_cb;
    s_ai_free_cb.on_wakeup     = wukong_ai_free_wakeup;
    s_ai_free_cb.on_vad        = wukong_ai_free_vad;
    s_ai_free_cb.on_client     = wukong_ai_free_client_run;
    s_ai_free_cb.on_notify_idle = wukong_ai_free_notify_idle_cb;
    *cb = &s_ai_free_cb;
    return rt;
}
#endif