/**
 * @file tuya_ai_toy_camera.h
 * @brief Camera module for Tuya AI toy: init, start/stop preview, JPEG capture.
 */

#ifndef __TUYA_AI_TOY_CAMERA_H__
#define __TUYA_AI_TOY_CAMERA_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the AI toy camera.
 *        Camera type and configuration are obtained from tuya_board_get_camera_cfg().
 * @return OPRT_OK on success.
 */
OPERATE_RET tuya_ai_toy_camera_init(VOID);

/**
 * @brief De-initialize the camera and release all resources.
 */
OPERATE_RET tuya_ai_toy_camera_deinit(VOID);

/**
 * @brief Start camera preview — pauses LVGL and begins LCD streaming.
 */
OPERATE_RET tuya_ai_toy_camera_start(VOID);

/**
 * @brief Stop camera preview — stops LCD streaming and resumes LVGL.
 */
OPERATE_RET tuya_ai_toy_camera_stop(VOID);

/**
 * @brief Capture one JPEG frame.
 * @param[out] data  Allocated buffer with JPEG data; caller must tal_free().
 * @param[out] len   Length of JPEG data in bytes.
 * @param[in]  user_data  Reserved, pass NULL.
 * @return OPRT_OK on success, OPRT_COM_ERROR on timeout or error.
 */
OPERATE_RET tuya_ai_toy_camera_get_jpeg_frame(BYTE_T **data, UINT_T *len, VOID *user_data);

/**
 * @brief Switch DVP to H264+YUV422 dual-stream mode (for P2P live view).
 *        No-op on non-DVP boards.
 */
OPERATE_RET tuya_ai_toy_camera_switch_to_h264_mode(VOID);

/**
 * @brief Switch DVP back to JPEG+YUV422 dual-stream mode (normal AI mode).
 *        No-op on non-DVP boards.
 */
OPERATE_RET tuya_ai_toy_camera_switch_to_jpeg_mode(VOID);

OPERATE_RET tuya_ai_toy_camera_h264_start();

OPERATE_RET tuya_ai_toy_camera_h264_stop();

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_AI_TOY_CAMERA_H__ */
