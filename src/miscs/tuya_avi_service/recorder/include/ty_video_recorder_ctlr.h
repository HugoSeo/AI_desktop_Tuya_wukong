#ifndef __TY_VIDEO_RECORDER_CTLR_H__
#define __TY_VIDEO_RECORDER_CTLR_H__

#include "ty_video_recorder_types.h"
#include "tal_mutex.h"
#include "tal_semaphore.h"
#include "tal_thread.h"

typedef struct ty_video_recorder_config ty_video_recorder_ctlr_config_t;

typedef struct {
    ty_video_recorder_config_t config;
    ty_video_recorder_t ops;
    ty_video_recorder_status_t status;
    char *file_path;          /* owned copy, valid until stop/close */
    void *avi_handle;
    MUTEX_HANDLE avi_write_mutex;
    THREAD_HANDLE record_thread;
    SEM_HANDLE thread_sem;
    SEM_HANDLE stop_sem;
    volatile uint8_t thread_running;
    volatile uint8_t thread_exit;
    uint32_t video_frame_count;
    uint32_t recording_start_time_ms;
    uint32_t last_frame_time_ms;
    uint32_t first_saved_frame_time_ms;
    uint32_t last_saved_frame_time_ms;
    uint32_t audio_bytes_written;
} ty_video_recorder_ctlr_t;

ty_avdk_err_t ty_video_recorder_ctlr_new(ty_video_recorder_handle_t *handle, ty_video_recorder_ctlr_config_t *config);

#endif
