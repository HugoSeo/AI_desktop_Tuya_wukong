#include "wukong_fc.h"
#include "wukong_audio_player.h"
#if defined(ENABLE_TUYA_PICTURE) && (ENABLE_TUYA_PICTURE == 1)
#include "wukong_picture_output.h"
#endif
#if defined(ENABLE_TOOLKITS_PLAYBACK) && (ENABLE_TOOLKITS_PLAYBACK == 1)
#include "wukong_playback_ctrl.h"
#endif
#include "fc_emotion.h"
#include "fc_music_story.h"
#include "fc_cloudevent.h"
#include "wukong_tool.h"
#include "tal_log.h"
#include "tal_memory.h"
#include "ty_cJSON.h"
#include <stdio.h>

//Add by Hugo
#include "tal_queue.h"

QUEUE_HANDLE  s_queue_voice_cmd;
QUEUE_HANDLE  s_queue_state;
QUEUE_HANDLE  s_queue_name_str;

/* NLG 流状态：STOP=空闲(下一个有内容的包发 START)，DATA=流进行中。
 * 对话被打断(chat break)时直接复位为 STOP，保证打断后的新回复
 * 一定以 START 开始，不会被 UI 拼接到旧气泡上。 */
STATIC WUKONG_AI_EVENT_TYPE_E __s_nlg_stream_state = WUKONG_AI_EVENT_TEXT_STREAM_STOP;

/* fc 执行体：注册到底座(WUKONG_TOOL_FC)，由 __wukong_ai_skill_process 经
 * wukong_tool_exec 分发到这里。side effect：不填 out_content，返回底层 rt。 */
STATIC OPERATE_RET __fc_music_handler(CONST CHAR_T *name, CONST ty_cJSON *args,
                                      ty_cJSON **out_content, VOID *ud)
{
    OPERATE_RET rt = OPRT_OK;
    ty_cJSON *root = (ty_cJSON *)args;
    WUKONG_AI_MUSIC_T *music = NULL;

    (VOID)name; (VOID)out_content; (VOID)ud;

    if ((rt = wukong_ai_parse_music(root, &music)) == OPRT_OK) {
        wukong_ai_parse_music_dump(music);
        wukong_ai_play_music(music);
        wukong_ai_parse_music_free(music);
    }

    return rt;
}

STATIC OPERATE_RET __fc_playcontrol_handler(CONST CHAR_T *name, CONST ty_cJSON *args,
                                            ty_cJSON **out_content, VOID *ud)
{
    OPERATE_RET rt = OPRT_OK;
    ty_cJSON *root = (ty_cJSON *)args;

    (VOID)name; (VOID)out_content; (VOID)ud;

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
        rt = wukong_playback_ctrl_dispatch_response(
            action_node->valuestring, resp_data);
        if (rt != OPRT_OK) {
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

    return rt;
}

STATIC OPERATE_RET __fc_cloudevent_handler(CONST CHAR_T *name, CONST ty_cJSON *args,
                                           ty_cJSON **out_content, VOID *ud)
{
    (VOID)name; (VOID)out_content; (VOID)ud;
    return wukong_ai_parse_cloud_event((ty_cJSON *)args);
}

OPERATE_RET wukong_fc_init(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;
    TUYA_CALL_ERR_LOG(WUKONG_TOOL_ADD("music",       "cloud FC: music/story playlist",  __fc_music_handler,       NULL, WUKONG_TOOL_FC));
    TUYA_CALL_ERR_LOG(WUKONG_TOOL_ADD("story",       "cloud FC: story playlist",        __fc_music_handler,       NULL, WUKONG_TOOL_FC));
    TUYA_CALL_ERR_LOG(WUKONG_TOOL_ADD("PlayControl", "cloud FC: play control response", __fc_playcontrol_handler, NULL, WUKONG_TOOL_FC));
    TUYA_CALL_ERR_LOG(WUKONG_TOOL_ADD("cloud_event", "cloud FC: clock/alert/call task",  __fc_cloudevent_handler,  NULL, WUKONG_TOOL_FC));
    return rt;
}

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

    rt = wukong_tool_exec(WUKONG_TOOL_FC, code, root, NULL);   /* 未注册 code → NOT_FOUND */
    if (rt == OPRT_NOT_FOUND) {
        TAL_PR_NOTICE("skill %s not handled", code);
        /* 未注册的 skill code 仍需原样转发给模式层（wukong_ai_mode_*.c 消费该事件） */
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
    
    //Added by Hugo
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
    TAL_PR_DEBUG("nlg stream: eof=%d len=%u prev=%d",
                 eof, text.datalen, __s_nlg_stream_state);
    if (__s_nlg_stream_state == WUKONG_AI_EVENT_TEXT_STREAM_STOP) {
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
            /* __s_nlg_stream_state stays STREAM_STOP */
        } else if (has_content) {
            __s_nlg_stream_state = WUKONG_AI_EVENT_TEXT_STREAM_DATA;
        }
        /* 无内容且非 eof(如仅携带 tags/情绪的空帧)：不发事件、保持 STOP，
         * 继续等待首个有内容的包再发 START */
    } else {
        if (__s_nlg_stream_state == WUKONG_AI_EVENT_TEXT_STREAM_DATA) {
            TAL_PR_DEBUG("nlg stream -> %s, len=%u",
                         eof ? "STOP" : "DATA", text.datalen);
            wukong_ai_event_notify(eof?WUKONG_AI_EVENT_TEXT_STREAM_STOP:WUKONG_AI_EVENT_TEXT_STREAM_DATA, &text);
            __s_nlg_stream_state = eof?WUKONG_AI_EVENT_TEXT_STREAM_STOP:WUKONG_AI_EVENT_TEXT_STREAM_DATA;
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

VOID  wukong_fc_process(AI_TEXT_TYPE_E type, ty_cJSON *root, BOOL_T eof)
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
        (VOID_T)wukong_tool_exec(WUKONG_TOOL_FC, "cloud_event", root, NULL);
        break;
    default:
        break;
    }
}

VOID wukong_fc_notify_chat_break()
{
    /* 打断即认为上一条 NLG 流已结束，复位流状态；
     * 新回复的首个有内容包会重新发 TEXT_STREAM_START，UI 另起新气泡 */
    __s_nlg_stream_state = WUKONG_AI_EVENT_TEXT_STREAM_STOP;
}
