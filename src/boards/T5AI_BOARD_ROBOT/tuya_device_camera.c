#include "tuya_device_cfg.h"
#include "tal_camera.h"
#include "tal_log.h"
#include "tal_mutex.h"
#include "tal_memory.h"
#include "tuya_device_camera.h"

#if defined(ENABLE_TUYA_UI) && ENABLE_TUYA_UI == 1
#include "tal_lcd_service.h"
#include "tuya_ai_display.h"
#endif

#if defined(ENABLE_TUYA_CAMERA) && (ENABLE_TUYA_CAMERA == 1)

typedef struct {
    TAL_CAMERA_HANDLE_T handle;
    TAL_CAMERA_TYPE_E   type;
} ai_camera_ctx_t;

static ai_camera_ctx_t s_ctx = {
    .handle = NULL,
    .type   = TAL_CAMERA_TYPE_MAX,
};

typedef enum {
    CAM_USER_ADAPTER = (1 << 0),
} CAM_USER_E;

static UINT8_T      sg_yuv_user_mask   = 0;
static UINT8_T      sg_mjpeg_user_mask = 0;
static UINT8_T      sg_h264_user_mask  = 0;
static MUTEX_HANDLE sg_user_mask_mtx   = NULL;

static CAM_FRAME_CB sg_raw_cbs[CAM_STREAM_MAX]  = {NULL};
static VOID        *sg_raw_ctxs[CAM_STREAM_MAX] = {NULL};

/* ---------------------------------------------------------------------------
 * LCD display co-ordination.
 * On DVP the display consumes YUV422 frames; on UVC the MJPEG stream doubles
 * as the display source because no YUV422 is available. UI pause/resume and
 * the DMA2D grab/release are bound to the display stream's first acquire /
 * last release so the UI reclaims the LCD exactly when the preview stops.
 * --------------------------------------------------------------------------- */
static CAM_STREAM_E __display_stream(VOID)
{
    return (s_ctx.type == TAL_CAMERA_TYPE_UVC) ? CAM_STREAM_MJPEG : CAM_STREAM_YUV422;
}

static VOID __display_acquired(VOID)
{
#if defined(ENABLE_TUYA_UI) && ENABLE_TUYA_UI == 1
    tuya_ai_display_pause();
    tal_lcd_service_dma2d_init();
#endif
}

static VOID __display_released(VOID)
{
#if defined(ENABLE_TUYA_UI) && ENABLE_TUYA_UI == 1
    tal_lcd_service_dma2d_deinit();
    tuya_ai_display_resume();
#endif
}

static void __on_camera_lcd_display(TAL_CAMERA_FRAME_T *frame)
{
#if defined(ENABLE_TUYA_UI) && ENABLE_TUYA_UI == 1
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

/* ---------------------------------------------------------------------------
 * Per-stream ref-counted acquire / release.
 * --------------------------------------------------------------------------- */
static OPERATE_RET __yuv_acquire(CAM_USER_E user)
{
    if (!s_ctx.handle) return OPRT_COM_ERROR;
    if (s_ctx.type != TAL_CAMERA_TYPE_DVP) return OPRT_NOT_SUPPORTED;

    BOOL_T need_start = FALSE;
    if (sg_user_mask_mtx) tal_mutex_lock(sg_user_mask_mtx);
    need_start = (sg_yuv_user_mask == 0);
    sg_yuv_user_mask |= (UINT8_T)user;
    if (sg_user_mask_mtx) tal_mutex_unlock(sg_user_mask_mtx);

    if (need_start) {
        if (__display_stream() == CAM_STREAM_YUV422) {
            __display_acquired();
        }
        return tal_camera_start_stream(s_ctx.handle, TAL_STREAM_YUV422);
    }
    return OPRT_OK;
}

static OPERATE_RET __yuv_release(CAM_USER_E user)
{
    if (!s_ctx.handle) return OPRT_COM_ERROR;
    if (s_ctx.type != TAL_CAMERA_TYPE_DVP) return OPRT_OK;

    BOOL_T need_stop = FALSE;
    if (sg_user_mask_mtx) tal_mutex_lock(sg_user_mask_mtx);
    sg_yuv_user_mask &= (UINT8_T)~user;
    need_stop = (sg_yuv_user_mask == 0);
    if (sg_user_mask_mtx) tal_mutex_unlock(sg_user_mask_mtx);

    if (need_stop) {
        tal_camera_stop_stream(s_ctx.handle, TAL_STREAM_YUV422);
        if (__display_stream() == CAM_STREAM_YUV422) {
            __display_released();
        }
    }
    return OPRT_OK;
}

static OPERATE_RET __mjpeg_acquire(CAM_USER_E user)
{
    if (!s_ctx.handle) return OPRT_COM_ERROR;

    BOOL_T need_start = FALSE;
    if (sg_user_mask_mtx) tal_mutex_lock(sg_user_mask_mtx);
    need_start = (sg_mjpeg_user_mask == 0);
    sg_mjpeg_user_mask |= (UINT8_T)user;
    if (sg_user_mask_mtx) tal_mutex_unlock(sg_user_mask_mtx);

    if (need_start) {
        if (__display_stream() == CAM_STREAM_MJPEG) {
            __display_acquired();
        }
        return tal_camera_start_stream(s_ctx.handle, TAL_STREAM_MJPEG);
    }
    return OPRT_OK;
}

static OPERATE_RET __mjpeg_release(CAM_USER_E user)
{
    if (!s_ctx.handle) return OPRT_COM_ERROR;

    BOOL_T need_stop = FALSE;
    if (sg_user_mask_mtx) tal_mutex_lock(sg_user_mask_mtx);
    sg_mjpeg_user_mask &= (UINT8_T)~user;
    need_stop = (sg_mjpeg_user_mask == 0);
    if (sg_user_mask_mtx) tal_mutex_unlock(sg_user_mask_mtx);

    if (need_stop) {
        tal_camera_stop_stream(s_ctx.handle, TAL_STREAM_MJPEG);
        if (__display_stream() == CAM_STREAM_MJPEG) {
            __display_released();
        }
    }
    return OPRT_OK;
}

static OPERATE_RET __h264_stream_acquire(CAM_USER_E user)
{
    if (!s_ctx.handle) return OPRT_COM_ERROR;
    if (s_ctx.type != TAL_CAMERA_TYPE_DVP) return OPRT_NOT_SUPPORTED;

    BOOL_T need_start = FALSE;
    if (sg_user_mask_mtx) tal_mutex_lock(sg_user_mask_mtx);
    need_start = (sg_h264_user_mask == 0);
    sg_h264_user_mask |= (UINT8_T)user;
    if (sg_user_mask_mtx) tal_mutex_unlock(sg_user_mask_mtx);

    if (need_start) {
        return tal_camera_start_stream(s_ctx.handle, TAL_STREAM_H264);
    }
    return OPRT_OK;
}

static OPERATE_RET __h264_stream_release(CAM_USER_E user)
{
    if (!s_ctx.handle) return OPRT_COM_ERROR;
    if (s_ctx.type != TAL_CAMERA_TYPE_DVP) return OPRT_OK;

    BOOL_T need_stop = FALSE;
    if (sg_user_mask_mtx) tal_mutex_lock(sg_user_mask_mtx);
    sg_h264_user_mask &= (UINT8_T)~user;
    need_stop = (sg_h264_user_mask == 0);
    if (sg_user_mask_mtx) tal_mutex_unlock(sg_user_mask_mtx);

    if (need_stop) {
        return tal_camera_stop_stream(s_ctx.handle, TAL_STREAM_H264);
    }
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Raw frame dispatch.
 * --------------------------------------------------------------------------- */
static void __dispatch_raw(CAM_STREAM_E stream, TAL_CAMERA_FRAME_T *frame)
{
    if (stream >= CAM_STREAM_MAX || !frame) return;

    CAM_FRAME_CB raw_cb  = NULL;
    VOID        *raw_ctx = NULL;

    if (sg_user_mask_mtx) tal_mutex_lock(sg_user_mask_mtx);
    raw_cb  = sg_raw_cbs[stream];
    raw_ctx = sg_raw_ctxs[stream];
    if (sg_user_mask_mtx) tal_mutex_unlock(sg_user_mask_mtx);

    if (!raw_cb) return;

    CAM_FRAME_T f = {
        .data         = (UCHAR_T *)frame->data,
        .length       = frame->length,
        .width        = frame->width,
        .height       = frame->height,
        .timestamp_ms = frame->timestamp,
        .stream       = stream,
        .is_i_frame   = frame->is_i_frame,
    };
    raw_cb(&f, raw_ctx);
}

static void __on_yuv_frame(TAL_CAMERA_HANDLE_T handle, TAL_CAMERA_FRAME_T *frame, void *args)
{
    /* DVP display: YUV → RGB → LCD via hardware path. */
    __on_camera_lcd_display(frame);
    __dispatch_raw(CAM_STREAM_YUV422, frame);
}

static void __on_mjpeg_frame(TAL_CAMERA_HANDLE_T handle, TAL_CAMERA_FRAME_T *frame, void *args)
{
    /* UVC display: MJPEG decoded to RGB → LCD. DVP skips this path. */
    if (s_ctx.type == TAL_CAMERA_TYPE_UVC) {
        __on_camera_lcd_display(frame);
    }
    __dispatch_raw(CAM_STREAM_MJPEG, frame);
}

static void __on_h264_frame(TAL_CAMERA_HANDLE_T handle, TAL_CAMERA_FRAME_T *frame, void *args)
{
    __dispatch_raw(CAM_STREAM_H264, frame);
}

/* ---------------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------------- */
OPERATE_RET tuya_device_camera_init(VOID)
{
    TAL_PR_INFO("tuya device camera init");

    TAL_CAMERA_CFG_T cfg = {0};
    OPERATE_RET rt = tuya_board_get_camera_cfg(&cfg);
    if (rt != OPRT_OK) {
        TAL_PR_WARN("No camera on this board, skip init");
        return rt;
    }
    s_ctx.type = cfg.type;

#if defined(ENABLE_TUYA_UI) && ENABLE_TUYA_UI == 1
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

    s_ctx.handle = tal_camera_init(&cfg);
    if (!s_ctx.handle) {
        TAL_PR_ERR("tal_camera_init failed");
        return OPRT_COM_ERROR;
    }

    tal_camera_register_cb(s_ctx.handle, TAL_STREAM_MJPEG,  __on_mjpeg_frame, NULL);
    tal_camera_register_cb(s_ctx.handle, TAL_STREAM_YUV422, __on_yuv_frame,   NULL);
    tal_camera_register_cb(s_ctx.handle, TAL_STREAM_H264,   __on_h264_frame,  NULL);

    if (!sg_user_mask_mtx) {
        tal_mutex_create_init(&sg_user_mask_mtx);
    }
    sg_yuv_user_mask   = 0;
    sg_mjpeg_user_mask = 0;
    sg_h264_user_mask  = 0;
    for (UINT_T i = 0; i < CAM_STREAM_MAX; i++) {
        sg_raw_cbs[i]  = NULL;
        sg_raw_ctxs[i] = NULL;
    }

    TAL_PR_DEBUG("camera init ok, type=%d", s_ctx.type);
    return OPRT_OK;
}

OPERATE_RET tuya_device_camera_deinit(VOID)
{
    TAL_PR_INFO("tuya device camera deinit");

    if (s_ctx.handle) {
        tal_camera_deinit(s_ctx.handle);
        s_ctx.handle = NULL;
    }

#if defined(ENABLE_TUYA_UI) && ENABLE_TUYA_UI == 1
    tal_lcd_service_deinit();
#endif

    if (sg_user_mask_mtx) {
        tal_mutex_release(sg_user_mask_mtx);
        sg_user_mask_mtx = NULL;
    }
    sg_yuv_user_mask   = 0;
    sg_mjpeg_user_mask = 0;
    sg_h264_user_mask  = 0;
    for (UINT_T i = 0; i < CAM_STREAM_MAX; i++) {
        sg_raw_cbs[i]  = NULL;
        sg_raw_ctxs[i] = NULL;
    }

    s_ctx.type = TAL_CAMERA_TYPE_MAX;
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 *  Primitive API
 * --------------------------------------------------------------------------- */
OPERATE_RET tuya_device_camera_start_stream(CAM_STREAM_E stream)
{
    switch (stream) {
    case CAM_STREAM_YUV422: return __yuv_acquire(CAM_USER_ADAPTER);
    case CAM_STREAM_MJPEG:  return __mjpeg_acquire(CAM_USER_ADAPTER);
    case CAM_STREAM_H264:   return __h264_stream_acquire(CAM_USER_ADAPTER);
    default:                return OPRT_INVALID_PARM;
    }
}

OPERATE_RET tuya_device_camera_stop_stream(CAM_STREAM_E stream)
{
    switch (stream) {
    case CAM_STREAM_YUV422: return __yuv_release(CAM_USER_ADAPTER);
    case CAM_STREAM_MJPEG:  return __mjpeg_release(CAM_USER_ADAPTER);
    case CAM_STREAM_H264:   return __h264_stream_release(CAM_USER_ADAPTER);
    default:                return OPRT_INVALID_PARM;
    }
}

OPERATE_RET tuya_device_camera_set_raw_cb(CAM_STREAM_E stream, CAM_FRAME_CB cb, VOID *ctx)
{
    if (stream >= CAM_STREAM_MAX) {
        return OPRT_INVALID_PARM;
    }
    if (sg_user_mask_mtx) tal_mutex_lock(sg_user_mask_mtx);
    sg_raw_cbs[stream]  = cb;
    sg_raw_ctxs[stream] = ctx;
    if (sg_user_mask_mtx) tal_mutex_unlock(sg_user_mask_mtx);
    return OPRT_OK;
}

OPERATE_RET tuya_device_camera_switch_output_mode(CAM_OUTPUT_MODE_E mode)
{
    if (!s_ctx.handle || s_ctx.type != TAL_CAMERA_TYPE_DVP) {
        return OPRT_NOT_SUPPORTED;
    }

    TAL_CAMERA_CFG_T cfg = {0};
    if (tuya_board_get_camera_cfg(&cfg) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }

    TUYA_CAMERA_OUTPUT_MODE hw_mode;
    switch (mode) {
    case CAM_MODE_JPEG_YUV: hw_mode = TUYA_CAMERA_OUTPUT_JPEG_YUV422_BOTH; break;
    case CAM_MODE_H264_YUV: hw_mode = TUYA_CAMERA_OUTPUT_H264_YUV422_BOTH; break;
    default:                return OPRT_INVALID_PARM;
    }
    return tal_camera_switch_output_mode(s_ctx.handle, &cfg, hw_mode);
}

#endif /* ENABLE_TUYA_CAMERA */
