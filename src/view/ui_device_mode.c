/**
 * @file ui_device_mode.c
 * @brief Device mode selection screen for T5AI_BOARD (320x480)
 *
 * Lists 4 selectable device modes (chat / translate / picture / detection)
 * in a 2x2 grid. Tapping a mode invokes wukong_ai_device_mode_switch() and
 * navigates back to the previous screen via the action dispatcher.
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
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular18_Static);
LV_IMG_DECLARE(icon_back_24_24);
LV_IMG_DECLARE(icon_ChatMode);
LV_IMG_DECLARE(icon_TranslationMode);
LV_IMG_DECLARE(icon_PictureMode);
LV_IMG_DECLARE(icon_DetectionMode);

/* External getters (no public header in current project layout) */
extern AI_DEVICE_MODE_E tuya_ai_toy_device_mode_get(VOID);

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define DEV_BG_COLOR            0x25262A
#define DEV_TITLE_BAR_W         320
#define DEV_TITLE_BAR_H         50
#define DEV_BACK_BTN_SIZE       36
#define DEV_BACK_BTN_PAD        12

#define DEV_CONTENT_Y           DEV_TITLE_BAR_H
#define DEV_BTN_W               144
#define DEV_BTN_H               80
#define DEV_BTN_RADIUS          32
#define DEV_BTN_COLS            2
#define DEV_BTN_X0              10
#define DEV_BTN_Y0              12
#define DEV_BTN_COL_GAP         12
#define DEV_BTN_ROW_GAP         12
#define DEV_BTN_LABEL_X         12
#define DEV_BTN_LABEL_Y         48

#define DEV_BTN_BG_SELECTED     0x005CC4
#define DEV_BTN_BG_NORMAL       0xB8BDDE

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
STATIC DEVICE_MODE_UI_T s_dev = {0};

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

STATIC CONST lv_img_dsc_t *s_mode_icons[] = {
    &icon_ChatMode,
    &icon_TranslationMode,
    &icon_PictureMode,
    &icon_DetectionMode,
};

#define DEV_MODE_COUNT (sizeof(s_modes) / sizeof(s_modes[0]))

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __dev_back_cb(lv_event_t *e);
STATIC VOID_T __dev_btn_cb(lv_event_t *e);
STATIC VOID_T __dev_build_buttons(VOID_T);

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Back button callback, posts CLOSE_DEVICE_MODE
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __dev_back_cb(lv_event_t *e)
{
    (VOID_T)e;
    PR_DEBUG("device mode: back");
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_CLOSE_DEVICE_MODE);
}

/**
 * @brief Mode button callback, switches device mode and dismisses page
 * @param[in] e LVGL event, user_data points to AI_DEVICE_MODE_E
 * @return none
 */
STATIC VOID_T __dev_btn_cb(lv_event_t *e)
{
    CONST AI_DEVICE_MODE_E *mode_ptr = (CONST AI_DEVICE_MODE_E *)lv_event_get_user_data(e);
    if (mode_ptr == NULL) {
        return;
    }

    PR_DEBUG("device mode selected: %d", *mode_ptr);
    wukong_ai_device_mode_switch(*mode_ptr);

    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_CLOSE_DEVICE_MODE);
}

/**
 * @brief Build 4 mode buttons (2x2 grid) in the content area
 * @return none
 */
STATIC VOID_T __dev_build_buttons(VOID_T)
{
    AI_DEVICE_MODE_E cur_mode = tuya_ai_toy_device_mode_get();
    UINT32_T i;

    for (i = 0; i < DEV_MODE_COUNT; i++) {
        UINT32_T col = i % DEV_BTN_COLS;
        UINT32_T row = i / DEV_BTN_COLS;
        lv_coord_t bx = DEV_BTN_X0 + col * (DEV_BTN_W + DEV_BTN_COL_GAP);
        lv_coord_t by = DEV_BTN_Y0 + row * (DEV_BTN_H + DEV_BTN_ROW_GAP);

        lv_obj_t *btn = lv_btn_create(s_dev.content);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, DEV_BTN_W, DEV_BTN_H);
        lv_obj_set_pos(btn, bx, by);
        lv_obj_set_style_radius(btn, DEV_BTN_RADIUS, 0);
        lv_obj_set_style_clip_corner(btn, true, 0);
        lv_obj_add_event_cb(btn, __dev_btn_cb, LV_EVENT_CLICKED,
                            (VOID_T *)&s_modes[i]);

        if (s_modes[i] == cur_mode) {
            lv_obj_set_style_bg_color(btn, lv_color_hex(DEV_BTN_BG_SELECTED), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_bg_color(btn, lv_color_hex(DEV_BTN_BG_NORMAL), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_10, 0);
        }

        if (s_mode_icons[i] != NULL) {
            lv_obj_t *icon = lv_img_create(btn);
            lv_img_set_src(icon, s_mode_icons[i]);
            lv_obj_set_pos(icon, 0, 0);
            lv_obj_set_size(icon, DEV_BTN_W, DEV_BTN_H);
            lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
        }

        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, s_mode_names[i]);
        lv_obj_set_pos(label, DEV_BTN_LABEL_X, DEV_BTN_LABEL_Y);
        lv_obj_set_style_text_font(label, &AlibabaPuHuiTi3_Regular18_Static, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
    }
}

/**
 * @brief Create device mode selection screen (lazy)
 * @return none
 */
VOID_T setup_scr_device_mode(VOID_T)
{
    if (s_dev.scr) {
        return;
    }

    memset(&s_dev, 0, sizeof(s_dev));

    /* ---- Full-screen base ---- */
    s_dev.scr = lv_obj_create(NULL);
    lv_obj_set_size(s_dev.scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_dev.scr, lv_color_hex(DEV_BG_COLOR), 0);
    lv_obj_set_style_pad_all(s_dev.scr, 0, 0);
    lv_obj_set_scrollbar_mode(s_dev.scr, LV_SCROLLBAR_MODE_OFF);

    /* ---- Title bar ---- */
    s_dev.title_bar = lv_obj_create(s_dev.scr);
    lv_obj_remove_style_all(s_dev.title_bar);
    lv_obj_set_pos(s_dev.title_bar, 0, 0);
    lv_obj_set_size(s_dev.title_bar, DEV_TITLE_BAR_W, DEV_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(s_dev.title_bar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_dev.title_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_dev.back_btn = lv_btn_create(s_dev.title_bar);
    lv_obj_remove_style_all(s_dev.back_btn);
    lv_obj_set_size(s_dev.back_btn, DEV_BACK_BTN_SIZE, DEV_BACK_BTN_SIZE);
    lv_obj_set_pos(s_dev.back_btn, DEV_BACK_BTN_PAD,
                   (DEV_TITLE_BAR_H - DEV_BACK_BTN_SIZE) / 2);
    lv_obj_set_style_bg_opa(s_dev.back_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_dev.back_btn, __dev_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_icon = lv_img_create(s_dev.back_btn);
    lv_img_set_src(back_icon, &icon_back_24_24);
    lv_obj_center(back_icon);

    s_dev.title_lbl = lv_label_create(s_dev.title_bar);
    lv_label_set_text(s_dev.title_lbl, "设备模式");
    lv_obj_set_style_text_font(s_dev.title_lbl, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_dev.title_lbl, lv_color_white(), 0);
    lv_obj_center(s_dev.title_lbl);

    /* ---- Content area ---- */
    s_dev.content = lv_obj_create(s_dev.scr);
    lv_obj_remove_style_all(s_dev.content);
    lv_obj_set_pos(s_dev.content, 0, DEV_CONTENT_Y);
    lv_obj_set_size(s_dev.content, LV_HOR_RES, LV_VER_RES - DEV_CONTENT_Y);
    lv_obj_set_style_bg_opa(s_dev.content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_dev.content, 0, 0);
    lv_obj_set_scrollbar_mode(s_dev.content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_dev.content, LV_OBJ_FLAG_SCROLLABLE);

    __dev_build_buttons();

    ui_control_register_gesture(s_dev.scr);

    lv_obj_update_layout(s_dev.scr);
}

/**
 * @brief Show device mode selection screen (creates if needed)
 * @return none
 */
VOID_T ui_device_mode_show(VOID_T)
{
    if (s_dev.scr == NULL) {
        setup_scr_device_mode();
    } else {
        /* Refresh selection state to reflect current device mode */
        if (s_dev.content) {
            lv_obj_clean(s_dev.content);
            __dev_build_buttons();
        }
    }

    if (lv_scr_act() != s_dev.scr) {
        lv_scr_load(s_dev.scr);
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
    return s_dev.scr;
}
