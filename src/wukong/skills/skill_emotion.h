#ifndef __SKILL_EMOTION_H__
#define __SKILL_EMOTION_H__

#include "tuya_cloud_types.h"
#include "ty_cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    CONST CHAR_T  *emoji;
    CONST CHAR_T  *name;
} WUKONG_AI_EMO_T;

// emotion
CONST CHAR_T* wukong_emoji_get_name(CONST CHAR_T* emoji);
CONST CHAR_T* wukong_emoji_get_by_name(CONST CHAR_T* name);
INT_T wukong_emoji_unicode_to_utf8(CONST CHAR_T* unicode_str, CHAR_T* utf8_buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif  /* __SKILL_EMOTION_H__ */
