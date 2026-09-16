#include "ty_video_recorder_ctlr.h"
#include "ty_video_recorder_avi.h"
#include "avi_port.h"

#include "uni_log.h"
#include "tal_memory.h"
#include "tal_thread.h"

#include <string.h>
#include <stddef.h>

#define TAG "ty_recorder_ctlr"
#define TY_REC_AUDIO_BATCH_BYTES          3200u
#define TY_REC_AUDIO_BATCHES_PER_PASS     1u

#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif

static uint32_t ty_recorder_audio_bytes_per_ms(ty_video_recorder_ctlr_t *ctlr)
{
    uint32_t rate = ctlr->config.audio_rate ? ctlr->config.audio_rate : 16000;
    uint32_t bits = ctlr->config.audio_bits ? ctlr->config.audio_bits : 16;
    uint32_t chans = ctlr->config.audio_channels ? ctlr->config.audio_channels : 1;

    return (rate * chans * (bits / 8)) / 1000;
}

static void ty_recorder_sync_audio(ty_video_recorder_ctlr_t *ctlr,
                                   ty_video_recorder_audio_data_t *audio_data,
                                   uint32_t target_ms,
                                   uint32_t max_batches,
                                   bool flush_tail)
{
    uint32_t bytes_per_ms = ty_recorder_audio_bytes_per_ms(ctlr);
    uint32_t target_bytes;
    uint32_t batches = 0;
    uint32_t write_len;

    if (!ctlr->config.get_audio_cb || bytes_per_ms == 0) {
        return;
    }

    target_bytes = target_ms * bytes_per_ms;
    while (batches < max_batches &&
           (flush_tail ? ctlr->audio_bytes_written < target_bytes :
                         ctlr->audio_bytes_written + TY_REC_AUDIO_BATCH_BYTES <= target_bytes)) {
        if (ctlr->config.get_audio_cb(ctlr->config.user_data, audio_data) != 0 ||
            audio_data->data == NULL || audio_data->length == 0) {
            break;
        }
        write_len = audio_data->length;
        if (flush_tail && ctlr->audio_bytes_written + write_len > target_bytes) {
            write_len = target_bytes - ctlr->audio_bytes_written;
        }
        if (write_len == 0 ||
            ty_avi_write_audio_data(ctlr, audio_data->data, write_len) != TY_AVDK_ERR_OK) {
            if (ctlr->config.release_audio_cb) {
                ctlr->config.release_audio_cb(ctlr->config.user_data, audio_data);
            }
            memset(audio_data, 0, sizeof(*audio_data));
            break;
        }
        ctlr->audio_bytes_written += write_len;
        batches++;
        if (ctlr->config.release_audio_cb) {
            ctlr->config.release_audio_cb(ctlr->config.user_data, audio_data);
        }
        memset(audio_data, 0, sizeof(*audio_data));
    }
}

static void ty_recorder_write_audio_for_frame(ty_video_recorder_ctlr_t *ctlr,
                                            ty_video_recorder_audio_data_t *audio_data)
{
    uint32_t now = (uint32_t)sys_port.get_tick();
    uint32_t elapsed = now - ctlr->recording_start_time_ms;

    ty_recorder_sync_audio(ctlr, audio_data, elapsed,
                           TY_REC_AUDIO_BATCHES_PER_PASS, false);
}

static void ty_recorder_release_file_path(ty_video_recorder_ctlr_t *ctlr)
{
    if (ctlr->file_path) {
        tal_free(ctlr->file_path);
        ctlr->file_path = NULL;
    }
}

static void ty_recorder_discard_empty_files(ty_video_recorder_ctlr_t *ctlr)
{
    char *index_path;
    size_t path_len;

    if (!ctlr->file_path) {
        return;
    }

    path_len = strlen(ctlr->file_path);
    index_path = (char *)tal_malloc(path_len + sizeof(".idx"));
    if (index_path) {
        memcpy(index_path, ctlr->file_path, path_len);
        memcpy(index_path + path_len, ".idx", sizeof(".idx"));
    }

    (void)tkl_fs_remove(ctlr->file_path);
    if (index_path) {
        (void)tkl_fs_remove(index_path);
        tal_free(index_path);
    }
}

static void ty_recorder_thread(void *arg)
{
    ty_video_recorder_ctlr_t *ctlr = (ty_video_recorder_ctlr_t *)arg;
    ty_video_recorder_frame_data_t frame_data = {0};
    ty_video_recorder_audio_data_t audio_data = {0};

    while (!ctlr->thread_exit) {
        tal_semaphore_wait(ctlr->thread_sem, SEM_WAIT_FOREVER);
        if (ctlr->thread_exit) {
            break;
        }
        if (!ctlr->thread_running) {
            /* stop() may clear thread_running before the start semaphore is
             * consumed. Acknowledge that stop request instead of going back
             * to sleep and leaving stop() blocked forever. */
            tal_semaphore_post(ctlr->stop_sem);
            continue;
        }

        while (ctlr->thread_running && !ctlr->thread_exit) {
            bool video_written = false;

            if (ctlr->config.get_frame_cb) {
                if (ctlr->config.get_frame_cb(ctlr->config.user_data, &frame_data) == 0 &&
                    frame_data.data && frame_data.length > 0) {
                    if (ty_avi_write_video_frame(ctlr, frame_data.data, frame_data.length) == TY_AVDK_ERR_OK) {
                        uint32_t saved_time_ms = frame_data.timestamp_ms ?
                                                 frame_data.timestamp_ms :
                                                 (uint32_t)sys_port.get_tick();

                        if (ctlr->first_saved_frame_time_ms == 0) {
                            ctlr->first_saved_frame_time_ms = saved_time_ms;
                        }
                        ctlr->last_saved_frame_time_ms = saved_time_ms;
                        video_written = true;
                    }
                    if (ctlr->config.release_frame_cb) {
                        ctlr->config.release_frame_cb(ctlr->config.user_data, &frame_data);
                    }
                    memset(&frame_data, 0, sizeof(frame_data));
                }
            }

            ty_recorder_write_audio_for_frame(ctlr, &audio_data);

            if (!video_written) {
                sys_port.sleep(5);
            }
        }

        tal_semaphore_post(ctlr->stop_sem);
    }

}

static ty_avdk_err_t ty_recorder_ctlr_open(ty_video_recorder_handle_t handler)
{
    ty_video_recorder_ctlr_t *ctlr = container_of(handler, ty_video_recorder_ctlr_t, ops);
    TY_AVDK_RETURN_ON_FALSE(ctlr->status == TY_VIDEO_RECORDER_STATUS_NONE, TY_AVDK_ERR_INVAL, TAG, "bad status");

    ctlr->avi_handle = NULL;
    ctlr->thread_running = 0;
    ctlr->thread_exit = 0;
    ctlr->video_frame_count = 0;
    ctlr->recording_start_time_ms = 0;
    ctlr->last_frame_time_ms = 0;
    ctlr->first_saved_frame_time_ms = 0;
    ctlr->last_saved_frame_time_ms = 0;

    if (tal_semaphore_create_init(&ctlr->thread_sem, 0, 1) != OPRT_OK) {
        return TY_AVDK_ERR_IO;
    }
    if (tal_semaphore_create_init(&ctlr->stop_sem, 0, 1) != OPRT_OK) {
        tal_semaphore_release(ctlr->thread_sem);
        ctlr->thread_sem = NULL;
        return TY_AVDK_ERR_IO;
    }

    THREAD_CFG_T thrd = {
        .priority = THREAD_PRIO_1,
        .stackDepth = 8192,
        .thrdname = "ty_avi_rec",
    };
    if (tal_thread_create_and_start(&ctlr->record_thread, NULL, NULL, ty_recorder_thread, ctlr, &thrd) != OPRT_OK) {
        tal_semaphore_release(ctlr->thread_sem);
        tal_semaphore_release(ctlr->stop_sem);
        return TY_AVDK_ERR_IO;
    }

    ctlr->status = TY_VIDEO_RECORDER_STATUS_OPENED;
    return TY_AVDK_ERR_OK;
}

static ty_avdk_err_t ty_recorder_ctlr_close(ty_video_recorder_handle_t handler)
{
    ty_video_recorder_ctlr_t *ctlr = container_of(handler, ty_video_recorder_ctlr_t, ops);

    if (ctlr->record_thread) {
        ctlr->thread_exit = 1;
        ctlr->thread_running = 0;
        tal_semaphore_post(ctlr->thread_sem);
        tal_semaphore_post(ctlr->stop_sem);
        tal_thread_delete(ctlr->record_thread);
        ctlr->record_thread = NULL;
        tal_semaphore_release(ctlr->thread_sem);
        tal_semaphore_release(ctlr->stop_sem);
        ctlr->thread_sem = NULL;
        ctlr->stop_sem = NULL;
    }

    if (ctlr->avi_handle) {
        ty_avi_record_close(ctlr);
    }
    ty_recorder_release_file_path(ctlr);

    ctlr->status = TY_VIDEO_RECORDER_STATUS_CLOSED;
    return TY_AVDK_ERR_OK;
}

static ty_avdk_err_t ty_recorder_ctlr_start(ty_video_recorder_handle_t handler, char *file_path, uint32_t record_type)
{
    (void)record_type;
    ty_video_recorder_ctlr_t *ctlr = container_of(handler, ty_video_recorder_ctlr_t, ops);
    char *owned_file_path;
    size_t file_path_len;
    TY_AVDK_RETURN_ON_FALSE(file_path, TY_AVDK_ERR_INVAL, TAG, "path null");

    file_path_len = strlen(file_path) + 1u;
    owned_file_path = (char *)tal_malloc(file_path_len);
    TY_AVDK_RETURN_ON_FALSE(owned_file_path, TY_AVDK_ERR_NOMEM, TAG, "path alloc failed");
    memcpy(owned_file_path, file_path, file_path_len);

    ctlr->recording_start_time_ms = (uint32_t)sys_port.get_tick();
    ctlr->last_frame_time_ms = ctlr->recording_start_time_ms;
    ty_avdk_err_t ret = ty_avi_record_start(ctlr, owned_file_path);
    if (ret != TY_AVDK_ERR_OK) {
        tal_free(owned_file_path);
        return ret;
    }
    ctlr->file_path = owned_file_path;

    ctlr->video_frame_count = 0;
    ctlr->first_saved_frame_time_ms = 0;
    ctlr->last_saved_frame_time_ms = 0;
    ctlr->audio_bytes_written = 0;

    ctlr->thread_running = 1;
    tal_semaphore_post(ctlr->thread_sem);
    ctlr->status = TY_VIDEO_RECORDER_STATUS_STARTED;
    return TY_AVDK_ERR_OK;
}

static uint32_t ty_recorder_video_duration_ms(const ty_video_recorder_ctlr_t *ctlr,
                                              uint32_t session_duration_ms)
{
    uint32_t frame_count = ctlr->video_frame_count;

    if (frame_count >= 2 && ctlr->first_saved_frame_time_ms != 0 &&
        ctlr->last_saved_frame_time_ms != 0) {
        uint32_t span_ms = ctlr->last_saved_frame_time_ms -
                           ctlr->first_saved_frame_time_ms;

        if (span_ms > 0) {
            /* N frames contain N-1 measured intervals. Add one average frame
             * interval so AVI_update_video_frame_rate_by_duration(), which
             * computes N / duration, preserves the capture cadence. */
            uint64_t duration = (uint64_t)span_ms * frame_count /
                                (frame_count - 1u);
            uint32_t measured_ms = duration > UINT32_MAX ?
                                   UINT32_MAX : (uint32_t)duration;

            /* The estimated final frame interval can extend beyond the
             * instant recording actually stopped (the field log showed
             * 7156ms video from a 7023ms session). Never manufacture media
             * time that has no matching captured audio. */
            if (session_duration_ms > 0 && measured_ms > session_duration_ms) {
                measured_ms = session_duration_ms;
            }
            return measured_ms;
        }
    }

    if (frame_count == 1 && ctlr->config.record_framerate > 0) {
        return (1000u + ctlr->config.record_framerate - 1u) /
               ctlr->config.record_framerate;
    }
    return session_duration_ms;
}

static ty_avdk_err_t ty_recorder_ctlr_stop(ty_video_recorder_handle_t handler)
{
    ty_video_recorder_ctlr_t *ctlr = container_of(handler, ty_video_recorder_ctlr_t, ops);
    ty_avdk_err_t ret = TY_AVDK_ERR_OK;
    bool has_video;
    TY_AVDK_RETURN_ON_FALSE(ctlr->status == TY_VIDEO_RECORDER_STATUS_STARTED, TY_AVDK_ERR_INVAL, TAG, "not started");

    ctlr->thread_running = 0;
    tal_semaphore_post(ctlr->thread_sem);
    tal_semaphore_wait(ctlr->stop_sem, SEM_WAIT_FOREVER);

    has_video = ctlr->video_frame_count > 0;
    if (ctlr->avi_handle) {
        uint32_t session_duration_ms = (uint32_t)sys_port.get_tick() -
                                       ctlr->recording_start_time_ms;
        uint32_t video_duration_ms = 0;

        if (has_video) {
            ty_video_recorder_audio_data_t audio_data = {0};

            video_duration_ms = ty_recorder_video_duration_ms(
                ctlr, session_duration_ms);
            /* Pace the tail to the exact duration written into the AVI header.
             * For measured multi-frame clips this is capped to the real session,
             * so audio and video cannot diverge by a synthetic last interval. */
            ty_recorder_sync_audio(ctlr, &audio_data, video_duration_ms,
                                   UINT32_MAX, true);
            PR_NOTICE("avi stop sync session=%ums video=%ums audio=%ums/%uB",
                      session_duration_ms, video_duration_ms,
                      ty_recorder_audio_bytes_per_ms(ctlr) ?
                      ctlr->audio_bytes_written / ty_recorder_audio_bytes_per_ms(ctlr) : 0,
                      ctlr->audio_bytes_written);
        }
        ret = ty_avi_record_stop(ctlr, video_duration_ms);
        ctlr->avi_handle = NULL;
    }

    if (!has_video) {
        PR_ERR("recording stopped before any video frame was saved");
        ty_recorder_discard_empty_files(ctlr);
        ret = TY_AVDK_ERR_IO;
    }
    ty_recorder_release_file_path(ctlr);

    ctlr->status = TY_VIDEO_RECORDER_STATUS_STOPPED;
    return ret;
}

static ty_avdk_err_t ty_recorder_ctlr_delete(ty_video_recorder_handle_t handler)
{
    ty_video_recorder_ctlr_t *ctlr = container_of(handler, ty_video_recorder_ctlr_t, ops);

    if (ctlr->status == TY_VIDEO_RECORDER_STATUS_STARTED) {
        ty_recorder_ctlr_stop(handler);
    }
    if (ctlr->status != TY_VIDEO_RECORDER_STATUS_NONE && ctlr->status != TY_VIDEO_RECORDER_STATUS_CLOSED) {
        ty_recorder_ctlr_close(handler);
    }

    tal_free(ctlr);
    return TY_AVDK_ERR_OK;
}

ty_avdk_err_t ty_video_recorder_ctlr_new(ty_video_recorder_handle_t *handle, ty_video_recorder_ctlr_config_t *config)
{
    TY_AVDK_RETURN_ON_FALSE(handle && config, TY_AVDK_ERR_INVAL, TAG, "invalid param");

    ty_video_recorder_ctlr_t *ctlr = (ty_video_recorder_ctlr_t *)tal_malloc(sizeof(ty_video_recorder_ctlr_t));
    TY_AVDK_RETURN_ON_FALSE(ctlr, TY_AVDK_ERR_NOMEM, TAG, "no mem");
    memset(ctlr, 0, sizeof(*ctlr));

    memcpy(&ctlr->config, config, sizeof(*config));
    ctlr->ops.open = ty_recorder_ctlr_open;
    ctlr->ops.close = ty_recorder_ctlr_close;
    ctlr->ops.start = ty_recorder_ctlr_start;
    ctlr->ops.stop = ty_recorder_ctlr_stop;
    ctlr->ops.delete_recorder = ty_recorder_ctlr_delete;
    ctlr->status = TY_VIDEO_RECORDER_STATUS_NONE;

    *handle = &ctlr->ops;
    return TY_AVDK_ERR_OK;
}
