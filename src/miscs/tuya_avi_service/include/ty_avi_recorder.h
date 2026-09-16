#ifndef __TY_AVI_RECORDER_H__
#define __TY_AVI_RECORDER_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TY_AVI_RECORDER_EVENT_STORAGE_FAULT = 1,
} TY_AVI_RECORDER_EVENT_E;

/* Runs on the recorder thread. The callback must return promptly and must not
 * call ty_avi_recorder_stop() directly; schedule teardown on an owner workqueue. */
typedef void (*ty_avi_recorder_event_cb_t)(TY_AVI_RECORDER_EVENT_E event,
                                           void *user_data);

typedef struct {
    const char *file_path;
    uint16_t width;
    uint16_t height;
    uint8_t fps;
    uint8_t enable_preview;
    uint8_t enable_video_preview;
    uint8_t enable_audio_preview;
    const char *lcd_name;
    uint8_t lcd_name_len;
    ty_avi_recorder_event_cb_t event_cb;
    void *event_user_data;
} TY_AVI_RECORDER_CFG_T;

/* Demo integration contract:
 * - file_path is absolute and storage is already mounted by Wukong.
 * - active MJPEG source dimensions override width/height.
 * - camera UI owns preview resources.
 * - audio: the mic is a single, exclusive resource shared with AI wake-word
 *   listening (see ADR — Plan A). While recording, the AI mode layer
 *   reroutes mic PCM here via ty_avi_recorder_audio_feed() instead of the
 *   normal AI upload path; the assistant does not hear wake words during
 *   that window. If a P2P call owns the mic when recording starts, this
 *   recorder falls back to video-only rather than declare an audio track
 *   that never receives data. */

OPERATE_RET ty_avi_recorder_start(const TY_AVI_RECORDER_CFG_T *cfg);
OPERATE_RET ty_avi_recorder_stop(void);
BOOL_T ty_avi_recorder_is_running(void);
/* Feed one mic PCM chunk (16kHz/16bit/mono) to the active recording. Callers
 * should route audio here instead of the normal AI path only while
 * ty_avi_recorder_is_running() — this call is a no-op (not an error) when no
 * recording is active, so callers do not need to double-check under a lock.
 * Best-effort: silently drops the chunk if recording isn't accepting audio
 * (not started yet, or fell back to video-only for this session). */
OPERATE_RET ty_avi_recorder_audio_feed(const uint8_t *data, uint32_t len);
OPERATE_RET ty_avi_recorder_set_video_preview(uint8_t on);
OPERATE_RET ty_avi_recorder_set_audio_preview(uint8_t on);
uint8_t ty_avi_recorder_get_video_preview(void);
uint8_t ty_avi_recorder_get_audio_preview(void);

void ty_avi_rec_stat_on_dvp_frame(void);
void ty_avi_rec_stat_on_enq_frame(void);
void ty_avi_rec_stat_on_avi_frame(void);
void ty_avi_rec_stat_on_avi_skip(void);
void ty_avi_rec_stat_on_write(uint32_t bytes, uint32_t cost_ms, uint8_t is_video);
void ty_avi_rec_stat_on_write_error(uint8_t is_video);

#ifdef __cplusplus
}
#endif

#endif
