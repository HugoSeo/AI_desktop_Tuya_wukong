/**
 * @file ui_svc_stubs.c
 * @brief 功能簇关闭时，为核心页（home/chat/settings）仍引用的可选服务函数
 *        提供 no-op/false stub，使核心页无需 #if 即可链接并优雅降级。
 *
 * 每块用 #if !(UI_FEATURE_X) 守护，与真服务文件被 local.mk 剔除的条件严格互补，
 * 功能开启时本块编空（真服务提供符号），不重复定义。
 */
#include "tuya_app_config.h"   /* UI_FEATURE_* */
#include "ui_svc_camera.h"
#include "ui_svc_video.h"
#include "ui_svc_picture.h"
#include "ui_svc_weather.h"
#include "ui_svc_call.h"
#include "ui_svc_tm.h"
#include "ui_svc_audio_diag.h"

/* ---- CAMERA (camera + picture) ---- */
#if !(defined(UI_FEATURE_CAMERA) && UI_FEATURE_CAMERA)
bool ui_svc_camera_available(void) { return false; }

void ui_svc_picture_free_rgb565(void *buf) { (void)buf; }
void ui_svc_picture_set_view_cb(ui_picture_cb_t on_view) { (void)on_view; }
void ui_svc_picture_view_request(const char *name) { (void)name; }
bool ui_svc_picture_take_pending_attachment(ui_picture_t *out) { (void)out; return false; }
void ui_svc_picture_recognize_current(ui_picture_ai_done_cb_t cb) { if (cb) cb(false); }
void ui_svc_picture_generate_from_current(ui_picture_ai_done_cb_t cb) { if (cb) cb(false); }
#endif

/* ---- VIDEO ---- */
#if !(defined(UI_FEATURE_VIDEO) && UI_FEATURE_VIDEO)
void ui_svc_video_init(void) {}
bool ui_svc_video_available(void) { return false; }
void ui_svc_video_set_cb(ui_svc_video_cb_t cb) { (void)cb; }
ui_svc_video_state_t ui_svc_video_get_state(void) { return UI_VIDEO_STATE_IDLE; }
uint32_t ui_svc_video_get_elapsed_ms(void) { return 0; }
void ui_svc_video_record_toggle(void) {}
void ui_svc_video_record_stop(void) {}
#endif

/* ---- WEATHER ---- */
#if !(defined(UI_FEATURE_WEATHER) && UI_FEATURE_WEATHER)
void ui_svc_weather_set_cb(ui_svc_weather_cb_t cb) { (void)cb; }
bool ui_svc_weather_available(void) { return false; }
const ui_svc_weather_t *ui_svc_weather_get(void)
{
    static const ui_svc_weather_t empty = {0};   /* 调用方可能解引用，不能返 NULL */
    return &empty;
}
#endif

/* ---- CALL（call_available 已删除，不补 stub；仅页面仍引用的子接口） ---- */
#if !(defined(UI_FEATURE_CALL) && UI_FEATURE_CALL)
bool ui_svc_call_enabled_get(void) { return false; }
void ui_svc_call_enabled_set(bool on) { (void)on; }
void ui_svc_call_contact_delete_async(const char *dev_id, ui_svc_call_contact_delete_cb_t cb)
{
    (void)dev_id;
    if (cb) cb(UI_SVC_CALL_CONTACT_DELETE_FAILED);
}
void ui_svc_call_contact_apply_uuid_async(const char *uuid,
                                          ui_svc_call_contact_apply_cb_t cb)
{
    (void)uuid;
    if (cb) cb(UI_SVC_CALL_CONTACT_APPLY_FAILED);
}
bool ui_svc_call_auto_answer_get(void) { return false; }
void ui_svc_call_auto_answer_set(bool on) { (void)on; }
#endif

/* ---- TIME ---- */
#if !(defined(UI_FEATURE_TIME) && UI_FEATURE_TIME)
int ui_svc_tm_alarm_list(char **json) { if (json) *json = (void *)0; return 1; }  /* 非 OPRT_OK */
void ui_svc_tm_json_free(char *json) { (void)json; }
int ui_svc_tm_countdown_query(ui_svc_tm_countdown_t *snap)
{
    if (snap) snap->active = false;
    return 1;
}
int ui_svc_tm_stopwatch_query(ui_svc_tm_stopwatch_t *snap)
{
    if (snap) snap->active = false;
    return 1;
}
int ui_svc_tm_pomodoro_query(ui_svc_tm_pomodoro_t *snap)
{
    if (snap) snap->active = false;
    return 1;
}
void ui_svc_tm_clock_tab_request(ui_svc_tm_clock_tab_t tab) { (void)tab; }
ui_svc_tm_clock_tab_t ui_svc_tm_clock_tab_take(void)
{
    return UI_SVC_TM_CLOCK_TAB_NONE;
}
#endif

/* ---- AUDIO_DIAG ---- */
#if !(defined(UI_FEATURE_AUDIO_DIAG) && UI_FEATURE_AUDIO_DIAG)
bool ui_svc_audio_diag_available(void) { return false; }
#endif
