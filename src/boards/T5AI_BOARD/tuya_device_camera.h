/**
 * @file tuya_device_camera.h
 * @brief T5AI_BOARD camera board-level driver — hardware primitives only.
 *
 * Higher-level consumers (mode/view) should NOT include this header.
 * They go through the adaptation layer <tuya_ai_toy_camera.h> which owns
 * subscription fan-out and stream reference counting.
 */

 #ifndef __TUYA_DEVICE_CAMERA_H__
 #define __TUYA_DEVICE_CAMERA_H__

 #ifdef __cplusplus
 extern "C" {
 #endif

 #include "tuya_cloud_types.h"
 #include "tuya_app_config.h"

 #if defined(ENABLE_TUYA_CAMERA) && (ENABLE_TUYA_CAMERA == 1)
 #include "tal_camera.h"
 #include "tuya_ai_toy_camera.h"

 /**
  * @brief Initialize / deinitialize the camera hardware.
  *        Camera config is pulled from tuya_board_get_camera_cfg().
  */
 OPERATE_RET tuya_device_camera_init(VOID);
 OPERATE_RET tuya_device_camera_deinit(VOID);

 /**
  * @brief Start / stop a single physical stream. Internally reference-counted
  *        so multiple callers may start the same stream safely; the hardware
  *        stream is only torn down when the last caller releases it.
  */
 OPERATE_RET tuya_device_camera_start_stream(CAM_STREAM_E stream);
 OPERATE_RET tuya_device_camera_stop_stream(CAM_STREAM_E stream);

 /**
  * @brief Install / clear the single raw frame callback for a physical stream.
  *        Called by the adaptation layer; each stream has exactly one slot.
  *        Fan-out to multiple consumers happens in the adaptation layer.
  */
 OPERATE_RET tuya_device_camera_set_raw_cb(CAM_STREAM_E stream, CAM_FRAME_CB cb, VOID *ctx);

 /**
  * @brief Switch the DVP dual-stream output mode (JPEG+YUV ↔ H264+YUV).
  *        Returns OPRT_NOT_SUPPORTED on non-DVP boards.
  */
 OPERATE_RET tuya_device_camera_switch_output_mode(CAM_OUTPUT_MODE_E mode);
 #endif /* ENABLE_TUYA_CAMERA */

 #ifdef __cplusplus
 }
 #endif
 #endif
