#ifndef __SKILL_MUSIC_STORY_H__
#define __SKILL_MUSIC_STORY_H__

#include "tuya_cloud_types.h"
#include "ty_cJSON.h"
#include "svc_ai_player.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    UINT_T                      id;
    CHAR_T                      *url;
    INT64_T                     length;
    INT64_T                     duration;
    AI_AUDIO_CODEC_E            format;
    CHAR_T                      *artist;
    CHAR_T                      *song_name;
    CHAR_T                      *audio_id;
    CHAR_T                      *img_url;
} WUKONG_AI_MUSIC_SRC_T;

typedef struct {
    CHAR_T                      action[32];     // play/next/prev/resume/
    BOOL_T                      has_tts;        // 需要tts播放完后才能播放media
    INT_T                       src_cnt;
    WUKONG_AI_MUSIC_SRC_T       *src_array;
} WUKONG_AI_MUSIC_T;

OPERATE_RET wukong_ai_parse_music(ty_cJSON *json, WUKONG_AI_MUSIC_T **media);
VOID wukong_ai_parse_music_free(WUKONG_AI_MUSIC_T *media);
VOID wukong_ai_parse_music_dump(WUKONG_AI_MUSIC_T *music);
VOID wukong_ai_play_music(WUKONG_AI_MUSIC_T *music);
OPERATE_RET wukong_ai_parse_playcontrol(ty_cJSON *json, WUKONG_AI_MUSIC_T **music);

#ifdef __cplusplus
}
#endif

#endif  /* __SKILL_MUSIC_STORY_H__ */
