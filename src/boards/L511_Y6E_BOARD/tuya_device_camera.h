/**
 * @file tuya_device_camera.h
 * @brief L511_Y6E_BOARD camera interface (stubs, no camera on this board).
 * @version 0.1
 * @date 2025-05-13
 *
 * @copyright Copyright (c) 2023 Tuya Inc. All Rights Reserved.
 */

#ifndef __TUYA_DEVICE_CAMERA_H__
#define __TUYA_DEVICE_CAMERA_H__

#ifdef __cplusplus
extern "C" {
#endif
#include "tuya_cloud_types.h"
#include "tuya_app_config.h"

OPERATE_RET tuya_device_camera_init(VOID);

OPERATE_RET tuya_device_camera_deinit(VOID);

OPERATE_RET tuya_device_camera_start(VOID);

OPERATE_RET tuya_device_camera_stop(VOID);

OPERATE_RET tuya_device_camera_h264_start(VOID);

OPERATE_RET tuya_device_camera_h264_stop(VOID);

OPERATE_RET tuya_device_camera_switch_to_h264_mode(VOID);

OPERATE_RET tuya_device_camera_switch_to_jpeg_mode(VOID);

OPERATE_RET tuya_device_camera_get_jpeg_frame(BYTE_T **data, UINT_T *len, VOID *user_data);

#ifdef __cplusplus
}
#endif
#endif
