/**
 * @file ui_device_mode.c
 * @brief Device mode selection screen for T5AI_BOARD (320x480)
 *
 * Lists 4 selectable device modes (chat / translate / picture / detection)
 * in a 2x2 grid. Tapping a mode invokes wukong_ai_device_mode_switch() and
 * navigates to the chat screen so the success notification (posted by the
 * mode layer) can be rendered as an AI bubble.
 *
 * @version 1.0
 * @date 2026-04-28
 * @copyright Copyright (c) Tuya Inc.
 */
#include <stdio.h>
#include <string.h>
#include "ui_common.h"
#include "wukong_ai_mode.h"

/* ---------------------------------------------------------------------------
 * Font / icon declarations
 * --------------------------------------------------------------------------- */
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular24);

/* External getters (no public header in current project layout) */
extern AI_DEVICE_MODE_E tuya_ai_toy_device_mode_get(VOID);

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define DEVICE_MODE_TITLE_BAR_W         320
#define DEVICE_MODE_BACK_BTN_SIZE       36
#define DEVICE_MODE_BACK_BTN_PAD        12

#define DEVICE_MODE_CONTENT_Y           UI_TITLE_BAR_H
#define DEVICE_MODE_BTN_W               144
#define DEVICE_MODE_BTN_H               80
#define DEVICE_MODE_BTN_RADIUS          32
#define DEVICE_MODE_BTN_COLS            2
#define DEVICE_MODE_BTN_X0              10
#define DEVICE_MODE_BTN_Y0              12
#define DEVICE_MODE_BTN_COL_GAP         12
#define DEVICE_MODE_BTN_ROW_GAP         12

#define DEVICE_MODE_BTN_BG_SELECTED     0x005CC4
#define DEVICE_MODE_BTN_BG_NORMAL       0xB8BDDE

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    lv_obj_t *scr;
    lv_obj_t *title_bar;
    lv_obj_t *content;
    lv_obj_t *back_btn;
    lv_obj_t *title_lbl;
} DEVICE_MODE_UI_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC DEVICE_MODE_UI_T s_device_mode = {0};

STATIC CONST AI_DEVICE_MODE_E s_modes[] = {
    AI_DEVICE_MODE_CHAT,
    AI_DEVICE_MODE_TRANSLATE,
    AI_DEVICE_MODE_PICTURE,
    AI_DEVICE_MODE_DETECTION,
};

STATIC CONST CHAR_T *s_mode_names[] = {
    "闲聊模式",
    "翻译模式",
    "生图模式",
    "侦测模式",
};

#define DEVICE_MODE_MODE_COUNT (sizeof(s_modes) / sizeof(s_modes[0]))

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __device_mode_back_cb(lv_event_t *e);
STATIC VOID_T __device_mode_btn_cb(lv_event_t *e);
STATIC VOID_T __device_mode_build_buttons(VOID_T);

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Back button callback, posts CLOSE_DEVICE_MODE
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __device_mode_back_cb(lv_event_t *e)
{
    (VOID_T)e;
    PR_DEBUG("device mode: back");
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_CLOSE_DEVICE_MODE);
}

/**
 * @brief Mode button callback, switches device mode and jumps to chat screen
 * @param[in] e LVGL event, user_data points to AI_DEVICE_MODE_E
 * @return none
 * @note wukong_ai_device_mode_switch() posts the "已成功切换到XX模式" notification
 *       internally; ui_nav_replace(UI_SCR_CHAT) replaces the device mode page
 *       on the nav stack so swipe-back from chat returns to home, and the
 *       chat screen flushes the pending notification on show.
 */
STATIC VOID_T __device_mode_btn_cb(lv_event_t *e)
{
    CONST AI_DEVICE_MODE_E *mode_ptr = (CONST AI_DEVICE_MODE_E *)lv_event_get_user_data(e);
    if (mode_ptr == NULL) {
        return;
    }

    PR_DEBUG("device mode selected: %d", *mode_ptr);
    wukong_ai_device_mode_switch(*mode_ptr);

    ui_nav_replace(UI_SCR_CHAT);
}

/**
 * @brief Build 4 mode buttons (2x2 grid) in the content area
 * @return none
 */
STATIC VOID_T __device_mode_build_buttons(VOID_T)
{
    AI_DEVICE_MODE_E cur_mode = tuya_ai_toy_device_mode_get();
    UINT32_T i;

    for (i = 0; i < DEVICE_MODE_MODE_COUNT; i++) {
        UINT32_T col = i % DEVICE_MODE_BTN_COLS;
        UINT32_T row = i / DEVICE_MODE_BTN_COLS;
        lv_coord_t bx = DEVICE_MODE_BTN_X0 + col * (DEVICE_MODE_BTN_W + DEVICE_MODE_BTN_COL_GAP);
        lv_coord_t by = DEVICE_MODE_BTN_Y0 + row * (DEVICE_MODE_BTN_H + DEVICE_MODE_BTN_ROW_GAP);

        lv_obj_t *btn = lv_btn_create(s_device_mode.content);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, DEVICE_MODE_BTN_W, DEVICE_MODE_BTN_H);
        lv_obj_set_pos(btn, bx, by);
        lv_obj_set_style_radius(btn, DEVICE_MODE_BTN_RADIUS, 0);
        lv_obj_set_style_clip_corner(btn, true, 0);
        lv_obj_add_event_cb(btn, __device_mode_btn_cb, LV_EVENT_CLICKED,
                            (VOID_T *)&s_modes[i]);

        if (s_modes[i] == cur_mode) {
            lv_obj_set_style_bg_color(btn, lv_color_hex(DEVICE_MODE_BTN_BG_SELECTED), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_color(btn, lv_color_hex(DEVICE_MODE_BTN_BG_NORMAL), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_10, 0);
        }

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, s_mode_names[i]);
        lv_obj_center(label);
        lv_obj_set_style_text_font(label, &AlibabaPuHuiTi3_Regular24, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
    }
}

/**
 * @brief Create device mode selection screen (lazy)
 * @return none
 */
VOID_T setup_scr_device_mode(VOID_T)
{
    if (s_device_mode.scr) {
        return;
    }

    memset(&s_device_mode, 0, sizeof(s_device_mode));

    /* ---- Full-screen base ---- */
    s_device_mode.scr = lv_obj_create(NULL);
    lv_obj_set_size(s_device_mode.scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_device_mode.scr, lv_color_hex(UI_BG_COLOR_DARK), 0);
    lv_obj_set_style_pad_all(s_device_mode.scr, 0, 0);
    lv_obj_set_scrollbar_mode(s_device_mode.scr, LV_SCROLLBAR_MODE_OFF);

    /* ---- Title bar ---- */
    s_device_mode.title_bar = lv_obj_create(s_device_mode.scr);
    lv_obj_remove_style_all(s_device_mode.title_bar);
    lv_obj_set_pos(s_device_mode.title_bar, 0, 0);
    lv_obj_set_size(s_device_mode.title_bar, DEVICE_MODE_TITLE_BAR_W, UI_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(s_device_mode.title_bar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_device_mode.title_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_device_mode.back_btn = lv_btn_create(s_device_mode.title_bar);
    lv_obj_remove_style_all(s_device_mode.back_btn);
    lv_obj_set_size(s_device_mode.back_btn, DEVICE_MODE_BACK_BTN_SIZE, DEVICE_MODE_BACK_BTN_SIZE);
    lv_obj_set_pos(s_device_mode.back_btn, DEVICE_MODE_BACK_BTN_PAD,
                   (UI_TITLE_BAR_H - DEVICE_MODE_BACK_BTN_SIZE) / 2);
    lv_obj_set_style_bg_opa(s_device_mode.back_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_device_mode.back_btn, __device_mode_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_icon = lv_img_create(s_device_mode.back_btn);
    lv_img_set_src(back_icon, &icon_back_24_24);
    lv_obj_center(back_icon);

    s_device_mode.title_lbl = lv_label_create(s_device_mode.title_bar);
    lv_label_set_text(s_device_mode.title_lbl, "设备模式");
    lv_obj_set_style_text_font(s_device_mode.title_lbl, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_device_mode.title_lbl, lv_color_white(), 0);
    lv_obj_center(s_device_mode.title_lbl);

    /* ---- Content area ---- */
    s_device_mode.content = lv_obj_create(s_device_mode.scr);
    lv_obj_remove_style_all(s_device_mode.content);
    lv_obj_set_pos(s_device_mode.content, 0, DEVICE_MODE_CONTENT_Y);
    lv_obj_set_size(s_device_mode.content, LV_HOR_RES, LV_VER_RES - DEVICE_MODE_CONTENT_Y);
    lv_obj_set_style_bg_opa(s_device_mode.content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_device_mode.content, 0, 0);
    lv_obj_set_scrollbar_mode(s_device_mode.content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_device_mode.content, LV_OBJ_FLAG_SCROLLABLE);

    __device_mode_build_buttons();

    ui_control_center_register_gesture(s_device_mode.scr);

    lv_obj_update_layout(s_device_mode.scr);
}

/**
 * @brief Show device mode selection screen (creates if needed)
 * @return none
 */
VOID_T ui_device_mode_show(VOID_T)
{
    if (s_device_mode.scr == NULL) {
        setup_scr_device_mode();
    } else {
        /* Refresh selection state to reflect current device mode */
        if (s_device_mode.content) {
            lv_obj_clean(s_device_mode.content);
            __device_mode_build_buttons();
        }
    }

    if (lv_scr_act() != s_device_mode.scr) {
        lv_scr_load(s_device_mode.scr);
    }
}

/**
 * @brief Hide the device mode selection screen
 * @return none
 */
VOID_T ui_device_mode_hide(VOID_T)
{
    /* No per-show heavy resources to release; keep screen for fast re-open */
}

/**
 * @brief Get the device mode screen object
 * @return device mode screen pointer, NULL if not created
 */
lv_obj_t *ui_device_mode_get_scr(VOID_T)
{
    return s_device_mode.scr;
}
