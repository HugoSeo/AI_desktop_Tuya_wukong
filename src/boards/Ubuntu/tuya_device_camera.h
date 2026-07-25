/**
 * @file tuya_device_board.h
 * @brief
 * @version 0.1
 * @date 2023-06-26
 *
 * @copyright Copyright (c) 2023 Tuya Inc. All Rights Reserved.
 *
 * Permission is hereby granted, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), Under the premise of complying
 * with the license of the third-party open source software contained in the software,
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software.
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
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