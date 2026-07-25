/**
 * @file tuya_device_camera.c
 * @brief T5AI_BOARD_EYES — stub camera driver.
 *
 * This board has no camera. The new adapter-facing primitives are provided
 * as stubs so the app links cleanly; subscribers see no frames.
 */

#include "tuya_device_camera.h"

#if defined(ENABLE_TUYA_CAMERA) && (ENABLE_TUYA_CAMERA == 1)

OPERATE_RET tuya_device_camera_init(VOID)
{
    return OPRT_OK;
}

OPERATE_RET tuya_device_camera_deinit(VOID)
{
    return OPRT_OK;
}

OPERATE_RET tuya_device_camera_start_stream(CAM_STREAM_E stream)
{
    (VOID)stream;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tuya_device_camera_stop_stream(CAM_STREAM_E stream)
{
    (VOID)stream;
    return OPRT_OK;
}

OPERATE_RET tuya_device_camera_set_raw_cb(CAM_STREAM_E stream, CAM_FRAME_CB cb, VOID *ctx)
{
    (VOID)stream;
    (VOID)cb;
    (VOID)ctx;
    return OPRT_OK;
}

OPERATE_RET tuya_device_camera_switch_output_mode(CAM_OUTPUT_MODE_E mode)
{
    (VOID)mode;
    return OPRT_NOT_SUPPORTED;
}

#endif /* ENABLE_TUYA_CAMERA */
