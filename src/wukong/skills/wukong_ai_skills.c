#include "wukong_ai_skills.h"
#include "wukong_audio_player.h"
#if defined(ENABLE_TUYA_PICTURE) && (ENABLE_TUYA_PICTURE == 1)
#include "wukong_picture_output.h"
#endif
#if defined(ENABLE_TOOLKITS_PLAYBACK) && (ENABLE_TOOLKITS_PLAYBACK == 1)
#include "wukong_playback_ctrl.h"
#endif
#include "skill_emotion.h"
#include "skill_music_story.h"
#include "skill_cloudevent.h"
#include "tal_log.h"
#include "tal_memory.h"
#include "ty_cJSON.h"
#include <stdio.h>

#include "tal_queue.h"

STATIC BOOL_T __s_chat_break = FALSE;

STATIC UINT8_T get_offon_state = 0;
QUEUE_HANDLE  s_queue_voice_cmd;
QUEUE_HANDLE  s_queue_state;
QUEUE_HANDLE  s_queue_name_str;
OPERATE_RET __wukong_ai_skill_process(AI_TEXT_TYPE_E type, ty_cJSON *root, BOOL_T eof)
{
    OPERATE_RET rt = OPRT_OK;
    CONST ty_cJSON *node = NULL;
    CONST CHAR_T *code = NULL;

    //! root is data:{}, parse code
    node = ty_cJSON_GetObjectItem(root, "code");
    code = ty_cJSON_GetStringValue(node);
    if (!code) 
        return OPRT_OK;
    // ty_cJSON_PrintUnformatted(root);
    TAL_PR_NOTICE("wukong text -> skill code: %s", ty_cJSON_PrintUnformatted(root));
    if (strcmp(code, "music") == 0 ||
               strcmp(code, "story") == 0) {
        WUKONG_AI_MUSIC_T *music = NULL;
        if (wukong_ai_parse_music(root, &music) == OPRT_OK) {
            wukong_ai_parse_music_dump(music);
            wukong_ai_play_music(music);
            wukong_ai_parse_music_free(music);
        }
    } else if (strcmp(code, "PlayControl") == 0) {
        /* Check if this is a music_list or refresh_play_url async response */
        ty_cJSON *general = ty_cJSON_GetObjectItem(root, "general");
        ty_cJSON *custom  = ty_cJSON_GetObjectItem(root, "custom");
        ty_cJSON *action_node = NULL;

        if (general) {
            action_node = ty_cJSON_GetObjectItem(general, "action");
        }
        if (action_node == NULL && custom) {
            action_node = ty_cJSON_GetObjectItem(custom, "action");
        }

#if defined(ENABLE_TOOLKITS_PLAYBACK) && (ENABLE_TOOLKITS_PLAYBACK == 1)
        if (action_node && ty_cJSON_IsString(action_node) &&
            (strcmp(action_node->valuestring, "music_list") == 0 ||
             strcmp(action_node->valuestring, "refresh_play_url") == 0)) {
            ty_cJSON *resp_data = general ? general : custom;
            OPERATE_RET dispatch_rt = wukong_playback_ctrl_dispatch_response(
                action_node->valuestring, resp_data);
            if (dispatch_rt != OPRT_OK) {
                TAL_PR_WARN("PlayControl %s dispatch failed (no pending req?)",
                            action_node->valuestring);
            }
        } else {
#endif
            WUKONG_AI_MUSIC_T *music = NULL;
            if ((rt = wukong_ai_parse_playcontrol(root, &music)) == 0) {
                wukong_ai_parse_music_dump(music);
                wukong_ai_play_music(music);
                wukong_ai_parse_music_free(music);
            }
#if defined(ENABLE_TOOLKITS_PLAYBACK) && (ENABLE_TOOLKITS_PLAYBACK == 1)
        }
#endif
    } else {
        TAL_PR_NOTICE("skill %s not handled", code);
        // TAL_PR_NOTICE("skill content %s ", ty_cJSON_PrintUnformatted(root));

        wukong_ai_event_notify(WUKONG_AI_EVENT_SKILL, root);
    }

    return OPRT_OK; 
}

OPERATE_RET __wukong_ai_asr_process(AI_TEXT_TYPE_E type, ty_cJSON *root, BOOL_T eof)
{
    // ty_cJSON *data = ty_cJSON_GetObjectItem(root, "data");
    // TUYA_CHECK_NULL_RETURN(data, OPRT_INVALID_PARM);
    CHAR_T *content =  ty_cJSON_GetStringValue(root);
    TAL_PR_NOTICE("wukong text -> ASR result: %s", content);
    STATIC UINT8_T cmd_data[4] = {0};
    STATIC UINT8_T name_str[31] = {0};
    char* start_cnt = NULL;
    char* end_cnt = NULL;

    if((strstr((const char*)content,"开启小夜灯")!=NULL)||(strstr((const char*)content,"打开小夜灯")!=NULL))
    {
        TAL_PR_NOTICE("get cmd:开启小夜灯");
        // flag_rgb_bit = 1;
        cmd_data[0] = 10;
        cmd_data[1] = 1;
        tal_queue_post(s_queue_voice_cmd, &cmd_data, 0); 
    }
    else if(strstr((const char*)content,"关闭小夜灯")!=NULL)
    {
        TAL_PR_NOTICE("get cmd:关闭小夜灯");
        cmd_data[0] = 10;
        cmd_data[1] = 2;
        tal_queue_post(s_queue_voice_cmd, &cmd_data, 0);
    }

    else if((strstr((const char*)content,"向右转")!=NULL)||(strstr((const char*)content,"向右旋转")!=NULL))
    {
        TAL_PR_NOTICE("get cmd:向右转");
        cmd_data[0] = 1;
        cmd_data[1] = 1;
        tal_queue_post(s_queue_voice_cmd, &cmd_data, 0);
    }
    else if((strstr((const char*)content,"向左转")!=NULL)||(strstr((const char*)content,"向左旋转")!=NULL))
    {
        TAL_PR_NOTICE("get cmd:向左转");
        cmd_data[0] = 1;
        cmd_data[1] = 2;
        tal_queue_post(s_queue_voice_cmd, &cmd_data, 0);
    }    
    else if((strstr((const char*)content,"向后转")!=NULL)||(strstr((const char*)content,"向后旋转")!=NULL))
    {
        TAL_PR_NOTICE("get cmd:向后转");
        cmd_data[0] = 1;
        cmd_data[1] = 3;
        tal_queue_post(s_queue_voice_cmd, &cmd_data, 0);
    }
    else if((strstr((const char*)content,"坐着")!=NULL)||(strstr((const char*)content,"坐下")!=NULL)||(strstr((const char*)content,"做下")!=NULL)||(strstr((const char*)content,"趴下")!=NULL))
    {
        TAL_PR_NOTICE("get cmd:坐下");
        cmd_data[0] = 1;
        cmd_data[1] = 6;
        tal_queue_post(s_queue_voice_cmd, &cmd_data, 0);
    }
    else if(strstr((const char*)content,"站起来")!=NULL)
    {
        TAL_PR_NOTICE("get cmd:站起来");
        cmd_data[0] = 1;
        cmd_data[1] = 7;
        tal_queue_post(s_queue_voice_cmd, &cmd_data, 0);
    }
    else if((strstr((const char*)content,"不要说话")!=NULL)||(strstr((const char*)content,"别说话")!=NULL)||(strstr((const char*)content,"闭嘴")!=NULL)||(strstr((const char*)content,"安静")!=NULL))
    {
        TAL_PR_NOTICE("get cmd:stop talk");
        cmd_data[0] = 1;
        cmd_data[1] = 8;
        tal_queue_post(s_queue_voice_cmd, &cmd_data, 0);
    }
    else if((strstr((const char*)content,"摇摇头")!=NULL)||(strstr((const char*)content,"摇一摇")!=NULL))
    {
        TAL_PR_NOTICE("get cmd:摇摇头");
        cmd_data[0] = 1;
        cmd_data[1] = 9;
        tal_queue_post(s_queue_voice_cmd, &cmd_data, 0);
    }
    else if((strstr((const char*)content,"蹲下")!=NULL)||(strstr((const char*)content,"蹲着")!=NULL)||(strstr((const char*)content,"跪下")!=NULL)||(strstr((const char*)content,"跪着")!=NULL))
    {
        TAL_PR_NOTICE("get cmd:蹲下");
        cmd_data[0] = 1;
        cmd_data[1] = 10;
        tal_queue_post(s_queue_voice_cmd, &cmd_data, 0);
    }
    else if((strstr((const char*)content,"跳个舞")!=NULL)||(strstr((const char*)content,"跳一个舞")!=NULL)||(strstr((const char*)content,"跳舞")!=NULL))
    {
        TAL_PR_NOTICE("get cmd:跳个舞");
        cmd_data[0] = 1;
        cmd_data[1] = 11;
        tal_queue_post(s_queue_voice_cmd, &cmd_data, 0);
    }    
    // else if((strstr((const char*)content,"开启设备")!=NULL)||(strstr((const char*)content,"打开设备")!=NULL))
    // {
    //     TAL_PR_NOTICE("get cmd:打开设备");
    //     cmd_data[0] = 2;
    //     cmd_data[1] = 1;
    //     tal_queue_post(s_queue_voice_cmd, &cmd_data, 0);
    // }
    else if(strstr((const char*)content,"关闭设备")!=NULL)
    {
        TAL_PR_NOTICE("get cmd:关闭设备");
        cmd_data[0] = 2;
        cmd_data[1] = 2;
        tal_queue_post(s_queue_voice_cmd, &cmd_data, 0);
    }
    else if((strstr((const char*)content,"我是")!=NULL)||(strstr((const char*)content,"我叫")!=NULL))
    {
        cmd_data[0] = 11;
        cmd_data[1] = 1;
        tal_queue_post(s_queue_voice_cmd, &cmd_data, 0);
        // strncpy((const char*)content,"",);
        // TAL_PR_NOTICE("len=%d [%d %d]  data=%.2X %.2X %.2X   %.2X %.2X %.2X  %.2X %.2X %.2X",strlen((const char*)"。"),strlen((const char*)"，"),strlen((const char*)content),content[0],content[1],content[2],content[3],content[4],content[5],content[6],content[7],content[8]);

        //  使用UTF-8，每个汉字3个字节
        // name_str        
        memset(name_str,0x00,31);

        start_cnt = strstr((const char*)content,"我是");
        if (start_cnt == NULL)
        {
            start_cnt = strstr((const char*)content,"我叫");
        }        
        if(start_cnt != NULL)
        {
            end_cnt = strstr((const char*)content,"，");
            if (end_cnt == NULL)
            {
                end_cnt = strstr((const char*)content,"。");
            }
            if (end_cnt == NULL)
            {
                end_cnt = strlen((const char*)content);
            }
            start_cnt += 6;
            if((end_cnt > start_cnt)&&(end_cnt-start_cnt <= 30))
            {
                strncpy(name_str,start_cnt,end_cnt-start_cnt);
                TAL_PR_NOTICE("cnt=[%d, %d]  str=%s",start_cnt,end_cnt ,(const char*)name_str);
                tal_queue_post(s_queue_name_str, &name_str, 0);
            }
        }     
    }
    // else
    // {
    // //     cmd_data[0] = 0xFF;
    // //     tal_queue_post(s_queue_voice_cmd, &cmd_data, 0);
    //     if (tal_queue_fetch(s_queue_state, &get_offon_state, 100) == OPRT_OK)
    //     {
    //         TAL_PR_NOTICE("------------------get_offon_state=%d------------------",get_offon_state);
    //     }
    // }
    
    // if(get_offon_state == 2)
    // {
    //     // tuya_ai_input_start(TRUE);
    //     // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text(""));
    //     // tuya_ai_input_stop();
    //     return OPRT_OK;
    // }

    
    // send data to register cb
    WUKONG_AI_TEXT_T text;
    text.data      = content;
    text.datalen   = strlen(content);
    text.timeindex = 0;
    wukong_ai_event_notify((0 == strlen(content))?WUKONG_AI_EVENT_ASR_EMPTY:WUKONG_AI_EVENT_ASR_OK, &text);
    return OPRT_OK;
}

//{"bizId":"micro_chat_vdevo176101510735192_1764153315615","bizType":"NLG","eof":0,
//"data":{"content":"😆","reasoningContent":"","appendMode":"append","timeIndex":400,"finish":false,"tags":"U+1F606"}
OPERATE_RET __wukong_ai_nlg_process(AI_TEXT_TYPE_E type, ty_cJSON *root, BOOL_T eof)
{
    CHAR_T *json_str = ty_cJSON_PrintUnformatted(root);
    TAL_PR_NOTICE("json-str %s", json_str);
    tal_free(json_str);

    // ty_cJSON *time = ty_cJSON_GetObjectItem(root, "timeIndex");
    CHAR_T *content = ty_cJSON_GetStringValue(ty_cJSON_GetObjectItem(root, "content"));
    if (!content) {
        content = "";
    }

    WUKONG_AI_TEXT_T text;
    text.data      = content;
    text.datalen   = strlen(content);
    BOOL_T has_content = (text.datalen > 0);
    // text.timeindex = time ? time->valueint : 0;
    TAL_PR_NOTICE("wukong text -> NLG eof: %d, content: %s, time: %d", eof, content, text.timeindex);

    // send data to register cb
    STATIC WUKONG_AI_EVENT_TYPE_E event_type = WUKONG_AI_EVENT_TEXT_STREAM_STOP;
    TAL_PR_DEBUG("nlg stream: eof=%d len=%u break=%d prev=%d",
                 eof, text.datalen, __s_chat_break, event_type);
    if (__s_chat_break) {   // restart after chat break
        if (has_content) {
            TAL_PR_DEBUG("nlg stream -> START after break, len=%u", text.datalen);
            wukong_ai_event_notify(WUKONG_AI_EVENT_TEXT_STREAM_START, &text);
        }
        __s_chat_break = FALSE;
        if (eof) {
            if (has_content) {
                TAL_PR_DEBUG("nlg stream -> STOP after break, len=%u", text.datalen);
                wukong_ai_event_notify(WUKONG_AI_EVENT_TEXT_STREAM_STOP, &text);
            } else {
                TAL_PR_DEBUG("nlg stream -> ignore empty eof after break");
            }
            /* event_type stays STREAM_STOP */
        } else if (has_content) {
            event_type = WUKONG_AI_EVENT_TEXT_STREAM_DATA;
        }
    } else if (event_type == WUKONG_AI_EVENT_TEXT_STREAM_STOP) {
        if (has_content) {
            TAL_PR_DEBUG("nlg stream -> START, len=%u", text.datalen);
            wukong_ai_event_notify(WUKONG_AI_EVENT_TEXT_STREAM_START, &text);
        }
        if (eof) {
            if (has_content) {
                TAL_PR_DEBUG("nlg stream -> STOP single-packet, len=%u", text.datalen);
                wukong_ai_event_notify(WUKONG_AI_EVENT_TEXT_STREAM_STOP, &text);
            } else {
                TAL_PR_DEBUG("nlg stream -> ignore empty single-packet eof");
            }
            /* event_type stays STREAM_STOP */
        } else if (has_content) {
            event_type = WUKONG_AI_EVENT_TEXT_STREAM_DATA;
        }
    } else {
        if (event_type == WUKONG_AI_EVENT_TEXT_STREAM_DATA) {
            TAL_PR_DEBUG("nlg stream -> %s, len=%u",
                         eof ? "STOP" : "DATA", text.datalen);
            wukong_ai_event_notify(eof?WUKONG_AI_EVENT_TEXT_STREAM_STOP:WUKONG_AI_EVENT_TEXT_STREAM_DATA, &text);
            event_type = eof?WUKONG_AI_EVENT_TEXT_STREAM_STOP:WUKONG_AI_EVENT_TEXT_STREAM_DATA;
        }
    }

    // emtion
    WUKONG_AI_EMO_T emo;
    ty_cJSON *tags_array = ty_cJSON_GetObjectItem(root, "tags");
    if (tags_array && ty_cJSON_IsArray(tags_array) && ty_cJSON_GetArraySize(tags_array) > 0) {
        CHAR_T *emoji = ty_cJSON_GetStringValue(ty_cJSON_GetArrayItem(tags_array, 0));
        if (emoji && strlen(emoji)) {
            emo.emoji = emoji;
            emo.name = wukong_emoji_get_name(emoji);
            wukong_ai_event_notify(WUKONG_AI_EVENT_EMOTION, &emo);
        }
    }    

    return OPRT_OK;
}

VOID  wukong_ai_text_process(AI_TEXT_TYPE_E type, ty_cJSON *root, BOOL_T eof)
{
    switch (type)
    {
    case AI_TEXT_SKILL:
        __wukong_ai_skill_process(type, root, eof);
        break;
    case AI_TEXT_ASR:
        __wukong_ai_asr_process(type, root, eof);
        break;
    case AI_TEXT_NLG:
        __wukong_ai_nlg_process(type, root, eof);
        break;
    case AI_TEXT_CLOUD_EVENT:
        wukong_ai_parse_cloud_event(root);
        break;
    default:
        break;
    }
}

VOID wukong_skill_notify_chat_break()
{
    __s_chat_break = TRUE;
}
