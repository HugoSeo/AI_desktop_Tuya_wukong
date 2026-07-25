#include "skill_cloudevent.h"
#include "wukong_audio_player.h"
#include "svc_ai_player.h"
#include "tal_memory.h"
#include "tal_log.h"

static AI_AUDIO_CODEC_E __parse_get_codec_type(CHAR_T *format)
{
    AI_AUDIO_CODEC_E fmt = AI_AUDIO_CODEC_MP3;

    if (strcmp(format, "mp3") == 0) {
        fmt = AI_AUDIO_CODEC_MP3;
    } else if (strcmp(format, "wav") == 0) {
        fmt = AI_AUDIO_CODEC_WAV;
    } else if (strcmp(format, "speex") == 0) {
        fmt = AI_AUDIO_CODEC_SPEEX;
    } else if (strcmp(format, "opus") == 0) {
        fmt = AI_AUDIO_CODEC_OPUS;
    } else if (strcmp(format, "oggopus") == 0) {
        fmt = AI_AUDIO_CODEC_OGGOPUS;
    } else {
        TAL_PR_ERR("decode type invald:%s", format);
        fmt = AI_AUDIO_CODEC_MAX;
    }

    return fmt;
}

OPERATE_RET __wukong_ai_parse_playtts(CONST CHAR_T *action, ty_cJSON *json, WUKONG_AI_PLAYTTS_T **playtts)
{
    ty_cJSON *node = NULL;
    WUKONG_AI_PLAYTTS_T *playtts_ptr;

    playtts_ptr = tal_malloc(sizeof(WUKONG_AI_PLAYTTS_T));
    if (playtts_ptr == NULL) {
        TAL_PR_ERR("malloc arr fail.");
        return OPRT_MALLOC_FAILED;
    }

    memset(playtts_ptr, 0, sizeof(WUKONG_AI_PLAYTTS_T));

    ty_cJSON *tts = ty_cJSON_GetObjectItem(json, "tts");
    if (tts) {
        playtts_ptr->tts.tts_type = WUKONG_AI_TTS_TYPE_NORMAL;
        if (strcmp(action, "alert") == 0) {
            playtts_ptr->tts.tts_type = WUKONG_AI_TTS_TYPE_ALERT;
        }
        node = ty_cJSON_GetObjectItem(tts, "url");
        if (node) {
            playtts_ptr->tts.url = mm_strdup(node->valuestring);
        }

        node = ty_cJSON_GetObjectItem(tts, "requestBody");
        if (node) {
            playtts_ptr->tts.req_body = mm_strdup(node->valuestring);
        } else {
            playtts_ptr->tts.req_body = NULL;
        }

        playtts_ptr->tts.http_method = WUKONG_AI_HTTP_GET;
        node = ty_cJSON_GetObjectItem(tts, "requestType");
        if (node) {
            if (strcmp(node->valuestring, "post") == 0) {
                playtts_ptr->tts.http_method = WUKONG_AI_HTTP_POST;
            }
        }

        node = ty_cJSON_GetObjectItem(tts, "format");
        if (node) {
            playtts_ptr->tts.format = __parse_get_codec_type(node->valuestring);
        }
    }

    ty_cJSON *bgmusic = ty_cJSON_GetObjectItem(json, "bgMusic");
    if (bgmusic) {
        node = ty_cJSON_GetObjectItem(bgmusic, "url");
        if (node) {
            playtts_ptr->bg_music.url = mm_strdup(node->valuestring);
        }

        node = ty_cJSON_GetObjectItem(bgmusic, "requestBody");
        if (node) {
            playtts_ptr->bg_music.req_body = mm_strdup(node->valuestring);
        } else {
            playtts_ptr->bg_music.req_body = NULL;
        }
        playtts_ptr->bg_music.http_method = WUKONG_AI_HTTP_GET;
        node = ty_cJSON_GetObjectItem(bgmusic, "requestType");
        if (node) {
            if (strcmp(node->valuestring, "post") == 0) {
                playtts_ptr->bg_music.http_method = WUKONG_AI_HTTP_POST;
            }
        }

        node = ty_cJSON_GetObjectItem(bgmusic, "format");
        if (node) {
            playtts_ptr->bg_music.format = __parse_get_codec_type(node->valuestring);
        }

        node = ty_cJSON_GetObjectItem(bgmusic, "duration");
        if (node) {
            playtts_ptr->bg_music.duration = ty_cJSON_GetNumberValue(node);
        }
    }

    *playtts = playtts_ptr;
    return OPRT_OK;
}

OPERATE_RET __wukong_ai_parse_playtts_free(WUKONG_AI_PLAYTTS_T *playtts)
{
    if (!playtts) {
        return OPRT_OK;
    }

    if (playtts->tts.url) {
        tal_free(playtts->tts.url);
    }
    if (playtts->tts.req_body) {
        tal_free(playtts->tts.req_body);
    }
    if (playtts->bg_music.url) {
        tal_free(playtts->bg_music.url);
    }
    if (playtts->bg_music.req_body) {
        tal_free(playtts->bg_music.req_body);
    }

    tal_free(playtts);
    return OPRT_OK;
}

OPERATE_RET wukong_ai_parse_cloud_event(ty_cJSON *json)
{
    OPERATE_RET rt = OPRT_OK;
    ty_cJSON *action = ty_cJSON_GetObjectItem(json, "action");
    if (action && (strcmp(action->valuestring, "playTts") == 0 ||
                   strcmp(action->valuestring, "alert") == 0)) {
        ty_cJSON *data = ty_cJSON_GetObjectItem(json, "data");
        WUKONG_AI_PLAYTTS_T *playtts = NULL;
        if ((rt = __wukong_ai_parse_playtts(action->valuestring, data, &playtts)) == 0) {
            wukong_audio_play_tts_url(playtts, FALSE);
            // if (s_chat_cbc.tuya_ai_chat_playtts) {
            //     s_chat_cbc.tuya_ai_chat_playtts(playtts, s_chat_cbc.user_data);
            // }
            __wukong_ai_parse_playtts_free(playtts);
        }
        return rt;
    }

    return rt;
}