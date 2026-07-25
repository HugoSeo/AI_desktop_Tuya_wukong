/**
 * @file ui_detection.h
 * @brief Detection list screen API (read-only browser of recent AI detection records)
 * @version 1.0
 * @date 2026-05-18
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_DETECTION_H__
#define __UI_DETECTION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "lvgl.h"

/**
 * @brief Show the detection screen (creates lazily, refetches page 1 every visit)
 * @return none
 */
VOID_T ui_detection_show(VOID_T);

/**
 * @brief Hide the detection screen (no-op; lv_obj tree persists for next show)
 * @return none
 */
VOID_T ui_detection_hide(VOID_T);

/**
 * @brief Get the detection screen object
 * @return detection screen pointer, NULL if not created yet
 */
lv_obj_t *ui_detection_get_scr(VOID_T);

#ifdef __cplusplus
}
#endif

#endif /* __UI_DETECTION_H__ */
