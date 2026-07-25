/**
 * @file ui_toast.h
 * @brief Lightweight transient toast on lv_layer_top
 *
 * Non-blocking text bubble that auto-disappears after a short duration.
 * Used by code paths that need to surface a one-line failure / status
 * message without forcing the user to acknowledge it (which would call
 * for ui_dialog instead).
 *
 * Calls re-entering ui_toast_show while a toast is up replace the text
 * and reset the timer in place.
 *
 * @version 1.0
 * @date 2026-05-23
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_TOAST_H__
#define __UI_TOAST_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "lvgl.h"

/**
 * @brief Show a toast at the bottom of the screen.
 * @param[in] text NUL-terminated string; copied internally so the caller
 *                 does not need to keep the buffer alive.
 * @return none
 */
VOID_T ui_toast_show(const CHAR_T *text);

/**
 * @brief Hide any in-flight toast immediately.
 * @return none
 */
VOID_T ui_toast_hide(VOID_T);

#ifdef __cplusplus
}
#endif

#endif /* __UI_TOAST_H__ */
