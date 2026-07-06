#ifndef __TUYA_DEVICE_CAMERA_H__
#define __TUYA_DEVICE_CAMERA_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_ai_toy_camera.h"
#include "tuya_cloud_types.h"

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */

typedef enum {
    YUV_USER_PREVIEW  = (1 << 0),
    YUV_USER_MD       = (1 << 1),
} YUV_USER_E;

typedef VOID_T (*TUYA_YUV_FRAME_CB)(UINT8_T *data, UINT16_T width, UINT16_T height);
typedef VOID_T (*TUYA_MJPEG_FRAME_CB)(UINT8_T *data, UINT32_T len);

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */

OPERATE_RET tuya_desktop_camera_init();
OPERATE_RET tuya_device_camera_switch_to_h264_mode(VOID);
OPERATE_RET tuya_device_camera_switch_to_jpeg_mode(VOID);
OPERATE_RET tuya_device_camera_yuv_start();
OPERATE_RET tuya_device_camera_yuv_stop();
OPERATE_RET tuya_device_camera_jpeg_start();
OPERATE_RET tuya_device_camera_jpeg_stop();
OPERATE_RET tuya_device_camera_h264_start();
OPERATE_RET tuya_device_camera_h264_stop();
OPERATE_RET tuya_device_camera_get_jpeg_frame(BYTE_T **data, UINT_T *len, VOID *user_data);

/**
 * @brief Acquire YUV stream for a specific user (reference counted)
 * @param[in] user caller identity bitmask
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_device_camera_yuv_acquire(YUV_USER_E user);

/**
 * @brief Release YUV stream for a specific user (reference counted)
 * @param[in] user caller identity bitmask
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_device_camera_yuv_release(YUV_USER_E user);

/**
 * @brief Register YUV frame callback and acquire YUV stream for MD user
 * @param[in] cb YUV frame callback to receive raw YUV422 data
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_device_camera_md_start(TUYA_YUV_FRAME_CB cb);

/**
 * @brief Unregister YUV frame callback and release YUV stream for MD user
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_device_camera_md_stop(VOID);

/**
 * @brief Register MJPEG frame callback for MD one-shot capture
 *
 * Starts the MJPEG stream and dispatches frames to the callback instead of
 * the normal camera_to_jpg path.  Use this for detection-mode photo capture
 * so the UI camera page is not involved.
 *
 * @param[in] cb MJPEG frame callback (data pointer + length)
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_device_camera_md_jpeg_start(TUYA_MJPEG_FRAME_CB cb);

/**
 * @brief Unregister MJPEG frame callback for MD and stop MJPEG stream
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_device_camera_md_jpeg_stop(VOID);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_DEVICE_CAMERA_H__ */