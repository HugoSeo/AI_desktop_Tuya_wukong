#include "wukong_ai_skills.h"
#include "wukong_audio_player.h"
#include "skill_music_story.h"
#include "tal_memory.h"
#include "tal_log.h"
#include <stdio.h>


STATIC VOID _music_src_free(WUKONG_AI_MUSIC_SRC_T *music)
{
    if (!music)
        return;

    if (music->url) {
        tal_free(music->url);
    }

    if (music->artist) {
        tal_free(music->artist);
    }

    if (music->song_name) {
        tal_free(music->song_name);
    }

    if (music->audio_id) {
        tal_free(music->audio_id);
    }

    if (music->img_url) {
        tal_free(music->img_url);
    }

    tal_free(music);
}

STATIC AI_AUDIO_CODEC_E _parse_get_codec_type(CHAR_T *format)
{
    AI_AUDIO_CODEC_E fmt = AI_AUDIO_CODEC_MAX;

    if (strcmp(format, "mp3") == 0) {
        fmt = AI_AUDIO_CODEC_MP3;
    } else {
        TAL_PR_ERR("decode type invald:%s", format);
    }

    return fmt;
}

STATIC OPERATE_RET _parse_music_item(UINT_T id, ty_cJSON *audio_item, WUKONG_AI_MUSIC_SRC_T *music_src)
{
    ty_cJSON *item = NULL;

    if ((item = ty_cJSON_GetObjectItem(audio_item, "id")) != NULL) {
        music_src->id = item->valueint;
    }

    if ((item = ty_cJSON_GetObjectItem(audio_item, "url")) != NULL) {
        music_src->url = mm_strdup(item->valuestring);
        if (music_src->url == NULL) {
            TAL_PR_ERR("no memory, strdup failed");
            return -1;
        }
    } else {
        TAL_PR_ERR("the data is error");
        return -1;
    }

    if ((item = ty_cJSON_GetObjectItem(audio_item, "size")) != NULL) {
        music_src->length = item->valueint;
    }

    if ((item = ty_cJSON_GetObjectItem(audio_item, "duration")) != NULL) {
        music_src->duration = item->valueint;
    }

    if ((item = ty_cJSON_GetObjectItem(audio_item, "format")) != NULL) {
        music_src->format = _parse_get_codec_type(item->valuestring);
        if (music_src->format == AI_AUDIO_CODEC_MAX) {
            TAL_PR_ERR("the format not support");
            return -1;
        }
    } else {
        TAL_PR_ERR("the data is error");
        return -1;
    }

    if ((item = ty_cJSON_GetObjectItem(audio_item, "artist")) != NULL) {
        music_src->artist = mm_strdup(item->valuestring);
    }

    if ((item = ty_cJSON_GetObjectItem(audio_item, "name")) != NULL) {
        music_src->song_name = mm_strdup(item->valuestring);
    }

    if ((item = ty_cJSON_GetObjectItem(audio_item, "audioId")) != NULL) {
        music_src->audio_id = mm_strdup(item->valuestring);
    }

    if ((item = ty_cJSON_GetObjectItem(audio_item, "imageUrl")) != NULL) {
        music_src->img_url = mm_strdup(item->valuestring);
    }

    return 0;
}

OPERATE_RET wukong_ai_parse_music(ty_cJSON *json, WUKONG_AI_MUSIC_T **music)
{
    INT_T audio_num = 0;
    ty_cJSON *skill_general = ty_cJSON_GetObjectItem(json, "general");
    ty_cJSON *skill_custom = ty_cJSON_GetObjectItem(json, "custom");
    ty_cJSON *action = NULL, *skill_data = NULL, *audios = NULL, *node = NULL;
    WUKONG_AI_MUSIC_SRC_T *music_src;
    WUKONG_AI_MUSIC_T *music_ptr;

    if (skill_custom && (action = ty_cJSON_GetObjectItem(skill_custom, "action"))) {
        skill_data = ty_cJSON_GetObjectItem(skill_custom, "data");
        if (skill_data) {
            if ((audios = ty_cJSON_GetObjectItem(skill_data, "audios")) != NULL) {
                audio_num = ty_cJSON_GetArraySize(audios);
            }
        }
    }

    if (action == NULL && skill_general) {
        action = ty_cJSON_GetObjectItem(skill_general, "action");
        skill_data = ty_cJSON_GetObjectItem(skill_general, "data");
        if (skill_data) {
            if ((audios = ty_cJSON_GetObjectItem(skill_data, "audios")) != NULL) {
                audio_num = ty_cJSON_GetArraySize(audios);
            }
        } else {
            TAL_PR_WARN("no skill data");
        }
    }

    if (action == NULL || (strcmp(action->valuestring, "play") == 0 && audio_num == 0)) {
        TAL_PR_WARN("the music list not exsit:%d", audio_num);
        return -1;
    }

    music_ptr = tal_malloc(sizeof(WUKONG_AI_MUSIC_T));
    if (music_ptr == NULL) {
        TAL_PR_ERR("malloc arr fail.");
        return OPRT_MALLOC_FAILED;
    }

    memset(music_ptr, 0, sizeof(WUKONG_AI_MUSIC_T));
    music_ptr->src_cnt = audio_num;
    if ((node = ty_cJSON_GetObjectItem(skill_data, "preTtsFlag")) != NULL) {
        if (node->type == ty_cJSON_True) {
            music_ptr->has_tts = TRUE;
        }
    }

    if (action && strlen(action->valuestring) > 0) {
        snprintf(music_ptr->action, sizeof(music_ptr->action), "%s", action->valuestring);
    } else {
        snprintf(music_ptr->action, sizeof(music_ptr->action), "play");
    }

    if (audio_num != 0) {
        music_ptr->src_array = tal_malloc(sizeof(WUKONG_AI_MUSIC_SRC_T) * audio_num);
        if (music_ptr->src_array == NULL) {
            TAL_PR_ERR("malloc arr fail.");
            wukong_ai_parse_music_free(music_ptr);
            return OPRT_MALLOC_FAILED;
        }

        memset(music_ptr->src_array, 0, sizeof(WUKONG_AI_MUSIC_SRC_T) * audio_num);

        INT_T i = 0;
        for (i = 0; i < music_ptr->src_cnt; i++) {
            music_src = &music_ptr->src_array[i];
            node = ty_cJSON_GetArrayItem(audios, i);
            if (_parse_music_item(i, node, music_src) != OPRT_OK) {
                TAL_PR_ERR("parse audio %d fail.", i);
                wukong_ai_parse_music_free(music_ptr);
                return OPRT_CJSON_PARSE_ERR;
            }
        }

        // dump music
        for (i = 0; i < music_ptr->src_cnt; i++) {
            music_src = &music_ptr->src_array[i];
            TAL_PR_DEBUG("music[%d]: %s, %s, %s, %s, %s, %s, %d", i, music_src->artist, music_src->song_name, music_src->url, music_src->audio_id, music_src->img_url, music_src->format, music_src->duration);
        }
    }
    *music = music_ptr;
    return OPRT_OK;
}

VOID wukong_ai_parse_music_free(WUKONG_AI_MUSIC_T *music)
{
    if (music == NULL) {
        return;
    }

    if (music->src_array) {
        _music_src_free(music->src_array);
    }

    tal_free(music);
}

VOID wukong_ai_parse_music_dump(WUKONG_AI_MUSIC_T *music)
{
    if (music == NULL) {
        TAL_PR_WARN("can not dump media info");
        return;
    }

    INT_T i = 0;
    WUKONG_AI_MUSIC_SRC_T *media_src = NULL;

    TAL_PR_INFO("media info: has tts:%d, action:%s, count:%d", 
        music->has_tts, music->action, music->src_cnt);

    for (i = 0; i < music->src_cnt; i++) {
        media_src = &music->src_array[i];
        TAL_PR_INFO("  id:%d", media_src->id);
        TAL_PR_INFO("  url:%s", media_src->url ? media_src->url : "NULL");
        TAL_PR_INFO("  length:%lld", media_src->length);
        TAL_PR_INFO("  duration:%lld", media_src->duration);
        TAL_PR_INFO("  format:%d", media_src->format);
        TAL_PR_INFO("  artist:%s", media_src->artist ? media_src->artist : "NULL");
        TAL_PR_INFO("  song_name:%s", media_src->song_name ? media_src->song_name : "NULL");
        TAL_PR_INFO("  audio_id:%s", media_src->audio_id  ? media_src->audio_id : "NULL");
        TAL_PR_INFO("  img_url:%s\n", media_src->img_url  ? media_src->img_url : "NULL");
    }

    if(media_src != NULL)
    {
        OPERATE_RET rt;

        WUKONG_MUSIC_PLAYER_T music_msg = {0};
        music_msg.cmd = MUSIC_PLAYER_DATA;

        if(media_src->song_name)
        {
            strncpy(music_msg.song_name, media_src->song_name, sizeof(music_msg.song_name)-1);
        }
        if(media_src->artist)
        {
            strncpy(music_msg.artist, media_src->artist, sizeof(music_msg.artist)-1);
        }
        if(media_src->url)
        {
            strncpy(music_msg.song_url, media_src->url, sizeof(music_msg.song_url)-1);
        }

        TAL_PR_DEBUG("[%s] cmd: %d, song: %s, artist: %s", __FUNCTION__, music_msg.cmd, music_msg.song_name, music_msg.artist);

        TUYA_CALL_ERR_LOG(ty_publish_event(EVENT_MUSIC_PLAYER, &music_msg));
    }
}

static OPERATE_RET __parse_playcontrol_data(ty_cJSON *data, ty_cJSON *skill_data, WUKONG_AI_MUSIC_T **music)
{
    if (skill_data == NULL) {
        return -1;
    }

    ty_cJSON *action = ty_cJSON_GetObjectItem(skill_data, "action");
    if (action == NULL || strlen(action->valuestring) <= 0) {
        return -1;
    }

    if (strcmp(action->valuestring, "next") == 0 || 
        strcmp(action->valuestring, "prev") == 0 || 
        strcmp(action->valuestring, "play") == 0 ||
        strcmp(action->valuestring, "stop") == 0 ) {
        return wukong_ai_parse_music(data, music);
    }

    WUKONG_AI_MUSIC_T *media = tal_malloc(sizeof(WUKONG_AI_MUSIC_T));
    if (media == NULL) {
        TAL_PR_ERR("malloc arr fail.");
        return OPRT_MALLOC_FAILED;
    }

    memset(media, 0, sizeof(WUKONG_AI_MUSIC_T));
    snprintf(media->action, sizeof(media->action), "%s", action->valuestring);

    *music = media;

    return 0;
}

OPERATE_RET wukong_ai_parse_playcontrol(ty_cJSON *json, WUKONG_AI_MUSIC_T **music)
{
    ty_cJSON *skill_general = ty_cJSON_GetObjectItem(json, "general");
    ty_cJSON *skill_custom = ty_cJSON_GetObjectItem(json, "custom");

    if (__parse_playcontrol_data(json, skill_custom, music) != 0) {
        return __parse_playcontrol_data(json, skill_general, music);
    }

    return 0;
}

VOID wukong_ai_play_music(WUKONG_AI_MUSIC_T *music)
{
    if (strcmp(music->action, "play") == 0 && music->src_cnt > 0) {
        wukong_audio_play_music(music);
    } else if (strcmp(music->action, "resume") == 0) {
        wukong_ai_event_notify(WUKONG_AI_EVENT_PLAY_CTL_RESUME, NULL);
    } else if (strcmp(music->action, "stop") == 0) {
        wukong_ai_event_notify(WUKONG_AI_EVENT_PLAY_CTL_PAUSE, NULL);
    } else if (strcmp(music->action, "replay") == 0) {
        wukong_ai_event_notify(WUKONG_AI_EVENT_PLAY_CTL_REPLAY, NULL);
    } else if (strcmp(music->action, "prev") == 0 || strcmp(music->action, "next") == 0) {
        if (music->src_cnt > 0) {
            wukong_audio_play_music(music);
        } else {
            // play tts
        }
    } else if (strcmp(music->action, "single_loop") == 0) {
        wukong_ai_event_notify(WUKONG_AI_EVENT_PLAY_CTL_SINGLE_LOOP, NULL);
    } else if (strcmp(music->action, "sequential_loop") == 0) {
        wukong_ai_event_notify(WUKONG_AI_EVENT_PLAY_CTL_SEQUENTIAL_LOOP, NULL);
    } else if (strcmp(music->action, "no_loop") == 0) {
        wukong_ai_event_notify(WUKONG_AI_EVENT_PLAY_CTL_SEQUENTIAL, NULL);
    } else {
        TAL_PR_WARN("unknown action:%s", music->action);
    }

    return;
}