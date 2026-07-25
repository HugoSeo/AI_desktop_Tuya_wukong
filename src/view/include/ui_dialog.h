/**
 * @file ui_dialog.h
 * @brief Cfg-driven modal dialog drawn on lv_layer_top
 *
 * Reusable across screens: callers fill a UI_DIALOG_CFG_T and call
 * ui_dialog_show(); the dialog paints a full-screen mask + centered card
 * on lv_layer_top so it sits above whatever screen is currently active.
 *
 * Two safety knobs intentionally exposed (see ADR-0004 for the reset use
 * case that drove these): countdown_sec arms a destructive-action button
 * only after N seconds of countdown, dismissable controls whether the
 * mask region eats clicks as a "cancel" gesture.
 *
 * @version 1.0
 * @date 2026-05-23
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_DIALOG_H__
#define __UI_DIALOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "lvgl.h"

#define UI_DIALOG_BTN_MAX       2

/**
 * @brief Single button descriptor in a dialog.
 *
 * `is_primary` matters only when the dialog has a non-zero countdown:
 * exactly one button should be primary, that one stays disabled and
 * shows "<text>(N)" countdown until N hits 0.
 */
typedef struct {
    const CHAR_T *text;
    BOOL_T        is_primary;
    VOID_T      (*on_click)(VOID_T);
} UI_DIALOG_BTN_T;

/**
 * @brief Dialog configuration.
 *
 * @note Strings (title/body/buttons[].text) must outlive the dialog.
 *       Pass string literals or static buffers — no copy is made.
 */
typedef struct {
    const CHAR_T   *title;
    const CHAR_T   *body;
    UI_DIALOG_BTN_T buttons[UI_DIALOG_BTN_MAX];
    UINT8_T         button_count;
    UINT8_T         countdown_sec;
    BOOL_T          dismissable;
    VOID_T        (*on_dismiss)(VOID_T);
} UI_DIALOG_CFG_T;

/**
 * @brief Show a modal dialog. Hides any currently-shown dialog first.
 * @param[in] cfg dialog configuration; must be non-NULL with button_count >= 1
 * @return none
 */
VOID_T ui_dialog_show(const UI_DIALOG_CFG_T *cfg);

/**
 * @brief Hide the currently-shown dialog (if any). Safe to call when none.
 * @return none
 * @note Does NOT invoke on_dismiss — that fires only on user-initiated
 *       cancel paths (mask click, cancel button). Programmatic close from
 *       the owner code is silent.
 */
VOID_T ui_dialog_hide(VOID_T);

/**
 * @brief Whether a dialog is currently shown.
 */
BOOL_T ui_dialog_is_visible(VOID_T);

#ifdef __cplusplus
}
#endif

#endif /* __UI_DIALOG_H__ */
