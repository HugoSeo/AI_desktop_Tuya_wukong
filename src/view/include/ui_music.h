/**
 * @file ui_music.h
 * @brief Music screen API (decorative placeholder)
 * @version 1.0
 * @date 2026-05-15
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_MUSIC_H__
#define __UI_MUSIC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "lvgl.h"

/**
 * @brief Create the music screen (does NOT load/show it)
 * @return none
 */
VOID_T setup_scr_music(VOID_T);

/**
 * @brief Show the music screen (creates if needed)
 * @return none
 */
VOID_T ui_music_show(VOID_T);

/**
 * @brief Hide the music screen (no-op placeholder; kept for nav-registry symmetry)
 * @return none
 */
VOID_T ui_music_hide(VOID_T);

/**
 * @brief Get the music screen object
 * @return music screen pointer, NULL if not created
 */
lv_obj_t *ui_music_get_scr(VOID_T);

#ifdef __cplusplus
}
#endif

#endif /* __UI_MUSIC_H__ */
