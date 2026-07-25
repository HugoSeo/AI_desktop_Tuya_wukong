/**
 * @file ui_record.h
 * @brief Voice recording (main capture) screen API
 * @version 1.0
 * @date 2026-05-13
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_RECORD_H__
#define __UI_RECORD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "lvgl.h"

/**
 * @brief Create the main record screen (does NOT load/show it)
 * @return none
 */
VOID_T setup_scr_record(VOID_T);

/**
 * @brief Show the main record screen (creates if needed)
 * @return none
 */
VOID_T ui_record_show(VOID_T);

/**
 * @brief Hide the main record screen and release per-show resources
 * @return none
 * @note Stops the internal LVGL timer so the timer label stops ticking
 *       once the screen leaves the navigation stack.
 */
VOID_T ui_record_hide(VOID_T);

/**
 * @brief Get the main record screen object
 * @return record screen pointer, NULL if not created
 */
lv_obj_t *ui_record_get_scr(VOID_T);

#ifdef __cplusplus
}
#endif

#endif /* __UI_RECORD_H__ */
