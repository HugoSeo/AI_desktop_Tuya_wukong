#include "ui_feature.h"
#include "tuya_app_config.h"   /* UI_FEATURE_* 宏 */

bool ui_feature_available(ui_feature_id_t f)
{
    switch (f) {
    case UI_FEATURE_ID_NONE: return true;   /* 核心功能 */
#if defined(UI_FEATURE_CAMERA) && UI_FEATURE_CAMERA
    case UI_FEATURE_ID_CAMERA: return true;
#endif
#if defined(UI_FEATURE_VIDEO) && UI_FEATURE_VIDEO
    case UI_FEATURE_ID_VIDEO: return true;
#endif
#if defined(UI_FEATURE_MUSIC) && UI_FEATURE_MUSIC
    case UI_FEATURE_ID_MUSIC: return true;
#endif
#if defined(UI_FEATURE_RECORDING) && UI_FEATURE_RECORDING
    case UI_FEATURE_ID_RECORDING: return true;
#endif
#if defined(UI_FEATURE_TIME) && UI_FEATURE_TIME
    case UI_FEATURE_ID_TIME: return true;
#endif
#if defined(UI_FEATURE_DETECTION) && UI_FEATURE_DETECTION
    case UI_FEATURE_ID_DETECTION: return true;
#endif
#if defined(UI_FEATURE_CALL) && UI_FEATURE_CALL
    case UI_FEATURE_ID_CALL: return true;
#endif
#if defined(UI_FEATURE_FILES) && UI_FEATURE_FILES
    case UI_FEATURE_ID_FILES: return true;
#endif
#if defined(UI_FEATURE_AUDIO_DIAG) && UI_FEATURE_AUDIO_DIAG
    case UI_FEATURE_ID_AUDIO_DIAG: return true;
#endif
#if defined(UI_FEATURE_WEATHER) && UI_FEATURE_WEATHER
    case UI_FEATURE_ID_WEATHER: return true;
#endif
#if defined(ENABLE_BATTERY) && (ENABLE_BATTERY == 1)
    case UI_FEATURE_ID_BATTERY: return true;
#endif
    default: break;
    }
    return false;
}
