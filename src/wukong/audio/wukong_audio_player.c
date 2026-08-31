#include "wukong_audio_player.h"
#include "wukong_audio_output.h"
#include "tal_log.h"
#include "tal_memory.h"
#include "wukong_ai_skills.h"
#include "skill_cloudevent.h"
#include "skill_music_story.h"
#include "svc_ai_player.h"
#include "base_event.h"
#include "media_src.h"
#include "tuya_device_cfg.h"


#include "wukong_ai_mode.h"

#include "tal_wifi.h"
#include "tuya_svc_netmgr.h"
#include "tuya_svc_netmgr_linkage.h"
#include <stdio.h>
#include "tal_queue.h"
STATIC BOOL_T __s_music_continuous = FALSE;
STATIC BOOL_T __s_music_replay = FALSE;
STATIC BOOL_T __s_tts_play_flag = FALSE;
STATIC AI_PLAYER_HANDLE __s_tone_player = NULL;
STATIC AI_PLAYLIST_HANDLE __s_tone_playlist = NULL;
STATIC AI_PLAYER_HANDLE __s_music_player = NULL;
STATIC AI_PLAYLIST_HANDLE __s_music_playlist = NULL;

STATIC WF_STATION_STAT_E net_state={0};
QUEUE_HANDLE  s_queue_wake;
QUEUE_HANDLE  s_queue_test;

STATIC OPERATE_RET __audio_output_open(TKL_AUDIO_SAMPLE_E sample, TKL_AUDIO_DATABITS_E datebits, TKL_AUDIO_CHANNEL_E channel)
{
    WUKONG_AUDIO_OUTPUT_CFG_T cfg = {0};
    cfg.sample_rate = sample;
    cfg.sample_bits = datebits;
    cfg.channel = channel;
    return wukong_audio_output_init(&cfg);
}

STATIC OPERATE_RET __audio_output_close(VOID)
{
    return wukong_audio_output_deinit();
}

STATIC OPERATE_RET __audio_output_start(AI_PLAYER_HANDLE handle)
{
    return wukong_audio_output_start();
}

STATIC OPERATE_RET __audio_output_write(AI_PLAYER_HANDLE handle, CONST VOID *buf, UINT_T len)
{
    return wukong_audio_output_write(buf, len);
}

STATIC OPERATE_RET __audio_output_stop(AI_PLAYER_HANDLE handle)
{
    return wukong_audio_output_stop();
}

STATIC OPERATE_RET __audio_output_set_volume(UINT_T volume)
{
    return wukong_audio_output_set_vol((INT32_T)volume);
}

OPERATE_RET __player_event(VOID *data)
{
    TUYA_CHECK_NULL_RETURN(data, OPRT_OK);    

    AI_PLAYER_EVT_T *event = (AI_PLAYER_EVT_T*)data;
    TAL_PR_DEBUG("wukong audio player -> player %s event: %d", (event->handle == __s_tone_player) ? "tts" : "music", event->state);

    OPERATE_RET rt = OPRT_OK;
    INT32_T player_vol = 0;
    tuya_ai_player_get_volume(NULL, &player_vol);
    //! player finished or failed.
    if (event->state == AI_PLAYER_STOPPED) {
        TAL_PR_DEBUG("wukong audio player -> stop event");
        if (!wukong_audio_player_is_playing()) {
            wukong_ai_event_notify(WUKONG_AI_EVENT_PLAY_END, NULL);
        } else {
            TAL_PR_DEBUG("wukong audio player -> playing stop event, music vol change to %d", player_vol);
            tuya_ai_player_set_volume(__s_music_player, player_vol);
        }

        //! if tts play stop, reset play flag
        if(event->handle == __s_tone_player && __s_tts_play_flag) {
            __s_tts_play_flag = FALSE;
        }
    }
    else if (event->state == AI_PLAYER_PLAYING) {
        TAL_PR_DEBUG("wukong audio player -> playing start event");
        if (event->handle == __s_tone_player && AI_PLAYER_PLAYING == tuya_ai_player_get_state(__s_music_player)) {
            TAL_PR_DEBUG("wukong audio player -> playing start event, music vol change to %d", player_vol/2);
            tuya_ai_player_set_volume(__s_music_player, player_vol/2);
        }
        wukong_ai_event_notify(WUKONG_AI_EVENT_PLAY_CTL_PLAY, NULL);
    } else if (event->state == AI_PLAYER_PAUSED) {
        TAL_PR_DEBUG("wukong audio player -> pause event");
        wukong_ai_event_notify(WUKONG_AI_EVENT_PLAY_CTL_PAUSE, NULL);
    }
    
    if(event->handle == __s_music_player)
    {
        WUKONG_MUSIC_PLAYER_T music_msg = {0};
        music_msg.cmd = MUSIC_PLAYER_STATE;
        music_msg.state = event->state;
        TAL_PR_DEBUG("[%s] cmd: %d, state: %d", __FUNCTION__, music_msg.cmd, music_msg.state);
        TUYA_CALL_ERR_LOG(ty_publish_event(EVENT_MUSIC_PLAYER, &music_msg));
    }

    return rt;
}

OPERATE_RET wukong_audio_player_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    
    // player init
    AI_PLAYER_CFG_T cfg = {.sample = 16000, .datebits = 16, .channel = 1};
    TUYA_CALL_ERR_GOTO(tuya_ai_player_service_init(&cfg), __error);

    //! tts player
    TUYA_CALL_ERR_GOTO(tuya_ai_player_create(AI_PLAYER_MODE_FOREGROUND, &__s_tone_player), __error);

    AI_PLAYLIST_CFG_T ton_cfg = {.auto_play = TRUE,.capacity = 2};
    TUYA_CALL_ERR_GOTO(tuya_ai_playlist_create(__s_tone_player, &ton_cfg, &__s_tone_playlist), __error);

    //! music player
    TUYA_CALL_ERR_GOTO(tuya_ai_player_create(AI_PLAYER_MODE_BACKGROUND, &__s_music_player), __error);

    AI_PLAYLIST_CFG_T misc_cfg = {.auto_play = TRUE,.capacity = 32};
    TUYA_CALL_ERR_GOTO(tuya_ai_playlist_create(__s_music_player, &misc_cfg, &__s_music_playlist), __error);

    //! player stat
    TUYA_CALL_ERR_GOTO(ty_subscribe_event(EVENT_AI_PLAYER_STATE, "wk_player", __player_event, SUBSCRIBE_TYPE_NORMAL), __error);

    AI_PLAYER_CONSUMER_T consumer = {0};
    consumer.open = __audio_output_open;
    consumer.close = __audio_output_close;
    consumer.start = __audio_output_start;
    consumer.stop = __audio_output_stop;
    consumer.write = __audio_output_write;
    consumer.set_volume = __audio_output_set_volume;
    tuya_ai_player_set_consumer(&consumer);
    tuya_ai_player_set_mix_mode(TRUE);

    return rt;

__error:
    wukong_audio_player_deinit();
    return rt;
}

OPERATE_RET wukong_audio_player_deinit(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    if (__s_tone_player) {
        TUYA_CALL_ERR_LOG(tuya_ai_player_destroy(__s_tone_player));
        __s_tone_player = NULL;
    }
    
    if (__s_tone_playlist) {
        TUYA_CALL_ERR_LOG(tuya_ai_playlist_destroy(__s_tone_playlist));
        __s_tone_playlist = NULL;
    }

    if (__s_music_player) {
        TUYA_CALL_ERR_LOG(tuya_ai_player_destroy(__s_music_player));
        __s_music_player = NULL;
    }
    
    if (__s_music_playlist) {
        TUYA_CALL_ERR_LOG(tuya_ai_playlist_destroy(__s_music_playlist));
        __s_music_playlist = NULL;
    }

    // player deinit
    TUYA_CALL_ERR_RETURN(tuya_ai_player_service_deinit());
    return rt;
}

OPERATE_RET wukong_audio_player_resume(VOID)
{
    if (__s_music_player) {
        tuya_ai_player_resume(__s_music_player);
    }

    return OPRT_OK;
}


OPERATE_RET wukong_audio_player_pause(VOID)
{
    if (__s_music_player) {
        tuya_ai_player_pause(__s_music_player);
    }

    return OPRT_OK;    
}

OPERATE_RET wukong_audio_player_replay(VOID)
{
    if (__s_music_player) {
        // tuya_ai_player_replay(__s_music_player);
    }

    return OPRT_NOT_SUPPORTED;
}

BOOL_T wukong_audio_player_is_playing(VOID)
{
    if (tuya_ai_player_get_state(__s_tone_player) == AI_PLAYER_PLAYING ||
        tuya_ai_player_get_state(__s_music_player) == AI_PLAYER_PLAYING) {
            return TRUE;
    } 

    return FALSE;
}
OPERATE_RET wukong_audio_play_music(WUKONG_AI_MUSIC_T *music)
{
    OPERATE_RET rt = OPRT_OK;
    if (music->src_cnt <= 0) {
        TAL_PR_ERR("music src cnt is 0");
        return rt;
    }

    TUYA_CALL_ERR_LOG(tuya_ai_playlist_clear(__s_music_playlist));
    for (INT_T i = 0; i < music->src_cnt; i++) {

        TAL_PR_DEBUG("wukong audio player -> player music url %s", music->src_array[i].url);
        TUYA_CALL_ERR_LOG(tuya_ai_playlist_add(__s_music_playlist, AI_PLAYER_SRC_URL, music->src_array[i].url, music->src_array[i].format));
    }

    return rt;
}

OPERATE_RET wukong_audio_play_tts_url(WUKONG_AI_PLAYTTS_T *playtts, BOOL_T is_loop)
{
    OPERATE_RET rt = OPRT_OK;
    TUYA_CHECK_NULL_RETURN(playtts, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(playtts->tts.url, OPRT_INVALID_PARM);

    TAL_PR_DEBUG("wukong audio player -> player tts url %s", playtts->tts.url);
    TUYA_CALL_ERR_LOG(tuya_ai_playlist_clear(__s_tone_playlist));
    TUYA_CALL_ERR_LOG(tuya_ai_playlist_add(__s_tone_playlist, AI_PLAYER_SRC_URL, playtts->tts.url, playtts->tts.format));
    return rt;
}

OPERATE_RET wukong_audio_play_data(INT_T format, CONST CHAR_T *data, INT_T len)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_LOG(tuya_ai_playlist_stop(__s_tone_playlist));
    if (data && len > 0) {
        TAL_PR_DEBUG("wukong audio player -> player tts mem data len %d", len);
        TUYA_CALL_ERR_LOG(tuya_ai_player_start(__s_tone_player, AI_PLAYER_SRC_MEM, NULL, format));
        TUYA_CALL_ERR_LOG(tuya_ai_player_feed(__s_tone_player, (UINT8_T *)data, len));
        TUYA_CALL_ERR_LOG(tuya_ai_player_feed(__s_tone_player, NULL, 0));
    } 

    return rt;
}

OPERATE_RET wukong_audio_play_tts_stream(WUKONG_AI_EVENT_TYPE_E type, AI_AUDIO_CODEC_E codec, CHAR_T *data, INT_T len)
{
    OPERATE_RET rt = OPRT_OK;
    switch(type) {
    case WUKONG_AI_EVENT_TTS_START:
        TAL_PR_DEBUG("wukong audio player -> tts stream start");
        __s_tts_play_flag = TRUE;
        wukong_ai_event_notify(WUKONG_AI_EVENT_TTS_PRE, NULL);
        TUYA_CALL_ERR_LOG(tuya_ai_playlist_clear(__s_tone_playlist));
        TUYA_CALL_ERR_LOG(tuya_ai_player_stop(__s_tone_player));
        TUYA_CALL_ERR_LOG(tuya_ai_player_start(__s_tone_player, AI_PLAYER_SRC_MEM, NULL, codec));
        wukong_ai_event_notify(WUKONG_AI_EVENT_TTS_START, NULL);
        break;
    case WUKONG_AI_EVENT_TTS_DATA:
        TAL_PR_DEBUG("wukong audio player -> tts stream data %d", len);        
        if (data && len > 0 && __s_tts_play_flag) {
            TUYA_CALL_ERR_LOG(tuya_ai_player_feed(__s_tone_player, (UINT8_T *)data, len));
        }
        wukong_ai_event_notify(WUKONG_AI_EVENT_TTS_DATA, NULL);
        break;
    case WUKONG_AI_EVENT_TTS_STOP:
        TAL_PR_DEBUG("wukong audio player -> tts stream stop");
        TUYA_CALL_ERR_LOG(tuya_ai_player_feed(__s_tone_player, NULL, 0));
        wukong_ai_event_notify(WUKONG_AI_EVENT_TTS_STOP, NULL);
        break;
    case WUKONG_AI_EVENT_TTS_ABORT:
        TAL_PR_DEBUG("wukong audio player -> tts stream abort");
        TUYA_CALL_ERR_LOG(tuya_ai_player_feed(__s_tone_player, NULL, 0));
        wukong_ai_event_notify(WUKONG_AI_EVENT_TTS_ABORT, NULL);
        break;
    default:
        break;
    }

    return rt;
}

OPERATE_RET wukong_audio_play_local(CHAR_T *url, CHAR_T *song_name, CHAR_T *artist, INT_T format, INT_T size)
{
    OPERATE_RET rt = OPRT_OK;
    TAL_PR_DEBUG("wukong audio player -> play local url: %s", url);
    AI_PLAYER_SRC_E src = (strstr(url, "http://") == url || strstr(url, "https://") == url) ?
                           AI_PLAYER_SRC_URL : AI_PLAYER_SRC_FILE;
    TUYA_CALL_ERR_LOG(tuya_ai_playlist_clear(__s_music_playlist));
    TUYA_CALL_ERR_LOG(tuya_ai_playlist_add(__s_music_playlist, src, url, format));
    return rt;
}

OPERATE_RET wukong_audio_player_stop(TY_AI_TOY_PLAYER_TYPE_E type)
{
    OPERATE_RET rt = OPRT_OK;
    TAL_PR_DEBUG("wukong audio player -> stop %d player", type);
    switch (type)
    {
    case AI_PLAYER_FG:
        TUYA_CALL_ERR_LOG(tuya_ai_playlist_clear(__s_tone_playlist));
        TUYA_CALL_ERR_LOG(tuya_ai_player_stop(__s_tone_player));
        break;
    case AI_PLAYER_BG:
        TUYA_CALL_ERR_LOG(tuya_ai_playlist_clear(__s_music_playlist));
        TUYA_CALL_ERR_LOG(tuya_ai_player_stop(__s_music_player));
        break;
    case AI_PLAYER_ALL:
        TUYA_CALL_ERR_LOG(tuya_ai_playlist_clear(__s_tone_playlist));
        TUYA_CALL_ERR_LOG(tuya_ai_player_stop(__s_tone_player));
        TUYA_CALL_ERR_LOG(tuya_ai_playlist_clear(__s_music_playlist));
        TUYA_CALL_ERR_LOG(tuya_ai_player_stop(__s_music_player));
        break;
    default:
        break;
    }

    if(type == AI_PLAYER_BG || type == AI_PLAYER_ALL) {
        TUYA_CALL_ERR_LOG(ty_publish_event(EVENT_MUSIC_BREAK, NULL));
    }
    
    return rt;
}

OPERATE_RET wukong_audio_player_alert(TY_AI_TOY_ALERT_TYPE_E type, BOOL_T send_eof)
{
    TAL_PR_NOTICE("wukong audio player -> play alert type=%d", type);

    OPERATE_RET rt = OPRT_OK;
    CONST CHAR_T *audio_data = NULL;
    UINT32_T audio_size = 0;
    UINT8_T wakeupflg = 1;
    UINT8_T testflg = 0xFF;

    tal_wifi_station_get_status(&net_state);
    TAL_PR_NOTICE("------------net_state=%d------------",net_state);
    switch (type) {
    case AI_TOY_ALERT_TYPE_POWER_ON:
    // case AI_TOY_ALERT_TYPE_NETWORK_CONNECTED:
    case AI_TOY_ALERT_TYPE_BATTERY_LOW:
    case AI_TOY_ALERT_TYPE_PLEASE_AGAIN:
    case AI_TOY_ALERT_TYPE_LONG_KEY_TALK:
    case AI_TOY_ALERT_TYPE_KEY_TALK:
    case AI_TOY_ALERT_TYPE_WAKEUP_TALK:
    case AI_TOY_ALERT_TYPE_RANDOM_TALK:
    // case AI_TOY_ALERT_TYPE_WAKEUP:  
#if defined(ENABLE_CLOUD_ALERT) && ENABLE_CLOUD_ALERT==1    
        if (OPRT_OK == wukong_ai_agent_cloud_alert(type)) break;
#endif
    case AI_TOY_ALERT_TYPE_NOT_ACTIVE:
    case AI_TOY_ALERT_TYPE_NETWORK_CFG:
    // case AI_TOY_ALERT_TYPE_NETWORK_FAIL:
    // case AI_TOY_ALERT_TYPE_NETWORK_DISCONNECT:
    default:
        audio_data = (CONST CHAR_T*)media_src_dingdong_zh;
        audio_size = sizeof(media_src_dingdong_zh);   
        break;        

    case AI_TOY_ALERT_TYPE_NETWORK_FAIL:
    case AI_TOY_ALERT_TYPE_NETWORK_DISCONNECT:
        audio_data = (CONST CHAR_T*)media_src_connect_error_zh;
        audio_size = sizeof(media_src_connect_error_zh);
        break;
    case AI_TOY_ALERT_TYPE_NETWORK_CONNECTED:
        // audio_data = (CONST CHAR_T*)media_src_connected_zh;
        // audio_size = sizeof(media_src_connected_zh);

        if (tal_queue_fetch(s_queue_test, &testflg, 1) == OPRT_OK)
        {
            if(testflg == 0)
            {
                return rt;
            }
        }
        audio_data = (CONST CHAR_T*)media_src_bingo_msc;
        audio_size = sizeof(media_src_bingo_msc);
        // tuya_ai_input_start(TRUE);
        // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("刚连接上网络，用下面话术进行播报，不要加任何其他的词包括“好的”和“正在处理”等词，直接说：“已联网，开始我们的对话吧。”。"));
        // tuya_ai_input_stop();
        break;
    case AI_TOY_ALERT_TYPE_WAKEUP: 
        
        tal_queue_post(s_queue_wake, &wakeupflg, 0); 
        if(net_state == WSS_GOT_IP)
        {
            // audio_data = (CONST CHAR_T*)media_src_wozaine_zh;
            // audio_size = sizeof(media_src_wozaine_zh);
            tuya_ai_input_start(TRUE);
            // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("用户正在呼唤你，用下面话术进行应答，不要加任何其他的词包括“好的”和“正在处理”等词，直接说：“我在呢。”。"));
            // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("用户正在呼唤你。"));
            TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("你好!"));
            tuya_ai_input_stop();
            break;
        }        
        audio_data = (CONST CHAR_T*)media_src_connect_error_zh;
        audio_size = sizeof(media_src_connect_error_zh);
        break;
    }

    TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
    return rt;
}


/**
 * @brief Get current audio player volume
 * @param[out] vol  Pointer to store volume level (0-100)
 * @return OPRT_OK on success, OPRT_INVALID_PARM if vol is NULL
 */
OPERATE_RET wukong_audio_player_get_vol(UINT8_T *vol)
{
    INT32_T player_vol = 0;

    if (!vol) {
        return OPRT_INVALID_PARM;
    }

    tuya_ai_player_get_volume(NULL, &player_vol);
    *vol = (UINT8_T)player_vol;
    return OPRT_OK;
}

/**
 * @brief Set audio player volume
 * @param[in] vol  Volume level (0-100)
 * @return OPRT_OK on success
 */
OPERATE_RET wukong_audio_player_set_vol(UINT8_T vol)
{
    tuya_ai_player_set_volume(NULL, vol);
    return OPRT_OK;
}