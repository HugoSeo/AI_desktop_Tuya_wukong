#ifndef __SKILL_CLOUDEVENT_H__
#define __SKILL_CLOUDEVENT_H__

#include "tuya_cloud_types.h"
#include "ty_cJSON.h"
#include "skill_music_story.h"
#include "tuya_ai_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WUKONG_AI_HTTP_GET,
    WUKONG_AI_HTTP_POST,
    WUKONG_AI_HTTP_PUT,
    WUKONG_AI_HTTP_INVALD
} WUKONG_AI_HTTP_METHOD_E;

typedef enum {
    WUKONG_AI_TASK_NORMAL = 0,     //music story ...
    WUKONG_AI_TASK_CLOCK = 1,
    WUKONG_AI_TASK_ALERT = 2,
    WUKONG_AI_TASK_RING_TONE = 3,
    WUKONG_AI_TASK_CALL = 4,
    WUKONG_AI_TASK_CALL_TTS = 5,
    WUKONG_AI_TASK_INVALD
} WUKONG_AI_TASK_TYPE_E;

typedef enum {
    WUKONG_AI_CODE_CLOUD_SENT_COMMAND,            //云端正确下发指令，被控制设备正常返回状态
    WUKONG_AI_CODE_CLOUD_NOT_SENT_COMMAND,        //云端识别了指令，但是没有下发指令
    WUKONG_AI_CODE_CLOUD_NOT_IDENTIFY,            //未识别语音
    WUKONG_AI_CODE_CLOUD_DEVICE_NOT_RESPONSE,     //云端下发了指令，被控制设备未返回状态
} WUKONG_AI_PROCESS_CODE_E;

typedef BYTE_T WUKONG_AI_TTS_TYPE_E;
#define WUKONG_AI_TTS_TYPE_NORMAL                0x00
#define WUKONG_AI_TTS_TYPE_ALERT                 0x01
#define WUKONG_AI_TTS_TYPE_CALL                  0x02

typedef struct {
    CHAR_T                          *url;
    CHAR_T                          *req_body;
    WUKONG_AI_HTTP_METHOD_E         http_method;
    AI_AUDIO_CODEC_E                format;
    WUKONG_AI_TTS_TYPE_E            tts_type;
    INT_T                           duration;
} WUKONG_AI_TTS_T;

typedef struct {
    WUKONG_AI_TTS_T      tts;
    WUKONG_AI_TTS_T      bg_music;
} WUKONG_AI_PLAYTTS_T;

OPERATE_RET wukong_ai_parse_cloud_event(ty_cJSON *json);


#ifdef __cplusplus
}
#endif

#endif  /* __SKILL_CLOUDEVENT_H__ */
