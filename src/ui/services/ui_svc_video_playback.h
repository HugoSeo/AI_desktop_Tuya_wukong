#ifndef __UI_SVC_VIDEO_PLAYBACK_H__
#define __UI_SVC_VIDEO_PLAYBACK_H__

#include <stdbool.h>
#include <stdint.h>

#include "tuya_cloud_types.h"

#define UI_VIDEO_PLAYBACK_NAME_MAX 128
#define UI_VIDEO_PLAYBACK_LIST_MAX 64

typedef enum {
    UI_VIDEO_PLAYBACK_IDLE = 0,
    UI_VIDEO_PLAYBACK_OPENING,
    UI_VIDEO_PLAYBACK_PLAYING,
    UI_VIDEO_PLAYBACK_PAUSED,
    UI_VIDEO_PLAYBACK_STOPPING,
    UI_VIDEO_PLAYBACK_COMPLETED,
    UI_VIDEO_PLAYBACK_ERROR,
} ui_video_playback_state_t;

typedef enum {
    UI_VIDEO_PLAYBACK_ERROR_NONE = 0,
    UI_VIDEO_PLAYBACK_ERROR_STORAGE,
    UI_VIDEO_PLAYBACK_ERROR_OPEN,
    UI_VIDEO_PLAYBACK_ERROR_DECODE,
    UI_VIDEO_PLAYBACK_ERROR_CONTROL,
} ui_video_playback_error_t;

typedef struct {
    char name[UI_VIDEO_PLAYBACK_NAME_MAX];
    uint64_t size;
} ui_video_playback_item_t;

typedef struct {
    uint8_t *data;
    uint16_t width;
    uint16_t height;
    uint32_t index;
    uint32_t pts_ms;
} ui_video_playback_frame_t;

/* All callbacks run on the UI thread. List data is borrowed for the callback;
 * frame data ownership transfers to the receiver. */
typedef void (*ui_video_playback_list_cb_t)(OPERATE_RET rt,
                                            const ui_video_playback_item_t *items,
                                            uint16_t count, uint32_t seq);
typedef void (*ui_video_playback_frame_cb_t)(ui_video_playback_frame_t *frame);
typedef void (*ui_video_playback_state_cb_t)(ui_video_playback_state_t state,
                                              ui_video_playback_error_t error);
typedef void (*ui_video_playback_delete_cb_t)(OPERATE_RET rt,
                                               uint16_t deleted_count,
                                               uint16_t failed_count,
                                               uint32_t seq);

void ui_svc_video_playback_init(void);
bool ui_svc_video_playback_available(void);
void ui_svc_video_playback_set_cbs(ui_video_playback_list_cb_t list_cb,
                                   ui_video_playback_frame_cb_t frame_cb,
                                   ui_video_playback_state_cb_t state_cb,
                                   ui_video_playback_delete_cb_t delete_cb);
void ui_svc_video_playback_list_request(uint32_t seq);
/**
 * Copy and asynchronously delete app-owned AVI leaf names from UI_FS_VIDEO.
 * The result callback runs on the UI thread. Deletion is rejected while a
 * player session still owns a file.
 */
OPERATE_RET ui_svc_video_playback_delete_batch(const char *const names[],
                                                uint16_t count, uint32_t seq);
void ui_svc_video_playback_play(const char *name);
void ui_svc_video_playback_pause_resume(void);
void ui_svc_video_playback_stop(void);
ui_video_playback_state_t ui_svc_video_playback_get_state(void);
uint32_t ui_svc_video_playback_get_position_ms(void);
uint32_t ui_svc_video_playback_get_duration_ms(void);
void ui_svc_video_playback_frame_free(void *buf);

#endif /* __UI_SVC_VIDEO_PLAYBACK_H__ */
