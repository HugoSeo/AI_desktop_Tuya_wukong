#ifndef __WUKONG_AI_AGENT_H__
#define __WUKONG_AI_AGENT_H__

#include "tuya_cloud_types.h"

typedef enum {
    WUKONG_AI_EVENT_CLOCK_MCP_COUNTDOWN_TIMER = 65,
    WUKONG_AI_EVENT_CLOCK_MCP_STOPWATCH_TIMER = 66,
    WUKONG_AI_EVENT_CLOCK_MCP_POMODORO_TIMER  = 67,
    WUKONG_AI_EVENT_CLOCK_MCP_ALARM           = 68,
} WUKONG_AI_EVENT_TYPE_E;

VOID wukong_ai_event_notify(WUKONG_AI_EVENT_TYPE_E type, VOID *data);
OPERATE_RET wukong_ai_agent_send_text(CONST CHAR_T *text);

#endif
