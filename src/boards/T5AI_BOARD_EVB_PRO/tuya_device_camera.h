/**
 * @file tuya_device_camera.h
 * @brief T5AI_BOARD_EVB_PRO camera board-level driver — hardware primitives.
 *
 * Higher-level consumers go through <tuya_ai_toy_camera.h>.
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

 OPERATE_RET tuya_device_camera_init(VOID);
 OPERATE_RET tuya_device_camera_deinit(VOID);
 OPERATE_RET tuya_device_camera_start_stream(CAM_STREAM_E stream);
 OPERATE_RET tuya_device_camera_stop_stream(CAM_STREAM_E stream);
 OPERATE_RET tuya_device_camera_set_raw_cb(CAM_STREAM_E stream, CAM_FRAME_CB cb, VOID *ctx);
 OPERATE_RET tuya_device_camera_switch_output_mode(CAM_OUTPUT_MODE_E mode);
 #endif /* ENABLE_TUYA_CAMERA */

 #ifdef __cplusplus
 }
 #endif
 #endif
