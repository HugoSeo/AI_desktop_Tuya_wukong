/**
 * @file ui_album.h
 * @brief Album single-photo viewer screen API
 * @version 1.0
 * @date 2026-05-13
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_ALBUM_H__
#define __UI_ALBUM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "lvgl.h"

/**
 * @brief Create album viewer screen (does NOT load/show it)
 * @return none
 */
VOID_T setup_scr_album(VOID_T);

/**
 * @brief Show the album viewer (creates if needed)
 * @return none
 */
VOID_T ui_album_show(VOID_T);

/**
 * @brief Push a JPEG photo to the album viewer
 * @param[in] width photo width in pixels
 * @param[in] height photo height in pixels
 * @param[in] data JPEG buffer (caller retains ownership)
 * @param[in] len JPEG buffer length in bytes
 * @return none
 */
VOID_T ui_album_set_jpeg_photo(uint16_t width, uint16_t height,
                               uint8_t *data, uint32_t len);

/**
 * @brief Show or hide empty-album hint and release photo canvas buffer when empty
 * @param[in] empty TRUE to show "暂无图片" and hide photo; FALSE to show photo area
 * @return none
 */
VOID_T ui_album_set_empty_state(BOOL_T empty);

/**
 * @brief Update the title and time labels
 * @param[in] title title string (e.g. "今天")
 * @param[in] time time string (e.g. "10:00")
 * @return none
 */
VOID_T ui_album_set_info(CONST CHAR_T *title, CONST CHAR_T *time);

/**
 * @brief Get the album screen object
 * @return album screen pointer, NULL if not created
 */
lv_obj_t *ui_album_get_scr(VOID_T);

/**
 * @brief Hide album screen and release canvas buffer to save memory
 * @return none
 */
VOID_T ui_album_hide(VOID_T);

#ifdef __cplusplus
}
#endif

#endif /* __UI_ALBUM_H__ */
