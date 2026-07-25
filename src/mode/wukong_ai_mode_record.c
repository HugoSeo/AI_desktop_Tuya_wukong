/**
 * @file wukong_ai_mode_record.c
 * @brief record mode implementation.
 *
 * Implements the record interaction mode for direct peer-to-peer
 * audio/video communication via mobile app.
 *
 * @copyright Copyright (c) 2023 Tuya Inc. All Rights Reserved.
 */

 #include "wukong_ai_mode.h"
 #include "tuya_ai_toy.h"
 #include "wukong_kws.h"
 #include "wukong_tm_internal.h"
 
 #if defined(ENABLE_AI_MODE_RECORD) && (ENABLE_AI_MODE_RECORD == 1)
 STATIC AI_CHAT_MODE_HANDLE_T s_ai_record_cb = {0};
 STATIC AI_CHAT_MODE_PARAM_T s_ai_record = {0};
 
 #if defined(ENABLE_TUYA_CODEC_OPUS) && (ENABLE_TUYA_CODEC_OPUS == 1)

 typedef struct{
    TUYA_AI_ENCODER_T *encoder;
    TUYA_AI_ENCODER_INFO_T encoder_info;
 }AI_RECORD_OPUS_CTX_T;

 STATIC AI_RECORD_OPUS_CTX_T s_record_opus = {0};
 
 STATIC OPERATE_RET wukong_ai_record_opus_encoder(VOID)
 {
    OPERATE_RET rt = OPRT_OK;
    s_record_opus.encoder = tuya_ai_get_encoder(AUDIO_CODEC_OPUS);
    s_record_opus.encoder_info.encode_type = AUDIO_CODEC_OPUS;
    s_record_opus.encoder_info.sample_rate = 16000;
    s_record_opus.encoder_info.channels = 1;
    s_record_opus.encoder_info.bits_per_sample = 16;
    s_record_opus.encoder_info.frame_size = 0;
    s_record_opus.encoder_info.bitrate = 16000;
    s_record_opus.encoder_info.bandwidth = 1102;
    s_record_opus.encoder_info.vbr = 0;
    s_record_opus.encoder_info.dtx = 0;
    s_record_opus.encoder_info.complexity = 0;
    if(s_record_opus.encoder) {
        if (s_record_opus.encoder && s_record_opus.encoder->handle) {
            s_record_opus.encoder->destroy(s_record_opus.encoder->handle);
            s_record_opus.encoder->handle = NULL;
        }
        rt = s_record_opus.encoder->create(&s_record_opus.encoder->handle, &s_record_opus.encoder_info);
        if(rt != OPRT_OK) {
            TAL_PR_ERR("create opus encoder failed");
            s_record_opus.encoder = NULL;
            return rt;
        }
        TAL_PR_DEBUG("[====DHR]create opus encoder success");
    }
    return rt;
 }

 STATIC OPERATE_RET __ai_record_encoder_data_cb(AI_AUDIO_CODEC_TYPE codec_type, UCHAR_T *data, UINT_T len, void *usr_data)
 {
    if(wukong_ai_record_handle_get()->input_audio != NULL) {
        return wukong_ai_record_handle_get()->input_audio(AUDIO_CODEC_OPUS, data, len);
    }
    TAL_PR_DEBUG("[====ai_record]input audio cb is null,[%p, %d]", data, len);
    return OPRT_OK; 
 } 

 #endif

 /**
  * @brief Initialize record mode.
  *
  * Sets up manual VAD and disables KWS, switching to IDLE state for record connection.
  *
  * @param[in] data Unused.
  * @param[in] len  Unused.
  * @return OPRT_OK on success.
  */
 STATIC OPERATE_RET wukong_ai_record_int_cb(VOID *data, INT_T len)
 {
     TAL_PR_DEBUG("[====ai_record] init");
     OPERATE_RET rt = OPRT_OK;
 
     tuya_ai_toy_led_on();
 
     //set vad mode
     wukong_audio_input_wakeup_mode_set(WUKONG_AUDIO_VAD_MANUAL);
 
     //disenable kws
     wukong_kws_disable();
 
     DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_RECORD, s_ai_record.state, AI_CHAT_IDLE);
 
     s_ai_record.wakeup_stat = TRUE;
 
     wukong_audio_input_wakeup_set(FALSE);
//  #ifdef ENABLE_TUYA_UI
//      AI_DEVICE_MODE_E trigger_mode = AI_DEVICE_MODE_RECORD;
//      tuya_ai_display_msg(&trigger_mode, 1, TY_DISPLAY_TP_CHAT_MODE);
//  #endif

 #if defined(ENABLE_TUYA_CODEC_OPUS) && (ENABLE_TUYA_CODEC_OPUS == 1)
     wukong_ai_record_opus_encoder();
 #endif
 
     return rt;
 }
 
 /**
  * @brief De-initialize record mode.
  *
  * Stops audio input paths to safely switch back to normal AI modes.
  *
  * @param[in] data Unused.
  * @param[in] len  Unused.
  * @return OPRT_OK on success.
  */
 STATIC OPERATE_RET wukong_ai_record_deint_cb(VOID *data, INT_T len)
 {
     TAL_PR_DEBUG("[====ai_record] deinit");
     OPERATE_RET rt = OPRT_OK;
 
     /*
      * Stop the manual wakeup/audio path before switching back to
      * normal AI modes, otherwise in-flight mic frames may fall
      * through to the default AI input path before a new session exists.
      */
     s_ai_record.wakeup_stat = FALSE;
     TUYA_CALL_ERR_LOG(wukong_audio_input_wakeup_set(FALSE));
     TUYA_CALL_ERR_LOG(wukong_audio_input_reset());
     wukong_ai_agent_input_stop();
     return rt;
 }
 
 /**
  * @brief Periodic task callback for record mode.
  *
  * @param[in] data Unused.
  * @param[in] len  Unused.
  * @return OPRT_OK on success.
  */
 STATIC OPERATE_RET wukong_ai_record_task_cb(VOID *data, INT_T len)
 {
     return OPRT_OK;
 }
 
 /**
  * @brief Event callback for record mode.
  *
  * @param[in] data Pointer to WUKONG_AI_EVENT_T structure.
  * @param[in] len  Length of the data (unused).
  * @return OPRT_OK on success.
  */
 STATIC OPERATE_RET wukong_ai_record_event_cb(VOID *data, INT_T len)
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
  * @brief Handle KWS (Keyword Spotting) wakeup in record mode.
  *
  * @param[in] data Unused.
  * @param[in] len  Unused.
  * @return OPRT_OK on success.
  */
 STATIC OPERATE_RET wukong_ai_record_wakeup(VOID *data, INT_T len)
 {
     TAL_PR_DEBUG("[====ai_record] wakeup"); 
     OPERATE_RET rt = OPRT_OK;
     return rt;
 }
 
 /**
  * @brief Handle VAD (Voice Activity Detection) event in record mode.
  *
  * @param[in] data Pointer to WUKONG_AUDIO_VAD_FLAG_E containing VAD flag.
  * @param[in] len  Length of the data.
  * @return OPRT_OK on success.
  */
 STATIC OPERATE_RET wukong_ai_record_vad(VOID *data, INT_T len)
 {
     return OPRT_OK;
 }
 
 /**
  * @brief Client run callback for record mode.
  *
  * @param[in] data Unused.
  * @param[in] len  Unused.
  * @return OPRT_OK on success.
  */
 STATIC OPERATE_RET wukong_ai_record_client_run(VOID_T *data, INT_T len)
 {
     TAL_PR_DEBUG("[====ai_record] client run");
     return OPRT_OK;
 }
 
 /**
  * @brief Handle key press event in record mode.
  *
  * @param[in] data Pointer to PUSH_KEY_TYPE_E containing the key event.
  * @param[in] len  Length of the data.
  * @return OPRT_OK on success.
  */
 STATIC OPERATE_RET wukong_ai_record_key_cb(VOID *data, INT_T len)
 {
     OPERATE_RET rt = OPRT_OK;
     PUSH_KEY_TYPE_E event = *(PUSH_KEY_TYPE_E *)data;
     TAL_PR_DEBUG("[====ai_record] key: %d", event);
     switch (event) 
     {        
         case NORMAL_KEY:
         {
             wukong_audio_player_stop(AI_PLAYER_ALL);
             wukong_audio_input_reset();
             wukong_ai_agent_chat_break(NULL);
             DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_RECORD, s_ai_record.state, AI_CHAT_IDLE);
             s_ai_record.wakeup_stat = FALSE;
 
         } 
         break;  
 
         case SEQ_KEY:
         {
             ;
         }
         break;
 
         case LONG_KEY: 
         {
             DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_RECORD, s_ai_record.state, AI_CHAT_LISTEN);
             s_ai_record.wakeup_stat = TRUE;
         }
         break;   
 
         case RELEASE_KEY: 
         {
             DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_RECORD, s_ai_record.state, AI_CHAT_UPLOAD);
         }
         break;
 
         default:
         break;
     }
     
     return rt;
 }
 
 /**
  * @brief Handle idle notification in record mode.
  *
  * @param[in] data Unused.
  * @param[in] len  Unused.
  * @return OPRT_OK on success.
  */
 STATIC OPERATE_RET wukong_ai_record_notify_idle_cb(VOID *data, INT_T len)
 {
     TAL_PR_DEBUG("[====ai_record] notify idle");
     return OPRT_OK;
 }
 
 /**
  * @brief Handle audio input routing for record mode.
  *
  * Re-routes captured audio data directly to the record stream instead of the default AI agent.
  *
  * @param[in] data Pointer to audio payload.
  * @param[in] len  Length of the audio payload.
  * @return OPRT_OK on success.
  */
 STATIC OPERATE_RET wukong_ai_record_handle_audio_input(VOID *data, INT_T len)
 {
    if(data == NULL || len == 0) {
        TAL_PR_ERR("[====ai_record]input error");
        return OPRT_INVALID_PARM;
    }

 #if defined(ENABLE_TUYA_CODEC_OPUS) && (ENABLE_TUYA_CODEC_OPUS == 1)
    if(s_record_opus.encoder && s_record_opus.encoder->handle)   
    {
        OPERATE_RET rt = s_record_opus.encoder->encode(s_record_opus.encoder->handle, (UCHAR_T *)data, len, __ai_record_encoder_data_cb, NULL);
        if(rt != OPRT_OK) {
            TAL_PR_ERR("[====ai_record]encode error: %d", rt);
            return rt;
        }
    }
    return OPRT_OK; 
 #else  
    if(wukong_ai_record_handle_get()->input_audio != NULL) {
        return wukong_ai_record_handle_get()->input_audio(AUDIO_CODEC_PCM, data, len);
    }
    TAL_PR_DEBUG("[====ai_record]input audio cb is null,[%p, %d]", data, len);
    return OPRT_OK; 
 #endif   
 }
 
 /**
  * @brief Register the record mode.
  *
  * Assigns all relevant callbacks to the provided mode handle structure.
  *
  * @param[out] cb Pointer to the mode handle pointer.
  * @return OPRT_OK on success.
  */
 OPERATE_RET ai_record_register(AI_CHAT_MODE_HANDLE_T **cb)
 {
     OPERATE_RET rt = OPRT_OK;
 
     s_ai_record_cb.on_init         = wukong_ai_record_int_cb;
     s_ai_record_cb.on_deinit       = wukong_ai_record_deint_cb;
     s_ai_record_cb.on_key          = wukong_ai_record_key_cb;
     s_ai_record_cb.on_task         = wukong_ai_record_task_cb;
     s_ai_record_cb.on_event        = wukong_ai_record_event_cb;
     s_ai_record_cb.on_wakeup       = wukong_ai_record_wakeup;
     s_ai_record_cb.on_vad          = wukong_ai_record_vad;
     s_ai_record_cb.on_client       = wukong_ai_record_client_run;
     s_ai_record_cb.on_notify_idle  = wukong_ai_record_notify_idle_cb;
     s_ai_record_cb.on_audio_input  = wukong_ai_record_handle_audio_input;
     *cb = &s_ai_record_cb;
     return rt;
 }
 #endif