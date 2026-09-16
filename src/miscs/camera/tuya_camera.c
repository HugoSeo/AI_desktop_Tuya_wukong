#include "tuya_camera.h"

#if defined(ENABLE_TUYA_CAMERA) && (ENABLE_TUYA_CAMERA == 1)

#include "wukong_video_service.h"
#include "tal_log.h"
#include "tal_memory.h"
#include <string.h>

/* Preview YUV422->RGB565 conversion goes through the shared DMA2D engine
 * (miscs/display/tuya_dma2d), which serialises every DMA2D op (UI flush + this
 * conversion) on one mutex and falls back to software internally — independent
 * of whether the UI is built. */
#include "tuya_dma2d.h"

/* JPEG->RGB565 thumbnail / scale path lives here. Guarded so the camera can
 * still build when tal_image is compiled out. */
#if defined(ENABLE_TAL_IMAGE) && (ENABLE_TAL_IMAGE == 1)
#include "tal_image_yuv422_to_rgb.h"
#include "tal_image_scale.h"
#define CAM_HAVE_IMAGE_CONV 1
#endif

/* ================================================================== */
/*  Lifecycle                                                          */
/* ================================================================== */

/* TRUE only when our tuya_dma2d_init() actually took a reference, so deinit
 * drops exactly the references we took — even if init itself failed (rare OOM)
 * while another consumer (UI) still holds the engine. */
static BOOL_T s_holds_dma2d = FALSE;

OPERATE_RET tuya_camera_init(VOID)
{
    /* Bring up the shared DMA2D engine for HW-accelerated preview conversion
     * (ref-counted; the UI display port may hold its own reference). Only record
     * a reference if init actually succeeded, so a failed bring-up here can't
     * make deinit drop someone else's reference. Stream subscription tables and
     * hardware bring-up now live in the video service / input source. */
    s_holds_dma2d = (tuya_dma2d_init() == OPRT_OK);
    return OPRT_OK;
}

OPERATE_RET tuya_camera_deinit(VOID)
{
    /* Symmetric with init: only drop the DMA2D reference if we actually took
     * one. Guards against a deinit after a failed init (which never took a
     * reference) and against a double deinit — either would otherwise
     * over-decrement the shared engine and tear it out from under the UI,
     * which may still hold its own reference. */
    if (s_holds_dma2d) {
        tuya_dma2d_deinit();
        s_holds_dma2d = FALSE;
    }
    return OPRT_OK;
}

/* ================================================================== */
/*  Support probe                                                      */
/* ================================================================== */

BOOL_T tuya_camera_supported(VOID)
{
    VIDEO_INPUT_CAPS_T caps;
    return (wukong_video_get_caps(&caps) == OPRT_OK) ? TRUE : FALSE;
}

/* ================================================================== */
/*  Preview (YUV422 -> RGB565)                                         */
/* ================================================================== */

/* Live-preview hook: written on the caller thread (start/stop), read on the
 * video service's dispatch thread (__preview_yuv_cb). Single-writer + atomic
 * pointer store on Cortex-M33; the stream is stopped before the callback is
 * cleared so no straggler fires. */
static TUYA_CAMERA_PREVIEW_CB s_preview_cb  = NULL;
static VOID                  *s_preview_ctx = NULL;

/* The camera page owns a 320px-wide square viewfinder. Converting at sensor
 * resolution and asking LVGL to transform the 480x480 canvas on every display
 * refresh dropped the UI from ~18 FPS to ~5 FPS. Keep the fast DMA2D colour
 * conversion, then compact the RGB565 frame once in-place before it reaches
 * LVGL. The forward walk is overlap-safe because every destination pixel lies
 * at or before the source pixel selected for it. */
#define CAM_PREVIEW_MAX_SIDE 320u

static VOID __preview_downscale_rgb565(UCHAR_T *buf,
                                       USHORT_T src_w, USHORT_T src_h,
                                       USHORT_T dst_w, USHORT_T dst_h)
{
    if (buf == NULL || src_w == 0 || src_h == 0 || dst_w == 0 || dst_h == 0 ||
        (src_w == dst_w && src_h == dst_h)) {
        return;
    }

    USHORT_T *pixels = (USHORT_T *)buf;
    UINT_T x_step = ((UINT_T)src_w << 16) / dst_w;
    UINT_T y_step = ((UINT_T)src_h << 16) / dst_h;
    UINT_T y_fp = 0;

    for (USHORT_T dy = 0; dy < dst_h; ++dy, y_fp += y_step) {
        UINT_T sy = y_fp >> 16;
        USHORT_T *dst = pixels + (UINT_T)dy * dst_w;
        const USHORT_T *src = pixels + sy * src_w;
        UINT_T x_fp = 0;
        for (USHORT_T dx = 0; dx < dst_w; ++dx, x_fp += x_step) {
            dst[dx] = src[x_fp >> 16];
        }
    }
}

/**
 * @brief Internal YUV422 subscriber: convert each preview frame to RGB565 and
 *        hand it to the registered preview callback (ownership transfers).
 * @param[in] frame raw YUV422 frame from the video service (valid during the call)
 * @param[in] ctx   unused
 * @return none
 */
STATIC VOID __preview_yuv_cb(const VIDEO_FRAME_T *frame, VOID *ctx)
{
    (VOID)ctx;

    TUYA_CAMERA_PREVIEW_CB cb     = s_preview_cb;
    VOID                   *cb_ctx = s_preview_ctx;
    if (cb == NULL || frame == NULL || frame->data == NULL ||
        frame->width == 0 || frame->height == 0) {
        return;
    }

    USHORT_T out_w = frame->width;
    USHORT_T out_h = frame->height;
    if (out_w > CAM_PREVIEW_MAX_SIDE || out_h > CAM_PREVIEW_MAX_SIDE) {
        if (out_w >= out_h) {
            out_h = (USHORT_T)((UINT_T)out_h * CAM_PREVIEW_MAX_SIDE / out_w);
            out_w = CAM_PREVIEW_MAX_SIDE;
        } else {
            out_w = (USHORT_T)((UINT_T)out_w * CAM_PREVIEW_MAX_SIDE / out_h);
            out_h = CAM_PREVIEW_MAX_SIDE;
        }
    }

    UINT_T   alloc_len = (UINT_T)frame->width * frame->height * 2;
    UCHAR_T *rgb       = (UCHAR_T *)tal_psram_malloc(alloc_len);
    if (rgb == NULL) {
        TAL_PR_ERR("preview rgb565 psram_malloc failed, len=%u", alloc_len);
        return;
    }

    /* DMA2D engine (shared with the UI flush path) does the conversion with HW
     * acceleration, falling back to software internally; works with or without UI. */
    if (tuya_dma2d_yuv422_to_rgb565(frame->data, frame->width, frame->height,
                                    rgb, frame->width, frame->height) != OPRT_OK) {
        tal_psram_free(rgb);
        return;
    }

    __preview_downscale_rgb565(rgb, frame->width, frame->height, out_w, out_h);
    cb(out_w, out_h, rgb, (UINT_T)out_w * out_h * 2, cb_ctx);
}

OPERATE_RET tuya_camera_preview_start(TUYA_CAMERA_PREVIEW_CB cb, VOID *ctx)
{
    if (cb == NULL) {
        return OPRT_INVALID_PARM;
    }
#if !defined(CAM_HAVE_IMAGE_CONV)
    TAL_PR_ERR("camera preview requires tal_image (ENABLE_TAL_IMAGE)");
    return OPRT_NOT_SUPPORTED;
#else
    s_preview_cb  = cb;
    s_preview_ctx = ctx;

    OPERATE_RET rt = wukong_video_subscribe(VIDEO_FMT_YUV422, VIDEO_CONSUMER_PREVIEW,
                                            __preview_yuv_cb, NULL);
    if (OPRT_OK != rt) {
        s_preview_cb  = NULL;
        s_preview_ctx = NULL;
    }
    return rt;
#endif
}

OPERATE_RET tuya_camera_preview_stop(VOID)
{
    /* Unsubscribe stops the hardware stream then clears the raw cb, so once it
     * returns no further frames reach __preview_yuv_cb — safe to drop the hook. */
    OPERATE_RET rt = wukong_video_unsubscribe(VIDEO_FMT_YUV422, VIDEO_CONSUMER_PREVIEW);
    s_preview_cb  = NULL;
    s_preview_ctx = NULL;
    return rt;
}

/* ================================================================== */
/*  Snapshot                                                           */
/* ================================================================== */

OPERATE_RET tuya_camera_snapshot(BYTE_T **jpeg, UINT_T *len, UINT_T timeout_ms)
{
    /* Snapshot is a pass-through input-source op, orthogonal to stream
     * subscription; the video service handles it without a consumer slot. */
    return wukong_video_snapshot(jpeg, len, timeout_ms);
}

/* ================================================================== */
/*  Detection (thin subscribe/unsubscribe wrapper)                     */
/* ================================================================== */

OPERATE_RET tuya_camera_detection_start(VIDEO_FRAME_CB cb, VOID *ctx)
{
    return wukong_video_subscribe(VIDEO_FMT_YUV422, VIDEO_CONSUMER_DETECTION, cb, ctx);
}

OPERATE_RET tuya_camera_detection_stop(VOID)
{
    return wukong_video_unsubscribe(VIDEO_FMT_YUV422, VIDEO_CONSUMER_DETECTION);
}

/* ================================================================== */
/*  JPEG -> RGB565 thumbnail                                           */
/* ================================================================== */

OPERATE_RET tuya_camera_jpeg_to_rgb565(CONST UCHAR_T *jpeg, UINT_T jpeg_len,
                                       USHORT_T target_px, UCHAR_T **out_rgb,
                                       USHORT_T *out_w, USHORT_T *out_h)
{
    if (jpeg == NULL || jpeg_len == 0 || target_px == 0 || out_rgb == NULL) {
        return OPRT_INVALID_PARM;
    }
    *out_rgb = NULL;
    if (out_w != NULL) {
        *out_w = 0;
    }
    if (out_h != NULL) {
        *out_h = 0;
    }

#if !defined(CAM_HAVE_IMAGE_CONV)
    return OPRT_NOT_SUPPORTED;
#else
    TAL_IMAGE_JPEG_SCALE_IN_T in = {0};
    in.method     = TAL_IMAGE_SCALE_MTH_BILINEAR;
    in.mode       = TAL_IMAGE_SCALE_MODE_SIZE;
    in.data       = jpeg;
    in.size       = jpeg_len;
    in.out_width  = target_px;
    in.out_height = target_px;

    TAL_IMAGE_SCALE_OUT_T out = {0};
    OPERATE_RET rt = tal_image_jpeg_scale_rgb565(&in, &out);
    if (OPRT_OK != rt || out.buf == NULL) {
        TAL_PR_ERR("jpeg scale to rgb565 failed: %d", rt);
        return (OPRT_OK != rt) ? rt : OPRT_COM_ERROR;
    }

    /* Copy into PSRAM so the caller frees through tuya_camera_rgb_free (same
     * allocator as the preview path), then release the scale buffer. */
    UCHAR_T *buf = (UCHAR_T *)tal_psram_malloc(out.size);
    if (buf == NULL) {
        TAL_PR_ERR("thumbnail psram_malloc failed, len=%u", out.size);
        tal_image_scale_buf_free(&out);
        return OPRT_MALLOC_FAILED;
    }
    memcpy(buf, out.buf, out.size);
    if (out_w != NULL) {
        *out_w = out.width;
    }
    if (out_h != NULL) {
        *out_h = out.height;
    }
    tal_image_scale_buf_free(&out);

    *out_rgb = buf;
    return OPRT_OK;
#endif
}

VOID tuya_camera_rgb_free(VOID *buf)
{
    if (buf != NULL) {
        tal_psram_free(buf);
    }
}

#endif /* ENABLE_TUYA_CAMERA */
