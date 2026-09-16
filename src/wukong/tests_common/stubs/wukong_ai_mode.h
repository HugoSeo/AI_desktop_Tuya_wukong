#ifndef __WUKONG_AI_MODE_H__
#define __WUKONG_AI_MODE_H__

#include "tuya_cloud_types.h"

typedef enum {
    AI_CHAT_SUB_HOLD,
    AI_CHAT_SUB_ONESHOT,
    AI_CHAT_SUB_WAKEUP,
    AI_CHAT_SUB_FREE,
    AI_CHAT_SUB_MAX,
} AI_CHAT_SUB_MODE_E;

OPERATE_RET wukong_ai_chat_sub_mode_switch(AI_CHAT_SUB_MODE_E sub);

#endif
