/**
 * @file ui_device_mode.h
 * @brief Device mode selection screen API
 * @version 1.0
 * @date 2026-05-13
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_DEVICE_MODE_H__
#define __UI_DEVICE_MODE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "lvgl.h"

/**
 * @brief Create device mode selection screen (does NOT load/show it)
 * @return none
 */
VOID_T setup_scr_device_mode(VOID_T);

/**
 * @brief Show the device mode selection screen (creates if needed)
 * @return none
 */
VOID_T ui_device_mode_show(VOID_T);

/**
 * @brief Hide device mode screen and release per-show resources
 * @return none
 */
VOID_T ui_device_mode_hide(VOID_T);

/**
 * @brief Get the device mode screen object
 * @return device mode screen pointer, NULL if not created
 */
lv_obj_t *ui_device_mode_get_scr(VOID_T);

#ifdef __cplusplus
}
#endif

#endif /* __UI_DEVICE_MODE_H__ */
