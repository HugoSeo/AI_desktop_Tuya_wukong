#include "tuya_app_config.h"
#include "ui_services.h"
#include "ui_svc_fs.h"
#include "ui_svc_weather.h"
#include "ui_svc_dev_ctrl.h"
#include "ui_svc_call.h"
#include "ui_svc_picture.h"
#include "ui_svc_music.h"
#include "ui_svc_recording.h"
#include "ui_svc_transcribe.h"
#include "ui_svc_camera.h"
#include "ui_svc_video.h"
#include "ui_svc_video_playback.h"
#include "ui_svc_detection.h"
#include "ui_svc_tm.h"
#include "ui_svc_audio_diag.h"
#include "ui_svc_wlan.h"
#include "ui_svc_activate.h"
#include "ui_svc_ota.h"

void ui_services_init(void)
{
    /* Arm the unified app filesystem first — other services persist under it.
     * Mount ownership lives in wukong_storage; ui_fs builds its tree once that
     * volume reports ready (event-driven, see ui_svc_fs.h). Mount failure is
     * non-fatal (features that need fs just degrade). Only armed when at least
     * one fs-backed feature is on. */
#if (defined(UI_FEATURE_CAMERA) && UI_FEATURE_CAMERA)    || \
    (defined(UI_FEATURE_VIDEO) && UI_FEATURE_VIDEO)      || \
    (defined(UI_FEATURE_MUSIC) && UI_FEATURE_MUSIC)      || \
    (defined(UI_FEATURE_RECORDING) && UI_FEATURE_RECORDING) || \
    (defined(UI_FEATURE_FILES) && UI_FEATURE_FILES)
    ui_fs_init();
#endif

    ui_svc_dev_ctrl_load();
    ui_svc_wlan_init();
    ui_svc_activate_init();
    ui_svc_ota_init();
#if defined(UI_FEATURE_WEATHER) && UI_FEATURE_WEATHER
    ui_svc_weather_init();
#endif
#if defined(UI_FEATURE_CALL) && UI_FEATURE_CALL
    ui_svc_call_init();
#endif
#if defined(UI_FEATURE_CAMERA) && UI_FEATURE_CAMERA
    ui_svc_picture_init();
    ui_svc_camera_init();
#endif
#if defined(UI_FEATURE_VIDEO) && UI_FEATURE_VIDEO
    ui_svc_video_init();
    ui_svc_video_playback_init();
#endif
#if defined(UI_FEATURE_MUSIC) && UI_FEATURE_MUSIC
    ui_svc_music_init();
#endif
#if defined(UI_FEATURE_RECORDING) && UI_FEATURE_RECORDING
    ui_svc_recording_init();
    ui_svc_transcribe_init();   /* 起常驻转写轮询线程（阻塞态，按需唤醒） */
#endif
#if defined(UI_FEATURE_DETECTION) && UI_FEATURE_DETECTION
    ui_svc_detection_init();
#endif
#if defined(UI_FEATURE_TIME) && UI_FEATURE_TIME
    ui_svc_tm_init();           /* time-management isolation: observer + count sync */
#endif
}
