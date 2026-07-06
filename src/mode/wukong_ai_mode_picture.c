/**
 * @file wukong_ai_mode_picture.c
 * @brief Picture mode implementation with oneshot and wakeup triggers.
 *
 * Implements the picture interaction mode. A conversation can be started
 * either by a single button press (oneshot) or by a keyword wakeup,
 * using automatic VAD for end-of-speech detection.
 *
 * @copyright Copyright (c) 2023 Tuya Inc. All Rights Reserved.
 */

#include "wukong_ai_mode.h"
#include "wukong_kws.h"
#include "tuya_ai_toy.h"
 
 #if defined(ENABLE_TUYA_PICTURE) && (ENABLE_TUYA_PICTURE == 1)
 #include "wukong_picture_input.h"
 #endif
 
 #if defined(ENABLE_AI_MODE_PICTURE) && (ENABLE_AI_MODE_PICTURE == 1)
 
 STATIC AI_CHAT_MODE_HANDLE_T s_ai_picture_cb = {0};
 STATIC AI_CHAT_MODE_PARAM_T s_ai_picture = {0};
 STATIC AI_CHAT_STATE_E s_ai_cur_state = AI_CHAT_INVALID;
 
 /**
  * @brief Handle ASR result event in picture mode.
  *
  * @param[in] type AI event type.
  * @param[in] data Event payload (ASR text data).
  */
 STATIC VOID __ai_picture_asr_result(WUKONG_AI_EVENT_TYPE_E type, UCHAR_T *data)
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
  * @brief Handle text stream event in picture mode.
  *
  * Updates UI based on text stream start, data and stop events.
  *
  * @param[in] type AI event type.
  * @param[in] data Event payload (text data).
  */
 STATIC VOID __ai_picture_text_stream(WUKONG_AI_EVENT_TYPE_E type, UCHAR_T *data)
 {
     TUYA_CHECK_NULL_RETURN(data, OPRT_INVALID_PARM);
 
     WUKONG_AI_TEXT_T *text = (WUKONG_AI_TEXT_T *)data;
     TAL_PR_NOTICE("ai toy -> recv wukong text result: %s", text->data);
 
     switch (type)
     {
     case WUKONG_AI_EVENT_TEXT_STREAM_START:
         #ifdef ENABLE_TUYA_UI
         tuya_ai_display_msg((UINT8_T*)text->data, text->datalen,  TY_DISPLAY_TP_AI_CHAT_START);
         #endif
         break;
     case WUKONG_AI_EVENT_TEXT_STREAM_DATA:
         #ifdef ENABLE_TUYA_UI
         tuya_ai_display_msg((UINT8_T*)text->data, text->datalen, TY_DISPLAY_TP_AI_CHAT_DATA);
         #endif
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
  * @brief Handle emotion event in picture mode.
  *
  * Dispatches the received emotion to the display and emotion skill handler.
  *
  * @param[in] type AI event type.
  * @param[in] data Event payload (emotion data).
  */
 STATIC VOID __ai_picture_emition(WUKONG_AI_EVENT_TYPE_E type, UCHAR_T *data)
 {
     TUYA_CHECK_NULL_RETURN(data, OPRT_INVALID_PARM);
 
     WUKONG_AI_EMO_T *emo = (WUKONG_AI_EMO_T *)data;
     TAL_PR_NOTICE("ai toy -> recv wukong emotion result: %s", emo->name);
 #ifdef ENABLE_TUYA_UI
     tuya_ai_display_msg((UINT8_T*)emo->name, strlen(emo->name), TY_DISPLAY_TP_EMOJI);
 #endif 
     return;
 }
 
 STATIC VOID __ai_picture_image(char *name)
 {
     PR_NOTICE("[pic_chain] picture mode recv ACCEPT_PICTURE, name:%s", name ? name : "null");
 #ifdef ENABLE_TUYA_UI
     tuya_ai_display_msg(name, strlen(name), TY_DISPLAY_TP_AI_IMAGE);
 #endif
 }
 
 /**
  * @brief Enter IDLE state in picture mode.
  *
  * Turns off recording LED, starts idle timers, and clears wakeup status.
  *
  * @param[in] data Unused.
  * @param[in] len  Unused.
  * @return OPRT_OK on success.
  */
 STATIC OPERATE_RET __ai_picture_idle_cb(VOID *data, INT_T len)
 {
     TAL_PR_DEBUG("[====ai_picture] idle");
     OPERATE_RET rt = OPRT_OK;
 
     tuya_ai_toy_led_off();
 
     //close idle timer
     tuya_ai_toy_idle_timer_ctrl(FALSE);
 
     //open low power timer
     tuya_ai_toy_lowpower_timer_ctrl(TRUE);
 
     //disable wakeup
     wukong_audio_input_wakeup_set(FALSE);
     s_ai_picture.wakeup_stat = FALSE;
 
     return rt;
 }
 
 /**
  * @brief Enter LISTEN state in picture mode.
  *
  * Turns on recording LED, plays wakeup sound, and interrupts previous TTS/skills.
  *
  * @param[in] data Unused.
  * @param[in] len  Unused.
  * @return OPRT_OK on success.
  */
 STATIC OPERATE_RET __ai_picture_listen_cb(VOID *data, INT_T len)
 {
     TAL_PR_DEBUG("[====ai_picture] listen");
     OPERATE_RET rt = OPRT_OK;
 
     tuya_ai_toy_led_flash(500);
 
     //open idle timer
     tuya_ai_toy_idle_timer_ctrl(TRUE);
 
     //close low power timer
     tuya_ai_toy_lowpower_timer_ctrl(FALSE);
 
     //wakeup audio input
     s_ai_picture.wakeup_stat = TRUE;
     wukong_audio_input_wakeup_set(TRUE);
 
     return rt;
 }
 
 /**
  * @brief Enter UPLOAD state in picture mode.
  *
  * Flashes LED quickly to indicate audio data is being uploaded.
  *
  * @param[in] data Unused.
  * @param[in] len  Unused.
  * @return OPRT_OK on success.
  */
STATIC OPERATE_RET __ai_picture_upload_cb(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_picture] upload");
    OPERATE_RET rt = OPRT_OK;

    s_ai_picture.wakeup_stat = FALSE;
    wukong_audio_input_wakeup_set(FALSE);

    tuya_ai_input_stop();

    return rt;
}
 
 /**
  * @brief Enter THINK state in picture mode.
  *
  * Flashes LED slowly to indicate the AI is processing the input.
  *
  * @param[in] data Unused.
  * @param[in] len  Unused.
  * @return OPRT_OK on success.
  */
STATIC OPERATE_RET __ai_picture_think_cb(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_picture] think");
    OPERATE_RET rt = OPRT_OK;

    tuya_ai_toy_led_flash(2000);

    tuya_ai_toy_idle_timer_ctrl(TRUE);

    if (s_ai_picture.wakeup_stat) {
        tuya_ai_input_stop();
    }

    wukong_audio_input_wakeup_set(FALSE);
    s_ai_picture.wakeup_stat = FALSE;

    return rt;
}
 
 /**
  * @brief Enter SPEAK state in picture mode.
  *
  * Controls the LED breathing effect during TTS playback.
  *
  * @param[in] data Unused.
  * @param[in] len  Unused.
  * @return OPRT_OK on success.
  */
 STATIC OPERATE_RET __ai_picture_speak_cb(VOID *data, INT_T len)
 {
     TAL_PR_DEBUG("[====ai_picture] speak");
     OPERATE_RET rt = OPRT_OK;
 
     tuya_ai_toy_led_on();
     
     tuya_ai_toy_idle_timer_ctrl(FALSE);
 
     return rt;
 }
 
/**
 * @brief Initialize picture mode.
 *
 * Configures the VAD to automatic mode, enables keyword spotting (KWS),
 * and transitions to the IDLE state.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_picture_int_cb(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_picture] init");
    OPERATE_RET rt = OPRT_OK;

    tuya_ai_toy_led_on();

    wukong_audio_input_wakeup_mode_set(WUKONG_AUDIO_VAD_MANUAL);

    wukong_kws_disable();

    DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_PICTURE, s_ai_picture.state, AI_CHAT_IDLE);

    tuya_ai_agent_server_vad_ctrl(FALSE);

    s_ai_picture.wakeup_stat = FALSE;

    return rt;
}
 
 /**
  * @brief De-initialize picture mode.
  *
  * Disables KWS and ensures the VAD is stopped to clean up resources.
  *
  * @param[in] data Unused.
  * @param[in] len  Unused.
  * @return OPRT_OK on success.
  */
STATIC OPERATE_RET wukong_ai_picture_deint_cb(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_picture] deinit");
    OPERATE_RET rt = OPRT_OK;

    tuya_ai_input_stop();
    s_ai_cur_state = AI_CHAT_INVALID;
    tuya_ai_agent_server_vad_ctrl(TRUE);
    return rt;
}
 
 /**
  * @brief Periodic task callback for picture mode.
  *
  * Handles state transitions based on the current logical state of the AI interaction.
  *
  * @param[in] data Unused.
  * @param[in] len  Unused.
  * @return OPRT_OK on success.
  */
 STATIC OPERATE_RET wukong_ai_picture_task_cb(VOID *data, INT_T len)
 {
     if(s_ai_cur_state == s_ai_picture.state)
     {
         return OPRT_OK;
     }
 
     switch(s_ai_picture.state)
     {
         case AI_CHAT_INIT:
         {
             ;
         }
         break;
 
         case AI_CHAT_IDLE:
         {
             __ai_picture_idle_cb(NULL, 0);
         }
         break;
 
         case AI_CHAT_LISTEN:
         {
             __ai_picture_listen_cb(NULL, 0);
         }
         break;
 
         case AI_CHAT_UPLOAD:
         {
             __ai_picture_upload_cb(NULL, 0);
         }
         break;
 
         case AI_CHAT_THINK:
         {
             __ai_picture_think_cb(NULL, 0);
         }
         break;
 
         case AI_CHAT_SPEAK:
         {
             __ai_picture_speak_cb(NULL, 0);
         }
         break;
 
         default:
         break;
     }
 
     s_ai_cur_state = s_ai_picture.state;
 
 #ifdef ENABLE_TUYA_UI   
     tuya_ai_display_msg(&s_ai_picture.state, 1, TY_DISPLAY_TP_CHAT_STAT);
 #endif  
 
     return OPRT_OK;
 }
 
 /**
  * @brief Event callback for picture mode.
  *
  * Processes various AI system events (e.g., ASR/TTS status, skills)
  * and drives the state machine accordingly.
  *
  * @param[in] data Pointer to WUKONG_AI_EVENT_T structure.
  * @param[in] len  Length of the data (unused).
  * @return OPRT_OK on success.
  */
 STATIC OPERATE_RET wukong_ai_picture_event_cb(VOID *data, INT_T len)
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
 
     TAL_PR_DEBUG("[====ai_picture] event type: %d", event->type);
     switch (event->type) 
     {
        case WUKONG_AI_EVENT_ASR_EMPTY:
        case WUKONG_AI_EVENT_ASR_ERROR:
        {
            DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_PICTURE, s_ai_picture.state, AI_CHAT_IDLE);
        }
        break;
 
         case WUKONG_AI_EVENT_ASR_OK:
         {
             // s_ai_picture.state = AI_CHAT_THINK;
             DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_PICTURE, s_ai_picture.state, AI_CHAT_THINK);
             __ai_picture_asr_result(event->type, event->data);
         }
         break;
             
         case WUKONG_AI_EVENT_TTS_PRE:
         {
             // s_ai_picture.state = AI_CHAT_SPEAK;
             DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_PICTURE, s_ai_picture.state, AI_CHAT_SPEAK);
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
             __ai_picture_text_stream(event->type, event->data);
         }
         break;
 
         case WUKONG_AI_EVENT_EMOTION:
         case WUKONG_AI_EVENT_LLM_EMOTION:
         {
             __ai_picture_emition(event->type, event->data);
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
             if(s_ai_picture.wakeup_stat)
             {
                 // s_ai_picture.state = AI_CHAT_LISTEN;
                 DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_PICTURE, s_ai_picture.state, AI_CHAT_LISTEN);
             }
             else
             {
                 // s_ai_picture.state = AI_CHAT_IDLE;
                 DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_PICTURE, s_ai_picture.state, AI_CHAT_IDLE);
             }
         }
         break;        
 
         case WUKONG_AI_EVENT_PLAY_ALERT:
         {
             wukong_audio_player_alert((TY_AI_TOY_ALERT_TYPE_E)event->data, TRUE);
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
 
         case WUKONG_AI_EVENT_ACCEPT_PICTURE: {
             __ai_picture_image((char *)event->data);
             DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_PICTURE, s_ai_picture.state, AI_CHAT_IDLE);
         }
         break;
 
         case WUKONG_AI_EVENT_SEND_PICTURE_END: {
            DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_PICTURE, s_ai_picture.state, AI_CHAT_IDLE);
 #ifdef ENABLE_TUYA_UI
             tuya_ai_display_msg(NULL, 0, TY_DISPLAY_TP_CLEAR_ATTACHMENT);
 #endif
         }
         break;
 
         default:
         break;
     }
 
     return rt;
 }
 
/**
 * @brief Handle KWS (Keyword Spotting) wakeup in picture mode.
 *
 * Transitions the mode to LISTEN state to start receiving audio input.
 *
 * @param[in] data Unused.
 * @param[in] len  Unused.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_picture_wakeup(VOID *data, INT_T len)
{
    TAL_PR_DEBUG("[====ai_picture] wakeup");
    OPERATE_RET rt = OPRT_OK;
    return rt;
}
 
/**
 * @brief Handle VAD (Voice Activity Detection) event in picture mode.
 *
 * Responds to automatic VAD start/stop events after a button press or
 * keyword wakeup has set wakeup_stat.
 *
 * @param[in] data Pointer to WUKONG_AUDIO_VAD_FLAG_E containing VAD flag.
 * @param[in] len  Length of the data.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_picture_vad(VOID *data, INT_T len)
{
    TUYA_CHECK_NULL_RETURN(data, OPRT_INVALID_PARM);
    OPERATE_RET rt = OPRT_OK;

    WUKONG_AUDIO_VAD_FLAG_E vad_flag = (len >= (INT_T)sizeof(WUKONG_AUDIO_VAD_FLAG_E))
        ? *(WUKONG_AUDIO_VAD_FLAG_E *)data : (WUKONG_AUDIO_VAD_FLAG_E)0;
    TAL_PR_DEBUG("[====ai_picture] vad: [%d]", vad_flag);
    if (WUKONG_AUDIO_VAD_START == vad_flag)
    {
        if (!tuya_ai_agent_is_ready()) {
            TAL_PR_DEBUG("ai agent is not ready, ignore audio input");
            return OPRT_RESOURCE_NOT_READY;
        }
        tuya_ai_agent_set_scode(AI_AGENT_SCODE_PICTURE);
        tuya_ai_input_start(FALSE);
#if defined(ENABLE_TUYA_PICTURE) && (ENABLE_TUYA_PICTURE == 1)
        if (wukong_picture_input_get_num() > 0) {
            wukong_picture_input_from_album();
        }
#endif
        tuya_ai_toy_idle_timer_ctrl(FALSE);
    }
    else
    {
        tuya_ai_input_stop();
        tuya_ai_toy_idle_timer_ctrl(TRUE);
    }

    return rt;
}
 
 /**
  * @brief Client run callback for picture mode.
  *
  * Triggered periodically by the main application to handle client-side logic.
  *
  * @param[in] data Unused.
  * @param[in] len  Unused.
  * @return OPRT_OK on success.
  */
 STATIC OPERATE_RET wukong_ai_picture_client_run(VOID_T *data, INT_T len)
 {
     TAL_PR_DEBUG("[====ai_picture] client run");
     // s_ai_picture.state = AI_CHAT_IDLE;
     DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_PICTURE, s_ai_picture.state, AI_CHAT_IDLE);
     return OPRT_OK;
 }
 
/**
 * @brief Handle key press event in picture mode.
 *
 * A single press interrupts the current session and starts a new
 * listening cycle (oneshot trigger).
 *
 * @param[in] data Pointer to PUSH_KEY_TYPE_E containing the key event.
 * @param[in] len  Length of the data.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET wukong_ai_picture_key_cb(VOID *data, INT_T len)
{
    OPERATE_RET rt = OPRT_OK;
    PUSH_KEY_TYPE_E event = *(PUSH_KEY_TYPE_E *)data;
    TAL_PR_DEBUG("[====ai_picture] key: %d", event);
    switch (event)
    {
        case NORMAL_KEY:
        {
            wukong_audio_player_stop(AI_PLAYER_ALL);
            wukong_audio_input_reset();
            tuya_ai_agent_event(AI_EVENT_CHAT_BREAK, 0);
            DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_PICTURE, s_ai_picture.state, AI_CHAT_IDLE);
            s_ai_picture.wakeup_stat = FALSE;
        }
        break;

        case SEQ_KEY:
        {
            ;
        }
        break;

        case LONG_KEY:
        {
            wukong_audio_player_stop(AI_PLAYER_ALL);
            wukong_audio_input_reset();
            tuya_ai_agent_event(AI_EVENT_CHAT_BREAK, 0);
            DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_PICTURE, s_ai_picture.state, AI_CHAT_LISTEN);
        }
        break;

        case RELEASE_KEY:
        {
            if (s_ai_picture.wakeup_stat) {
                DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_PICTURE, s_ai_picture.state, AI_CHAT_UPLOAD);
            }
        }
        break;

        default:
        break;
    }

    return rt;
}
 
 /**
  * @brief Handle idle notification in picture mode.
  *
  * Called when the system detects inactivity, causing the mode to reset to IDLE.
  *
  * @param[in] data Unused.
  * @param[in] len  Unused.
  * @return OPRT_OK on success.
  */
 STATIC OPERATE_RET wukong_ai_picture_notify_idle_cb(VOID *data, INT_T len)
 {
     TAL_PR_DEBUG("[====ai_picture] client run");
     OPERATE_RET rt = OPRT_OK;
 
     // s_ai_picture.state = AI_CHAT_IDLE;
     DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_PICTURE, s_ai_picture.state, AI_CHAT_IDLE);
 
     return rt;
 }
 
 /**
  * @brief Register the picture mode.
  *
  * Assigns all relevant callbacks to the provided mode handle structure.
  *
  * @param[out] cb Pointer to the mode handle pointer.
  * @return OPRT_OK on success.
  */
 OPERATE_RET ai_picture_register(AI_CHAT_MODE_HANDLE_T **cb)
 {
     OPERATE_RET rt = OPRT_OK;
 
     s_ai_picture_cb.on_init       = wukong_ai_picture_int_cb;
     s_ai_picture_cb.on_deinit     = wukong_ai_picture_deint_cb;
     s_ai_picture_cb.on_key        = wukong_ai_picture_key_cb;
     s_ai_picture_cb.on_task       = wukong_ai_picture_task_cb;
     s_ai_picture_cb.on_event      = wukong_ai_picture_event_cb;
     s_ai_picture_cb.on_wakeup     = wukong_ai_picture_wakeup;
     s_ai_picture_cb.on_vad        = wukong_ai_picture_vad;
     s_ai_picture_cb.on_client     = wukong_ai_picture_client_run;
     s_ai_picture_cb.on_notify_idle = wukong_ai_picture_notify_idle_cb;
     *cb = &s_ai_picture_cb;
     return rt;
 }
 #endif