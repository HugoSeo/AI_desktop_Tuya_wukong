#ifndef __WUKONG_AUDIO_PLAYER_H__
#define __WUKONG_AUDIO_PLAYER_H__

#include "tuya_cloud_types.h"

typedef enum {
    AI_TOY_ALERT_TYPE_POWER_ON = 0,
    AI_TOY_ALERT_TYPE_NOT_ACTIVE = 1,
} TY_AI_TOY_ALERT_TYPE_E;

OPERATE_RET wukong_audio_player_alert(TY_AI_TOY_ALERT_TYPE_E type, BOOL_T send_eof);

#endif
