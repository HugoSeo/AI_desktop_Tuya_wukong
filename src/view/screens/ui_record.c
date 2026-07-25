/**
 * @file ui_record.c
 * @brief Main record screen for T5AI_BOARD (320x480)
 *
 * Hosts a centered elapsed-time label and a tri-state record button:
 *   DEFAULT -> RECORDING -> PAUSED -> RECORDING -> ...
 * The right-hand title button shows the list icon (DEFAULT) or "完成"
 * (RECORDING/PAUSED). State transitions are driven entirely by UI taps;
 * each transition is forwarded to the dispatch layer through
 * TY_DISP_ACT_RECORD_START / PAUSE / RESUME / STOP, where the real
 * recording back-end will hook in later.
 *
 * @version 1.0
 * @date 2026-05-13
 * @copyright Copyright (c) Tuya Inc.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "ui_common.h"
#include "tuya_ai_display.h"

/* ---------------------------------------------------------------------------
 * Font / icon declarations
 * --------------------------------------------------------------------------- */
LV_IMG_DECLARE(icon_record_list);

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define RECORD_TIME_DEFAULT_TEXT   "00:00.00"
#define RECORD_TIME_TOP_PAD        30

#define RECORD_BTN_SIZE            50
#define RECORD_BTN_BOTTOM_PAD      20

/* Original PNGs reproduced with LVGL primitives (50×50 indicator):
 * Shared white ring across all states; only the inner figure changes.
 * DEFAULT   = ring + 40×40 red filled disc
 * RECORDING = ring + red rounded square inside
 * PAUSED    = ring + red play triangle (LV_SYMBOL_PLAY) inside
 */
#define RECORD_RED_COLOR           0xEC5C5C
#define RECORD_RING_BORDER_W       2
#define RECORD_INNER_DISC_SIZE     40
#define RECORD_INNER_SQUARE_SIZE   22
#define RECORD_INNER_SQUARE_RADIUS 5

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef enum {
    RECORD_STATE_DEFAULT = 0,
    RECORD_STATE_RECORDING,
    RECORD_STATE_PAUSED,
} RECORD_STATE_E;

typedef struct {
    lv_obj_t   *scr;
    lv_obj_t   *title_bar;
    lv_obj_t   *title_lbl;
    lv_obj_t   *back_btn;
    lv_obj_t   *right_btn;
    lv_obj_t   *right_list_icon;  /* lv_img, visible in DEFAULT */
    lv_obj_t   *right_done_lbl;   /* lv_label, visible otherwise */
    lv_obj_t   *content;
    lv_obj_t   *time_lbl;
    lv_obj_t   *record_btn;       /* 50x50 transparent click area */
    /* State indicators drawn with LVGL primitives — toggled per state */
    lv_obj_t   *record_ring;         /* Static white ring (always visible) */
    lv_obj_t   *record_inner_disc;   /* DEFAULT: 40×40 red filled circle */
    lv_obj_t   *record_inner_square; /* RECORDING: red rounded square */
    lv_obj_t   *record_inner_play;   /* PAUSED: red play-triangle symbol */
    lv_timer_t *tick_timer;
    RECORD_STATE_E state;
    UINT32_T    elapsed_sec;
} RECORD_UI_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC RECORD_UI_T s_record = {0};

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __record_back_cb(lv_event_t *e);
STATIC VOID_T __record_right_btn_cb(lv_event_t *e);
STATIC VOID_T __record_btn_cb(lv_event_t *e);
STATIC VOID_T __record_tick_cb(lv_timer_t *timer);
STATIC VOID_T __record_apply_state(RECORD_STATE_E state);
STATIC VOID_T __record_update_time_label(VOID_T);

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Format elapsed seconds as HH:MM.SS and refresh the time label
 * @return none
 */
STATIC VOID_T __record_update_time_label(VOID_T)
{
    CHAR_T buf[16];
    UINT32_T h = s_record.elapsed_sec / 3600;
    UINT32_T m = (s_record.elapsed_sec % 3600) / 60;
    UINT32_T s = s_record.elapsed_sec % 60;

    snprintf(buf, sizeof(buf), "%02u:%02u.%02u",
             (unsigned)h, (unsigned)m, (unsigned)s);
    if (s_record.time_lbl != NULL) {
        lv_label_set_text(s_record.time_lbl, buf);
    }
}

/**
 * @brief 1Hz timer callback driving the elapsed-time label
 * @param[in] timer LVGL timer handle
 * @return none
 */
STATIC VOID_T __record_tick_cb(lv_timer_t *timer)
{
    (VOID_T)timer;
    if (s_record.state != RECORD_STATE_RECORDING) {
        return;
    }
    s_record.elapsed_sec++;
    __record_update_time_label();
}

/**
 * @brief Apply visual updates and timer state for a target record state
 * @param[in] state target state
 * @return none
 * @note Centralizes record-icon swapping, right-title-button toggling and
 *       tick-timer lifecycle so every state-transition entry point converges
 *       through this helper.
 */
STATIC VOID_T __record_apply_state(RECORD_STATE_E state)
{
    s_record.state = state;

    if (s_record.right_list_icon != NULL && s_record.right_done_lbl != NULL) {
        if (state == RECORD_STATE_DEFAULT) {
            lv_obj_clear_flag(s_record.right_list_icon, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_record.right_done_lbl, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_record.right_list_icon, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_record.right_done_lbl, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_record.record_inner_disc != NULL && s_record.record_inner_square != NULL &&
        s_record.record_inner_play != NULL) {
        BOOL_T show_disc   = (state == RECORD_STATE_DEFAULT);
        BOOL_T show_square = (state == RECORD_STATE_RECORDING);
        BOOL_T show_play   = (state == RECORD_STATE_PAUSED);

        if (show_disc) {
            lv_obj_clear_flag(s_record.record_inner_disc, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_record.record_inner_disc, LV_OBJ_FLAG_HIDDEN);
        }
        if (show_square) {
            lv_obj_clear_flag(s_record.record_inner_square, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_record.record_inner_square, LV_OBJ_FLAG_HIDDEN);
        }
        if (show_play) {
            lv_obj_clear_flag(s_record.record_inner_play, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_record.record_inner_play, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (state == RECORD_STATE_RECORDING) {
        if (s_record.tick_timer == NULL) {
            s_record.tick_timer = lv_timer_create(__record_tick_cb, 1000, NULL);
        }
    } else {
        if (s_record.tick_timer != NULL) {
            lv_timer_del(s_record.tick_timer);
            s_record.tick_timer = NULL;
        }
    }

    if (state == RECORD_STATE_DEFAULT) {
        s_record.elapsed_sec = 0;
        __record_update_time_label();
    }
}

/**
 * @brief Back-button callback: close the record screen
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __record_back_cb(lv_event_t *e)
{
    (VOID_T)e;
    PR_DEBUG("record: back");
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_CLOSE_RECORD);
}

/**
 * @brief Right title-button callback: enter list (DEFAULT) or finish (else)
 * @param[in] e LVGL event
 * @return none
 * @note 完成 always pops the state machine back to DEFAULT before posting
 *       TY_DISP_ACT_RECORD_STOP so the visual feedback is immediate.
 */
STATIC VOID_T __record_right_btn_cb(lv_event_t *e)
{
    (VOID_T)e;

    if (s_record.state == RECORD_STATE_DEFAULT) {
        PR_DEBUG("record: open list");
        tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_OPEN_RECORD_LIST);
        return;
    }

    PR_DEBUG("record: stop (state=%d)", (int)s_record.state);
    __record_apply_state(RECORD_STATE_DEFAULT);
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_RECORD_STOP);
}

/**
 * @brief Record button callback: cycle DEFAULT -> RECORDING -> PAUSED -> RECORDING
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __record_btn_cb(lv_event_t *e)
{
    (VOID_T)e;

    switch (s_record.state) {
    case RECORD_STATE_DEFAULT:
        PR_DEBUG("record: start");
        __record_apply_state(RECORD_STATE_RECORDING);
        tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_RECORD_START);
        break;
    case RECORD_STATE_RECORDING:
        PR_DEBUG("record: pause");
        __record_apply_state(RECORD_STATE_PAUSED);
        tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_RECORD_PAUSE);
        break;
    case RECORD_STATE_PAUSED:
        PR_DEBUG("record: resume");
        __record_apply_state(RECORD_STATE_RECORDING);
        tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_RECORD_RESUME);
        break;
    default:
        break;
    }
}

/**
 * @brief Build the top title bar (back + title + list/done switch)
 * @return none
 */
STATIC VOID_T __record_build_title_bar(VOID_T)
{
    s_record.title_bar = lv_obj_create(s_record.scr);
    lv_obj_remove_style_all(s_record.title_bar);
    lv_obj_set_pos(s_record.title_bar, 0, 0);
    lv_obj_set_size(s_record.title_bar, LV_HOR_RES, UI_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(s_record.title_bar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_record.title_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_record.back_btn = lv_btn_create(s_record.title_bar);
    lv_obj_remove_style_all(s_record.back_btn);
    lv_obj_set_size(s_record.back_btn, UI_TITLE_BTN_W, UI_TITLE_BAR_H);
    lv_obj_set_pos(s_record.back_btn, 0, 0);
    lv_obj_set_style_bg_opa(s_record.back_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_record.back_btn, __record_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_icon = lv_img_create(s_record.back_btn);
    lv_img_set_src(back_icon, &icon_back_24_24);
    lv_obj_set_size(back_icon, UI_TITLE_ICON_SIZE, UI_TITLE_ICON_SIZE);
    lv_obj_center(back_icon);

    s_record.title_lbl = lv_label_create(s_record.title_bar);
    lv_label_set_text(s_record.title_lbl, "录音");
    lv_obj_set_style_text_font(s_record.title_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_record.title_lbl, lv_color_white(), 0);
    lv_obj_center(s_record.title_lbl);

    s_record.right_btn = lv_btn_create(s_record.title_bar);
    lv_obj_remove_style_all(s_record.right_btn);
    lv_obj_set_size(s_record.right_btn, UI_TITLE_BTN_W, UI_TITLE_BAR_H);
    lv_obj_set_pos(s_record.right_btn, LV_HOR_RES - UI_TITLE_BTN_W, 0);
    lv_obj_set_style_bg_opa(s_record.right_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_record.right_btn, __record_right_btn_cb, LV_EVENT_CLICKED,
                        NULL);

    s_record.right_list_icon = lv_img_create(s_record.right_btn);
    lv_img_set_src(s_record.right_list_icon, &icon_record_list);
    lv_obj_set_size(s_record.right_list_icon, UI_TITLE_ICON_SIZE,
                    UI_TITLE_ICON_SIZE);
    lv_obj_center(s_record.right_list_icon);
    lv_obj_clear_flag(s_record.right_list_icon, LV_OBJ_FLAG_CLICKABLE);

    s_record.right_done_lbl = lv_label_create(s_record.right_btn);
    lv_label_set_text(s_record.right_done_lbl, "完成");
    lv_obj_set_style_text_color(s_record.right_done_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_record.right_done_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_center(s_record.right_done_lbl);
    lv_obj_add_flag(s_record.right_done_lbl, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Build the content area: elapsed time label + tri-state record button
 * @return none
 */
STATIC VOID_T __record_build_content(VOID_T)
{
    s_record.content = lv_obj_create(s_record.scr);
    lv_obj_remove_style_all(s_record.content);
    lv_obj_set_pos(s_record.content, 0, UI_TITLE_BAR_H);
    lv_obj_set_size(s_record.content, LV_HOR_RES, LV_VER_RES - UI_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(s_record.content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_record.content, 0, 0);
    lv_obj_set_scrollbar_mode(s_record.content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_record.content, LV_OBJ_FLAG_SCROLLABLE);

    s_record.time_lbl = lv_label_create(s_record.content);
    lv_label_set_text(s_record.time_lbl, RECORD_TIME_DEFAULT_TEXT);
    lv_obj_set_style_text_font(s_record.time_lbl, &AlibabaPuHuiTi3_Regular40, 0);
    lv_obj_set_style_text_color(s_record.time_lbl, lv_color_white(), 0);
    lv_obj_align(s_record.time_lbl, LV_ALIGN_TOP_MID, 0, RECORD_TIME_TOP_PAD);

    s_record.record_btn = lv_btn_create(s_record.content);
    lv_obj_remove_style_all(s_record.record_btn);
    lv_obj_set_size(s_record.record_btn, RECORD_BTN_SIZE, RECORD_BTN_SIZE);
    lv_obj_align(s_record.record_btn, LV_ALIGN_BOTTOM_MID, 0, -RECORD_BTN_BOTTOM_PAD);
    lv_obj_set_style_bg_opa(s_record.record_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_record.record_btn, __record_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Static outer white ring (visible across all states) */
    s_record.record_ring = lv_obj_create(s_record.record_btn);
    lv_obj_remove_style_all(s_record.record_ring);
    lv_obj_set_size(s_record.record_ring, RECORD_BTN_SIZE, RECORD_BTN_SIZE);
    lv_obj_center(s_record.record_ring);
    lv_obj_set_style_radius(s_record.record_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_record.record_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_record.record_ring, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_record.record_ring, RECORD_RING_BORDER_W, 0);
    lv_obj_set_style_border_opa(s_record.record_ring, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_record.record_ring, LV_OBJ_FLAG_CLICKABLE);

    /* DEFAULT: 45×45 red filled circle inside the ring */
    s_record.record_inner_disc = lv_obj_create(s_record.record_btn);
    lv_obj_remove_style_all(s_record.record_inner_disc);
    lv_obj_set_size(s_record.record_inner_disc,
                    RECORD_INNER_DISC_SIZE, RECORD_INNER_DISC_SIZE);
    lv_obj_center(s_record.record_inner_disc);
    lv_obj_set_style_radius(s_record.record_inner_disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_record.record_inner_disc,
                              lv_color_hex(RECORD_RED_COLOR), 0);
    lv_obj_set_style_bg_opa(s_record.record_inner_disc, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_record.record_inner_disc, LV_OBJ_FLAG_CLICKABLE);

    /* RECORDING inner: red rounded square */
    s_record.record_inner_square = lv_obj_create(s_record.record_btn);
    lv_obj_remove_style_all(s_record.record_inner_square);
    lv_obj_set_size(s_record.record_inner_square,
                    RECORD_INNER_SQUARE_SIZE, RECORD_INNER_SQUARE_SIZE);
    lv_obj_center(s_record.record_inner_square);
    lv_obj_set_style_radius(s_record.record_inner_square,
                            RECORD_INNER_SQUARE_RADIUS, 0);
    lv_obj_set_style_bg_color(s_record.record_inner_square,
                              lv_color_hex(RECORD_RED_COLOR), 0);
    lv_obj_set_style_bg_opa(s_record.record_inner_square, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_record.record_inner_square, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_record.record_inner_square, LV_OBJ_FLAG_HIDDEN);

    /* PAUSED inner: red play triangle (LVGL built-in symbol — 0 flash cost) */
    s_record.record_inner_play = lv_label_create(s_record.record_btn);
    lv_obj_remove_style_all(s_record.record_inner_play);
    lv_label_set_text(s_record.record_inner_play, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(s_record.record_inner_play,
                               &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_record.record_inner_play,
                                lv_color_hex(RECORD_RED_COLOR), 0);
    lv_obj_center(s_record.record_inner_play);
    lv_obj_clear_flag(s_record.record_inner_play, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_record.record_inner_play, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Create the main record screen (lazy)
 * @return none
 */
VOID_T setup_scr_record(VOID_T)
{
    if (s_record.scr) {
        return;
    }

    memset(&s_record, 0, sizeof(s_record));
    s_record.state = RECORD_STATE_DEFAULT;

    s_record.scr = lv_obj_create(NULL);
    lv_obj_set_size(s_record.scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_record.scr, lv_color_hex(UI_BG_COLOR_DARK), 0);
    lv_obj_set_style_pad_all(s_record.scr, 0, 0);
    lv_obj_set_scrollbar_mode(s_record.scr, LV_SCROLLBAR_MODE_OFF);

    __record_build_title_bar();
    __record_build_content();

    ui_control_center_register_gesture(s_record.scr);

    lv_obj_update_layout(s_record.scr);
}

/**
 * @brief Show the main record screen (creates if needed)
 * @return none
 */
VOID_T ui_record_show(VOID_T)
{
    if (s_record.scr == NULL) {
        setup_scr_record();
    }

    if (lv_scr_act() != s_record.scr) {
        lv_scr_load(s_record.scr);
    }
}

/**
 * @brief Hide the main record screen and release per-show resources
 * @return none
 * @note Stops the tick timer to avoid background ticking after the user
 *       navigates away. PNG resources are static const and need no release.
 */
VOID_T ui_record_hide(VOID_T)
{
    if (s_record.tick_timer != NULL) {
        lv_timer_del(s_record.tick_timer);
        s_record.tick_timer = NULL;
    }
}

/**
 * @brief Get the main record screen object
 * @return record screen pointer, NULL if not created
 */
lv_obj_t *ui_record_get_scr(VOID_T)
{
    return s_record.scr;
}
