#include "ty_avi_recorder.h"
#include "ty_video_recorder.h"
#include "avi_port.h"

#include "wukong_video_service.h"
#include "tuya_app_config.h"
#if defined(ENABLE_AI_MODE_P2P) && (ENABLE_AI_MODE_P2P == 1)
#include "tuya_p2p_tmm_voip.h"
#endif
#include "uni_log.h"
#include "tal_queue.h"
#include "tal_mutex.h"

#include <stdint.h>
#include <string.h>

#define TAG "ty_avi_rec"
/* Slot cap is just a safety net; real admission control is the byte budget
 * below, since frame size (and thus how much recording time a fixed slot
 * count buys) swings a lot with scene bitrate. */
#define TY_AVI_FRAME_Q_DEPTH        144u
#define TY_AVI_FRAME_Q_BYTE_BUDGET  (1024u * 1024u)
#define TY_AVI_FRAME_Q_HIGH_DEPTH   ((TY_AVI_FRAME_Q_DEPTH * 3u + 3u) / 4u)
#define TY_AVI_FRAME_Q_HIGH_BYTES   ((TY_AVI_FRAME_Q_BYTE_BUDGET * 3u) / 4u)
#define TY_AVI_FRAME_Q_LOW_DEPTH    (TY_AVI_FRAME_Q_DEPTH / 4u)
#define TY_AVI_FRAME_Q_LOW_BYTES    (TY_AVI_FRAME_Q_BYTE_BUDGET / 4u)
#define TY_AVI_RECORD_FPS_DEF       15
#define TY_AVI_RECORD_FPS_MAX       15
/* Avg per-frame storage write time (this 1s window) above which the writer
 * is considered unable to keep up. */
#define TY_AVI_WRITE_LATENCY_WARN_MS 200
/* Repeated multi-second physical flushes in a short window indicate a broken
 * storage path (the T5 SDIO driver is already resetting the card in field
 * logs), not a momentary filesystem hiccup. Two stalls with renewed cache
 * pressure stop immediately; stale-frame pruning permits one bounded recovery
 * attempt, but the third stall always stops admission. */
#define TY_AVI_WRITE_LATENCY_SEVERE_MS 2000
#define TY_AVI_SEVERE_STALL_WINDOW_MS  5000
#define TY_AVI_SEVERE_STALL_LIMIT      2
#define TY_AVI_SEVERE_STALL_HARD_LIMIT 3
#define TY_AVI_THROTTLE_FLOOR_FPS      5
/* Mic PCM is 16kHz/16bit/mono, ~640B/20ms chunk. Bounded independently of
 * the video queue's byte budget: audio is a much lower, steadier bitrate
 * (~32KB/s) than JPEG. Keep its byte budget at one eighth of the video
 * budget so both queues gain the same 8x expansion over the original profile. */
#define TY_AVI_AUDIO_Q_DEPTH        400u
#define TY_AVI_AUDIO_Q_BYTE_BUDGET  (256u * 1024u)
#define TY_AVI_AUDIO_BATCH_BYTES    3200u /* 100ms at 16kHz/16bit/mono */
#define TY_AVI_AUDIO_RATE  16000
#define TY_AVI_AUDIO_BITS  16
#define TY_AVI_AUDIO_CHANS 1
#define TY_AVI_AUDIO_BYTES_PER_MS \
    (((TY_AVI_AUDIO_RATE * TY_AVI_AUDIO_CHANS) * (TY_AVI_AUDIO_BITS / 8)) / 1000u)

typedef struct {
    uint8_t *data;
    uint32_t len;
    uint32_t timestamp_ms;
} ty_avi_jpeg_item_t;

/* Same shape as ty_avi_jpeg_item_t; kept as a distinct name because it
 * flows through a separate queue (mic PCM chunks, not JPEG frames). */
typedef struct {
    uint8_t *data;
    uint32_t len;
} ty_avi_pcm_item_t;

typedef struct {
    ty_video_recorder_handle_t recorder;
    QUEUE_HANDLE frame_q;
    QUEUE_HANDLE audio_q;       /* NULL when this session fell back to video-only */
    uint32_t queued_audio_bytes;
    uint8_t *audio_batch;
    uint32_t audio_batch_len;
    uint32_t audio_batch_chunks;
    uint32_t audio_total_cnt;
    uint32_t audio_drop_cnt;
    /* When PCM admission fails, preserve its duration as silence instead of
     * deleting time from the audio track. New live PCM is also represented
     * as silence until this gap has been consumed, so later samples can
     * never jump ahead of earlier queued audio. */
    uint32_t audio_gap_bytes;
    uint32_t audio_silence_bytes_total;
    uint16_t width;
    uint16_t height;
    uint8_t source_fps;
    uint8_t record_fps;    /* user-requested target */
    uint8_t eff_fps;       /* adaptive target actually used for decimation;
                             * throttled down when storage falls behind and
                             * ramped back up toward record_fps once healthy. */
    uint8_t running;
    uint8_t subscribed;
    uint8_t recorder_started;
    uint16_t decim_acc;
    uint32_t total_cnt;
    uint32_t drop_cnt;
    uint32_t generation;
    uint32_t queued_bytes;
    uint8_t storage_failed;
    uint8_t severe_stall_cnt;
    uint32_t severe_stall_window_tick;
    ty_avi_recorder_event_cb_t event_cb;
    void *event_user_data;
} ty_avi_rec_ctx_t;

static ty_avi_rec_ctx_t s_ctx;
/* Process-lifetime guard: a dispatcher may have copied the callback just
 * before unsubscribe. Keeping this mutex alive lets that late callback see
 * running==0 safely instead of touching a released synchronization object. */
static MUTEX_HANDLE s_frame_mtx;
static uint32_t s_generation;

static uint32_t s_stat_src_fps_cnt;
static uint32_t s_stat_enq_fps_cnt;
static uint32_t s_stat_avi_fps_cnt;
static uint32_t s_stat_drop_cnt;
static uint32_t s_stat_skip_cnt;
static uint32_t s_stat_last_tick;
static uint32_t s_stat_q_depth;
static uint32_t s_stat_wr_bytes;
static uint32_t s_stat_vid_wr_ms;
static uint32_t s_stat_vid_wr_cnt;
static uint8_t  s_stall_logged;
static uint8_t  s_healthy_windows;

static void ty_avi_rec_drop_stale_frames_after_stall(void);

static bool ty_avi_rec_p2p_in_call(void)
{
#if defined(ENABLE_AI_MODE_P2P) && (ENABLE_AI_MODE_P2P == 1)
    return tuya_toy_tmm_voip_is_incall() ? true : false;
#else
    return false;
#endif
}

static void ty_avi_rec_cache_snapshot(uint32_t *bytes, uint32_t *depth)
{
    if (s_frame_mtx) tal_mutex_lock(s_frame_mtx);
    if (bytes) *bytes = s_ctx.queued_bytes;
    if (depth) *depth = s_stat_q_depth;
    if (s_frame_mtx) tal_mutex_unlock(s_frame_mtx);
}

static void ty_avi_rec_signal_storage_fault(const char *reason)
{
    ty_avi_recorder_event_cb_t cb;
    void *user_data;

    if (s_frame_mtx) tal_mutex_lock(s_frame_mtx);
    if (!s_ctx.running || s_ctx.storage_failed) {
        if (s_frame_mtx) tal_mutex_unlock(s_frame_mtx);
        return;
    }

    s_ctx.storage_failed = 1;
    cb = s_ctx.event_cb;
    user_data = s_ctx.event_user_data;
    if (s_frame_mtx) tal_mutex_unlock(s_frame_mtx);
    PR_ERR("AVI recording storage fault: %s; stopping frame admission", reason);
    if (cb) cb(TY_AVI_RECORDER_EVENT_STORAGE_FAULT, user_data);
}

static void ty_avi_rec_stat_report(void)
{
    uint32_t now = (uint32_t)sys_port.get_tick();
    uint32_t elapsed;
    uint32_t wr_kbps;
    uint32_t avg_wr_ms;
    bool congested;
    bool severe;

    if (s_stat_last_tick == 0) {
        s_stat_last_tick = now;
        return;
    }
    elapsed = now - s_stat_last_tick;
    if (elapsed < 1000) return;

    wr_kbps = (s_stat_wr_bytes * 1000) / (elapsed * 1024);
    avg_wr_ms = s_stat_vid_wr_cnt ? s_stat_vid_wr_ms / s_stat_vid_wr_cnt : 0;

    PR_NOTICE("avi rec fps tgt=%u eff=%u src=%u enq=%u avi=%u drop=%u skip=%u q=%u/%u cache=%u/%uKB wr=%uKB/s v=%ums",
              s_ctx.record_fps, s_ctx.eff_fps, s_stat_src_fps_cnt, s_stat_enq_fps_cnt,
              s_stat_avi_fps_cnt, s_stat_drop_cnt, s_stat_skip_cnt,
              s_stat_q_depth, TY_AVI_FRAME_Q_DEPTH,
              (s_ctx.queued_bytes + 1023u) / 1024u,
              TY_AVI_FRAME_Q_BYTE_BUDGET / 1024u, wr_kbps, avg_wr_ms);

    /* Storage backpressure feedback: when the writer can't keep up, throttle
     * the upstream sampling rate instead of relying solely on the frame
     * queue filling up and hard-dropping whatever happens to arrive during
     * the stall. This spreads the frames we do keep evenly across the
     * recording instead of leaving them clustered wherever the writer
     * briefly caught up. */
    if (s_ctx.running) {
        severe = (avg_wr_ms >= TY_AVI_WRITE_LATENCY_SEVERE_MS) ||
                 (wr_kbps == 0 && s_stat_drop_cnt > 0);
        congested = severe || (avg_wr_ms > TY_AVI_WRITE_LATENCY_WARN_MS) ||
                    (s_stat_drop_cnt > 0 &&
                     (s_stat_q_depth >= TY_AVI_FRAME_Q_HIGH_DEPTH ||
                      s_ctx.queued_bytes >= TY_AVI_FRAME_Q_HIGH_BYTES)) ||
                    (wr_kbps == 0 && s_stat_drop_cnt > 0);
        if (congested) {
            uint8_t floor_fps = s_ctx.record_fps < TY_AVI_THROTTLE_FLOOR_FPS ?
                                s_ctx.record_fps : TY_AVI_THROTTLE_FLOOR_FPS;

            if (severe && s_ctx.eff_fps > floor_fps) {
                uint8_t next = (uint8_t)((s_ctx.eff_fps + 1u) / 2u);
                s_ctx.eff_fps = next < floor_fps ? floor_fps : next;
            } else if (s_ctx.eff_fps > floor_fps) {
                s_ctx.eff_fps--;
            }
            s_ctx.decim_acc = 0;
            s_healthy_windows = 0;
            if (!s_stall_logged) {
                PR_ERR("AVI recording storage stalled: wr=%uKB/s v=%ums drop=%u q=%u/%u cache=%u/%uKB, throttling to eff_fps=%u",
                       wr_kbps, avg_wr_ms, s_stat_drop_cnt,
                       s_stat_q_depth, TY_AVI_FRAME_Q_DEPTH,
                       (s_ctx.queued_bytes + 1023u) / 1024u,
                       TY_AVI_FRAME_Q_BYTE_BUDGET / 1024u, s_ctx.eff_fps);
                s_stall_logged = 1;
            }
        } else {
            if (s_healthy_windows < UINT8_MAX) s_healthy_windows++;
            if (s_healthy_windows >= 3 && s_ctx.severe_stall_cnt > 0) {
                uint32_t cache_bytes = s_ctx.queued_bytes;
                uint32_t cache_depth = s_stat_q_depth;

                if (cache_bytes <= TY_AVI_FRAME_Q_LOW_BYTES &&
                    cache_depth <= TY_AVI_FRAME_Q_LOW_DEPTH) {
                    PR_NOTICE("AVI recording cache recovered: cache=%uKB q=%u; clearing slow-write window",
                              (cache_bytes + 1023u) / 1024u, cache_depth);
                    s_ctx.severe_stall_cnt = 0;
                    s_ctx.severe_stall_window_tick = 0;
                }
            }
            if (s_healthy_windows >= 3 && s_ctx.eff_fps < s_ctx.record_fps) {
                s_ctx.eff_fps++;
                s_ctx.decim_acc = 0;
                s_healthy_windows = 0;
            }
            if (s_stall_logged && s_ctx.eff_fps >= s_ctx.record_fps) {
                PR_NOTICE("AVI recording storage recovered, eff_fps back to %u", s_ctx.eff_fps);
                s_stall_logged = 0;
            }
        }
    }

    if (s_stat_src_fps_cnt > 0) s_ctx.source_fps = (uint8_t)s_stat_src_fps_cnt;
    s_stat_src_fps_cnt = 0;
    s_stat_enq_fps_cnt = 0;
    s_stat_avi_fps_cnt = 0;
    s_stat_drop_cnt = 0;
    s_stat_skip_cnt = 0;
    s_stat_wr_bytes = 0;
    s_stat_vid_wr_ms = 0;
    s_stat_vid_wr_cnt = 0;
    s_stat_last_tick = now;
}

static void ty_avi_rec_stat_on_drop(void)
{
    s_ctx.drop_cnt++;
    s_stat_drop_cnt++;
    ty_avi_rec_stat_report();
}

void ty_avi_rec_stat_on_dvp_frame(void)
{
    if (!s_ctx.running) return;
    s_stat_src_fps_cnt++;
    ty_avi_rec_stat_report();
}

void ty_avi_rec_stat_on_enq_frame(void)
{
    if (!s_ctx.running) return;
    s_stat_enq_fps_cnt++;
    ty_avi_rec_stat_report();
}

void ty_avi_rec_stat_on_avi_frame(void)
{
    if (!s_ctx.running) return;
    s_stat_avi_fps_cnt++;
    ty_avi_rec_stat_report();
}

void ty_avi_rec_stat_on_avi_skip(void)
{
    if (!s_ctx.running) return;
    s_stat_skip_cnt++;
    ty_avi_rec_stat_report();
}

void ty_avi_rec_stat_on_write(uint32_t bytes, uint32_t cost_ms, uint8_t is_video)
{
    uint32_t now;
    uint32_t cache_bytes;
    uint32_t cache_depth;

    if (!s_ctx.running) return;
    s_stat_wr_bytes += bytes;
    if (is_video) {
        s_stat_vid_wr_ms += cost_ms;
        s_stat_vid_wr_cnt++;
    }

    /* The 32KB AVI write cache may physically flush from either a video or
     * an audio API call. Treat both as storage stalls; otherwise a blocked
     * audio flush would be invisible to video backpressure. */
    if (cost_ms >= TY_AVI_WRITE_LATENCY_SEVERE_MS) {
        ty_avi_rec_drop_stale_frames_after_stall();
        now = (uint32_t)sys_port.get_tick();
        if (s_ctx.severe_stall_window_tick == 0 ||
            now - s_ctx.severe_stall_window_tick > TY_AVI_SEVERE_STALL_WINDOW_MS) {
            s_ctx.severe_stall_window_tick = now;
            s_ctx.severe_stall_cnt = 1;
        } else if (s_ctx.severe_stall_cnt < UINT8_MAX) {
            s_ctx.severe_stall_cnt++;
        }

        ty_avi_rec_cache_snapshot(&cache_bytes, &cache_depth);
        if (s_ctx.severe_stall_cnt >= TY_AVI_SEVERE_STALL_HARD_LIMIT) {
            ty_avi_rec_signal_storage_fault("three severe buffered writes in five seconds");
        } else if (s_ctx.severe_stall_cnt >= TY_AVI_SEVERE_STALL_LIMIT) {
            if (cache_bytes >= TY_AVI_FRAME_Q_HIGH_BYTES ||
                cache_depth >= TY_AVI_FRAME_Q_HIGH_DEPTH) {
                ty_avi_rec_signal_storage_fault("repeated slow writes with cache pressure");
            } else {
                PR_WARN("AVI recording slow writes buffered: cache=%u/%uKB q=%u/%u; waiting for recovery",
                        (cache_bytes + 1023u) / 1024u,
                        TY_AVI_FRAME_Q_BYTE_BUDGET / 1024u,
                        cache_depth, TY_AVI_FRAME_Q_DEPTH);
            }
        }
    }
}

void ty_avi_rec_stat_on_write_error(uint8_t is_video)
{
    if (!s_ctx.running) return;
    ty_avi_rec_signal_storage_fault(is_video ? "video write failed" :
                                               "audio write failed");
}

static void ty_avi_free_jpeg_item(ty_avi_jpeg_item_t *item)
{
    if (item && item->data) {
        sys_port.psram_free(item->data);
        item->data = NULL;
        item->len = 0;
    }
}

/* A completed multi-second write leaves the FIFO full of frames captured in
 * the past. Replaying that backlog creates a catch-up burst and keeps the
 * queue at high water for the next SD timeout. Keep only the newest frame so
 * the bounded cache regains headroom and the recording resumes near live. */
static void ty_avi_rec_drop_stale_frames_after_stall(void)
{
    ty_avi_jpeg_item_t item = {0};
    ty_avi_jpeg_item_t newest = {0};
    uint32_t dropped = 0;
    uint8_t old_fps;

    if (!s_frame_mtx) return;
    tal_mutex_lock(s_frame_mtx);
    old_fps = s_ctx.eff_fps;
    if (s_ctx.eff_fps > TY_AVI_THROTTLE_FLOOR_FPS) {
        uint8_t next = (uint8_t)((s_ctx.eff_fps + 1u) / 2u);
        s_ctx.eff_fps = next < TY_AVI_THROTTLE_FLOOR_FPS ?
                        TY_AVI_THROTTLE_FLOOR_FPS : next;
        s_ctx.decim_acc = 0;
    }

    while (s_ctx.frame_q && tal_queue_fetch(s_ctx.frame_q, &item, 0) == OPRT_OK) {
        if (s_stat_q_depth > 0) s_stat_q_depth--;
        s_ctx.queued_bytes = s_ctx.queued_bytes >= item.len ?
                             s_ctx.queued_bytes - item.len : 0;
        if (newest.data) {
            ty_avi_free_jpeg_item(&newest);
            dropped++;
        }
        newest = item;
        memset(&item, 0, sizeof(item));
    }
    if (newest.data) {
        if (tal_queue_post(s_ctx.frame_q, &newest, 0) == OPRT_OK) {
            s_ctx.queued_bytes += newest.len;
            s_stat_q_depth++;
        } else {
            ty_avi_free_jpeg_item(&newest);
            dropped++;
        }
    }
    s_ctx.drop_cnt += dropped;
    s_stat_drop_cnt += dropped;
    s_healthy_windows = 0;
    tal_mutex_unlock(s_frame_mtx);

    PR_WARN("AVI severe write recovery: dropped=%u stale frames, eff_fps=%u->%u",
            dropped, old_fps, s_ctx.eff_fps);
}

static void ty_avi_free_pcm_item(ty_avi_pcm_item_t *item)
{
    if (item && item->data) {
        sys_port.psram_free(item->data);
        item->data = NULL;
        item->len = 0;
    }
}

/* Caller holds s_frame_mtx. A dropped PCM block must still occupy the same
 * amount of media time; otherwise all later speech shifts earlier and the
 * A/V error grows after every storage stall. */
static void ty_avi_add_audio_gap_locked(uint32_t len, uint32_t chunks)
{
    if (UINT32_MAX - s_ctx.audio_gap_bytes < len) {
        s_ctx.audio_gap_bytes = UINT32_MAX;
    } else {
        s_ctx.audio_gap_bytes += len;
    }
    if (UINT32_MAX - s_ctx.audio_silence_bytes_total < len) {
        s_ctx.audio_silence_bytes_total = UINT32_MAX;
    } else {
        s_ctx.audio_silence_bytes_total += len;
    }
    if (UINT32_MAX - s_ctx.audio_drop_cnt < chunks) {
        s_ctx.audio_drop_cnt = UINT32_MAX;
    } else {
        s_ctx.audio_drop_cnt += chunks;
    }
}

/* Caller holds s_frame_mtx. PCM arrives in small real-time chunks (normally
 * 640B/20ms). Group them before handing data to libavi so a storage recovery
 * does not replay dozens of tiny AVI chunks back-to-back. */
static OPERATE_RET ty_avi_commit_audio_batch_locked(bool allow_partial)
{
    ty_avi_pcm_item_t item;
    OPERATE_RET ret = OPRT_OK;

    if (!s_ctx.audio_batch || s_ctx.audio_batch_len == 0 ||
        (!allow_partial && s_ctx.audio_batch_len < TY_AVI_AUDIO_BATCH_BYTES)) {
        return OPRT_OK;
    }

    item.data = s_ctx.audio_batch;
    item.len = s_ctx.audio_batch_len;
    if (s_ctx.audio_gap_bytes > 0 ||
        s_ctx.queued_audio_bytes + item.len > TY_AVI_AUDIO_Q_BYTE_BUDGET ||
        tal_queue_post(s_ctx.audio_q, &item, 0) != OPRT_OK) {
        ty_avi_add_audio_gap_locked(item.len, s_ctx.audio_batch_chunks);
        ty_avi_free_pcm_item(&item);
        ret = OPRT_COM_ERROR;
    } else {
        s_ctx.queued_audio_bytes += item.len;
    }

    s_ctx.audio_batch = NULL;
    s_ctx.audio_batch_len = 0;
    s_ctx.audio_batch_chunks = 0;
    return ret;
}

static bool ty_avi_should_enqueue_frame(void)
{
    uint32_t source_fps = s_ctx.source_fps ? s_ctx.source_fps : s_ctx.eff_fps;

    if (s_ctx.eff_fps == 0 || s_ctx.eff_fps >= source_fps) return true;

    s_ctx.decim_acc += s_ctx.eff_fps;
    if (s_ctx.decim_acc >= source_fps) {
        s_ctx.decim_acc = (uint16_t)(s_ctx.decim_acc - source_fps);
        return true;
    }
    return false;
}

/* Camera callbacks borrow their frame buffer. Copy into a bounded queue and
 * return; AVI and filesystem work stays on the recorder thread. */
static void ty_avi_video_frame_cb(const VIDEO_FRAME_T *frame, void *ctx)
{
    ty_avi_jpeg_item_t item = {0};
    uint32_t generation = (uint32_t)(uintptr_t)ctx;

    if (!frame || frame->fmt != VIDEO_FMT_MJPEG || !frame->data || frame->length == 0 ||
        !s_frame_mtx) {
        return;
    }

    tal_mutex_lock(s_frame_mtx);
    if (!s_ctx.running || s_ctx.storage_failed || !s_ctx.frame_q ||
        generation != s_ctx.generation) {
        tal_mutex_unlock(s_frame_mtx);
        return;
    }

    s_ctx.total_cnt++;
    ty_avi_rec_stat_on_dvp_frame();
    if (!ty_avi_should_enqueue_frame()) {
        ty_avi_rec_stat_on_drop();
        tal_mutex_unlock(s_frame_mtx);
        return;
    }

    /* Byte-budget admission: a fixed slot count buys wildly different
     * buffering time depending on scene bitrate. Gate on bytes as well so
     * buffering headroom tracks actual data volume instead of frame count. */
    if (s_ctx.queued_bytes + frame->length > TY_AVI_FRAME_Q_BYTE_BUDGET) {
        ty_avi_rec_stat_on_drop();
        tal_mutex_unlock(s_frame_mtx);
        return;
    }

    item.data = (uint8_t *)sys_port.psram_malloc(frame->length);
    if (!item.data) {
        ty_avi_rec_stat_on_drop();
        tal_mutex_unlock(s_frame_mtx);
        return;
    }
    memcpy(item.data, frame->data, frame->length);
    item.len = frame->length;
    item.timestamp_ms = frame->timestamp_ms;

    if (tal_queue_post(s_ctx.frame_q, &item, 0) != OPRT_OK) {
        ty_avi_free_jpeg_item(&item);
        ty_avi_rec_stat_on_drop();
    } else {
        s_ctx.queued_bytes += item.len;
        s_stat_q_depth++;
        ty_avi_rec_stat_on_enq_frame();
    }
    tal_mutex_unlock(s_frame_mtx);
}

static int ty_avi_get_frame_cb(void *user_data, ty_video_recorder_frame_data_t *frame_data)
{
    ty_avi_jpeg_item_t item = {0};
    (void)user_data;

    if (!frame_data || s_ctx.storage_failed || !s_ctx.frame_q ||
        tal_queue_fetch(s_ctx.frame_q, &item, 0) != OPRT_OK) {
        return -1;
    }
    if (s_frame_mtx) tal_mutex_lock(s_frame_mtx);
    if (s_stat_q_depth > 0) s_stat_q_depth--;
    s_ctx.queued_bytes = (s_ctx.queued_bytes >= item.len) ? (s_ctx.queued_bytes - item.len) : 0;
    if (s_frame_mtx) tal_mutex_unlock(s_frame_mtx);

    frame_data->data = item.data;
    frame_data->length = item.len;
    frame_data->timestamp_ms = item.timestamp_ms;
    frame_data->width = s_ctx.width;
    frame_data->height = s_ctx.height;
    frame_data->frame_buffer = item.data;
    frame_data->is_key_frame = 1;
    return 0;
}

static void ty_avi_release_frame_cb(void *user_data, ty_video_recorder_frame_data_t *frame_data)
{
    (void)user_data;
    if (frame_data && frame_data->data) {
        sys_port.psram_free(frame_data->data);
        frame_data->data = NULL;
        frame_data->length = 0;
    }
}

/* Pulled by the recorder thread's wall-clock-paced ty_recorder_sync_audio();
 * mirrors ty_avi_get_frame_cb but for the audio_q side. Returning -1 just
 * means "nothing queued right now" (audio may be arriving slower than video
 * pacing wants it, or not at all this session) — never an error. */
static int ty_avi_get_audio_cb(void *user_data, ty_video_recorder_audio_data_t *audio_data)
{
    ty_avi_pcm_item_t item = {0};
    uint32_t silence_len;
    bool deny_live_pull;
    (void)user_data;

    if (!audio_data || !s_ctx.audio_q || !s_frame_mtx) {
        return -1;
    }

    tal_mutex_lock(s_frame_mtx);
    deny_live_pull = s_ctx.storage_failed && s_ctx.running;
    tal_mutex_unlock(s_frame_mtx);
    if (deny_live_pull) return -1;

    /* A storage fault stops live admission, but recorder_stop() still needs
     * to pull already-buffered PCM and silence placeholders after running is
     * cleared. The AVI writer remains the authority: a real I/O error during
     * this bounded tail flush aborts ty_recorder_sync_audio() immediately. */

    if (tal_queue_fetch(s_ctx.audio_q, &item, 0) == OPRT_OK) {
        if (s_frame_mtx) tal_mutex_lock(s_frame_mtx);
        s_ctx.queued_audio_bytes = (s_ctx.queued_audio_bytes >= item.len) ?
                                   (s_ctx.queued_audio_bytes - item.len) : 0;
        if (s_frame_mtx) tal_mutex_unlock(s_frame_mtx);

        audio_data->data   = item.data;
        audio_data->length = item.len;
        return 0;
    }

    /* The real queue is empty, so any pending gap is now exactly next in
     * stream order. Materialize at most one normal 100ms block per pull. */
    tal_mutex_lock(s_frame_mtx);
    silence_len = s_ctx.audio_gap_bytes > TY_AVI_AUDIO_BATCH_BYTES ?
                  TY_AVI_AUDIO_BATCH_BYTES : s_ctx.audio_gap_bytes;
    if (silence_len == 0) {
        tal_mutex_unlock(s_frame_mtx);
        return -1;
    }
    item.data = (uint8_t *)sys_port.psram_malloc(silence_len);
    if (!item.data) {
        tal_mutex_unlock(s_frame_mtx);
        return -1;
    }
    memset(item.data, 0, silence_len);
    item.len = silence_len;
    s_ctx.audio_gap_bytes -= silence_len;
    tal_mutex_unlock(s_frame_mtx);

    audio_data->data   = item.data;
    audio_data->length = item.len;
    return 0;
}

static void ty_avi_release_audio_cb(void *user_data, ty_video_recorder_audio_data_t *audio_data)
{
    (void)user_data;
    if (audio_data && audio_data->data) {
        sys_port.psram_free(audio_data->data);
        audio_data->data = NULL;
        audio_data->length = 0;
    }
}

static void ty_avi_reset_stats(void)
{
    s_stat_src_fps_cnt = 0;
    s_stat_enq_fps_cnt = 0;
    s_stat_avi_fps_cnt = 0;
    s_stat_drop_cnt = 0;
    s_stat_skip_cnt = 0;
    s_stat_last_tick = 0;
    s_stat_q_depth = 0;
    s_stat_wr_bytes = 0;
    s_stat_vid_wr_ms = 0;
    s_stat_vid_wr_cnt = 0;
    s_stall_logged = 0;
    s_healthy_windows = 0;
}

static void ty_avi_drain_queue(void)
{
    ty_avi_jpeg_item_t item;

    if (!s_ctx.frame_q) return;
    while (tal_queue_fetch(s_ctx.frame_q, &item, 0) == OPRT_OK) {
        s_ctx.queued_bytes = (s_ctx.queued_bytes >= item.len) ? (s_ctx.queued_bytes - item.len) : 0;
        ty_avi_free_jpeg_item(&item);
        if (s_stat_q_depth > 0) s_stat_q_depth--;
    }
}

static void ty_avi_drain_audio_queue(void)
{
    ty_avi_pcm_item_t item;

    if (!s_ctx.audio_q) return;
    while (tal_queue_fetch(s_ctx.audio_q, &item, 0) == OPRT_OK) {
        s_ctx.queued_audio_bytes = (s_ctx.queued_audio_bytes >= item.len) ? (s_ctx.queued_audio_bytes - item.len) : 0;
        ty_avi_free_pcm_item(&item);
    }
    if (s_ctx.audio_batch) {
        sys_port.psram_free(s_ctx.audio_batch);
        s_ctx.audio_batch = NULL;
        s_ctx.audio_batch_len = 0;
        s_ctx.audio_batch_chunks = 0;
    }
}

OPERATE_RET ty_avi_recorder_start(const TY_AVI_RECORDER_CFG_T *cfg)
{
    VIDEO_INPUT_CAPS_T caps = {0};
    ty_video_recorder_config_t rec_cfg = {0};
    OPERATE_RET rt;

    if (!cfg || !cfg->file_path || cfg->file_path[0] != '/') return OPRT_INVALID_PARM;
    if (s_ctx.running || s_ctx.recorder) return OPRT_COM_ERROR;
    if (!wukong_video_available(VIDEO_FMT_MJPEG) ||
        wukong_video_get_caps(&caps) != OPRT_OK || caps.width == 0 || caps.height == 0) {
        PR_ERR("AVI recording requires an available MJPEG video source");
        return OPRT_NOT_SUPPORTED;
    }

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.width = caps.width;
    s_ctx.height = caps.height;
    s_ctx.source_fps = caps.fps ? caps.fps : TY_AVI_RECORD_FPS_DEF;
    s_ctx.record_fps = cfg->fps ? cfg->fps : s_ctx.source_fps;
    if (s_ctx.record_fps > TY_AVI_RECORD_FPS_MAX) {
        PR_NOTICE("AVI recording fps capped from %u to %u for storage/decode budget",
                  s_ctx.record_fps, TY_AVI_RECORD_FPS_MAX);
        s_ctx.record_fps = TY_AVI_RECORD_FPS_MAX;
    }
    s_ctx.generation = ++s_generation;
    s_ctx.event_cb = cfg->event_cb;
    s_ctx.event_user_data = cfg->event_user_data;
    /* Some DVP sensors report the configured fps rather than their effective
     * cadence (the 480x480 GC2145 stream is one example). Do not clamp the
     * requested target to that stale capability value: frame arrival naturally
     * caps the result, the first stats window measures the real source rate,
     * and AVI close writes the rate derived from saved-frame timestamps. */
    s_ctx.eff_fps = s_ctx.record_fps;

    if ((cfg->width && cfg->width != s_ctx.width) ||
        (cfg->height && cfg->height != s_ctx.height)) {
        PR_WARN("AVI requested %ux%u, using camera source %ux%u",
                cfg->width, cfg->height, s_ctx.width, s_ctx.height);
    }
    if (cfg->enable_preview || cfg->enable_video_preview || cfg->enable_audio_preview) {
        PR_NOTICE("AVI preview flags ignored; Demo camera/UI own preview resources");
    }

    if (!s_frame_mtx && tal_mutex_create_init(&s_frame_mtx) != OPRT_OK) {
        return OPRT_MALLOC_FAILED;
    }
    if (tal_queue_create_init(&s_ctx.frame_q, sizeof(ty_avi_jpeg_item_t),
                              TY_AVI_FRAME_Q_DEPTH) != OPRT_OK) {
        memset(&s_ctx, 0, sizeof(s_ctx));
        return OPRT_MALLOC_FAILED;
    }

    /* Mic is a single, exclusive resource shared with AI wake-word listening
     * (see ty_avi_recorder.h). If a P2P call already owns it, skip wiring an
     * audio track at all rather than declare one that will never receive
     * data: while a call is in progress the AI mode layer's audio dispatch
     * never reaches the __default_audio_input() reroute, so nothing would
     * ever call ty_avi_recorder_audio_feed() for this whole session. */
    if (!ty_avi_rec_p2p_in_call()) {
        if (tal_queue_create_init(&s_ctx.audio_q, sizeof(ty_avi_pcm_item_t),
                                  TY_AVI_AUDIO_Q_DEPTH) != OPRT_OK) {
            PR_WARN("AVI recording: audio queue alloc failed, falling back to video-only");
            s_ctx.audio_q = NULL;
        }
    } else {
        PR_NOTICE("AVI recording: P2P call owns the mic, recording video-only");
    }

    PR_NOTICE("AVI recording cache: video=%u frames/%uKB high=75%% audio=%u chunks/%uKB",
              TY_AVI_FRAME_Q_DEPTH, TY_AVI_FRAME_Q_BYTE_BUDGET / 1024u,
              s_ctx.audio_q ? TY_AVI_AUDIO_Q_DEPTH : 0,
              s_ctx.audio_q ? TY_AVI_AUDIO_Q_BYTE_BUDGET / 1024u : 0);

    rec_cfg.record_type = TY_VIDEO_RECORDER_TYPE_AVI;
    rec_cfg.record_format = TY_VIDEO_RECORDER_FORMAT_MJPEG;
    rec_cfg.record_framerate = s_ctx.record_fps;
    rec_cfg.video_width = s_ctx.width;
    rec_cfg.video_height = s_ctx.height;
    if (s_ctx.audio_q) {
        rec_cfg.audio_channels   = TY_AVI_AUDIO_CHANS;
        rec_cfg.audio_rate       = TY_AVI_AUDIO_RATE;
        rec_cfg.audio_bits       = TY_AVI_AUDIO_BITS;
        rec_cfg.audio_format     = TY_VIDEO_RECORDER_AUDIO_FORMAT_PCM;
        rec_cfg.get_audio_cb     = ty_avi_get_audio_cb;
        rec_cfg.release_audio_cb = ty_avi_release_audio_cb;
    } else {
        rec_cfg.audio_channels = 0; /* mic unavailable this session: video-only */
    }
    rec_cfg.get_frame_cb = ty_avi_get_frame_cb;
    rec_cfg.release_frame_cb = ty_avi_release_frame_cb;

    if (ty_video_recorder_new(&s_ctx.recorder, &rec_cfg) != TY_AVDK_ERR_OK ||
        ty_video_recorder_open(s_ctx.recorder) != TY_AVDK_ERR_OK ||
        ty_video_recorder_start(s_ctx.recorder, (char *)cfg->file_path,
                                TY_VIDEO_RECORDER_TYPE_AVI) != TY_AVDK_ERR_OK) {
        PR_ERR("AVI recorder core start failed: %s", cfg->file_path);
        ty_avi_recorder_stop();
        return OPRT_COM_ERROR;
    }
    s_ctx.recorder_started = 1;
    ty_avi_reset_stats();
    s_ctx.running = 1;

    rt = wukong_video_subscribe(VIDEO_FMT_MJPEG, VIDEO_CONSUMER_AVI_RECORD,
                                ty_avi_video_frame_cb,
                                (void *)(uintptr_t)s_ctx.generation);
    if (rt != OPRT_OK) {
        PR_ERR("AVI MJPEG subscribe failed: %d", rt);
        ty_avi_recorder_stop();
        return rt;
    }
    s_ctx.subscribed = 1;

    PR_NOTICE("AVI recording started: %s %ux%u@%ufps audio=%s",
              cfg->file_path, s_ctx.width, s_ctx.height, s_ctx.record_fps,
              s_ctx.audio_q ? "pcm" : "none");
    return OPRT_OK;
}

OPERATE_RET ty_avi_recorder_stop(void)
{
    uint32_t total = s_ctx.total_cnt;
    uint32_t dropped = s_ctx.drop_cnt;
    uint32_t audio_total = s_ctx.audio_total_cnt;
    uint32_t audio_dropped = s_ctx.audio_drop_cnt;
    uint32_t audio_silence_bytes = s_ctx.audio_silence_bytes_total;
    uint32_t audio_gap_pending = 0;
    uint32_t audio_queue_pending = 0;
    OPERATE_RET result = OPRT_OK;

    if (!s_ctx.recorder && !s_ctx.frame_q) return OPRT_OK;

    if (s_frame_mtx) {
        tal_mutex_lock(s_frame_mtx);
        if (s_ctx.running && s_ctx.audio_q) {
            (void)ty_avi_commit_audio_batch_locked(true);
        }
        audio_dropped = s_ctx.audio_drop_cnt;
        audio_silence_bytes = s_ctx.audio_silence_bytes_total;
        s_ctx.running = 0;
        tal_mutex_unlock(s_frame_mtx);
    } else {
        s_ctx.running = 0;
    }

    if (s_ctx.subscribed) {
        wukong_video_unsubscribe(VIDEO_FMT_MJPEG, VIDEO_CONSUMER_AVI_RECORD);
        s_ctx.subscribed = 0;
    }

    /* Synchronize with a callback copied by the dispatcher just before the
     * unsubscribe operation removed this consumer. */
    if (s_frame_mtx) {
        tal_mutex_lock(s_frame_mtx);
        tal_mutex_unlock(s_frame_mtx);
    }

    if (s_ctx.recorder) {
        if (s_ctx.recorder_started) {
            if (ty_video_recorder_stop(s_ctx.recorder) != TY_AVDK_ERR_OK) {
                result = OPRT_COM_ERROR;
            }
            s_ctx.recorder_started = 0;
        }
        ty_video_recorder_close(s_ctx.recorder);
        ty_video_recorder_delete(s_ctx.recorder);
        s_ctx.recorder = NULL;
    }

    if (s_frame_mtx) {
        tal_mutex_lock(s_frame_mtx);
        audio_gap_pending = s_ctx.audio_gap_bytes;
        audio_queue_pending = s_ctx.queued_audio_bytes;
        tal_mutex_unlock(s_frame_mtx);
    }

    /* Release queued PSRAM before clearing the recording context. */
    ty_avi_drain_queue();
    if (s_ctx.frame_q) {
        tal_queue_free(s_ctx.frame_q);
        s_ctx.frame_q = NULL;
    }
    ty_avi_drain_audio_queue();
    if (s_ctx.audio_q) {
        tal_queue_free(s_ctx.audio_q);
        s_ctx.audio_q = NULL;
    }

    PR_NOTICE("AVI recording stopped: source_frames=%u dropped=%u audio_chunks=%u audio_dropped=%u silence=%ums pending=%ums/%ums",
              total, dropped, audio_total, audio_dropped,
              audio_silence_bytes / TY_AVI_AUDIO_BYTES_PER_MS,
              audio_gap_pending / TY_AVI_AUDIO_BYTES_PER_MS,
              audio_queue_pending / TY_AVI_AUDIO_BYTES_PER_MS);
    memset(&s_ctx, 0, sizeof(s_ctx));
    return result;
}

BOOL_T ty_avi_recorder_is_running(void)
{
    return s_ctx.running ? TRUE : FALSE;
}

OPERATE_RET ty_avi_recorder_audio_feed(const uint8_t *data, uint32_t len)
{
    if (!data || len == 0 || !s_frame_mtx) {
        return OPRT_INVALID_PARM;
    }

    tal_mutex_lock(s_frame_mtx);
    if (!s_ctx.running || s_ctx.storage_failed || !s_ctx.audio_q) {
        tal_mutex_unlock(s_frame_mtx);
        return OPRT_OK; /* no recording, or this session is video-only: quietly drop */
    }

    s_ctx.audio_total_cnt++;
    if (len > TY_AVI_AUDIO_BATCH_BYTES) {
        ty_avi_add_audio_gap_locked(len, 1);
        tal_mutex_unlock(s_frame_mtx);
        return OPRT_OK;
    }

    /* Once a gap exists, recording newer PCM would put it before the missing
     * interval. Keep extending the silence until the recorder thread catches
     * up and consumes the gap, then resume real PCM admission. */
    if (s_ctx.audio_gap_bytes > 0) {
        ty_avi_add_audio_gap_locked(len, 1);
        tal_mutex_unlock(s_frame_mtx);
        return OPRT_OK;
    }

    if (s_ctx.audio_batch_len + len > TY_AVI_AUDIO_BATCH_BYTES) {
        (void)ty_avi_commit_audio_batch_locked(true);
    }
    if (!s_ctx.audio_batch) {
        s_ctx.audio_batch = (uint8_t *)sys_port.psram_malloc(TY_AVI_AUDIO_BATCH_BYTES);
    }
    if (!s_ctx.audio_batch) {
        ty_avi_add_audio_gap_locked(len, 1);
        tal_mutex_unlock(s_frame_mtx);
        return OPRT_MALLOC_FAILED;
    }

    memcpy(s_ctx.audio_batch + s_ctx.audio_batch_len, data, len);
    s_ctx.audio_batch_len += len;
    s_ctx.audio_batch_chunks++;
    if (s_ctx.audio_batch_len == TY_AVI_AUDIO_BATCH_BYTES) {
        (void)ty_avi_commit_audio_batch_locked(false);
    }
    tal_mutex_unlock(s_frame_mtx);
    return OPRT_OK; /* best-effort feed contract */
}

OPERATE_RET ty_avi_recorder_set_video_preview(uint8_t on)
{
    (void)on;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET ty_avi_recorder_set_audio_preview(uint8_t on)
{
    (void)on;
    return OPRT_NOT_SUPPORTED;
}

uint8_t ty_avi_recorder_get_video_preview(void)
{
    return 0;
}

uint8_t ty_avi_recorder_get_audio_preview(void)
{
    return 0;
}
