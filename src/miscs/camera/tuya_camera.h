/**
 * @file tuya_camera.h
 * @brief Camera business layer for Tuya AI toy.
 *
 * Sits on top of the video service (`wukong_video_service.h`) as one of its
 * consumers: preview and motion detection subscribe to the shared YUV422
 * stream, snapshot delegates straight to the input source, and the JPEG/RGB
 * thumbnail helpers are self-contained image-conversion utilities. Stream
 * subscription bookkeeping (ref-counting, hardware start/stop) lives in the
 * video service, not here.
 */

#ifndef __TUYA_CAMERA_H__
#define __TUYA_CAMERA_H__

#include "tuya_cloud_types.h"
/* tuya_app_config.h provides ENABLE_TUYA_CAMERA so the no-op stub branch
 * below is selected consistently regardless of caller include order. */
#include "tuya_app_config.h"
/* Video service header only declares plain types (VIDEO_FRAME_T /
 * VIDEO_FRAME_CB) with no build-flag dependency of its own, so it can be
 * included unconditionally in both the real and stub branches below. */
#include "wukong_video_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Types                                                              */
/* ------------------------------------------------------------------ */

/**
 * Preview frame callback. The camera layer subscribes to the YUV422 stream
 * internally, converts each frame to RGB565, and hands ownership of @p rgb565
 * to the callee, which MUST release it via tuya_camera_rgb_free().
 */
typedef VOID (*TUYA_CAMERA_PREVIEW_CB)(USHORT_T width, USHORT_T height,
                                       UCHAR_T *rgb565, UINT_T len, VOID *ctx);

/* ------------------------------------------------------------------ */
/*  API                                                                */
/*                                                                     */
/*  When ENABLE_TUYA_CAMERA != 1, every entry point is a static inline */
/*  no-op returning OPRT_NOT_SUPPORTED, so callers never need to wrap  */
/*  them in #ifdef. The unused real symbols are stripped by the linker.*/
/* ------------------------------------------------------------------ */

#if defined(ENABLE_TUYA_CAMERA) && (ENABLE_TUYA_CAMERA == 1)

/**
 * @brief Initialize the camera business layer (shared DMA2D engine for
 *        preview conversion). Hardware bring-up and stream bookkeeping are
 *        owned by the video service / input source, not here.
 */
OPERATE_RET tuya_camera_init(VOID);

/**
 * @brief De-initialize the camera business layer and release its resources.
 */
OPERATE_RET tuya_camera_deinit(VOID);

/**
 * @brief Whether the camera feature is compiled in and a video input source
 *        is available.
 * @return TRUE when the real implementation is built and a source responds
 *         to wukong_video_get_caps(); FALSE for the no-op stub.
 */
BOOL_T tuya_camera_supported(VOID);

/**
 * @brief Start the live preview: subscribe to the YUV422 stream, convert every
 *        frame to RGB565 internally, and deliver it through @p cb.
 * @param[in] cb  Preview frame callback (must be non-NULL); takes buffer ownership.
 * @param[in] ctx Opaque context forwarded to @p cb.
 * @return OPRT_OK on success; OPRT_NOT_SUPPORTED when image conversion is unavailable.
 * @note The conversion uses tal_image; only one preview consumer is supported.
 */
OPERATE_RET tuya_camera_preview_start(TUYA_CAMERA_PREVIEW_CB cb, VOID *ctx);

/**
 * @brief Stop the live preview and unsubscribe from the YUV422 stream.
 * @return OPRT_OK on success.
 * @note Once this returns the stream is stopped, so no further preview frames fire.
 */
OPERATE_RET tuya_camera_preview_stop(VOID);

/**
 * @brief Capture one JPEG frame synchronously via the video service.
 * @param[out] jpeg       PSRAM buffer with frame data; caller must tal_psram_free().
 * @param[out] len        Length in bytes.
 * @param[in]  timeout_ms Max wait time for a frame.
 * @return OPRT_OK on success; OPRT_TIMEOUT on timeout.
 */
OPERATE_RET tuya_camera_snapshot(BYTE_T **jpeg, UINT_T *len, UINT_T timeout_ms);

/**
 * @brief Subscribe to the YUV422 stream for motion-detection frame grabs.
 * @param[in] cb  Frame callback (must be non-NULL).
 * @param[in] ctx Opaque context forwarded to @p cb.
 * @return OPRT_OK on success; OPRT_COM_ERROR if already subscribed.
 */
OPERATE_RET tuya_camera_detection_start(VIDEO_FRAME_CB cb, VOID *ctx);

/**
 * @brief Unsubscribe from the detection stream.
 * @return OPRT_OK on success; OPRT_COM_ERROR if not currently subscribed.
 */
OPERATE_RET tuya_camera_detection_stop(VOID);

/**
 * @brief Decode and scale a JPEG buffer to a square RGB565 thumbnail.
 * @param[in]  jpeg      JPEG buffer (caller retains ownership).
 * @param[in]  jpeg_len  JPEG length in bytes.
 * @param[in]  target_px Target square size in pixels.
 * @param[out] out_rgb   PSRAM RGB565 buffer; caller releases via tuya_camera_rgb_free().
 * @param[out] out_w     Actual thumbnail width (may be NULL).
 * @param[out] out_h     Actual thumbnail height (may be NULL).
 * @return OPRT_OK on success; OPRT_NOT_SUPPORTED when image conversion is unavailable.
 */
OPERATE_RET tuya_camera_jpeg_to_rgb565(CONST UCHAR_T *jpeg, UINT_T jpeg_len,
                                       USHORT_T target_px, UCHAR_T **out_rgb,
                                       USHORT_T *out_w, USHORT_T *out_h);

/**
 * @brief Release an RGB565 buffer produced by the preview callback or
 *        tuya_camera_jpeg_to_rgb565().
 * @param[in] buf buffer to release (NULL is safe).
 * @return none
 */
VOID tuya_camera_rgb_free(VOID *buf);

#else  /* ENABLE_TUYA_CAMERA disabled: provide no-op inline stubs */

static inline OPERATE_RET tuya_camera_init(VOID)
{
    return OPRT_NOT_SUPPORTED;
}

static inline OPERATE_RET tuya_camera_deinit(VOID)
{
    return OPRT_NOT_SUPPORTED;
}

static inline BOOL_T tuya_camera_supported(VOID)
{
    return FALSE;
}

static inline OPERATE_RET tuya_camera_preview_start(TUYA_CAMERA_PREVIEW_CB cb, VOID *ctx)
{
    (VOID)cb;
    (VOID)ctx;
    return OPRT_NOT_SUPPORTED;
}

static inline OPERATE_RET tuya_camera_preview_stop(VOID)
{
    return OPRT_NOT_SUPPORTED;
}

static inline OPERATE_RET tuya_camera_snapshot(BYTE_T **jpeg, UINT_T *len, UINT_T timeout_ms)
{
    (VOID)timeout_ms;
    if (jpeg != NULL) {
        *jpeg = NULL;
    }
    if (len != NULL) {
        *len = 0;
    }
    return OPRT_NOT_SUPPORTED;
}

static inline OPERATE_RET tuya_camera_detection_start(VIDEO_FRAME_CB cb, VOID *ctx)
{
    (VOID)cb;
    (VOID)ctx;
    return OPRT_NOT_SUPPORTED;
}

static inline OPERATE_RET tuya_camera_detection_stop(VOID)
{
    return OPRT_NOT_SUPPORTED;
}

static inline OPERATE_RET tuya_camera_jpeg_to_rgb565(CONST UCHAR_T *jpeg, UINT_T jpeg_len,
                                                      USHORT_T target_px, UCHAR_T **out_rgb,
                                                      USHORT_T *out_w, USHORT_T *out_h)
{
    (VOID)jpeg;
    (VOID)jpeg_len;
    (VOID)target_px;
    if (out_rgb != NULL) {
        *out_rgb = NULL;
    }
    if (out_w != NULL) {
        *out_w = 0;
    }
    if (out_h != NULL) {
        *out_h = 0;
    }
    return OPRT_NOT_SUPPORTED;
}

static inline VOID tuya_camera_rgb_free(VOID *buf)
{
    (VOID)buf;
}

#endif /* ENABLE_TUYA_CAMERA */

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_CAMERA_H__ */
