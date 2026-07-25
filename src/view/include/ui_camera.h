/**
 * @file ui_camera.h
 * @brief Camera screen API
 * @version 1.0
 * @date 2026-05-13
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_CAMERA_H__
#define __UI_CAMERA_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "lvgl.h"

/**
 * @brief Create camera screen objects (does NOT load/show it)
 * @return none
 */
VOID_T setup_scr_camera(VOID_T);

/**
 * @brief Show the camera screen (creates if needed)
 * @return none
 */
VOID_T ui_camera_show(VOID_T);

/**
 * @brief Hide camera screen and switch to target screen
 * @param[in] target_scr screen to switch to (NULL to stay)
 * @return none
 */
VOID_T ui_camera_hide(lv_obj_t *target_scr);

/**
 * @brief Push a YUV preview frame to the camera screen
 * @param[in] width frame width in pixels
 * @param[in] height frame height in pixels
 * @param[in] data YUV422 buffer (caller retains ownership)
 * @param[in] len buffer length in bytes
 * @return none
 */
VOID_T ui_camera_set_preview_yuv_format(uint16_t width, uint16_t height,
                                        uint8_t *data, uint32_t len);

/**
 * @brief Set the thumbnail image after a photo is taken
 * @param[in] data JPEG buffer (caller retains ownership)
 * @param[in] len JPEG buffer length in bytes
 * @return none
 */
VOID_T ui_camera_set_thumbnail_jpeg(uint8_t *data, uint32_t len);

/**
 * @brief Hide thumbnail and release its buffer (e.g. album became empty)
 * @return none
 */
VOID_T ui_camera_clear_thumbnail(VOID_T);

/**
 * @brief Get the camera screen object
 * @return camera screen pointer, NULL if not created
 */
lv_obj_t *ui_camera_get_scr(VOID_T);

#ifdef __cplusplus
}
#endif

#endif /* __UI_CAMERA_H__ */
