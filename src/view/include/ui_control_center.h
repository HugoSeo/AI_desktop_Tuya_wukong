/**
 * @file ui_control_center.h
 * @brief Control center overlay API
 * @version 1.0
 * @date 2026-05-13
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_CONTROL_CENTER_H__
#define __UI_CONTROL_CENTER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "lvgl.h"

/**
 * @brief Build and show the control center overlay screen
 * @param[in] volume current volume (0-100)
 * @param[in] brightness current brightness (0-100)
 * @param[in] alarm_vol current alarm volume (0-100)
 * @return none
 */
VOID_T setup_scr_control_center(UINT8_T volume, UINT8_T brightness, UINT8_T alarm_vol);

/**
 * @brief Check whether control center is currently visible
 * @return TRUE if visible, FALSE otherwise
 */
BOOL_T ui_control_center_is_active(VOID_T);

/**
 * @brief Refresh the mode entry label to reflect the current device / chat
 *        sub-mode. No-op when the control center is not visible.
 * @return none
 */
VOID_T ui_control_center_refresh_mode(VOID_T);

/**
 * @brief Register swipe-down gesture on any screen to open control center
 * @param[in] scr screen object to register gesture on
 * @return none
 */
VOID_T ui_control_center_register_gesture(lv_obj_t *scr);

#ifdef __cplusplus
}
#endif

#endif /* __UI_CONTROL_CENTER_H__ */
