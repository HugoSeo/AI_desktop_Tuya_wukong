#ifndef __UI_SVC_VIDEO_H__
#define __UI_SVC_VIDEO_H__

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UI_VIDEO_STATE_IDLE = 0,
    UI_VIDEO_STATE_STARTING,
    UI_VIDEO_STATE_RECORDING,
    UI_VIDEO_STATE_STOPPING,
    UI_VIDEO_STATE_ERROR,
} ui_svc_video_state_t;

typedef enum {
    UI_VIDEO_ERROR_NONE = 0,
    UI_VIDEO_ERROR_STORAGE,
    UI_VIDEO_ERROR_CAMERA,
    UI_VIDEO_ERROR_START,
    UI_VIDEO_ERROR_STOP,
} ui_svc_video_error_t;

typedef void (*ui_svc_video_cb_t)(ui_svc_video_state_t state,
                                  ui_svc_video_error_t error);

/* The vendor index profile reserves 120 seconds. Stop below that boundary so
 * the close/index write has headroom. */
#define UI_SVC_VIDEO_MAX_DURATION_MS 110000u

void ui_svc_video_init(void);
bool ui_svc_video_available(void);
void ui_svc_video_set_cb(ui_svc_video_cb_t cb);
ui_svc_video_state_t ui_svc_video_get_state(void);
uint32_t ui_svc_video_get_elapsed_ms(void);
void ui_svc_video_record_toggle(void);
void ui_svc_video_record_stop(void);

#endif /* __UI_SVC_VIDEO_H__ */
