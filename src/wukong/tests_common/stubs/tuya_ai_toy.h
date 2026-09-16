#ifndef __TUYA_AI_TOY_H__
#define __TUYA_AI_TOY_H__

#include "tuya_cloud_types.h"
#include "wukong_ai_mode.h"

AI_CHAT_SUB_MODE_E tuya_ai_toy_trigger_mode_get(VOID);
VOID tuya_ai_toy_trigger_mode_set(AI_CHAT_SUB_MODE_E mode);

OPERATE_RET tuya_ai_toy_volume_set(UINT8_T value);

#endif
