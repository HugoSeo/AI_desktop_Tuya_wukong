#ifndef __TY_AVI_PLAYER_H__
#define __TY_AVI_PLAYER_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TY_AVI_PLAYER_FF_MS_DEF      5000u
#define TY_AVI_PLAYER_REWIND_MS_DEF  5000u

typedef enum {
    TY_AVI_PLAYER_EVENT_COMPLETE = 0,
    TY_AVI_PLAYER_EVENT_ERROR,
} TY_AVI_PLAYER_EVENT_E;

/**
 * One decoded RGB565 video frame. Ownership of @data transfers to frame_cb;
 * release it with ty_avi_player_frame_free() when it is replaced or dropped.
 */
typedef struct {
    uint8_t *data;
    uint16_t width;
    uint16_t height;
    uint32_t index;
    uint32_t pts_ms;
} TY_AVI_PLAYER_FRAME_T;

typedef void (*TY_AVI_PLAYER_FRAME_CB)(TY_AVI_PLAYER_FRAME_T *frame, void *user_data);
typedef void (*TY_AVI_PLAYER_EVENT_CB)(TY_AVI_PLAYER_EVENT_E event, void *user_data);
/**
 * One PCM audio chunk read straight from the AVI file, wall-clock-paced
 * against the same position() the video frames use. @data is a buffer owned
 * by the player, valid only for the duration of the call — copy it if you
 * need it past this call (matches the DAC-write idiom: consumers should just
 * enqueue/write synchronously here, not hold onto the pointer).
 */
typedef void (*TY_AVI_PLAYER_AUDIO_CB)(const uint8_t *data, uint32_t len, void *user_data);
/**
 * Colour-space converter supplied by the application, because the single DMA2D
 * engine is owned outside this module (see README "资源边界"). Converts a
 * YUV422 buffer to RGB565 at the SAME geometry — it must not scale, the player
 * does its own downscale afterwards.
 *
 * Supplying it arms the hardware JPEG path (platform decoder -> YUV422 -> this
 * hook -> RGB565), which is what makes playback keep up with the recorded frame
 * rate; the platform decoder only engages its hardware block for YUV422 output.
 * Leaving it NULL keeps the pure-software JPEG -> RGB565 path.
 */
typedef OPERATE_RET (*TY_AVI_PLAYER_YUV2RGB_CB)(uint8_t *in_buf, uint16_t in_w, uint16_t in_h,
                                                uint8_t *out_buf, uint16_t out_w, uint16_t out_h);

typedef struct {
    /** Absolute path supplied by the application filesystem service. */
    const char *file_path;
    /** Maximum decoded frame size; aspect ratio is preserved. */
    uint16_t max_width;
    uint16_t max_height;
    TY_AVI_PLAYER_FRAME_CB frame_cb;
    TY_AVI_PLAYER_EVENT_CB event_cb;
    /** Optional. NULL falls back to software JPEG decode. */
    TY_AVI_PLAYER_YUV2RGB_CB yuv422_to_rgb565;
    /** Optional. NULL, or a file with no audio track, plays video-only —
     *  identical to the pre-audio behavior. */
    TY_AVI_PLAYER_AUDIO_CB audio_cb;
    /** Open and create the decode thread, but hold the media clock until
     *  ty_avi_player_resume(). Lets a caller prepare its audio sink without
     *  losing the first PCM block. */
    BOOL_T start_paused;
    void *user_data;
} TY_AVI_PLAYER_CFG_T;

/* Callback-driven AVI decoder. It never mounts storage, opens a display, or
 * touches ADC/DAC; the application owns those resources — audio_cb hands
 * raw PCM to the caller the same way frame_cb hands over RGB565, so the
 * caller decides how (or whether) to reach a speaker. */
OPERATE_RET ty_avi_player_start(const TY_AVI_PLAYER_CFG_T *cfg);
OPERATE_RET ty_avi_player_stop(void);
OPERATE_RET ty_avi_player_pause(void);
OPERATE_RET ty_avi_player_resume(void);
OPERATE_RET ty_avi_player_seek(uint32_t time_ms);
OPERATE_RET ty_avi_player_fast_forward(uint32_t delta_ms);
OPERATE_RET ty_avi_player_rewind(uint32_t delta_ms);
uint32_t ty_avi_player_get_position_ms(void);
uint32_t ty_avi_player_get_duration_ms(void);
BOOL_T ty_avi_player_is_running(void);
BOOL_T ty_avi_player_is_paused(void);
/** Valid only once ty_avi_player_start() has returned OK — reflects whether
 *  this file has a usable audio track AND a non-NULL audio_cb was supplied,
 *  i.e. whether the caller should actually expect audio_cb calls. */
BOOL_T ty_avi_player_has_audio(void);
void ty_avi_player_frame_free(void *buf);

#ifdef __cplusplus
}
#endif

#endif /* __TY_AVI_PLAYER_H__ */
