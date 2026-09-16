#ifndef __TUYA_AI_AGENT_H__
#define __TUYA_AI_AGENT_H__

#include "tuya_cloud_types.h"

VOID tuya_ai_input_start_s(CHAR_T *scode, BOOL_T force);
VOID tuya_ai_input_stop(void);
CHAR_T *tuya_ai_agent_get_scode(void *unused);

#define tuya_ai_input_start(force) tuya_ai_input_start_s(tuya_ai_agent_get_scode(NULL), force)

#endif
