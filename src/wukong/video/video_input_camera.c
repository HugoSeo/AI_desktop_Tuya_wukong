/**
 * @file video_input_camera.c
 * @brief tal_camera-backed video input source.
 *
 * Owns the camera hardware end-to-end: board config pull, tal_camera
 * lifecycle, per-format start/stop with DVP dual-stream exclusivity
 * (JPEG+YUV <-> H264+YUV), one-shot JPEG snapshot, and — on ROBOT /
 * EVB_PRO — the legacy direct-to-LCD viewfinder path (ported as-is).
 *
 * Unified from the five per-board tuya_device_camera.c copies plus the
 * stream management of the retired tuya_ai_toy_camera adapter.
 */
#include "tuya_app_config.h"

#if defined(USING_CAMERA_VIDEO_INPUT) && (USING_CAMERA_VIDEO_INPUT == 1)

#include <string.h>
#include "wukong_video_input.h"
#include "tuya_device_cfg.h"
#include "tal_camera.h"
#include "tal_log.h"
#include "tal_mutex.h"
#include "tal_memory.h"
#include "tal_semaphore.h"
#include "tal_system.h"

/* ROBOT / EVB_PRO: camera frames render straight to the LCD while a stream
 * is up (DVP: YUV path, UVC: MJPEG path). Ported unchanged from the board
 * files; behaviour is deliberately identical to before the refactor. */
#if (defined(T5AI_BOARD_ROBOT) && (T5AI_BOARD_ROBOT == 1)) || \
    (defined(T5AI_BOARD_EVB_PRO) && (T5AI_BOARD_EVB_PRO == 1))
#define CAM_HAVE_LCD_DIRECT 1
#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
#include "tal_lcd_service.h"
#include "tuya_ai_display.h"
#endif
#endif

/* Internal per-stream users: the service layer counts consumers itself and
 * calls start/stop once per transition; SNAPSHOT is our own second user so a
 * one-shot grab can share or bring up the MJPEG stream without disturbing
 * the service's accounting. */
typedef enum {
    CAM_USER_SERVICE  = (1 << 0),
    CAM_USER_SNAPSHOT = (1 << 1),
} cam_user_e;

typedef struct {
    TAL_CAMERA_HANDLE_T handle;
    TAL_CAMERA_TYPE_E   type;
} cam_src_ctx_t;

static cam_src_ctx_t s_ctx = { .handle = NULL, .type = TAL_CAMERA_TYPE_MAX };

static UINT8_T      s_user_mask[VIDEO_FMT_MAX] = {0};
static MUTEX_HANDLE s_mtx = NULL;

/* One service callback slot per format (installed by ops->start). */
static VIDEO_FRAME_CB s_cbs[VIDEO_FMT_MAX]  = {NULL};
static VOID          *s_ctxs[VIDEO_FMT_MAX] = {NULL};

/* Snapshot capture slot (MJPEG frames). */
static SEM_HANDLE s_snap_sem     = NULL;
static BYTE_T    *s_snap_data    = NULL;
static UINT_T     s_snap_len     = 0;
static BOOL_T     s_snap_pending = FALSE;

static const TAL_STREAM_TYPE_E s_tal_stream[VIDEO_FMT_MAX] = {
    [VIDEO_FMT_YUV422] = TAL_STREAM_YUV422,
    [VIDEO_FMT_MJPEG]  = TAL_STREAM_MJPEG,
    [VIDEO_FMT_H264]   = TAL_STREAM_H264,
};

/* ------------------------------------------------------------------ */
/*  LCD direct display (ROBOT / EVB_PRO only)                          */
/* ------------------------------------------------------------------ */
#if defined(CAM_HAVE_LCD_DIRECT)
static VIDEO_FMT_E __display_fmt(VOID)
{
    return (s_ctx.type == TAL_CAMERA_TYPE_UVC) ? VIDEO_FMT_MJPEG : VIDEO_FMT_YUV422;
}

static VOID __display_acquired(VOID)
{
#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
    tuya_ai_display_pause();
    tal_lcd_service_dma2d_init();
#endif
}

static VOID __display_released(VOID)
{
#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
    tal_lcd_service_dma2d_deinit();
    tuya_ai_display_resume();
#endif
}

static VOID __on_camera_lcd_display(TAL_CAMERA_FRAME_T *frame)
{
#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
    tuya_lcd_pbuf_node_t *rgb_pbuf = NULL;
    OPERATE_RET ret = OPRT_COM_ERROR;

    if (s_ctx.type == TAL_CAMERA_TYPE_DVP && frame->fmt == TUYA_FRAME_FMT_YUV422) {
        ret = tal_lcd_service_yuv2rgb(frame->data, &rgb_pbuf);
    } else if (s_ctx.type == TAL_CAMERA_TYPE_UVC && frame->fmt == TUYA_FRAME_FMT_JPEG) {
        ret = tal_lcd_service_mjpeg2rgb(frame->data, frame->length, &rgb_pbuf);
    }
    if (ret != OPRT_OK || !rgb_pbuf) return;

    tuya_ai_display_flush(&rgb_pbuf->frame);
#else
    (VOID)frame;
#endif
}
#endif /* CAM_HAVE_LCD_DIRECT */

/* ------------------------------------------------------------------ */
/*  Per-format ref-counted acquire / release                           */
/* ------------------------------------------------------------------ */
static OPERATE_RET __fmt_acquire(VIDEO_FMT_E fmt, cam_user_e user)
{
    if (!s_ctx.handle) return OPRT_COM_ERROR;
    /* YUV/H264 exist only on the DVP dual-stream pipeline. */
    if ((fmt == VIDEO_FMT_YUV422 || fmt == VIDEO_FMT_H264) &&
        s_ctx.type != TAL_CAMERA_TYPE_DVP) {
        return OPRT_NOT_SUPPORTED;
    }

    BOOL_T need_start = FALSE;
    tal_mutex_lock(s_mtx);
    need_start = (s_user_mask[fmt] == 0);
    s_user_mask[fmt] |= (UINT8_T)user;
    tal_mutex_unlock(s_mtx);

    if (need_start) {
#if defined(CAM_HAVE_LCD_DIRECT)
        if (__display_fmt() == fmt) {
            __display_acquired();
        }
#endif

        OPERATE_RET rt = tal_camera_start_stream(s_ctx.handle, s_tal_stream[fmt]);
        if (OPRT_OK != rt) {
            tal_mutex_lock(s_mtx);
            s_user_mask[fmt] &= (UINT8_T)~user;
            tal_mutex_unlock(s_mtx);
    #if defined(CAM_HAVE_LCD_DIRECT)
            if (__display_fmt() == fmt) {
                __display_released();
            }
    #endif
            return rt;
        }
    }
    return OPRT_OK;
}

static OPERATE_RET __fmt_release(VIDEO_FMT_E fmt, cam_user_e user)
{
    if (!s_ctx.handle) return OPRT_COM_ERROR;
    if ((fmt == VIDEO_FMT_YUV422 || fmt == VIDEO_FMT_H264) &&
        s_ctx.type != TAL_CAMERA_TYPE_DVP) {
        return OPRT_OK;
    }

    BOOL_T need_stop = FALSE;
    tal_mutex_lock(s_mtx);
    s_user_mask[fmt] &= (UINT8_T)~user;
    need_stop = (s_user_mask[fmt] == 0);
    tal_mutex_unlock(s_mtx);

    if (need_stop) {
        tal_camera_stop_stream(s_ctx.handle, s_tal_stream[fmt]);
#if defined(CAM_HAVE_LCD_DIRECT)
        if (__display_fmt() == fmt) {
            __display_released();
        }
#endif
    }
    return OPRT_OK;
}

/* ------------------------------------------------------------------ */
/*  Frame dispatch: TAL_CAMERA_FRAME_T -> VIDEO_FRAME_T -> service cb  */
/* ------------------------------------------------------------------ */
static VOID __dispatch(VIDEO_FMT_E fmt, TAL_CAMERA_FRAME_T *frame)
{
    if (fmt >= VIDEO_FMT_MAX || !frame) return;

    VIDEO_FRAME_CB cb  = NULL;
    VOID          *ctx = NULL;

    tal_mutex_lock(s_mtx);
    cb  = s_cbs[fmt];
    ctx = s_ctxs[fmt];
    tal_mutex_unlock(s_mtx);

    if (!cb) return;

    VIDEO_FRAME_T f = {
        .data         = (UCHAR_T *)frame->data,
        .length       = frame->length,
        .width        = frame->width,
        .height       = frame->height,
        .timestamp_ms = frame->timestamp,
        .fmt          = fmt,
        .is_key_frame = frame->is_i_frame,
    };
    cb(&f, ctx);
}

static VOID __snap_capture(TAL_CAMERA_FRAME_T *frame)
{
    if (!s_snap_pending || s_snap_data) return;

    BYTE_T *buf = (BYTE_T *)tal_psram_malloc(frame->length);
    if (!buf) {
        TAL_PR_ERR("snapshot psram_malloc failed, len=%u", frame->length);
        return;
    }
    memcpy(buf, frame->data, frame->length);

    tal_mutex_lock(s_mtx);
    if (s_snap_pending && !s_snap_data) {
        s_snap_data    = buf;
        s_snap_len     = frame->length;
        s_snap_pending = FALSE;
        buf = NULL;
    }
    tal_mutex_unlock(s_mtx);

    if (buf) {
        tal_psram_free(buf);   /* raced with another frame; drop the copy */
    } else {
        tal_semaphore_post(s_snap_sem);
    }
}

static void __on_yuv_frame(TAL_CAMERA_HANDLE_T handle, TAL_CAMERA_FRAME_T *frame, void *args)
{
#if defined(CAM_HAVE_LCD_DIRECT)
    __on_camera_lcd_display(frame);
#endif
    __dispatch(VIDEO_FMT_YUV422, frame);
}

static void __on_mjpeg_frame(TAL_CAMERA_HANDLE_T handle, TAL_CAMERA_FRAME_T *frame, void *args)
{
#if defined(CAM_HAVE_LCD_DIRECT)
    if (s_ctx.type == TAL_CAMERA_TYPE_UVC) {
        __on_camera_lcd_display(frame);
    }
#endif
    __snap_capture(frame);
    __dispatch(VIDEO_FMT_MJPEG, frame);
}

static void __on_h264_frame(TAL_CAMERA_HANDLE_T handle, TAL_CAMERA_FRAME_T *frame, void *args)
{
    __dispatch(VIDEO_FMT_H264, frame);
}

/* ------------------------------------------------------------------ */
/*  DVP dual-stream output mode                                        */
/* ------------------------------------------------------------------ */
/* A mode switch tears the DVP down and re-inits it, which re-allocates the
 * yuv pingpong buffer (width*16*2 for JPEG, width*32*2 for H264). On the P2P
 * call teardown path this runs while the P2P session still holds its pools, so
 * the allocation can fail transiently — retry before giving up, otherwise the
 * camera comes back in the wrong mode (or not at all) for the rest of the
 * session. */
#define CAM_MODE_SWITCH_TRY_MAX 3
#define CAM_MODE_SWITCH_TRY_MS  100

static OPERATE_RET __switch_output_mode(BOOL_T h264)
{
    if (!s_ctx.handle || s_ctx.type != TAL_CAMERA_TYPE_DVP) {
        return OPRT_NOT_SUPPORTED;
    }

    TAL_CAMERA_CFG_T cfg = {0};
    if (tuya_board_get_camera_cfg(&cfg) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }

    TUYA_CAMERA_OUTPUT_MODE mode = h264 ? TUYA_CAMERA_OUTPUT_H264_YUV422_BOTH
                                        : TUYA_CAMERA_OUTPUT_JPEG_YUV422_BOTH;

    OPERATE_RET rt = OPRT_COM_ERROR;
    for (int i = 0; i < CAM_MODE_SWITCH_TRY_MAX; i++) {
        if (i) {
            tal_system_sleep(CAM_MODE_SWITCH_TRY_MS);
        }
        rt = tal_camera_switch_output_mode(s_ctx.handle, &cfg, mode);
        if (rt == OPRT_OK) {
            return OPRT_OK;
        }
        TAL_PR_WARN("switch output mode to %d failed: %d (try %d/%d)",
                    mode, rt, i + 1, CAM_MODE_SWITCH_TRY_MAX);
    }

    TAL_PR_ERR("switch output mode to %d gave up after %d tries",
               mode, CAM_MODE_SWITCH_TRY_MAX);
    return rt;
}

/* ------------------------------------------------------------------ */
/*  VIDEO_INPUT_OPS_T implementation                                   */
/* ------------------------------------------------------------------ */
static OPERATE_RET __cam_init(VOID)
{
    TAL_PR_INFO("video input: camera init");

    TAL_CAMERA_CFG_T cfg = {0};
    OPERATE_RET rt = tuya_board_get_camera_cfg(&cfg);
    if (rt != OPRT_OK) {
        TAL_PR_WARN("no camera on this board, skip init");
        return rt;
    }
    s_ctx.type = cfg.type;

#if defined(CAM_HAVE_LCD_DIRECT) && defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
    BOOL_T byte_swap = (strncmp(TUYA_LCD_IC_NAME, "spi_", 4) == 0);
    TAL_LCD_DMA2D_MODE_E lcd_mode = (cfg.type == TAL_CAMERA_TYPE_UVC)
                                    ? TAL_LCD_DMA2D_MODE_JPEG
                                    : TAL_LCD_DMA2D_MODE_YUV;
    rt = tal_lcd_service_init(TUYA_AI_TOY_ISP_WIDTH, TUYA_AI_TOY_ISP_HEIGHT,
                              TUYA_LCD_WIDTH, TUYA_LCD_HEIGHT,
                              3, lcd_mode, byte_swap);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("lcd_service init failed: %d", rt);
        return rt;
    }
#endif

    if (!s_mtx) {
        rt = tal_mutex_create_init(&s_mtx);
        if (OPRT_OK != rt) return rt;
    }
    if (!s_snap_sem) {
        rt = tal_semaphore_create_init(&s_snap_sem, 0, 1);
        if (OPRT_OK != rt) return rt;
    }

    s_ctx.handle = tal_camera_init(&cfg);
    if (!s_ctx.handle) {
        TAL_PR_ERR("tal_camera_init failed");
        return OPRT_COM_ERROR;
    }

    tal_camera_register_cb(s_ctx.handle, TAL_STREAM_MJPEG,  __on_mjpeg_frame, NULL);
    tal_camera_register_cb(s_ctx.handle, TAL_STREAM_YUV422, __on_yuv_frame,   NULL);
    tal_camera_register_cb(s_ctx.handle, TAL_STREAM_H264,   __on_h264_frame,  NULL);

    memset((void *)s_user_mask, 0, sizeof(s_user_mask));
    memset((void *)s_cbs, 0, sizeof(s_cbs));
    memset((void *)s_ctxs, 0, sizeof(s_ctxs));

    TAL_PR_DEBUG("camera input ok, type=%d", s_ctx.type);
    return OPRT_OK;
}

static OPERATE_RET __cam_deinit(VOID)
{
    if (s_ctx.handle) {
        tal_camera_deinit(s_ctx.handle);
        s_ctx.handle = NULL;
    }
#if defined(CAM_HAVE_LCD_DIRECT) && defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
    tal_lcd_service_deinit();
#endif
    s_ctx.type = TAL_CAMERA_TYPE_MAX;
    memset((void *)s_user_mask, 0, sizeof(s_user_mask));
    memset((void *)s_cbs, 0, sizeof(s_cbs));
    memset((void *)s_ctxs, 0, sizeof(s_ctxs));
    return OPRT_OK;
}

static OPERATE_RET __cam_start(VIDEO_FMT_E fmt, VIDEO_FRAME_CB cb, VOID *ctx)
{
    if (fmt >= VIDEO_FMT_MAX || !cb) return OPRT_INVALID_PARM;

    /* H264 rides the DVP dual-stream pipeline: switch mode first so the very
     * first started frame is already H264 (was the P2P caller's job before). */
    if (fmt == VIDEO_FMT_H264) {
        __switch_output_mode(TRUE);
    }

    tal_mutex_lock(s_mtx);
    s_cbs[fmt]  = cb;
    s_ctxs[fmt] = ctx;
    tal_mutex_unlock(s_mtx);

    OPERATE_RET rt = __fmt_acquire(fmt, CAM_USER_SERVICE);
    if (OPRT_OK != rt) {
        tal_mutex_lock(s_mtx);
        s_cbs[fmt]  = NULL;
        s_ctxs[fmt] = NULL;
        tal_mutex_unlock(s_mtx);
        if (fmt == VIDEO_FMT_H264) {
            __switch_output_mode(FALSE);
        }
    }
    return rt;
}

static OPERATE_RET __cam_stop(VIDEO_FMT_E fmt)
{
    if (fmt >= VIDEO_FMT_MAX) return OPRT_INVALID_PARM;

    OPERATE_RET rt = __fmt_release(fmt, CAM_USER_SERVICE);

    tal_mutex_lock(s_mtx);
    s_cbs[fmt]  = NULL;
    s_ctxs[fmt] = NULL;
    tal_mutex_unlock(s_mtx);

    if (fmt == VIDEO_FMT_H264) {
        __switch_output_mode(FALSE);   /* back to default AI (JPEG+YUV) mode */
    }
    return rt;
}

static OPERATE_RET __cam_get_caps(VIDEO_INPUT_CAPS_T *caps)
{
    if (!caps) return OPRT_INVALID_PARM;
    if (!s_ctx.handle) return OPRT_COM_ERROR;

    caps->width  = TUYA_AI_TOY_ISP_WIDTH;
    caps->height = TUYA_AI_TOY_ISP_HEIGHT;
    caps->fps    = TUYA_AI_TOY_ISP_FPS;
    caps->fmt_mask = (s_ctx.type == TAL_CAMERA_TYPE_DVP)
        ? (VIDEO_FMT_BIT(VIDEO_FMT_YUV422) | VIDEO_FMT_BIT(VIDEO_FMT_MJPEG) |
           VIDEO_FMT_BIT(VIDEO_FMT_H264))
        : VIDEO_FMT_BIT(VIDEO_FMT_MJPEG);
    return OPRT_OK;
}

static OPERATE_RET __cam_snapshot(BYTE_T **jpeg, UINT_T *len, UINT_T timeout_ms)
{
    if (!jpeg || !len) return OPRT_INVALID_PARM;
    *jpeg = NULL;
    *len  = 0;
    if (!s_ctx.handle || !s_snap_sem) return OPRT_COM_ERROR;

    tal_mutex_lock(s_mtx);
    if (s_snap_pending) {
        tal_mutex_unlock(s_mtx);
        return OPRT_COM_ERROR;   /* one snapshot at a time */
    }
    /* Drain any post left over from a PREVIOUS call: if that call's
     * tal_semaphore_wait() timed out while a late frame was concurrently
     * being captured by __snap_capture() (e.g. during __fmt_release()'s
     * stream teardown window), the late post lands on s_snap_sem with
     * nobody left to consume it. Left alone, it would instantly satisfy
     * the tal_semaphore_wait() below while s_snap_data is still NULL,
     * returning OPRT_OK with an empty buffer. s_snap_sem's max count is 1
     * (see tal_semaphore_create_init below), so this loop is bounded to a
     * single non-blocking try; timeout=0 is this codebase's established
     * non-blocking "try" convention (see src/miscs/display/tuya_dma2d.c). */
    while (tal_semaphore_wait(s_snap_sem, 0) == OPRT_OK) { }
    s_snap_pending = TRUE;
    s_snap_data    = NULL;
    s_snap_len     = 0;
    tal_mutex_unlock(s_mtx);

    OPERATE_RET rt = __fmt_acquire(VIDEO_FMT_MJPEG, CAM_USER_SNAPSHOT);
    if (OPRT_OK != rt) {
        tal_mutex_lock(s_mtx);
        s_snap_pending = FALSE;
        tal_mutex_unlock(s_mtx);
        return rt;
    }

    rt = tal_semaphore_wait(s_snap_sem, timeout_ms);
    __fmt_release(VIDEO_FMT_MJPEG, CAM_USER_SNAPSHOT);

    tal_mutex_lock(s_mtx);
    s_snap_pending = FALSE;
    BYTE_T *data = s_snap_data;
    UINT_T  dlen = s_snap_len;
    s_snap_data  = NULL;
    s_snap_len   = 0;
    tal_mutex_unlock(s_mtx);

    if (OPRT_OK != rt) {
        /* Timed out. A frame already in flight inside __snap_capture() may
         * still land after this point (it only re-checks s_snap_pending /
         * s_snap_data, both already reset above under s_mtx) and post
         * s_snap_sem on our way out. That post is deliberately left
         * outstanding here — NOT consumed on this path — so it can never be
         * mistaken for this call's own result; it is drained at the top of
         * the *next* __cam_snapshot() call instead (see above). Any buffer
         * such a late frame already stashed in `data` is freed here so it
         * doesn't leak. */
        if (data) tal_psram_free(data);
        TAL_PR_ERR("snapshot timeout (%u ms)", timeout_ms);
        return OPRT_TIMEOUT;
    }
    if (!data) {
        /* Defensive: wait reported success but no buffer was ever stashed.
         * Should not happen given the pending/data guards in
         * __snap_capture() plus the drain above, but the OK contract is
         * "valid buffer" — never hand back OPRT_OK with *jpeg == NULL. */
        TAL_PR_ERR("snapshot semaphore signaled with no data");
        return OPRT_COM_ERROR;
    }
    *jpeg = data;
    *len  = dlen;
    return OPRT_OK;
}

static const VIDEO_INPUT_OPS_T s_camera_ops = {
    .init     = __cam_init,
    .deinit   = __cam_deinit,
    .start    = __cam_start,
    .stop     = __cam_stop,
    .get_caps = __cam_get_caps,
    .snapshot = __cam_snapshot,
};

OPERATE_RET video_input_camera_register(VOID)
{
    return wukong_video_input_register(&s_camera_ops);
}

#endif /* USING_CAMERA_VIDEO_INPUT */
