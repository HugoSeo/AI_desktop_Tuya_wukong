/**
 * @file ui_dialog.c
 * @brief Cfg-driven modal dialog. See ui_dialog.h for the contract.
 *
 * The dialog paints two LVGL objects on lv_layer_top():
 *   - mask:  full-screen semi-transparent cont; eats input so the
 *            underlying screen can't be clicked through. If
 *            cfg->dismissable, a click on the mask itself fires the
 *            cancel path.
 *   - card:  centered cont with title/body/buttons rendered top-down.
 *
 * Lifecycle: a single static UI_DIALOG_T s_dlg holds the in-flight
 * dialog state. Show-while-shown is hide-then-show — no stacking, no
 * queueing. Owner code that wants to reuse the dialog for a follow-up
 * confirmation (e.g. "wifi reset done — return to home?") must drive
 * that itself from on_click callbacks.
 *
 * @version 1.0
 * @date 2026-05-23
 * @copyright Copyright (c) Tuya Inc.
 */
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "tuya_cloud_types.h"
#include "lvgl.h"
#include "uni_log.h"
#include "ui_theme.h"
#include "ui_dialog.h"

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define DIALOG_CARD_W           280
#define DIALOG_CARD_RADIUS      16
#define DIALOG_CARD_BG          0x25262A
#define DIALOG_CARD_BORDER      0x55585F

#define DIALOG_MASK_BG          0x000000
#define DIALOG_MASK_OPA         LV_OPA_60

#define DIALOG_PAD_X            20
#define DIALOG_TITLE_Y          24
#define DIALOG_PAD_BOTTOM       24
#define DIALOG_ROW_GAP          16   /* gap between title / body / btn row */

#define DIALOG_BTN_W            108
#define DIALOG_BTN_H            44
#define DIALOG_BTN_RADIUS       22
#define DIALOG_BTN_GAP          16
#define DIALOG_BTN_BG_IDLE      0xB8BDDE   /* matches settings cancel/idle */
#define DIALOG_BTN_BG_ARMED     0xFFF37B   /* matches settings armed yellow */
#define DIALOG_BTN_BG_OPA_IDLE  LV_OPA_30
#define DIALOG_BTN_BG_OPA_ARMED LV_OPA_COVER
#define DIALOG_BTN_TEXT_IDLE    0xFFFFFF   /* white on darker idle bg */
#define DIALOG_BTN_TEXT_ARMED   0x222222   /* dark on yellow armed bg for contrast */

#define DIALOG_TITLE_COLOR      0xFFF37B   /* yellow accent */
#define DIALOG_BODY_COLOR_GREY  0xCCCCCC

#define DIALOG_COUNTDOWN_PERIOD 1000

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    lv_obj_t       *mask;
    lv_obj_t       *card;
    lv_obj_t       *primary_btn;
    lv_obj_t       *primary_lbl;
    lv_timer_t     *countdown_tm;
    INT_T           countdown_left;
    UI_DIALOG_CFG_T cfg;
    BOOL_T          visible;
    /* Snapshot of cfg.buttons[].text used for label updates; cfg keeps the
     * caller-supplied pointers, this snapshot is what we copy/append "(N)" on. */
    CHAR_T          primary_text_base[24];
} UI_DIALOG_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC UI_DIALOG_T s_dlg = {0};

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __dlg_build(VOID_T);
STATIC VOID_T __dlg_destroy(VOID_T);
STATIC VOID_T __dlg_set_primary_label(INT_T sec_left);
STATIC VOID_T __dlg_arm_primary(VOID_T);
STATIC VOID_T __dlg_kill_countdown(VOID_T);
STATIC VOID_T __dlg_btn_click_cb(lv_event_t *e);
STATIC VOID_T __dlg_mask_click_cb(lv_event_t *e);
STATIC VOID_T __dlg_countdown_tick_cb(lv_timer_t *tm);

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */

VOID_T ui_dialog_show(const UI_DIALOG_CFG_T *cfg)
{
    if (cfg == NULL || cfg->button_count == 0 ||
        cfg->button_count > UI_DIALOG_BTN_MAX) {
        PR_ERR("dialog: invalid cfg");
        return;
    }

    if (s_dlg.visible) {
        ui_dialog_hide();
    }

    s_dlg.cfg = *cfg;
    __dlg_build();
    s_dlg.visible = TRUE;
}

VOID_T ui_dialog_hide(VOID_T)
{
    if (!s_dlg.visible) {
        return;
    }
    __dlg_kill_countdown();
    __dlg_destroy();
    s_dlg.visible = FALSE;
    memset(&s_dlg.cfg, 0, sizeof(s_dlg.cfg));
}

BOOL_T ui_dialog_is_visible(VOID_T)
{
    return s_dlg.visible;
}

/* ---------------------------------------------------------------------------
 * Build / destroy
 * --------------------------------------------------------------------------- */

STATIC VOID_T __dlg_build(VOID_T)
{
    lv_obj_t *layer = lv_layer_top();

    /* Mask: full screen, semi-transparent, eats clicks */
    s_dlg.mask = lv_obj_create(layer);
    lv_obj_remove_style_all(s_dlg.mask);
    lv_obj_set_size(s_dlg.mask, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(s_dlg.mask, 0, 0);
    lv_obj_set_style_bg_color(s_dlg.mask, lv_color_hex(DIALOG_MASK_BG), 0);
    lv_obj_set_style_bg_opa(s_dlg.mask, DIALOG_MASK_OPA, 0);
    lv_obj_clear_flag(s_dlg.mask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_dlg.mask, LV_OBJ_FLAG_CLICKABLE);
    if (s_dlg.cfg.dismissable) {
        lv_obj_add_event_cb(s_dlg.mask, __dlg_mask_click_cb,
                            LV_EVENT_CLICKED, NULL);
    }

    /* Card: centered, fixed width, content-sized height */
    s_dlg.card = lv_obj_create(s_dlg.mask);
    lv_obj_remove_style_all(s_dlg.card);
    lv_obj_set_width(s_dlg.card, DIALOG_CARD_W);
    lv_obj_set_height(s_dlg.card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_dlg.card, lv_color_hex(DIALOG_CARD_BG), 0);
    lv_obj_set_style_bg_opa(s_dlg.card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_dlg.card, DIALOG_CARD_RADIUS, 0);
    lv_obj_set_style_border_color(s_dlg.card,
                                  lv_color_hex(DIALOG_CARD_BORDER), 0);
    lv_obj_set_style_border_width(s_dlg.card, 1, 0);
    lv_obj_set_style_pad_hor(s_dlg.card, DIALOG_PAD_X, 0);
    lv_obj_set_style_pad_top(s_dlg.card, DIALOG_TITLE_Y, 0);
    lv_obj_set_style_pad_bottom(s_dlg.card, DIALOG_PAD_BOTTOM, 0);
    /* Spacing between title / body / btn_row is owned by the card's
     * row_gap so each child can stay LV_SIZE_CONTENT and the card height
     * accumulates correctly. Earlier btn_row had its own pad_top which
     * overflowed a fixed-height row and clipped the buttons. */
    lv_obj_set_style_pad_row(s_dlg.card, DIALOG_ROW_GAP, 0);
    lv_obj_clear_flag(s_dlg.card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_dlg.card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(s_dlg.card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_dlg.card,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    /* Title */
    if (s_dlg.cfg.title != NULL && s_dlg.cfg.title[0] != '\0') {
        lv_obj_t *title = lv_label_create(s_dlg.card);
        lv_label_set_text(title, s_dlg.cfg.title);
        lv_obj_set_style_text_font(title,
                                   &AlibabaPuHuiTi3_Regular18_Static, 0);
        lv_obj_set_style_text_color(title,
                                    lv_color_hex(DIALOG_TITLE_COLOR), 0);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    }

    /* Body */
    if (s_dlg.cfg.body != NULL && s_dlg.cfg.body[0] != '\0') {
        lv_obj_t *body = lv_label_create(s_dlg.card);
        lv_label_set_text(body, s_dlg.cfg.body);
        lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(body, DIALOG_CARD_W - 2 * DIALOG_PAD_X);
        lv_obj_set_style_text_font(body,
                                   &AlibabaPuHuiTi3_Regular18_Static, 0);
        lv_obj_set_style_text_color(body,
                                    lv_color_hex(DIALOG_BODY_COLOR_GREY), 0);
        lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    }

    /* Button row — height auto-fits the buttons (no internal pad_top
     * or fixed height; card.pad_row gives the gap above). */
    lv_obj_t *btn_row = lv_obj_create(s_dlg.card);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_width(btn_row, LV_PCT(100));
    lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(btn_row, DIALOG_BTN_GAP, 0);

    /* Stretch button width when only one button so it dominates the row. */
    INT_T btn_w = (s_dlg.cfg.button_count == 1) ?
                  (DIALOG_CARD_W - 2 * DIALOG_PAD_X) :
                  DIALOG_BTN_W;

    s_dlg.primary_btn = NULL;
    s_dlg.primary_lbl = NULL;
    s_dlg.primary_text_base[0] = '\0';

    for (UINT8_T i = 0; i < s_dlg.cfg.button_count; i++) {
        const UI_DIALOG_BTN_T *bcfg = &s_dlg.cfg.buttons[i];
        BOOL_T countdown_armed_btn =
            (s_dlg.cfg.countdown_sec > 0 && bcfg->is_primary);

        lv_obj_t *btn = lv_btn_create(btn_row);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, btn_w, DIALOG_BTN_H);
        lv_obj_set_style_radius(btn, DIALOG_BTN_RADIUS, 0);
        lv_obj_set_style_bg_color(btn,
                                  lv_color_hex(DIALOG_BTN_BG_IDLE), 0);
        lv_obj_set_style_bg_opa(btn, DIALOG_BTN_BG_OPA_IDLE, 0);

        /* Countdown-armed button starts disabled (not clickable). All
         * other buttons are clickable from the start. */
        if (countdown_armed_btn) {
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        }

        /* Wire click. Index goes via user_data so the cb knows which
         * button fired. button_count <= UI_DIALOG_BTN_MAX so the cast
         * loses no info. */
        lv_obj_add_event_cb(btn, __dlg_btn_click_cb,
                            LV_EVENT_CLICKED, (VOID_T *)(uintptr_t)i);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl,
                                   &AlibabaPuHuiTi3_Regular18_Static, 0);
        lv_obj_set_style_text_color(lbl,
                                    lv_color_hex(DIALOG_BTN_TEXT_IDLE), 0);
        lv_obj_center(lbl);

        if (countdown_armed_btn) {
            s_dlg.primary_btn = btn;
            s_dlg.primary_lbl = lbl;
            const CHAR_T *src = (bcfg->text != NULL) ? bcfg->text : "";
            strncpy(s_dlg.primary_text_base, src,
                    sizeof(s_dlg.primary_text_base) - 1);
            s_dlg.primary_text_base[sizeof(s_dlg.primary_text_base) - 1] = '\0';
            s_dlg.countdown_left = s_dlg.cfg.countdown_sec;
            __dlg_set_primary_label(s_dlg.countdown_left);
        } else {
            lv_label_set_text(lbl,
                              (bcfg->text != NULL) ? bcfg->text : "");
        }
    }

    if (s_dlg.cfg.countdown_sec > 0 && s_dlg.primary_btn != NULL) {
        s_dlg.countdown_tm = lv_timer_create(__dlg_countdown_tick_cb,
                                             DIALOG_COUNTDOWN_PERIOD,
                                             NULL);
    }
}

STATIC VOID_T __dlg_destroy(VOID_T)
{
    if (s_dlg.mask != NULL) {
        lv_obj_del(s_dlg.mask);
        s_dlg.mask = NULL;
    }
    s_dlg.card = NULL;
    s_dlg.primary_btn = NULL;
    s_dlg.primary_lbl = NULL;
    s_dlg.primary_text_base[0] = '\0';
}

/* ---------------------------------------------------------------------------
 * Countdown
 * --------------------------------------------------------------------------- */

STATIC VOID_T __dlg_set_primary_label(INT_T sec_left)
{
    if (s_dlg.primary_lbl == NULL) {
        return;
    }

    if (sec_left > 0) {
        CHAR_T buf[32] = {0};
        snprintf(buf, sizeof(buf), "%s(%d)",
                 s_dlg.primary_text_base, sec_left);
        lv_label_set_text(s_dlg.primary_lbl, buf);
    } else {
        lv_label_set_text(s_dlg.primary_lbl, s_dlg.primary_text_base);
    }
}

STATIC VOID_T __dlg_arm_primary(VOID_T)
{
    if (s_dlg.primary_btn == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(s_dlg.primary_btn,
                              lv_color_hex(DIALOG_BTN_BG_ARMED), 0);
    lv_obj_set_style_bg_opa(s_dlg.primary_btn,
                            DIALOG_BTN_BG_OPA_ARMED, 0);
    /* White-on-yellow has poor contrast — flip the label to dark when
     * the armed yellow comes in so the "确认" stays legible. */
    if (s_dlg.primary_lbl != NULL) {
        lv_obj_set_style_text_color(s_dlg.primary_lbl,
                                    lv_color_hex(DIALOG_BTN_TEXT_ARMED), 0);
    }
    lv_obj_add_flag(s_dlg.primary_btn, LV_OBJ_FLAG_CLICKABLE);
}

STATIC VOID_T __dlg_kill_countdown(VOID_T)
{
    if (s_dlg.countdown_tm != NULL) {
        lv_timer_del(s_dlg.countdown_tm);
        s_dlg.countdown_tm = NULL;
    }
    s_dlg.countdown_left = 0;
}

/* ---------------------------------------------------------------------------
 * Event handlers
 * --------------------------------------------------------------------------- */

STATIC VOID_T __dlg_btn_click_cb(lv_event_t *e)
{
    UINT8_T idx = (UINT8_T)(uintptr_t)lv_event_get_user_data(e);
    if (idx >= s_dlg.cfg.button_count) {
        return;
    }

    /* Snapshot callback before potentially destroying ourselves: the
     * on_click handler is allowed to call ui_dialog_hide() (or even
     * ui_dialog_show()) so we must not touch s_dlg.cfg after the
     * callback returns. */
    VOID_T (*cb)(VOID_T) = s_dlg.cfg.buttons[idx].on_click;
    if (cb != NULL) {
        cb();
    }
}

STATIC VOID_T __dlg_mask_click_cb(lv_event_t *e)
{
    /* Only fire when the click target is the mask itself, not bubbled from
     * a child (card / button click). lv_event_get_target returns the
     * dispatcher-level target, which for a CLICKED event bubbling up
     * arrives at the mask too — guard explicitly. */
    if (lv_event_get_target(e) != s_dlg.mask) {
        return;
    }

    if (!s_dlg.cfg.dismissable) {
        return;
    }

    /* Snapshot before hide; on_dismiss may chain-show another dialog. */
    VOID_T (*cb)(VOID_T) = s_dlg.cfg.on_dismiss;
    ui_dialog_hide();
    if (cb != NULL) {
        cb();
    }
}

STATIC VOID_T __dlg_countdown_tick_cb(lv_timer_t *tm)
{
    (VOID_T)tm;

    s_dlg.countdown_left--;
    __dlg_set_primary_label(s_dlg.countdown_left);

    if (s_dlg.countdown_left > 0) {
        return;
    }

    __dlg_arm_primary();

    if (s_dlg.countdown_tm != NULL) {
        lv_timer_del(s_dlg.countdown_tm);
        s_dlg.countdown_tm = NULL;
    }
}
