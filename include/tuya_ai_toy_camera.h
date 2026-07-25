/**
 * @file tuya_ai_toy_camera.h
 * @brief Camera adaptation layer for Tuya AI toy.
 *
 * Public model:
 *   - Consumers subscribe to one of the physical streams (YUV422 / MJPEG /
 *     H264) with a unique consumer id. The adaptation layer reference-counts
 *     the underlying hardware stream and fans out frames to all active
 *     subscribers of that stream.
 *   - Snapshot captures a one-shot JPEG frame synchronously.
 *   - Output mode switches the dual-stream format (JPEG+YUV / H264+YUV).
 *
 * Legacy APIs below are kept for transitional compatibility and will be
 * removed once all call sites migrate to subscribe/snapshot.
 */

#ifndef __TUYA_AI_TOY_CAMERA_H__
#define __TUYA_AI_TOY_CAMERA_H__

#include "tuya_cloud_types.h"
/* tuya_app_config.h provides ENABLE_TUYA_CAMERA so the no-op stub branch
 * below is selected consistently regardless of caller include order. */
#include "tuya_app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Types                                                              */
/* ------------------------------------------------------------------ */

/** Physical stream types (one per hardware encoder path). */
typedef enum {
    CAM_STREAM_YUV422 = 0,
    CAM_STREAM_MJPEG,
    CAM_STREAM_H264,
    CAM_STREAM_MAX,
} CAM_STREAM_E;

/** Consumer identifiers. Extend by adding a new value; no API change needed. */
typedef enum {
    CAM_CONSUMER_UI_PREVIEW = 0,
    CAM_CONSUMER_MD,            /* motion detection */
    CAM_CONSUMER_P2P,           /* P2P video chat */
    CAM_CONSUMER_AI_STREAM,     /* reserved for future continuous AI sampling */
    CAM_CONSUMER_SNAPSHOT,      /* internal: used by tuya_ai_toy_camera_snapshot() */
    CAM_CONSUMER_MAX,
} CAM_CONSUMER_E;

/** Dual-stream output mode. */
typedef enum {
    CAM_MODE_JPEG_YUV = 0,      /* MJPEG + YUV422 (default AI mode) */
    CAM_MODE_H264_YUV,          /* H264  + YUV422 (P2P video chat) */
} CAM_OUTPUT_MODE_E;

/** Snapshot pixel format. */
typedef enum {
    CAM_SNAP_JPEG = 0,
} CAM_SNAP_FMT_E;

/** Frame descriptor dispatched to subscribers. */
typedef struct {
    UCHAR_T      *data;
    UINT_T        length;
    USHORT_T      width;
    USHORT_T      height;
    UINT_T        timestamp_ms;
    CAM_STREAM_E  stream;
    BOOL_T        is_i_frame;   /* H264 only, FALSE for YUV/MJPEG */
} CAM_FRAME_T;

/** Subscriber callback. `frame` is valid only for the duration of the call. */
typedef VOID (*CAM_FRAME_CB)(const CAM_FRAME_T *frame, VOID *ctx);

/* ------------------------------------------------------------------ */
/*  API                                                                */
/*                                                                     */
/*  When ENABLE_TUYA_CAMERA != 1, every entry point is a static inline */
/*  no-op returning OPRT_NOT_SUPPORTED, so callers never need to wrap  */
/*  them in #ifdef. The unused real symbols are stripped by the linker.*/
/* ------------------------------------------------------------------ */

#if defined(ENABLE_TUYA_CAMERA) && (ENABLE_TUYA_CAMERA == 1)

/**
 * @brief Initialize the AI toy camera (hardware + adaptation layer).
 *        Camera type and configuration are obtained from tuya_board_get_camera_cfg().
 */
OPERATE_RET tuya_ai_toy_camera_init(VOID);

/**
 * @brief De-initialize the camera and release all resources.
 */
OPERATE_RET tuya_ai_toy_camera_deinit(VOID);

/**
 * @brief Subscribe a consumer to a physical stream.
 *        The first subscriber on a stream triggers the hardware start;
 *        the last unsubscribe triggers the hardware stop.
 *
 * @param stream    Physical stream type.
 * @param consumer  Consumer id (unique per stream).
 * @param cb        Frame callback (must be non-NULL).
 * @param ctx       Opaque context forwarded to `cb`.
 * @return OPRT_OK on success; OPRT_COM_ERROR if already subscribed.
 */
OPERATE_RET tuya_ai_toy_camera_subscribe(CAM_STREAM_E stream,
                                          CAM_CONSUMER_E consumer,
                                          CAM_FRAME_CB cb,
                                          VOID *ctx);

/**
 * @brief Unsubscribe a consumer from a stream.
 * @return OPRT_OK on success; OPRT_COM_ERROR if not currently subscribed.
 */
OPERATE_RET tuya_ai_toy_camera_unsubscribe(CAM_STREAM_E stream,
                                            CAM_CONSUMER_E consumer);

/**
 * @brief Capture one frame synchronously.
 *        Coexists with any ongoing subscriptions; when the target stream is
 *        already running, snapshot just grabs the next frame.
 *
 * @param[in]  fmt         Snapshot pixel format.
 * @param[out] out_data    PSRAM buffer with frame data; caller must tal_free().
 * @param[out] out_len     Length in bytes.
 * @param[in]  timeout_ms  Max wait time for a frame.
 * @return OPRT_OK on success; OPRT_TIMEOUT on timeout.
 */
OPERATE_RET tuya_ai_toy_camera_snapshot(CAM_SNAP_FMT_E fmt,
                                         BYTE_T **out_data,
                                         UINT_T *out_len,
                                         UINT_T timeout_ms);

/**
 * @brief Switch dual-stream output mode (JPEG+YUV ↔ H264+YUV).
 */
OPERATE_RET tuya_ai_toy_camera_switch_mode(CAM_OUTPUT_MODE_E mode);

#else  /* ENABLE_TUYA_CAMERA disabled: provide no-op inline stubs */

static inline OPERATE_RET tuya_ai_toy_camera_init(VOID)
{
    return OPRT_NOT_SUPPORTED;
}

static inline OPERATE_RET tuya_ai_toy_camera_deinit(VOID)
{
    return OPRT_NOT_SUPPORTED;
}

static inline OPERATE_RET tuya_ai_toy_camera_subscribe(CAM_STREAM_E stream,
                                                        CAM_CONSUMER_E consumer,
                                                        CAM_FRAME_CB cb,
                                                        VOID *ctx)
{
    (VOID)stream;
    (VOID)consumer;
    (VOID)cb;
    (VOID)ctx;
    return OPRT_NOT_SUPPORTED;
}

static inline OPERATE_RET tuya_ai_toy_camera_unsubscribe(CAM_STREAM_E stream,
                                                          CAM_CONSUMER_E consumer)
{
    (VOID)stream;
    (VOID)consumer;
    return OPRT_NOT_SUPPORTED;
}

static inline OPERATE_RET tuya_ai_toy_camera_snapshot(CAM_SNAP_FMT_E fmt,
                                                       BYTE_T **out_data,
                                                       UINT_T *out_len,
                                                       UINT_T timeout_ms)
{
    (VOID)fmt;
    (VOID)timeout_ms;
    if (out_data != NULL) {
        *out_data = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    return OPRT_NOT_SUPPORTED;
}

static inline OPERATE_RET tuya_ai_toy_camera_switch_mode(CAM_OUTPUT_MODE_E mode)
{
    (VOID)mode;
    return OPRT_NOT_SUPPORTED;
}

#endif /* ENABLE_TUYA_CAMERA */

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_AI_TOY_CAMERA_H__ */
