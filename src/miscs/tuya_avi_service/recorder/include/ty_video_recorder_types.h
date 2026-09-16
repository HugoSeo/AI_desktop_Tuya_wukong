#ifndef __TY_VIDEO_RECORDER_TYPES_H__
#define __TY_VIDEO_RECORDER_TYPES_H__

#include "ty_avdk_types.h"

typedef struct ty_video_recorder *ty_video_recorder_handle_t;
typedef struct ty_video_recorder ty_video_recorder_t;
typedef struct ty_video_recorder_config ty_video_recorder_config_t;

typedef enum {
    TY_VIDEO_RECORDER_STATUS_NONE = 0,
    TY_VIDEO_RECORDER_STATUS_OPENED,
    TY_VIDEO_RECORDER_STATUS_CLOSED,
    TY_VIDEO_RECORDER_STATUS_STARTED,
    TY_VIDEO_RECORDER_STATUS_STOPPED,
} ty_video_recorder_status_t;

#define TY_VIDEO_RECORDER_TYPE_AVI    0

#define TY_VIDEO_RECORDER_FORMAT_MJPEG 1

#define TY_VIDEO_RECORDER_AUDIO_FORMAT_PCM  0x0001u

typedef struct ty_video_recorder_frame_data_s {
    uint8_t *data;
    uint32_t length;
    uint32_t timestamp_ms;
    uint32_t width;
    uint32_t height;
    void *frame_buffer;
    uint32_t is_key_frame;
} ty_video_recorder_frame_data_t;

typedef struct ty_video_recorder_audio_data_s {
    uint8_t *data;
    uint32_t length;
} ty_video_recorder_audio_data_t;

typedef int (*ty_video_recorder_get_frame_cb_t)(void *user_data, ty_video_recorder_frame_data_t *frame_data);
typedef int (*ty_video_recorder_get_audio_cb_t)(void *user_data, ty_video_recorder_audio_data_t *audio_data);
typedef void (*ty_video_recorder_release_frame_cb_t)(void *user_data, ty_video_recorder_frame_data_t *frame_data);
typedef void (*ty_video_recorder_release_audio_cb_t)(void *user_data, ty_video_recorder_audio_data_t *audio_data);

typedef struct ty_video_recorder_config {
    uint32_t record_type;
    uint32_t record_format;
    uint32_t record_framerate;
    uint32_t video_width;
    uint32_t video_height;
    uint32_t audio_channels;
    uint32_t audio_rate;
    uint32_t audio_bits;
    uint32_t audio_format;
    ty_video_recorder_get_frame_cb_t get_frame_cb;
    ty_video_recorder_get_audio_cb_t get_audio_cb;
    ty_video_recorder_release_frame_cb_t release_frame_cb;
    ty_video_recorder_release_audio_cb_t release_audio_cb;
    void *user_data;
} ty_video_recorder_config_t;

typedef struct ty_video_recorder {
    ty_avdk_err_t (*open)(ty_video_recorder_handle_t handle);
    ty_avdk_err_t (*close)(ty_video_recorder_handle_t handle);
    ty_avdk_err_t (*start)(ty_video_recorder_handle_t handle, char *file_path, uint32_t record_type);
    ty_avdk_err_t (*stop)(ty_video_recorder_handle_t handle);
    ty_avdk_err_t (*delete_recorder)(ty_video_recorder_handle_t handle);
} ty_video_recorder_t;

#endif
