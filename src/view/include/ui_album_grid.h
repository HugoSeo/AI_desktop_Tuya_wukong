/**
 * @file ui_album_grid.h
 * @brief Album grid (thumbnail browser) screen API
 * @version 1.0
 * @date 2026-05-13
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_ALBUM_GRID_H__
#define __UI_ALBUM_GRID_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "wukong_picture.h"
#include "lvgl.h"

/**
 * @brief Create album grid screen (does NOT load/show it)
 * @return none
 */
VOID_T setup_scr_album_grid(VOID_T);

/**
 * @brief Show the album grid screen (creates if needed)
 * @return none
 */
VOID_T ui_album_grid_show(VOID_T);

/**
 * @brief Set thumbnail data for the album grid display
 * @param[in] list thumbnail list (caller retains ownership, must stay alive until hide)
 * @return none
 */
VOID_T ui_album_grid_set_thumbs(CONST WUKONG_PICTURE_THUMB_LIST_T *list);

/**
 * @brief Get filenames of currently selected items in the grid
 * @param[out] names array to fill with pointers to internal filename strings
 * @param[in] max_count capacity of names array
 * @return number of selected filenames written
 */
UINT32_T ui_album_grid_get_selected_names(CONST CHAR_T *names[], UINT32_T max_count);

/**
 * @brief Get filenames pending deletion (consumed once: buffer is cleared after call)
 * @param[out] names array to fill with pointers to internal filename strings
 * @param[in] max_count capacity of names array
 * @return number of filenames written
 */
UINT32_T ui_album_grid_get_pending_delete_names(CONST CHAR_T *names[],
                                                UINT32_T max_count);

/**
 * @brief Hide album grid screen and release resources
 * @return none
 */
VOID_T ui_album_grid_hide(VOID_T);

/**
 * @brief Get the album grid screen object
 * @return album grid screen pointer, NULL if not created
 */
lv_obj_t *ui_album_grid_get_scr(VOID_T);

#ifdef __cplusplus
}
#endif

#endif /* __UI_ALBUM_GRID_H__ */
