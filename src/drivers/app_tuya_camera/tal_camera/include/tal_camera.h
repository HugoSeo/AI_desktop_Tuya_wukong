/**
 * @file tal_camera.h
 * @brief Unified camera abstraction layer — stream management
 *
 * Architecture:
 *   board → tuya_board_get_camera_cfg() → TAL_CAMERA_CFG_T
 *   app   → tal_camera_init(cfg)        → TAL_CAMERA_HANDLE_T
 *         → tal_camera_register_cb(handle, stream, cb, args)
 *         → tal_camera_start_stream(handle, stream)
 *         → cb(handle, frame, args)   [frame auto-released after cb returns]
 *         → tal_camera_stop_stream(handle, stream)
 *         → tal_camera_deinit(handle)
 *
 * @author linch
 * @date 2025-02-09
 * @version 1.0.0
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All rights reserved.
 */

#ifndef __TAL_CAMERA_H__
#define __TAL_CAMERA_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "tal_dvp.h"
#include "tal_uvc.h"

/* ------------------------------------------------------------------ */
/*  Camera type                                                         */
/* ------------------------------------------------------------------ */
typedef enum {
    TAL_CAMERA_TYPE_DVP = 0,
    TAL_CAMERA_TYPE_UVC,
    TAL_CAMERA_TYPE_MAX,
} TAL_CAMERA_TYPE_E;

/* ------------------------------------------------------------------ */
/*  Board-level config — filled by tuya_board_get_camera_cfg()         */
/*  cfg pointer must point to a static variable (lifetime >= handle)   */
/*  cb fields (dvp_frame_handle / frame_cb) leave NULL,                */
/*  tal_camera_init() overwrites them internally                        */
/* ------------------------------------------------------------------ */
typedef struct {
    TAL_CAMERA_TYPE_E  type;
    void              *cfg;   /* TUYA_DVP_USR_CFG_T* or TAL_UVC_CFG_T* */
} TAL_CAMERA_CFG_T;

/* ------------------------------------------------------------------ */
/*  Stream type — matches hardware output                              */
/* ------------------------------------------------------------------ */
typedef enum {
    TAL_STREAM_YUV422 = 0,
    TAL_STREAM_MJPEG,
    TAL_STREAM_H264,
    TAL_STREAM_MAX,
} TAL_STREAM_TYPE_E;

/* ------------------------------------------------------------------ */
/*  Unified frame structure                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    UINT8_T          *data;
    UINT32_T          length;
    TUYA_FRAME_FMT_E  fmt;         /* TUYA_FRAME_FMT_YUV422 / JPEG / H264 */
    UINT16_T          width;
    UINT16_T          height;
    UINT32_T          timestamp;
    BOOL_T            is_i_frame;  /* H264 only */
} TAL_CAMERA_FRAME_T;

/* ------------------------------------------------------------------ */
/*  Handle & callback                                                   */
/* ------------------------------------------------------------------ */
typedef void *TAL_CAMERA_HANDLE_T;

/**
 * @brief Frame callback — runs in driver thread, must not block.
 *        frame->data is valid only during the callback;
 *        copy data before returning if async access is needed.
 *        tal_camera releases the frame buffer automatically after cb returns.
 */
typedef void (*tal_camera_frame_cb_t)(TAL_CAMERA_HANDLE_T handle,
                                      TAL_CAMERA_FRAME_T *frame,
                                      void *args);

/* ------------------------------------------------------------------ */
/*  Board config query — implemented per board in tuya_device_board.c  */
/* ------------------------------------------------------------------ */
/**
 * @return OPRT_OK: cfg filled; OPRT_NOT_SUPPORTED: no camera on board
 */
OPERATE_RET tuya_board_get_camera_cfg(TAL_CAMERA_CFG_T *cfg);

/* ------------------------------------------------------------------ */
/*  Core API                                                            */
/* ------------------------------------------------------------------ */

/**
 * @brief  Initialize camera, return opaque handle.
 *         Internally writes cb fields into the cfg struct and calls
 *         the underlying DVP/UVC driver init.
 * @return handle on success, NULL on failure
 */
TAL_CAMERA_HANDLE_T tal_camera_init(TAL_CAMERA_CFG_T *cfg);

/** @brief  De-initialize, stop all streams, release resources. */
OPERATE_RET tal_camera_deinit(TAL_CAMERA_HANDLE_T handle);

/**
 * @brief  Register a frame callback for a stream (one cb per stream type).
 *         Re-registering replaces the previous cb.
 * @return OPRT_NOT_SUPPORTED if stream unavailable on this board
 */
OPERATE_RET tal_camera_register_cb(TAL_CAMERA_HANDLE_T   handle,
                                   TAL_STREAM_TYPE_E     stream,
                                   tal_camera_frame_cb_t cb,
                                   void                 *args);

/**
 * @brief  Start frame delivery for a stream ("open the gate").
 *         Hardware keeps running regardless; only delivery is gated.
 * @return OPRT_NOT_SUPPORTED if stream unavailable on this board
 */
OPERATE_RET tal_camera_start_stream(TAL_CAMERA_HANDLE_T handle,
                                    TAL_STREAM_TYPE_E   stream);

/**
 * @brief  Stop frame delivery for a stream.
 *         Hardware keeps running; cb binding is preserved.
 */
OPERATE_RET tal_camera_stop_stream(TAL_CAMERA_HANDLE_T handle,
                                   TAL_STREAM_TYPE_E   stream);

/**
 * @brief  Runtime switch of DVP output mode (DVP only).
 *         All streams must be stopped before calling.
 *         Internally: deinit driver → modify output_mode → reinit driver.
 *         Registered cbs are preserved.
 * @param  cfg   Board config (same pointer passed to tal_camera_init).
 *               output_mode field is overwritten in the board's static struct.
 * @return OPRT_NOT_SUPPORTED for UVC handle
 */
OPERATE_RET tal_camera_switch_output_mode(TAL_CAMERA_HANDLE_T     handle,
                                          TAL_CAMERA_CFG_T       *cfg,
                                          TUYA_CAMERA_OUTPUT_MODE mode);

#ifdef __cplusplus
}
#endif

#endif /* __TAL_CAMERA_H__ */
