/**
 * @file ui_toast.c
 * @brief Lightweight transient toast. See ui_toast.h for the contract.
 *
 * One persistent lv_obj on lv_layer_top, hidden by default; re-shows
 * mutate text + restart the lv_timer. Auto-hide after
 * UI_TOAST_DURATION_MS. The toast container is non-clickable so taps
 * pass through to the underlying screen — toasts must never block input.
 *
 * @version 1.0
 * @date 2026-05-23
 * @copyright Copyright (c) Tuya Inc.
 */
#include <string.h>
#include "tuya_cloud_types.h"
#include "lvgl.h"
#include "uni_log.h"
#include "ui_theme.h"
#include "ui_toast.h"

#define UI_TOAST_DURATION_MS    2000
#define UI_TOAST_W              260
#define UI_TOAST_H              44
#define UI_TOAST_BOTTOM_PAD     80
#define UI_TOAST_RADIUS         12
#define UI_TOAST_BG             0x000000
#define UI_TOAST_BG_OPA         LV_OPA_80
#define UI_TOAST_PAD_X          16

typedef struct {
    lv_obj_t   *cont;
    lv_obj_t   *lbl;
    lv_timer_t *autohide_tm;
} UI_TOAST_T;

STATIC UI_TOAST_T s_toast = {0};

STATIC VOID_T __toast_autohide_cb(lv_timer_t *tm);
STATIC VOID_T __toast_build(VOID_T);

STATIC VOID_T __toast_build(VOID_T)
{
    if (s_toast.cont != NULL) {
        return;
    }

    lv_obj_t *layer = lv_layer_top();

    s_toast.cont = lv_obj_create(layer);
    lv_obj_remove_style_all(s_toast.cont);
    lv_obj_set_size(s_toast.cont, UI_TOAST_W, UI_TOAST_H);
    lv_obj_align(s_toast.cont, LV_ALIGN_BOTTOM_MID, 0, -UI_TOAST_BOTTOM_PAD);
    lv_obj_set_style_radius(s_toast.cont, UI_TOAST_RADIUS, 0);
    lv_obj_set_style_bg_color(s_toast.cont, lv_color_hex(UI_TOAST_BG), 0);
    lv_obj_set_style_bg_opa(s_toast.cont, UI_TOAST_BG_OPA, 0);
    lv_obj_set_style_pad_hor(s_toast.cont, UI_TOAST_PAD_X, 0);
    lv_obj_clear_flag(s_toast.cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_toast.cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_toast.cont, LV_OBJ_FLAG_HIDDEN);

    s_toast.lbl = lv_label_create(s_toast.cont);
    lv_label_set_long_mode(s_toast.lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_toast.lbl, UI_TOAST_W - 2 * UI_TOAST_PAD_X);
    lv_obj_set_style_text_font(s_toast.lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_toast.lbl, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_toast.lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_toast.lbl);
}

VOID_T ui_toast_show(const CHAR_T *text)
{
    if (text == NULL) {
        return;
    }

    __toast_build();

    lv_label_set_text(s_toast.lbl, text);
    /* Bring to front each show: callers who pop a dialog after toast'ing
     * (or vice versa) get predictable z-order. */
    lv_obj_move_foreground(s_toast.cont);
    lv_obj_clear_flag(s_toast.cont, LV_OBJ_FLAG_HIDDEN);

    if (s_toast.autohide_tm != NULL) {
        lv_timer_del(s_toast.autohide_tm);
    }
    s_toast.autohide_tm = lv_timer_create(__toast_autohide_cb,
                                          UI_TOAST_DURATION_MS, NULL);
    /* One-shot semantics: lv_timer fires periodically by default, so the
     * autohide cb deletes the timer itself after the first tick. */
    lv_timer_set_repeat_count(s_toast.autohide_tm, 1);
}

VOID_T ui_toast_hide(VOID_T)
{
    if (s_toast.autohide_tm != NULL) {
        lv_timer_del(s_toast.autohide_tm);
        s_toast.autohide_tm = NULL;
    }
    if (s_toast.cont != NULL) {
        lv_obj_add_flag(s_toast.cont, LV_OBJ_FLAG_HIDDEN);
    }
}

STATIC VOID_T __toast_autohide_cb(lv_timer_t *tm)
{
    (VOID_T)tm;
    if (s_toast.cont != NULL) {
        lv_obj_add_flag(s_toast.cont, LV_OBJ_FLAG_HIDDEN);
    }
    s_toast.autohide_tm = NULL;
}
