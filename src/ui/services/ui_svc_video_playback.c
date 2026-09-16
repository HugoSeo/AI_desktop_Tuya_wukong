/**
 * @file ui_svc_video_playback.c
 * @brief UI-facing local AVI list and playback service.
 *
 * Directory enumeration and path composition go exclusively through ui_svc_fs.
 * AVI open/close/control runs on WORKQ_SYSTEM; decoded frames are coalesced to
 * one pending buffer and marshalled to the UI thread.
 */

#include "ui_svc_video_playback.h"
#include "ui_svc_fs.h"
#include "ui_app.h"

#include "ty_avi_player.h"
#include "tuya_dma2d.h"
#include "wukong_audio_player.h"
#include "wukong_audio_output.h"
#include "tal_memory.h"
#include "tal_mutex.h"
#include "tal_semaphore.h"
#include "tal_thread.h"
#include "tal_queue.h"
#include "tal_workq_service.h"
#include "uni_log.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define UI_VIDEO_PLAYBACK_PATH_MAX 192
/* Audio out queue: 16kHz/16bit/mono is ~32KB/s. Keep at most ~200ms here;
 * the TAL output already owns another 4KB (~128ms) ring. A one-second UI
 * cushion makes audible audio continue long after video pauses and hides
 * growing output lag instead of correcting it. */
#define UI_VIDEO_AUDIO_Q_DEPTH       16
#define UI_VIDEO_AUDIO_Q_BYTE_BUDGET 6400u

typedef struct {
    uint8_t *data;
    uint32_t len;
} video_audio_item_t;

typedef struct {
    uint32_t generation;
    ui_video_playback_state_t state;
    ui_video_playback_error_t error;
} playback_state_event_t;

typedef struct {
    uint32_t generation;
    char name[UI_VIDEO_PLAYBACK_NAME_MAX];
} playback_start_work_t;

typedef struct {
    uint32_t generation;
    ui_video_playback_error_t completion_error;
} playback_finish_work_t;

typedef struct {
    uint32_t generation;
    ui_video_playback_state_t terminal_state;
    ui_video_playback_error_t error;
} playback_cleanup_request_t;

typedef struct {
    OPERATE_RET rt;
    uint32_t seq;
    uint16_t count;
    uint16_t deleted_count;
    uint16_t failed_count;
    char names[][UI_VIDEO_PLAYBACK_NAME_MAX];
} playback_delete_work_t;

static MUTEX_HANDLE s_lock;
/* Serializes complete player/audio lifecycle transitions. WORKQ_SYSTEM can
 * fall back to a helper thread for cleanup, so relying on work-queue ordering
 * alone is not sufficient to protect the player's process-global state. */
static MUTEX_HANDLE s_player_op_lock;
static ui_video_playback_state_t s_state = UI_VIDEO_PLAYBACK_IDLE;
static ui_video_playback_list_cb_t s_list_cb;
static ui_video_playback_frame_cb_t s_frame_cb;
static ui_video_playback_state_cb_t s_state_cb;
static ui_video_playback_delete_cb_t s_delete_cb;
static ui_video_playback_frame_t s_pending_frame;
static uint32_t s_pending_generation;
static bool s_frame_notify_queued;
static uint32_t s_generation;
static uint32_t s_position_ms;
static uint32_t s_duration_ms;
static bool s_delete_running;
static playback_finish_work_t s_finish_work;
static bool s_finish_work_queued;
static bool s_cleanup_pending;
static THREAD_HANDLE s_emergency_cleanup_th;
static playback_cleanup_request_t s_emergency_cleanup_req;
static bool s_emergency_cleanup_pending;

/* Audio out: fed by the player's decode thread (player_audio_cb, non-
 * blocking enqueue), drained by a dedicated thread that owns the one
 * blocking call (wukong_audio_output_write_owned) — same producer/consumer split
 * the AVI recorder uses for the opposite direction, and for the same
 * reason: the decode/pacing thread must never stall on a hardware write. */
static QUEUE_HANDLE s_audio_q;
static uint32_t s_queued_audio_bytes;
static THREAD_HANDLE s_audio_out_th;
static SEM_HANDLE s_audio_out_exit_sem;
static volatile uint8_t s_audio_out_running;
static uint8_t s_audio_output_started;
static uint32_t s_audio_drop_cnt;

static bool player_op_begin(void)
{
    if (!s_player_op_lock) {
        PR_ERR("video playback: operation mutex is unavailable");
        return false;
    }
    tal_mutex_lock(s_player_op_lock);
    return true;
}

static void player_op_end(void)
{
    tal_mutex_unlock(s_player_op_lock);
}

static bool generation_is_current(uint32_t generation)
{
    bool current;

    if (!s_lock) return false;
    tal_mutex_lock(s_lock);
    current = generation == s_generation;
    tal_mutex_unlock(s_lock);
    return current;
}

static bool has_avi_suffix(const char *name)
{
    size_t len;
    const char *ext;

    if (!name) return false;
    len = strlen(name);
    if (len < 4) return false;
    ext = name + len - 4;
    return ext[0] == '.' &&
           (ext[1] == 'a' || ext[1] == 'A') &&
           (ext[2] == 'v' || ext[2] == 'V') &&
           (ext[3] == 'i' || ext[3] == 'I');
}

static bool is_video_leaf_name(const char *name)
{
    return name && name[0] && strlen(name) < UI_VIDEO_PLAYBACK_NAME_MAX &&
           !strchr(name, '/') && !strchr(name, '\\') &&
           strcmp(name, ".") != 0 && strcmp(name, "..") != 0 &&
           has_avi_suffix(name);
}

static void remove_video_index_best_effort(const char *video_name)
{
    /* libavi appends ".idx" to the complete AVI path while recording, so
     * "foo.avi" is paired with "foo.avi.idx". The AVI is the user-visible
     * file and remains the primary deletion result; a stale sidecar must not
     * make an otherwise successful video deletion look like a failure. */
    char index_name[UI_VIDEO_PLAYBACK_NAME_MAX + sizeof(".idx")];
    int n = snprintf(index_name, sizeof(index_name), "%s.idx", video_name);

    if (n < 0 || (size_t)n >= sizeof(index_name)) {
        PR_WARN("video playback: index name too long for %s", video_name);
        return;
    }
    if (ui_fs_remove_app(UI_FS_VIDEO, index_name) != OPRT_OK) {
        PR_WARN("video playback: remove companion index %s failed", index_name);
    }
}

static void state_notify_ui(void *data)
{
    playback_state_event_t *event = (playback_state_event_t *)data;
    ui_video_playback_state_cb_t cb = NULL;

    if (!event) return;
    if (s_lock) tal_mutex_lock(s_lock);
    if (event->generation == s_generation) cb = s_state_cb;
    if (s_lock) tal_mutex_unlock(s_lock);
    if (cb) cb(event->state, event->error);
    tal_free(event);
}

static void publish_state(uint32_t generation, ui_video_playback_state_t state,
                          ui_video_playback_error_t error)
{
    playback_state_event_t *event;

    if (s_lock) tal_mutex_lock(s_lock);
    if (generation != s_generation) {
        if (s_lock) tal_mutex_unlock(s_lock);
        return;
    }
    s_state = state;
    if (s_lock) tal_mutex_unlock(s_lock);

    event = (playback_state_event_t *)tal_malloc(sizeof(*event));
    if (!event) return;
    event->generation = generation;
    event->state = state;
    event->error = error;
    ui_app_async_call(state_notify_ui, event);
}

static void frame_notify_ui(void *data)
{
    ui_video_playback_frame_t frame = {0};
    ui_video_playback_frame_cb_t cb = NULL;
    ui_video_playback_state_t state;
    uint32_t generation;
    (void)data;

    tal_mutex_lock(s_lock);
    frame = s_pending_frame;
    memset(&s_pending_frame, 0, sizeof(s_pending_frame));
    generation = s_pending_generation;
    s_pending_generation = 0;
    s_frame_notify_queued = false;
    state = s_state;
    if (generation == s_generation &&
        (state == UI_VIDEO_PLAYBACK_OPENING || state == UI_VIDEO_PLAYBACK_PLAYING ||
         state == UI_VIDEO_PLAYBACK_PAUSED)) {
        cb = s_frame_cb;
    }
    tal_mutex_unlock(s_lock);

    if (!frame.data) return;
    if (cb) {
        cb(&frame);
    } else {
        ty_avi_player_frame_free(frame.data);
    }
}

/* Runs on the player's decode thread (see player_sync_audio() in
 * ty_avi_player.c) — must not block. Copies and enqueues only; the blocking
 * DAC write happens on audio_out_thread below. */
static void player_audio_cb(const uint8_t *data, uint32_t len, void *user_data)
{
    uint32_t generation = (uint32_t)(uintptr_t)user_data;
    video_audio_item_t item = {0};

    if (!data || len == 0 || !s_audio_q) return;

    item.data = (uint8_t *)tal_malloc(len);
    if (!item.data) {
        s_audio_drop_cnt++;
        return;
    }
    memcpy(item.data, data, len);
    item.len = len;

    tal_mutex_lock(s_lock);
    if (generation != s_generation || s_state == UI_VIDEO_PLAYBACK_STOPPING ||
        s_state == UI_VIDEO_PLAYBACK_IDLE || s_queued_audio_bytes + len > UI_VIDEO_AUDIO_Q_BYTE_BUDGET) {
        s_audio_drop_cnt++;
        tal_mutex_unlock(s_lock);
        tal_free(item.data);
        return; /* stale session, or discard stale PCM to catch up */
    }
    /* Reserve before posting so the consumer cannot fetch/decrement the item
     * before the producer accounts for it. The count includes an item whose
     * blocking DAC write is currently in progress. */
    s_queued_audio_bytes += item.len;
    tal_mutex_unlock(s_lock);

    if (tal_queue_post(s_audio_q, &item, 0) != OPRT_OK) {
        tal_mutex_lock(s_lock);
        s_queued_audio_bytes = (s_queued_audio_bytes >= item.len) ?
                               (s_queued_audio_bytes - item.len) : 0;
        s_audio_drop_cnt++;
        tal_mutex_unlock(s_lock);
        tal_free(item.data);
        return;
    }
}

static void audio_out_thread(void *arg)
{
    video_audio_item_t item;
    (void)arg;

    while (s_audio_out_running) {
        if (tal_queue_fetch(s_audio_q, &item, 100) != OPRT_OK) continue;
        wukong_audio_output_write_owned(WUKONG_AUDIO_OUTPUT_OWNER_VIDEO,
                                        (UINT8_T *)item.data,
                                        (UINT_T)item.len);
        tal_mutex_lock(s_lock);
        s_queued_audio_bytes = (s_queued_audio_bytes >= item.len) ? (s_queued_audio_bytes - item.len) : 0;
        tal_mutex_unlock(s_lock);
        tal_free(item.data);
    }
    tal_semaphore_post(s_audio_out_exit_sem);
}

/* Only called once ty_avi_player_has_audio() confirms the file actually has
 * a track — a video-only file never touches the speaker or interrupts
 * TTS/music at all. */
static OPERATE_RET video_audio_out_start(void)
{
    THREAD_CFG_T thrd = {
        .priority = THREAD_PRIO_2,
        .stackDepth = 4096,
        .thrdname = "ty_avi_aout",
    };

    if (tal_queue_create_init(&s_audio_q, sizeof(video_audio_item_t), UI_VIDEO_AUDIO_Q_DEPTH) != OPRT_OK) {
        s_audio_q = NULL;
        return OPRT_MALLOC_FAILED;
    }
    s_queued_audio_bytes = 0;
    s_audio_drop_cnt = 0;
    if (tal_semaphore_create_init(&s_audio_out_exit_sem, 0, 1) != OPRT_OK) {
        tal_queue_free(s_audio_q);
        s_audio_q = NULL;
        return OPRT_MALLOC_FAILED;
    }

    /* Stop TTS/music, then acquire the shared DAC as the VIDEO owner. P2P may
     * preempt this owner; a later video cleanup therefore cannot close the
     * DAC underneath the call. */
    wukong_audio_player_stop(AI_PLAYER_ALL);
    if (wukong_audio_output_start_owned(WUKONG_AUDIO_OUTPUT_OWNER_VIDEO) != OPRT_OK) {
        tal_semaphore_release(s_audio_out_exit_sem);
        s_audio_out_exit_sem = NULL;
        tal_queue_free(s_audio_q);
        s_audio_q = NULL;
        return OPRT_COM_ERROR;
    }
    s_audio_output_started = 1;

    s_audio_out_running = 1;
    if (tal_thread_create_and_start(&s_audio_out_th, NULL, NULL,
                                    audio_out_thread, NULL, &thrd) != OPRT_OK) {
        s_audio_out_running = 0;
        wukong_audio_output_stop_owned(WUKONG_AUDIO_OUTPUT_OWNER_VIDEO);
        s_audio_output_started = 0;
        tal_semaphore_release(s_audio_out_exit_sem);
        s_audio_out_exit_sem = NULL;
        tal_queue_free(s_audio_q);
        s_audio_q = NULL;
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

/* Safe to call unconditionally (e.g. from every playback-stop path) even if
 * video_audio_out_start() was never called for this session. */
static void video_audio_out_stop(void)
{
    video_audio_item_t item;
    uint32_t dropped = s_audio_drop_cnt;

    if (s_audio_out_running) {
        s_audio_out_running = 0;
        /* Abort a write blocked on the TAL ring before waiting for the output
         * thread; stop() is explicitly responsible for waking that writer. */
        if (s_audio_output_started) {
            wukong_audio_output_stop_owned(WUKONG_AUDIO_OUTPUT_OWNER_VIDEO);
            s_audio_output_started = 0;
        }
        if (s_audio_out_exit_sem) tal_semaphore_wait(s_audio_out_exit_sem, SEM_WAIT_FOREVER);
    }
    if (s_audio_out_th) {
        tal_thread_delete(s_audio_out_th);
        s_audio_out_th = NULL;
    }
    if (s_audio_out_exit_sem) {
        tal_semaphore_release(s_audio_out_exit_sem);
        s_audio_out_exit_sem = NULL;
    }
    if (s_audio_q) {
        while (tal_queue_fetch(s_audio_q, &item, 0) == OPRT_OK) {
            tal_free(item.data);
        }
        tal_queue_free(s_audio_q);
        s_audio_q = NULL;
    }
    if (s_audio_output_started) {
        wukong_audio_output_stop_owned(WUKONG_AUDIO_OUTPUT_OWNER_VIDEO);
        s_audio_output_started = 0;
    }
    s_queued_audio_bytes = 0;
    s_audio_drop_cnt = 0;
    if (dropped > 0) {
        PR_WARN("video playback: discarded %u stale audio chunks to keep A/V sync", dropped);
    }
}

/* WORKQ_SYSTEM can reject a task when its fixed queue is full. Cleanup must
 * still happen without blocking the UI or decoder callback, so that rare
 * path gets a one-shot helper thread. The thread drains the latest request
 * and deletes itself; there is no steady-state stack cost. */
static void emergency_cleanup_thread(void *arg)
{
    THREAD_HANDLE self;
    (void)arg;

    for (;;) {
        playback_cleanup_request_t req;
        uint32_t position_ms;
        uint32_t duration_ms;

        tal_mutex_lock(s_lock);
        if (!s_emergency_cleanup_pending) {
            self = s_emergency_cleanup_th;
            s_emergency_cleanup_th = NULL;
            tal_mutex_unlock(s_lock);
            if (self) tal_thread_delete(self);
            return;
        }
        req = s_emergency_cleanup_req;
        s_emergency_cleanup_pending = false;
        tal_mutex_unlock(s_lock);

        if (!player_op_begin()) {
            tal_mutex_lock(s_lock);
            s_cleanup_pending = false;
            tal_mutex_unlock(s_lock);
            publish_state(req.generation, UI_VIDEO_PLAYBACK_ERROR,
                          UI_VIDEO_PLAYBACK_ERROR_CONTROL);
            continue;
        }
        if (!generation_is_current(req.generation)) {
            player_op_end();
            continue;
        }
        position_ms = ty_avi_player_get_position_ms();
        duration_ms = ty_avi_player_get_duration_ms();
        ty_avi_player_stop();
        video_audio_out_stop();
        player_op_end();

        tal_mutex_lock(s_lock);
        s_position_ms = position_ms;
        s_duration_ms = duration_ms;
        s_cleanup_pending = false;
        tal_mutex_unlock(s_lock);
        publish_state(req.generation, req.terminal_state, req.error);
    }
}

static bool request_emergency_cleanup(uint32_t generation,
                                      ui_video_playback_state_t terminal_state,
                                      ui_video_playback_error_t error)
{
    THREAD_CFG_T cfg = {
        .priority = THREAD_PRIO_2,
        .stackDepth = 4096,
        .thrdname = "avi_cleanup",
    };
    OPERATE_RET rt = OPRT_OK;

    if (!s_lock || !s_player_op_lock) return false;
    tal_mutex_lock(s_lock);
    s_emergency_cleanup_req.generation = generation;
    s_emergency_cleanup_req.terminal_state = terminal_state;
    s_emergency_cleanup_req.error = error;
    s_emergency_cleanup_pending = true;
    s_cleanup_pending = true;
    if (!s_emergency_cleanup_th) {
        rt = tal_thread_create_and_start(&s_emergency_cleanup_th, NULL, NULL,
                                         emergency_cleanup_thread, NULL, &cfg);
    }
    tal_mutex_unlock(s_lock);

    if (rt != OPRT_OK) {
        PR_ERR("video playback: emergency cleanup thread create failed: %d", rt);
        return false;
    }
    return true;
}

static void player_frame_cb(TY_AVI_PLAYER_FRAME_T *frame, void *user_data)
{
    uint32_t generation = (uint32_t)(uintptr_t)user_data;
    uint8_t *old = NULL;
    bool queue_notify = false;

    if (!frame || !frame->data) return;
    if (!s_lock) {
        ty_avi_player_frame_free(frame->data);
        frame->data = NULL;
        return;
    }

    tal_mutex_lock(s_lock);
    if (generation != s_generation || s_state == UI_VIDEO_PLAYBACK_STOPPING ||
        s_state == UI_VIDEO_PLAYBACK_IDLE) {
        tal_mutex_unlock(s_lock);
        ty_avi_player_frame_free(frame->data);
        frame->data = NULL;
        return;
    }

    old = s_pending_frame.data;
    s_pending_frame.data = frame->data;
    s_pending_frame.width = frame->width;
    s_pending_frame.height = frame->height;
    s_pending_frame.index = frame->index;
    s_pending_frame.pts_ms = frame->pts_ms;
    s_position_ms = frame->pts_ms;
    s_pending_generation = generation;
    frame->data = NULL;
    if (!s_frame_notify_queued) {
        s_frame_notify_queued = true;
        queue_notify = true;
    }
    tal_mutex_unlock(s_lock);

    if (old) ty_avi_player_frame_free(old);
    if (queue_notify) ui_app_async_call(frame_notify_ui, NULL);
}

static void finish_work(void *data)
{
    playback_finish_work_t work;
    uint32_t generation;
    uint32_t position_ms;
    uint32_t duration_ms;
    ui_video_playback_error_t error;
    (void)data;

    if (s_lock) tal_mutex_lock(s_lock);
    if (!s_finish_work_queued) {
        if (s_lock) tal_mutex_unlock(s_lock);
        return;
    }
    work = s_finish_work;
    s_finish_work_queued = false;
    if (s_lock) tal_mutex_unlock(s_lock);

    generation = work.generation;
    error = work.completion_error;

    if (!player_op_begin()) {
        if (s_lock) tal_mutex_lock(s_lock);
        s_cleanup_pending = false;
        if (s_lock) tal_mutex_unlock(s_lock);
        publish_state(generation, UI_VIDEO_PLAYBACK_ERROR,
                      UI_VIDEO_PLAYBACK_ERROR_CONTROL);
        return;
    }
    if (!generation_is_current(generation)) {
        player_op_end();
        return;
    }
    position_ms = ty_avi_player_get_position_ms();
    duration_ms = ty_avi_player_get_duration_ms();
    ty_avi_player_stop();
    video_audio_out_stop();
    player_op_end();
    if (s_lock) tal_mutex_lock(s_lock);
    s_position_ms = position_ms;
    s_duration_ms = duration_ms;
    s_cleanup_pending = false;
    if (s_lock) tal_mutex_unlock(s_lock);
    publish_state(generation,
                  error == UI_VIDEO_PLAYBACK_ERROR_NONE ?
                  UI_VIDEO_PLAYBACK_COMPLETED : UI_VIDEO_PLAYBACK_ERROR,
                  error);
}

static void player_event_cb(TY_AVI_PLAYER_EVENT_E event, void *user_data)
{
    uint32_t generation = (uint32_t)(uintptr_t)user_data;
    bool queue_finish = false;

    if (s_lock) tal_mutex_lock(s_lock);
    if (generation == s_generation && !s_finish_work_queued) {
        s_finish_work.generation = generation;
        s_finish_work.completion_error =
            event == TY_AVI_PLAYER_EVENT_COMPLETE ?
            UI_VIDEO_PLAYBACK_ERROR_NONE : UI_VIDEO_PLAYBACK_ERROR_DECODE;
        s_finish_work_queued = true;
        queue_finish = true;
    }
    if (s_lock) tal_mutex_unlock(s_lock);
    if (!queue_finish) return;

    publish_state(generation, UI_VIDEO_PLAYBACK_STOPPING,
                  UI_VIDEO_PLAYBACK_ERROR_NONE);
    if (tal_workq_schedule_instant(WORKQ_SYSTEM, finish_work, NULL) != OPRT_OK) {
        ui_video_playback_state_t terminal_state;
        ui_video_playback_error_t terminal_error;

        if (s_lock) tal_mutex_lock(s_lock);
        s_finish_work_queued = false;
        if (s_lock) tal_mutex_unlock(s_lock);
        terminal_state = event == TY_AVI_PLAYER_EVENT_COMPLETE ?
                         UI_VIDEO_PLAYBACK_COMPLETED : UI_VIDEO_PLAYBACK_ERROR;
        terminal_error = event == TY_AVI_PLAYER_EVENT_COMPLETE ?
                         UI_VIDEO_PLAYBACK_ERROR_NONE : UI_VIDEO_PLAYBACK_ERROR_DECODE;
        if (!request_emergency_cleanup(generation, terminal_state,
                                       terminal_error)) {
            /* The decoder callback cannot join its own player thread. Leave
             * cleanup pending so the next stop/replay can reap the session
             * under the lifecycle lock. */
            PR_ERR("video playback: cleanup deferred until next control request");
            publish_state(generation, UI_VIDEO_PLAYBACK_ERROR,
                          UI_VIDEO_PLAYBACK_ERROR_CONTROL);
        }
    }
}

static void start_work(void *data)
{
    playback_start_work_t *work = (playback_start_work_t *)data;
    TY_AVI_PLAYER_CFG_T cfg = {0};
    char path[UI_VIDEO_PLAYBACK_PATH_MAX];
    uint32_t generation;
    uint32_t duration_ms;
    OPERATE_RET rt;
    bool recovering;

    if (!work) return;
    generation = work->generation;
    if (!generation_is_current(generation)) {
        tal_free(work);
        return;
    }
    if (!ui_fs_ready()) {
        tal_free(work);
        publish_state(generation, UI_VIDEO_PLAYBACK_ERROR,
                      UI_VIDEO_PLAYBACK_ERROR_STORAGE);
        return;
    }
    rt = ui_fs_path(path, sizeof(path), UI_FS_VIDEO, work->name);
    tal_free(work);
    if (rt != OPRT_OK) {
        publish_state(generation, UI_VIDEO_PLAYBACK_ERROR,
                      UI_VIDEO_PLAYBACK_ERROR_STORAGE);
        return;
    }

    if (!player_op_begin()) {
        publish_state(generation, UI_VIDEO_PLAYBACK_ERROR,
                      UI_VIDEO_PLAYBACK_ERROR_CONTROL);
        return;
    }
    /* A stop/new play request may have superseded this queued start while it
     * waited behind another lifecycle transition. */
    if (!generation_is_current(generation)) {
        player_op_end();
        return;
    }

    /* Error recovery is idempotent. A previous completion/stop notification
     * may have failed to enter WORKQ_SYSTEM while the queue was full; reap
     * that exited or still-running session before opening the replay. */
    if (s_lock) tal_mutex_lock(s_lock);
    recovering = s_cleanup_pending;
    if (s_lock) tal_mutex_unlock(s_lock);
    if (recovering) PR_WARN("video playback: reaping an incomplete prior session");
    ty_avi_player_stop();
    video_audio_out_stop();
    if (s_lock) tal_mutex_lock(s_lock);
    s_cleanup_pending = false;
    if (s_lock) tal_mutex_unlock(s_lock);

    cfg.file_path = path;
    cfg.max_width = 320;
    cfg.max_height = 320;
    cfg.frame_cb = player_frame_cb;
    cfg.event_cb = player_event_cb;
    cfg.audio_cb = player_audio_cb;
    /* Arms the player's hardware JPEG path. The DMA2D engine is owned here (by
     * the app), not by the AVI module, so the converter is injected rather than
     * called from the player core; tuya_dma2d serialises it against the LVGL
     * flush path and falls back to software on its own if the engine is down. */
    cfg.yuv422_to_rgb565 = tuya_dma2d_yuv422_to_rgb565;
    cfg.start_paused = TRUE;
    cfg.user_data = (void *)(uintptr_t)generation;
    rt = ty_avi_player_start(&cfg);
    if (rt != OPRT_OK) {
        player_op_end();
        publish_state(generation, UI_VIDEO_PLAYBACK_ERROR,
                      UI_VIDEO_PLAYBACK_ERROR_OPEN);
        return;
    }

    if (!generation_is_current(generation)) {
        ty_avi_player_stop();
        video_audio_out_stop();
        player_op_end();
        return;
    }

    /* Only engage the speaker (and interrupt TTS/music for it) when the
     * file actually has an audio track — a video-only clip stays silent
     * and never touches the shared DAC. Non-fatal on failure: video still
     * plays, just without sound this session. */
    if (ty_avi_player_has_audio() && video_audio_out_start() != OPRT_OK) {
        PR_WARN("video playback: audio output setup failed, continuing video-only");
    }

    /* start_paused keeps the media clock at zero while the optional speaker
     * is prepared, so player_audio_cb cannot lose the first PCM chunk. */
    if (ty_avi_player_resume() != OPRT_OK) {
        ty_avi_player_stop();
        video_audio_out_stop();
        player_op_end();
        publish_state(generation, UI_VIDEO_PLAYBACK_ERROR,
                      UI_VIDEO_PLAYBACK_ERROR_CONTROL);
        return;
    }

    duration_ms = ty_avi_player_get_duration_ms();
    if (s_lock) tal_mutex_lock(s_lock);
    s_position_ms = 0;
    s_duration_ms = duration_ms;
    if (s_lock) tal_mutex_unlock(s_lock);
    if (!generation_is_current(generation)) {
        ty_avi_player_stop();
        video_audio_out_stop();
        player_op_end();
        return;
    }
    player_op_end();
    /* A tiny/corrupt file can finish on the decoder thread before start()
     * returns. Its event has already moved us to STOPPING; do not overwrite
     * that terminal transition with a late PLAYING notification. */
    if (ui_svc_video_playback_get_state() == UI_VIDEO_PLAYBACK_OPENING) {
        publish_state(generation, UI_VIDEO_PLAYBACK_PLAYING,
                      UI_VIDEO_PLAYBACK_ERROR_NONE);
    }
}

static void stop_work(void *data)
{
    uint32_t generation = (uint32_t)(uintptr_t)data;

    if (!player_op_begin()) {
        publish_state(generation, UI_VIDEO_PLAYBACK_ERROR,
                      UI_VIDEO_PLAYBACK_ERROR_CONTROL);
        return;
    }
    if (!generation_is_current(generation)) {
        player_op_end();
        return;
    }
    ty_avi_player_stop();
    video_audio_out_stop();
    player_op_end();
    if (s_lock) tal_mutex_lock(s_lock);
    s_cleanup_pending = false;
    s_position_ms = 0;
    s_duration_ms = 0;
    if (s_lock) tal_mutex_unlock(s_lock);
    publish_state(generation, UI_VIDEO_PLAYBACK_IDLE,
                  UI_VIDEO_PLAYBACK_ERROR_NONE);
}

static void pause_resume_work(void *data)
{
    uint32_t generation = (uint32_t)(uintptr_t)data;
    uint32_t position_ms;
    ui_video_playback_state_t state;
    OPERATE_RET rt;

    if (!generation_is_current(generation)) return;
    if (!player_op_begin()) {
        publish_state(generation, UI_VIDEO_PLAYBACK_ERROR,
                      UI_VIDEO_PLAYBACK_ERROR_CONTROL);
        return;
    }
    if (!generation_is_current(generation)) {
        player_op_end();
        return;
    }
    state = ui_svc_video_playback_get_state();
    if (state == UI_VIDEO_PLAYBACK_PLAYING) {
        rt = ty_avi_player_pause();
        if (rt == OPRT_OK) {
            position_ms = ty_avi_player_get_position_ms();
            if (s_lock) tal_mutex_lock(s_lock);
            s_position_ms = position_ms;
            if (s_lock) tal_mutex_unlock(s_lock);
            /* pause() synchronizes with player_audio_cb. Stop/reset both UI
             * and TAL queues so buffered sound cannot run past the frozen
             * video frame. Resume seeks both streams back to this position. */
            video_audio_out_stop();
            player_op_end();
            publish_state(generation, UI_VIDEO_PLAYBACK_PAUSED,
                          UI_VIDEO_PLAYBACK_ERROR_NONE);
            return;
        }
    } else if (state == UI_VIDEO_PLAYBACK_PAUSED) {
        if (ty_avi_player_has_audio() && video_audio_out_start() != OPRT_OK) {
            PR_WARN("video playback: audio resume setup failed, continuing video-only");
        }
        position_ms = ui_svc_video_playback_get_position_ms();
        rt = ty_avi_player_seek(position_ms);
        if (rt == OPRT_OK) rt = ty_avi_player_resume();
        if (rt == OPRT_OK) {
            player_op_end();
            publish_state(generation, UI_VIDEO_PLAYBACK_PLAYING,
                          UI_VIDEO_PLAYBACK_ERROR_NONE);
            return;
        }
        video_audio_out_stop();
    } else {
        player_op_end();
        return;
    }
    ty_avi_player_stop();
    video_audio_out_stop();
    player_op_end();
    if (s_lock) tal_mutex_lock(s_lock);
    s_cleanup_pending = false;
    if (s_lock) tal_mutex_unlock(s_lock);
    publish_state(generation, UI_VIDEO_PLAYBACK_ERROR,
                  UI_VIDEO_PLAYBACK_ERROR_CONTROL);
}

static void fs_list_cb(OPERATE_RET rt, const ui_fs_entry_t *entries,
                       uint16_t count, uint32_t seq, void *user)
{
    ui_video_playback_item_t *items = NULL;
    uint16_t out_count = 0;
    (void)user;

    if (rt == OPRT_OK && count > 0) {
        items = (ui_video_playback_item_t *)tal_malloc(
            sizeof(*items) * UI_VIDEO_PLAYBACK_LIST_MAX);
        if (!items) rt = OPRT_MALLOC_FAILED;
    }

    if (rt == OPRT_OK && items) {
        /* ui_fs sorts ascending; walk backwards so newest timestamp names lead. */
        for (uint16_t n = count; n > 0 && out_count < UI_VIDEO_PLAYBACK_LIST_MAX; n--) {
            const ui_fs_entry_t *entry = &entries[n - 1];
            if (entry->is_dir || !is_video_leaf_name(entry->name)) continue;
            memset(&items[out_count], 0, sizeof(items[out_count]));
            strncpy(items[out_count].name, entry->name,
                    sizeof(items[out_count].name) - 1);
            items[out_count].size = entry->size;
            out_count++;
        }
    }

    if (s_list_cb) s_list_cb(rt, items, out_count, seq);
    tal_free(items);
}

static void delete_notify_ui(void *data)
{
    playback_delete_work_t *result = (playback_delete_work_t *)data;
    ui_video_playback_delete_cb_t cb = NULL;

    if (!result) return;
    if (s_lock) tal_mutex_lock(s_lock);
    cb = s_delete_cb;
    if (s_lock) tal_mutex_unlock(s_lock);
    if (cb) {
        cb(result->rt, result->deleted_count, result->failed_count,
           result->seq);
    }
    tal_free(result);
}

static void delete_work(void *data)
{
    playback_delete_work_t *work = (playback_delete_work_t *)data;
    ui_video_playback_state_t state;

    if (!work) return;

    state = ui_svc_video_playback_get_state();
    if (!ui_fs_ready() || state != UI_VIDEO_PLAYBACK_IDLE) {
        work->rt = OPRT_COM_ERROR;
        work->failed_count += work->count;
    } else {
        for (uint16_t i = 0; i < work->count; i++) {
            if (ui_fs_remove_app(UI_FS_VIDEO, work->names[i]) == OPRT_OK) {
                remove_video_index_best_effort(work->names[i]);
                work->deleted_count++;
            } else {
                work->failed_count++;
            }
        }
        work->rt = work->failed_count == 0 ? OPRT_OK : OPRT_COM_ERROR;
    }

    if (s_lock) tal_mutex_lock(s_lock);
    s_delete_running = false;
    if (s_lock) tal_mutex_unlock(s_lock);
    ui_app_async_call(delete_notify_ui, work);
}

void ui_svc_video_playback_init(void)
{
    memset(&s_pending_frame, 0, sizeof(s_pending_frame));
    s_state = UI_VIDEO_PLAYBACK_IDLE;
    s_generation = 0;
    s_position_ms = 0;
    s_duration_ms = 0;
    s_delete_running = false;
    memset(&s_finish_work, 0, sizeof(s_finish_work));
    s_finish_work_queued = false;
    s_cleanup_pending = false;
    s_emergency_cleanup_th = NULL;
    memset(&s_emergency_cleanup_req, 0, sizeof(s_emergency_cleanup_req));
    s_emergency_cleanup_pending = false;
    if (!s_lock && tal_mutex_create_init(&s_lock) != OPRT_OK) {
        PR_ERR("video playback mutex init failed");
    }
    if (!s_player_op_lock && tal_mutex_create_init(&s_player_op_lock) != OPRT_OK) {
        PR_ERR("video playback operation mutex init failed");
    }
}

bool ui_svc_video_playback_available(void)
{
    return ui_fs_ready() && s_lock && s_player_op_lock;
}

void ui_svc_video_playback_set_cbs(ui_video_playback_list_cb_t list_cb,
                                   ui_video_playback_frame_cb_t frame_cb,
                                   ui_video_playback_state_cb_t state_cb,
                                   ui_video_playback_delete_cb_t delete_cb)
{
    uint8_t *pending = NULL;

    if (s_lock) tal_mutex_lock(s_lock);
    s_list_cb = list_cb;
    s_frame_cb = frame_cb;
    s_state_cb = state_cb;
    s_delete_cb = delete_cb;
    if (!frame_cb && s_pending_frame.data) {
        pending = s_pending_frame.data;
        memset(&s_pending_frame, 0, sizeof(s_pending_frame));
        s_pending_generation = 0;
    }
    if (s_lock) tal_mutex_unlock(s_lock);
    if (pending) ty_avi_player_frame_free(pending);
}

OPERATE_RET ui_svc_video_playback_delete_batch(const char *const names[],
                                                uint16_t count, uint32_t seq)
{
    playback_delete_work_t *work;
    OPERATE_RET rt;

    if (!names || count == 0) return OPRT_INVALID_PARM;
    if (count > UI_VIDEO_PLAYBACK_LIST_MAX) {
        count = UI_VIDEO_PLAYBACK_LIST_MAX;
    }

    work = (playback_delete_work_t *)tal_malloc(
        sizeof(*work) + (size_t)count * sizeof(work->names[0]));
    if (!work) return OPRT_MALLOC_FAILED;
    memset(work, 0, sizeof(*work) + (size_t)count * sizeof(work->names[0]));
    work->seq = seq;
    for (uint16_t i = 0; i < count; i++) {
        if (!is_video_leaf_name(names[i])) {
            work->failed_count++;
            continue;
        }
        strncpy(work->names[work->count], names[i],
                sizeof(work->names[work->count]) - 1);
        work->count++;
    }
    if (work->count == 0) {
        tal_free(work);
        return OPRT_INVALID_PARM;
    }

    if (s_lock) tal_mutex_lock(s_lock);
    if (s_state != UI_VIDEO_PLAYBACK_IDLE || s_delete_running) {
        if (s_lock) tal_mutex_unlock(s_lock);
        tal_free(work);
        return OPRT_COM_ERROR;
    }
    s_delete_running = true;
    if (s_lock) tal_mutex_unlock(s_lock);

    rt = tal_workq_schedule(WORKQ_SYSTEM, delete_work, work);
    if (rt != OPRT_OK) {
        if (s_lock) tal_mutex_lock(s_lock);
        s_delete_running = false;
        if (s_lock) tal_mutex_unlock(s_lock);
        tal_free(work);
    }
    return rt;
}

void ui_svc_video_playback_list_request(uint32_t seq)
{
    if (ui_fs_list_app_async(UI_FS_VIDEO, seq, fs_list_cb, NULL) != OPRT_OK &&
        s_list_cb) {
        s_list_cb(OPRT_COM_ERROR, NULL, 0, seq);
    }
}

void ui_svc_video_playback_play(const char *name)
{
    playback_start_work_t *work;
    uint32_t generation;
    ui_video_playback_state_t state;
    bool delete_running;

    if (!is_video_leaf_name(name)) return;
    if (s_lock) tal_mutex_lock(s_lock);
    state = s_state;
    delete_running = s_delete_running;
    if (s_lock) tal_mutex_unlock(s_lock);
    if (delete_running) return;
    if (state == UI_VIDEO_PLAYBACK_OPENING || state == UI_VIDEO_PLAYBACK_PLAYING ||
        state == UI_VIDEO_PLAYBACK_PAUSED || state == UI_VIDEO_PLAYBACK_STOPPING) {
        return;
    }

    work = (playback_start_work_t *)tal_malloc(sizeof(*work));
    if (!work) return;
    memset(work, 0, sizeof(*work));
    strncpy(work->name, name, sizeof(work->name) - 1);

    if (s_lock) tal_mutex_lock(s_lock);
    generation = ++s_generation;
    work->generation = generation;
    if (s_lock) tal_mutex_unlock(s_lock);
    publish_state(generation, UI_VIDEO_PLAYBACK_OPENING,
                  UI_VIDEO_PLAYBACK_ERROR_NONE);
    if (tal_workq_schedule(WORKQ_SYSTEM, start_work, work) != OPRT_OK) {
        tal_free(work);
        publish_state(generation, UI_VIDEO_PLAYBACK_ERROR,
                      UI_VIDEO_PLAYBACK_ERROR_CONTROL);
    }
}

void ui_svc_video_playback_pause_resume(void)
{
    uint32_t generation;
    ui_video_playback_state_t state;

    if (s_lock) tal_mutex_lock(s_lock);
    generation = s_generation;
    state = s_state;
    if (s_lock) tal_mutex_unlock(s_lock);
    if (state != UI_VIDEO_PLAYBACK_PLAYING && state != UI_VIDEO_PLAYBACK_PAUSED) return;
    if (tal_workq_schedule(WORKQ_SYSTEM, pause_resume_work,
                           (void *)(uintptr_t)generation) != OPRT_OK) {
        if (!request_emergency_cleanup(generation, UI_VIDEO_PLAYBACK_ERROR,
                                       UI_VIDEO_PLAYBACK_ERROR_CONTROL)) {
            PR_ERR("video playback: pause cleanup deferred until next control request");
            publish_state(generation, UI_VIDEO_PLAYBACK_ERROR,
                          UI_VIDEO_PLAYBACK_ERROR_CONTROL);
        }
    }
}

void ui_svc_video_playback_stop(void)
{
    uint32_t generation;
    ui_video_playback_state_t state;
    bool emergency_active;

    if (s_lock) tal_mutex_lock(s_lock);
    state = s_state;
    if (state == UI_VIDEO_PLAYBACK_IDLE) {
        if (s_lock) tal_mutex_unlock(s_lock);
        return;
    }
    generation = ++s_generation;
    emergency_active = s_emergency_cleanup_th != NULL;
    if (s_lock) tal_mutex_unlock(s_lock);

    publish_state(generation, UI_VIDEO_PLAYBACK_STOPPING,
                  UI_VIDEO_PLAYBACK_ERROR_NONE);
    if (emergency_active) {
        request_emergency_cleanup(generation, UI_VIDEO_PLAYBACK_IDLE,
                                  UI_VIDEO_PLAYBACK_ERROR_NONE);
        return;
    }
    if (tal_workq_schedule(WORKQ_SYSTEM, stop_work,
                           (void *)(uintptr_t)generation) != OPRT_OK) {
        if (!request_emergency_cleanup(generation, UI_VIDEO_PLAYBACK_IDLE,
                                       UI_VIDEO_PLAYBACK_ERROR_NONE)) {
            PR_ERR("video playback: stop cleanup deferred until next control request");
            publish_state(generation, UI_VIDEO_PLAYBACK_ERROR,
                          UI_VIDEO_PLAYBACK_ERROR_CONTROL);
        }
    }
}

ui_video_playback_state_t ui_svc_video_playback_get_state(void)
{
    ui_video_playback_state_t state;
    if (s_lock) tal_mutex_lock(s_lock);
    state = s_state;
    if (s_lock) tal_mutex_unlock(s_lock);
    return state;
}

uint32_t ui_svc_video_playback_get_position_ms(void)
{
    uint32_t position_ms;

    if (s_lock) tal_mutex_lock(s_lock);
    position_ms = s_position_ms;
    if (s_lock) tal_mutex_unlock(s_lock);
    return position_ms;
}

uint32_t ui_svc_video_playback_get_duration_ms(void)
{
    uint32_t duration_ms;

    if (s_lock) tal_mutex_lock(s_lock);
    duration_ms = s_duration_ms;
    if (s_lock) tal_mutex_unlock(s_lock);
    return duration_ms;
}

void ui_svc_video_playback_frame_free(void *buf)
{
    ty_avi_player_frame_free(buf);
}
