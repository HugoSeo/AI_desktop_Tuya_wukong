#ifndef __UI_FEATURE_H__
#define __UI_FEATURE_H__

#include <stdbool.h>

/* 功能簇运行时可用性判据（编译宏的唯一查询入口）。
 * 关闭的功能其页面/服务未编入；本函数让入口代码无需 #if 即可判断。 */
typedef enum {
    UI_FEATURE_ID_NONE = -1,   /* 哨兵：核心功能，永远可用 */
    UI_FEATURE_ID_CAMERA = 0,
    UI_FEATURE_ID_VIDEO,
    UI_FEATURE_ID_MUSIC,
    UI_FEATURE_ID_RECORDING,
    UI_FEATURE_ID_TIME,
    UI_FEATURE_ID_DETECTION,
    UI_FEATURE_ID_CALL,
    UI_FEATURE_ID_FILES,
    UI_FEATURE_ID_AUDIO_DIAG,
    UI_FEATURE_ID_WEATHER,
    UI_FEATURE_ID_BATTERY,     /* 设备电量检测能力（ENABLE_BATTERY），非 UI 裁剪簇 */
} ui_feature_id_t;

bool ui_feature_available(ui_feature_id_t f);

#endif /* __UI_FEATURE_H__ */
