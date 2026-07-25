/**
 * @file ui_music_list.h
 * @brief Music playlist screen API
 * @version 1.0
 * @date 2026-05-21
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_MUSIC_LIST_H__
#define __UI_MUSIC_LIST_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "lvgl.h"

/**
 * @brief Create the music playlist screen (does NOT load/show it)
 * @return none
 */
VOID_T setup_scr_music_list(VOID_T);

/**
 * @brief Show the music playlist screen (creates if needed)
 * @return none
 */
VOID_T ui_music_list_show(VOID_T);

/**
 * @brief Hide the music playlist screen, release per-visit subscriptions
 * @return none
 */
VOID_T ui_music_list_hide(VOID_T);

/**
 * @brief Get the music playlist screen object
 * @return music playlist screen pointer, NULL if not created
 */
lv_obj_t *ui_music_list_get_scr(VOID_T);

#ifdef __cplusplus
}
#endif

#endif /* __UI_MUSIC_LIST_H__ */
