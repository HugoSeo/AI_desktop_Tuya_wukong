/**
 * @file ui_svc_video.c
 * @brief UI-facing local AVI recording service.
 *
 * Recorder start/stop and SD I/O run on WORKQ_SYSTEM. State notifications are
 * marshalled to the UI thread; this file never touches LVGL.
 */

#include "ui_svc_video.h"
#include "ui_svc_fs.h"
#include "ui_app.h"
#include "ty_avi_recorder.h"
#include "wukong_video_service.h"

#include "tal_memory.h"
#include "tal_mutex.h"
#include "tal_system.h"
#include "tal_thread.h"
#include "tal_time_service.h"
#include "tal_workq_service.h"
#include "uni_log.h"

#include <stdio.h>
#include <string.h>

#define UI_VIDEO_PATH_MAX 192
#define UI_VIDEO_RECORD_FPS 15
#define UI_VIDEO_NAME_ATTEMPTS 1000u

typedef struct {
    ui_svc_video_state_t state;
    ui_svc_video_error_t error;
} ui_video_notify_t;

typedef enum {
    UI_VIDEO_STOP_REASON_NONE = 0,
    UI_VIDEO_STOP_REASON_USER,
    UI_VIDEO_STOP_REASON_STORAGE,
} ui_video_stop_reason_t;

static MUTEX_HANDLE s_lock;
static ui_svc_video_state_t s_state = UI_VIDEO_STATE_IDLE;
static ui_svc_video_cb_t s_cb;
static bool s_stop_requested;
static bool s_stop_work_queued;
static ui_video_stop_reason_t s_stop_reason;
static THREAD_HANDLE s_emergency_stop_th;
static uint32_t s_start_tick;

static void stop_work(void *arg);
static void publish_state(ui_svc_video_state_t state, ui_svc_video_error_t error);

static OPERATE_RET request_stop_work(ui_video_stop_reason_t reason,
                                     bool allow_emergency_thread);

static void recorder_event_cb(TY_AVI_RECORDER_EVENT_E event, void *user_data)
{
    OPERATE_RET rt;
    (void)user_data;

    if (event != TY_AVI_RECORDER_EVENT_STORAGE_FAULT) return;

    if (ui_svc_video_get_state() != UI_VIDEO_STATE_STOPPING) {
        publish_state(UI_VIDEO_STATE_STOPPING, UI_VIDEO_ERROR_NONE);
    }
    rt = request_stop_work(UI_VIDEO_STOP_REASON_STORAGE, true);
    if (rt != OPRT_OK) {
        /* The recorder callback cannot join its own recorder thread. Keep the
         * error actionable: record_toggle()/on_leave can retry the stop once
         * memory or workqueue pressure has recovered. */
        PR_ERR("video svc: could not dispatch storage-fault stop: %d", rt);
        publish_state(UI_VIDEO_STATE_ERROR, UI_VIDEO_ERROR_STORAGE);
    }
}

static void notify_on_ui(void *data)
{
    ui_video_notify_t *event = (ui_video_notify_t *)data;
    ui_svc_video_cb_t cb = s_cb;

    if (cb && event) cb(event->state, event->error);
    tal_free(event);
}

static void publish_state(ui_svc_video_state_t state, ui_svc_video_error_t error)
{
    ui_video_notify_t *event;

    if (s_lock) tal_mutex_lock(s_lock);
    s_state = state;
    if (state == UI_VIDEO_STATE_RECORDING && s_start_tick == 0) {
        s_start_tick = (uint32_t)tal_system_get_tick_count();
    } else if (state == UI_VIDEO_STATE_IDLE || state == UI_VIDEO_STATE_ERROR) {
        s_start_tick = 0;
    }
    if (s_lock) tal_mutex_unlock(s_lock);

    event = (ui_video_notify_t *)tal_malloc(sizeof(*event));
    if (!event) {
        PR_ERR("video svc: notify allocation failed");
        return;
    }
    event->state = state;
    event->error = error;
    ui_app_async_call(notify_on_ui, event);
}

static void stop_work(void *arg)
{
    OPERATE_RET rt;
    ui_video_stop_reason_t reason;
    ui_svc_video_state_t state_before;
    bool was_running;
    (void)arg;

    was_running = ty_avi_recorder_is_running() ? true : false;
    state_before = ui_svc_video_get_state();
    rt = ty_avi_recorder_stop();
    if (s_lock) tal_mutex_lock(s_lock);
    reason = s_stop_reason;
    s_stop_reason = UI_VIDEO_STOP_REASON_NONE;
    s_stop_work_queued = false;
    s_stop_requested = false;
    if (s_lock) tal_mutex_unlock(s_lock);

    if (reason == UI_VIDEO_STOP_REASON_STORAGE) {
        if (rt != OPRT_OK) {
            PR_ERR("video svc: storage-fault recorder stop failed: %d", rt);
        }
        publish_state(UI_VIDEO_STATE_ERROR, UI_VIDEO_ERROR_STORAGE);
    } else if (!was_running && state_before == UI_VIDEO_STATE_ERROR) {
        /* A stop requested while STARTING may already be queued when start
         * fails. Reap any partial core resources without hiding that error. */
        return;
    } else if (rt == OPRT_OK) {
        publish_state(UI_VIDEO_STATE_IDLE, UI_VIDEO_ERROR_NONE);
    } else {
        PR_ERR("video svc: recorder stop failed: %d", rt);
        publish_state(UI_VIDEO_STATE_ERROR, UI_VIDEO_ERROR_STOP);
    }
}

static void emergency_stop_thread(void *arg)
{
    THREAD_HANDLE self;
    (void)arg;

    /* A stop request may arrive while start_work still owns WORKQ_SYSTEM.
     * Wait for that worker to publish its terminal start state before touching
     * the recorder core, otherwise start and stop could mutate it together. */
    while (ui_svc_video_get_state() == UI_VIDEO_STATE_STARTING) {
        tal_system_sleep(5);
    }
    stop_work(NULL);

    if (s_lock) tal_mutex_lock(s_lock);
    self = s_emergency_stop_th;
    s_emergency_stop_th = NULL;
    if (s_lock) tal_mutex_unlock(s_lock);
    if (self) tal_thread_delete(self);
}

static OPERATE_RET request_stop_work(ui_video_stop_reason_t reason,
                                     bool allow_emergency_thread)
{
    THREAD_CFG_T cfg = {
        .priority = THREAD_PRIO_2,
        .stackDepth = 4096,
        .thrdname = "avi_rec_stop",
    };
    OPERATE_RET rt = OPRT_OK;

    if (!s_lock) return OPRT_RESOURCE_NOT_READY;

    tal_mutex_lock(s_lock);
    if (reason == UI_VIDEO_STOP_REASON_STORAGE ||
        s_stop_reason == UI_VIDEO_STOP_REASON_NONE) {
        s_stop_reason = reason;
    }
    if (s_stop_work_queued) {
        tal_mutex_unlock(s_lock);
        return OPRT_OK;
    }

    s_stop_work_queued = true;
    rt = tal_workq_schedule(WORKQ_SYSTEM, stop_work, NULL);
    if (rt != OPRT_OK && allow_emergency_thread && !s_emergency_stop_th) {
        rt = tal_thread_create_and_start(&s_emergency_stop_th, NULL, NULL,
                                         emergency_stop_thread, NULL, &cfg);
    }
    if (rt != OPRT_OK) {
        s_stop_work_queued = false;
    }
    tal_mutex_unlock(s_lock);
    return rt;
}

static OPERATE_RET build_unique_video_path(char *filename, size_t filename_len,
                                           char *path, size_t path_len)
{
    SYS_TICK_T timestamp = tal_time_get_posix_ms();
    uint32_t attempt;

    if (timestamp == 0) timestamp = tal_system_get_tick_count();
    for (attempt = 0; attempt < UI_VIDEO_NAME_ATTEMPTS; attempt++) {
        bool exists;
        int n;
        OPERATE_RET rt;

        if (attempt == 0) {
            n = snprintf(filename, filename_len, "wukong_video_%llu.avi",
                         (unsigned long long)timestamp);
        } else {
            n = snprintf(filename, filename_len, "wukong_video_%llu_%u.avi",
                         (unsigned long long)timestamp, (unsigned int)attempt);
        }
        if (n < 0 || (size_t)n >= filename_len) return OPRT_BUFFER_NOT_ENOUGH;

        rt = ui_fs_path(path, path_len, UI_FS_VIDEO, filename);
        if (rt != OPRT_OK) return rt;
        rt = ui_fs_app_exists(UI_FS_VIDEO, filename, &exists);
        if (rt != OPRT_OK) return rt;
        if (!exists) return OPRT_OK;
    }
    return OPRT_COM_ERROR;
}

static void remove_recording_files_best_effort(const char *filename)
{
    char index_name[80];
    bool exists;
    int n;

    if (ui_fs_app_exists(UI_FS_VIDEO, filename, &exists) == OPRT_OK && exists) {
        (void)ui_fs_remove_app(UI_FS_VIDEO, filename);
    }

    n = snprintf(index_name, sizeof(index_name), "%s.idx", filename);
    if (n < 0 || (size_t)n >= sizeof(index_name)) return;
    if (ui_fs_app_exists(UI_FS_VIDEO, index_name, &exists) == OPRT_OK && exists) {
        (void)ui_fs_remove_app(UI_FS_VIDEO, index_name);
    }
}

static void start_work(void *arg)
{
    char filename[64];
    char path[UI_VIDEO_PATH_MAX];
    TY_AVI_RECORDER_CFG_T cfg = {0};
    OPERATE_RET rt;
    bool stop_after_start;
    (void)arg;

    if (!ui_fs_ready()) {
        publish_state(UI_VIDEO_STATE_ERROR, UI_VIDEO_ERROR_STORAGE);
        return;
    }
    if (!wukong_video_available(VIDEO_FMT_MJPEG)) {
        publish_state(UI_VIDEO_STATE_ERROR, UI_VIDEO_ERROR_CAMERA);
        return;
    }

    rt = build_unique_video_path(filename, sizeof(filename), path, sizeof(path));
    if (rt != OPRT_OK) {
        PR_ERR("video svc: build unique path failed: %d", rt);
        publish_state(UI_VIDEO_STATE_ERROR, UI_VIDEO_ERROR_STORAGE);
        return;
    }

    cfg.file_path = path;
    cfg.fps = UI_VIDEO_RECORD_FPS;
    cfg.event_cb = recorder_event_cb;
    rt = ty_avi_recorder_start(&cfg);
    if (rt != OPRT_OK) {
        PR_ERR("video svc: recorder start failed: %d", rt);
        remove_recording_files_best_effort(filename);
        publish_state(UI_VIDEO_STATE_ERROR, UI_VIDEO_ERROR_START);
        return;
    }

    PR_NOTICE("video svc: recording %s", path);
    publish_state(UI_VIDEO_STATE_RECORDING, UI_VIDEO_ERROR_NONE);

    if (s_lock) tal_mutex_lock(s_lock);
    stop_after_start = s_stop_requested;
    if (s_lock) tal_mutex_unlock(s_lock);
    if (stop_after_start) {
        publish_state(UI_VIDEO_STATE_STOPPING, UI_VIDEO_ERROR_NONE);
        if (request_stop_work(UI_VIDEO_STOP_REASON_USER, false) != OPRT_OK) {
            /* Already on WORKQ_SYSTEM, so a direct stop is safe and closes the
             * schedule-failure race without creating a concurrent helper. */
            stop_work(NULL);
        }
    }
}

void ui_svc_video_init(void)
{
    s_cb = NULL;
    s_state = UI_VIDEO_STATE_IDLE;
    s_stop_requested = false;
    s_stop_work_queued = false;
    s_stop_reason = UI_VIDEO_STOP_REASON_NONE;
    s_emergency_stop_th = NULL;
    s_start_tick = 0;
    if (!s_lock && tal_mutex_create_init(&s_lock) != OPRT_OK) {
        PR_ERR("video svc: mutex init failed");
    }
}

bool ui_svc_video_available(void)
{
    return ui_fs_ready() && wukong_video_available(VIDEO_FMT_MJPEG);
}

void ui_svc_video_set_cb(ui_svc_video_cb_t cb)
{
    s_cb = cb;
}

ui_svc_video_state_t ui_svc_video_get_state(void)
{
    ui_svc_video_state_t state;

    if (s_lock) tal_mutex_lock(s_lock);
    state = s_state;
    if (s_lock) tal_mutex_unlock(s_lock);
    return state;
}

uint32_t ui_svc_video_get_elapsed_ms(void)
{
    uint32_t start;
    ui_svc_video_state_t state;

    if (s_lock) tal_mutex_lock(s_lock);
    start = s_start_tick;
    state = s_state;
    if (s_lock) tal_mutex_unlock(s_lock);
    if (!start || (state != UI_VIDEO_STATE_RECORDING && state != UI_VIDEO_STATE_STOPPING)) {
        return 0;
    }
    return (uint32_t)tal_system_get_tick_count() - start;
}

void ui_svc_video_record_toggle(void)
{
    ui_svc_video_state_t state = ui_svc_video_get_state();

    if (state == UI_VIDEO_STATE_RECORDING || ty_avi_recorder_is_running()) {
        ui_svc_video_record_stop();
        return;
    }
    if (state == UI_VIDEO_STATE_STARTING || state == UI_VIDEO_STATE_STOPPING) return;

    if (s_lock) tal_mutex_lock(s_lock);
    s_stop_requested = false;
    s_stop_reason = UI_VIDEO_STOP_REASON_NONE;
    if (s_lock) tal_mutex_unlock(s_lock);
    publish_state(UI_VIDEO_STATE_STARTING, UI_VIDEO_ERROR_NONE);
    if (tal_workq_schedule(WORKQ_SYSTEM, start_work, NULL) != OPRT_OK) {
        publish_state(UI_VIDEO_STATE_ERROR, UI_VIDEO_ERROR_START);
    }
}

void ui_svc_video_record_stop(void)
{
    ui_svc_video_state_t state;

    if (s_lock) tal_mutex_lock(s_lock);
    state = s_state;
    if (state == UI_VIDEO_STATE_STARTING) {
        s_stop_requested = true;
        if (s_lock) tal_mutex_unlock(s_lock);
        if (request_stop_work(UI_VIDEO_STOP_REASON_USER, true) != OPRT_OK) {
            PR_WARN("video svc: could not dispatch post-start stop; start worker will stop directly");
        }
        return;
    }
    if (s_lock) tal_mutex_unlock(s_lock);
    if (state != UI_VIDEO_STATE_RECORDING && !ty_avi_recorder_is_running()) return;

    publish_state(UI_VIDEO_STATE_STOPPING, UI_VIDEO_ERROR_NONE);
    if (request_stop_work(UI_VIDEO_STOP_REASON_USER, true) != OPRT_OK) {
        /* Recorder is still active; restore the actionable state so the user
         * can retry instead of orphaning a live recorder behind STOPPING. */
        publish_state(UI_VIDEO_STATE_RECORDING, UI_VIDEO_ERROR_STOP);
    }
}
