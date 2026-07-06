#include "tuya_ai_toy_camera.h"
#include "tuya_device_cfg.h"
#include "tuya_device_camera.h"

/* ------------------------------------------------------------------ */
/*  Public API                                                          */
/* ------------------------------------------------------------------ */
OPERATE_RET tuya_ai_toy_camera_init(VOID)
{
    return tuya_device_camera_init();
}

OPERATE_RET tuya_ai_toy_camera_deinit(VOID)
{
    return tuya_device_camera_deinit();
}

OPERATE_RET tuya_ai_toy_camera_start(VOID)
{
    return tuya_device_camera_start();
}

OPERATE_RET tuya_ai_toy_camera_stop(VOID)
{
    return tuya_device_camera_stop();
}

OPERATE_RET tuya_ai_toy_camera_h264_start()
{
    return tuya_device_camera_h264_start();
}

OPERATE_RET tuya_ai_toy_camera_h264_stop()
{
    return tuya_device_camera_h264_stop();
}

OPERATE_RET tuya_ai_toy_camera_switch_to_h264_mode(VOID)
{
    return tuya_device_camera_switch_to_h264_mode();
}

OPERATE_RET tuya_ai_toy_camera_switch_to_jpeg_mode(VOID)
{
    return tuya_device_camera_switch_to_jpeg_mode();
}

OPERATE_RET tuya_ai_toy_camera_get_jpeg_frame(BYTE_T **data, UINT_T *len, VOID *user_data)
{
    return tuya_device_camera_get_jpeg_frame(data, len, user_data);
}

void camera_test_cli_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    if (strcmp(argv[1], "init") == 0) {
        tuya_ai_toy_camera_init();
    } else if (strcmp(argv[1], "deinit") == 0) {
        tuya_ai_toy_camera_deinit();
    } else if (strcmp(argv[1], "start") == 0) {
        tuya_ai_toy_camera_start();
    } else if (strcmp(argv[1], "stop") == 0) {
        tuya_ai_toy_camera_stop();
    } else {
        TAL_PR_ERR("Unknown command: %s", argv[1]);
    }
}
