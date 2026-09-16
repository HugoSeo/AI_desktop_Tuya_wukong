#pragma once
/* Host stub for wukong_fc.h — the real header pulls heavy device deps
 * (tuya_ai_output/smart_frame/svc_ai_player) that don't compile on host. The
 * agent loop only needs the WUKONG_AI_TEXT_T shape. */
#include "tuya_cloud_types.h"
typedef struct {
    CHAR_T *data;
    UINT16_T datalen;
    UINT32_T timeindex;
} WUKONG_AI_TEXT_T;
