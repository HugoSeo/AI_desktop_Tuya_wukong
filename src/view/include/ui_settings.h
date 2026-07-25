/**
 * @file ui_settings.h
 * @brief Settings screen API (currently houses the device-reset entry only)
 * @version 1.0
 * @date 2026-05-19
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_SETTINGS_H__
#define __UI_SETTINGS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "lvgl.h"

/**
 * @brief Show the settings screen (creates lazily on first call)
 * @return none
 */
VOID_T ui_settings_show(VOID_T);

/**
 * @brief Hide the settings screen, drop any in-flight reset countdown,
 *        and rewind the inner view to the home list
 * @return none
 */
VOID_T ui_settings_hide(VOID_T);

/**
 * @brief Get the settings screen object
 * @return settings screen pointer, NULL if not created yet
 */
lv_obj_t *ui_settings_get_scr(VOID_T);

/**
 * @brief Get current P2P toggle state
 * @return TRUE when P2P is enabled, FALSE otherwise
 */
BOOL_T ui_settings_p2p_get(VOID_T);

#ifdef __cplusplus
}
#endif

#endif /* __UI_SETTINGS_H__ */
