/**
 * @file ui_call.h
 * @brief Voice call screen API (outbound device-to-app call)
 * @version 1.0
 * @date 2026-05-18
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_CALL_H__
#define __UI_CALL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "lvgl.h"

/**
 * @brief Show the call screen (creates lazily on first call)
 * @return none
 */
VOID_T ui_call_show(VOID_T);

/**
 * @brief Hide the call screen, hang up if a call is active, release per-visit resources
 * @return none
 */
VOID_T ui_call_hide(VOID_T);

/**
 * @brief Get the call screen object
 * @return call screen pointer, NULL if not created yet
 */
lv_obj_t *ui_call_get_scr(VOID_T);

#ifdef __cplusplus
}
#endif

#endif /* __UI_CALL_H__ */
