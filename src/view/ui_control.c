/**
 * @file ui_control.c
 * @brief Control center UI for T5AI_BOARD (320x480), triggered by swipe-down
 * @version 1.0
 * @date 2025-04-02
 * @copyright Copyright (c) Tuya Inc.
 */
#include <stdio.h>
#include "ui_common.h"
#include "tuya_ai_display.h"
#include "uni_log.h"
#include "wukong_ai_mode.h"
#include "tuya_app_gui_gw_core0.h"

extern OPERATE_RET tuya_ai_toy_volume_set(UINT8_T value);
extern UINT8_T tuya_ai_toy_volume_get(VOID);

/* ---------------------------------------------------------------------------
 * Font declarations
 * --------------------------------------------------------------------------- */
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular18_Static);

/* ---------------------------------------------------------------------------
 * Icon declarations
 * --------------------------------------------------------------------------- */
LV_IMG_DECLARE(icon_volume);
LV_IMG_DECLARE(icon_brightness);
LV_IMG_DECLARE(icon_clock_vol);
LV_IMG_DECLARE(icon_up);
LV_IMG_DECLARE(icon_photo_app);
LV_IMG_DECLARE(icon_camera_app);
LV_IMG_DECLARE(icon_arrow_yellow);

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define CTRL_BG_COLOR           0x25262A
#define CTRL_SLIDER_BG          0xB8BDDE
#define CTRL_SLIDER_FILL        0xFFF37B
#define CTRL_SLIDER_BG_OPA      28

#define CTRL_TITLE_BAR_W        320
#define CTRL_TITLE_BAR_H        50
#define CTRL_CONTENT_BAR_W      320
#define CTRL_CONTENT_BAR_Y      CTRL_TITLE_BAR_H

#define CTRL_SLIDER_W           288
#define CTRL_SLIDER_H           45
#define CTRL_SLIDER_RADIUS      (CTRL_SLIDER_H / 2)
#define CTRL_SLIDER_TOP_PAD     12
#define CTRL_SLIDER_GAP         12
#define CTRL_SLIDER_X           ((CTRL_CONTENT_BAR_W - CTRL_SLIDER_W) / 2)
#define CTRL_SLIDER_ICON_PAD    12
#define CTRL_ICON_SIZE          24

#define CTRL_MODE_ENTRY_W       CTRL_SLIDER_W
#define CTRL_MODE_ENTRY_H       64
#define CTRL_MODE_ENTRY_RADIUS  32
#define CTRL_MODE_ENTRY_BG      0x005CC4
#define CTRL_MODE_ENTRY_TOP_PAD 0
#define CTRL_MODE_ENTRY_LBL_PAD 20
#define CTRL_MODE_ENTRY_ARROW_PAD 16

#define CTRL_CARD_TOP_PAD       15
#define CTRL_CARD_H             75
#define CTRL_CARD_GAP           12
#define CTRL_CARD_BOTTOM_GAP    15
#define CTRL_CARD_RADIUS        16
#define CTRL_CARD_BG            0xB8BDDE
#define CTRL_CARD_BG_OPA        28
#define CTRL_CARD_PAD           12
#define CTRL_CARD_COLS          2

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    CONST lv_img_dsc_t *icon;
    CONST CHAR_T *label;
    TY_DISPLAY_ACTION_E action;
} CTRL_CARD_CFG_T;

typedef struct {
    lv_obj_t *ctrl_scr;
    lv_obj_t *prev_scr;
    lv_obj_t *title_bar;
    lv_obj_t *content_bar;
    lv_obj_t *volume_sli;
    lv_obj_t *brightness_sli;
    lv_obj_t *mode_entry_btn;
    lv_obj_t *mode_entry_lbl;
    lv_obj_t *up_icon;
    lv_obj_t *up_btn;
} CTRL_UI_T;

/* External getters (no public header in current project layout) */
extern AI_DEVICE_MODE_E tuya_ai_toy_device_mode_get(VOID);
extern AI_CHAT_SUB_MODE_E tuya_ai_toy_trigger_mode_get(VOID);

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC CTRL_UI_T s_ctrl_ui = {0};

STATIC CONST CTRL_CARD_CFG_T s_card_cfgs[] = {
    { &icon_photo_app,  "相册", TY_DISP_ACT_OPEN_ALBUM },
    { &icon_camera_app, "相机", TY_DISP_ACT_OPEN_CAMERA },
};

#define CTRL_CARD_COUNT (sizeof(s_card_cfgs) / sizeof(s_card_cfgs[0]))

STATIC CONST CHAR_T *s_chat_sub_mode_names[] = {
    "长按", "按键", "唤醒", "自由",
};

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __ctrl_slider_event_cb(lv_event_t *e);
STATIC VOID_T __ctrl_dismiss(VOID_T);
STATIC VOID_T __ctrl_dismiss_cb(lv_event_t *e);
STATIC VOID_T __ctrl_gesture_cb(lv_event_t *e);
STATIC VOID_T __ctrl_open_gesture_cb(lv_event_t *e);
STATIC VOID_T __ctrl_card_click_cb(lv_event_t *e);
STATIC VOID_T __ctrl_mode_entry_cb(lv_event_t *e);
STATIC CONST CHAR_T *__ctrl_get_mode_text(VOID_T);
STATIC VOID_T __ctrl_create_slider(lv_obj_t *parent, lv_obj_t **sli,
                                   lv_coord_t x, lv_coord_t y,
                                   CONST lv_img_dsc_t *icon, INT_T value);
STATIC lv_obj_t *__ctrl_create_card(lv_obj_t *parent,
                                    CONST CTRL_CARD_CFG_T *cfg,
                                    lv_coord_t x, lv_coord_t y,
                                    lv_coord_t w, lv_coord_t h);
STATIC VOID_T __ctrl_create_mode_entry(lv_obj_t *parent, lv_coord_t x,
                                       lv_coord_t y);

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Create a horizontal slider with rounded-pill style and embedded left icon
 * @param[in] parent parent object
 * @param[out] sli pointer to store created slider handle
 * @param[in] x horizontal position relative to parent
 * @param[in] y vertical position relative to parent
 * @param[in] icon icon image descriptor placed inside slider on left
 * @param[in] value initial slider value (0-100)
 * @return none
 */
STATIC VOID_T __ctrl_create_slider(lv_obj_t *parent, lv_obj_t **sli,
                                   lv_coord_t x, lv_coord_t y,
                                   CONST lv_img_dsc_t *icon, INT_T value)
{
    *sli = lv_slider_create(parent);
    lv_slider_set_range(*sli, 0, 100);
    lv_slider_set_value(*sli, value, LV_ANIM_OFF);
    lv_obj_set_pos(*sli, x, y);
    lv_obj_set_size(*sli, CTRL_SLIDER_W, CTRL_SLIDER_H);

    lv_obj_set_style_radius(*sli, CTRL_SLIDER_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(*sli, CTRL_SLIDER_BG_OPA, LV_PART_MAIN);
    lv_obj_set_style_bg_color(*sli, lv_color_hex(CTRL_SLIDER_BG), LV_PART_MAIN);

    lv_obj_set_style_radius(*sli, CTRL_SLIDER_RADIUS, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(*sli, lv_color_hex(CTRL_SLIDER_FILL), LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(*sli, LV_OPA_TRANSP, LV_PART_KNOB);

    lv_obj_clear_flag(*sli, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(*sli, __ctrl_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    if (icon != NULL) {
        lv_obj_t *img = lv_img_create(*sli);
        lv_img_set_src(img, icon);
        lv_obj_set_pos(img, CTRL_SLIDER_ICON_PAD,
                       (CTRL_SLIDER_H - CTRL_ICON_SIZE) / 2);
        lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    }
}

/**
 * @brief Create a shortcut card with icon, label and arrow
 * @param[in] parent parent screen object
 * @param[in] cfg card configuration (icon + label)
 * @param[in] x horizontal position
 * @param[in] y vertical position
 * @param[in] w card width
 * @param[in] h card height
 * @return created card object
 */
STATIC lv_obj_t *__ctrl_create_card(lv_obj_t *parent,
                                    CONST CTRL_CARD_CFG_T *cfg,
                                    lv_coord_t x, lv_coord_t y,
                                    lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, CTRL_CARD_RADIUS, 0);
    lv_obj_set_style_bg_opa(card, CTRL_CARD_BG_OPA, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(CTRL_CARD_BG), 0);
    lv_obj_set_style_pad_all(card, 0, 0);

    lv_obj_t *icon = lv_img_create(card);
    lv_img_set_src(icon, cfg->icon);
    lv_obj_set_pos(icon, CTRL_CARD_PAD, CTRL_CARD_PAD);

    lv_obj_t *arrow = lv_img_create(card);
    lv_img_set_src(arrow, &icon_arrow_yellow);
    lv_obj_set_pos(arrow, w - CTRL_CARD_PAD - CTRL_ICON_SIZE, CTRL_CARD_PAD);

    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, cfg->label);
    lv_obj_set_pos(label, CTRL_CARD_PAD, h - 18 - CTRL_CARD_PAD);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &AlibabaPuHuiTi3_Regular18_Static, 0);

    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, __ctrl_card_click_cb, LV_EVENT_CLICKED, (VOID_T *)cfg);

    return card;
}

/**
 * @brief Build display text for the current device / chat sub-mode
 * @return pointer to a static buffer holding the text
 * @note For CHAT mode the text is "闲聊模式: <sub-mode>"; otherwise the
 *       device mode name is returned.
 */
STATIC CONST CHAR_T *__ctrl_get_mode_text(VOID_T)
{
    STATIC CHAR_T s_buf[64];
    AI_DEVICE_MODE_E cur = tuya_ai_toy_device_mode_get();

    switch (cur) {
    case AI_DEVICE_MODE_CHAT: {
        AI_CHAT_SUB_MODE_E sub = tuya_ai_toy_trigger_mode_get();
        CONST CHAR_T *sub_name = (sub >= 0 && sub < AI_CHAT_SUB_MAX)
                                 ? s_chat_sub_mode_names[sub] : "";
        snprintf(s_buf, sizeof(s_buf), "闲聊模式: %s", sub_name);
        return s_buf;
    }
    case AI_DEVICE_MODE_TRANSLATE:
        return "翻译模式";
    case AI_DEVICE_MODE_P2P:
        return "P2P模式";
    case AI_DEVICE_MODE_RECORD:
        return "录音模式";
    case AI_DEVICE_MODE_PICTURE:
        return "生图模式";
    case AI_DEVICE_MODE_DETECTION:
        return "侦测模式";
    default:
        return "设备模式";
    }
}

/**
 * @brief Create the device-mode entry button (label + arrow on right)
 * @param[in] parent parent object (content_bar)
 * @param[in] x horizontal position relative to parent
 * @param[in] y vertical position relative to parent
 * @return none
 */
STATIC VOID_T __ctrl_create_mode_entry(lv_obj_t *parent, lv_coord_t x,
                                       lv_coord_t y)
{
    s_ctrl_ui.mode_entry_btn = lv_btn_create(parent);
    lv_obj_remove_style_all(s_ctrl_ui.mode_entry_btn);
    lv_obj_set_size(s_ctrl_ui.mode_entry_btn,
                    CTRL_MODE_ENTRY_W, CTRL_MODE_ENTRY_H);
    lv_obj_set_pos(s_ctrl_ui.mode_entry_btn, x, y);
    lv_obj_set_style_radius(s_ctrl_ui.mode_entry_btn,
                            CTRL_MODE_ENTRY_RADIUS, 0);
    lv_obj_set_style_bg_color(s_ctrl_ui.mode_entry_btn,
                              lv_color_hex(CTRL_MODE_ENTRY_BG), 0);
    lv_obj_set_style_bg_opa(s_ctrl_ui.mode_entry_btn, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_ctrl_ui.mode_entry_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ctrl_ui.mode_entry_btn, __ctrl_mode_entry_cb,
                        LV_EVENT_CLICKED, NULL);

    s_ctrl_ui.mode_entry_lbl = lv_label_create(s_ctrl_ui.mode_entry_btn);
    lv_label_set_text(s_ctrl_ui.mode_entry_lbl, __ctrl_get_mode_text());
    lv_obj_set_style_text_color(s_ctrl_ui.mode_entry_lbl,
                                lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_ctrl_ui.mode_entry_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_align(s_ctrl_ui.mode_entry_lbl, LV_ALIGN_LEFT_MID,
                 CTRL_MODE_ENTRY_LBL_PAD, 0);

    lv_obj_t *arrow = lv_img_create(s_ctrl_ui.mode_entry_btn);
    lv_img_set_src(arrow, &icon_arrow_yellow);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -CTRL_MODE_ENTRY_ARROW_PAD, 0);
    lv_obj_clear_flag(arrow, LV_OBJ_FLAG_CLICKABLE);
}

/**
 * @brief Slider value changed callback (placeholder for actual control logic)
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __ctrl_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *sli = lv_event_get_target(e);
    INT_T val = lv_slider_get_value(sli);

    if (sli == s_ctrl_ui.volume_sli) {
        TAL_PR_DEBUG("volume: %d", val);
        wukong_audio_player_set_vol((UINT8_T)val);
    } else if (sli == s_ctrl_ui.brightness_sli) {
        TAL_PR_DEBUG("brightness: %d", val);
        tuya_disp_lcd_backlight_set((UINT8_T)val);
    }
}

/**
 * @brief Card click callback, dismisses control center and posts action
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __ctrl_card_click_cb(lv_event_t *e)
{
    CONST CTRL_CARD_CFG_T *cfg = (CONST CTRL_CARD_CFG_T *)lv_event_get_user_data(e);
    if (cfg == NULL) {
        return;
    }

    TAL_PR_DEBUG("card clicked: %s, action: %d", cfg->label, cfg->action);
    __ctrl_dismiss();
    tuya_ai_display_action_post(NULL, 0, cfg->action);
}

/**
 * @brief Mode entry click callback, dismisses control center and opens
 *        the device mode selection page.
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __ctrl_mode_entry_cb(lv_event_t *e)
{
    (VOID_T)e;
    TAL_PR_DEBUG("mode entry clicked");
    __ctrl_dismiss();
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_OPEN_DEVICE_MODE);
}

/**
 * @brief Switch back to previous screen and delete control center
 * @return none
 */
STATIC VOID_T __ctrl_dismiss(VOID_T)
{
    if (s_ctrl_ui.ctrl_scr == NULL) {
        return;
    }

    if (s_ctrl_ui.prev_scr) {
        lv_scr_load(s_ctrl_ui.prev_scr);
    }

    lv_obj_del(s_ctrl_ui.ctrl_scr);
    s_ctrl_ui.ctrl_scr = NULL;
    s_ctrl_ui.prev_scr = NULL;
}

/**
 * @brief Dismiss button click callback
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __ctrl_dismiss_cb(lv_event_t *e)
{
    (VOID_T)e;
    __ctrl_dismiss();
}

/**
 * @brief Control center gesture callback, swipe-up to dismiss
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __ctrl_gesture_cb(lv_event_t *e)
{
    (VOID_T)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_TOP) {
        __ctrl_dismiss();
    }
}

/**
 * @brief Build and show the control center overlay screen
 * @param[in] volume current volume (0-100)
 * @param[in] brightness current brightness (0-100)
 * @param[in] alarm_vol current alarm volume (0-100)
 * @return none
 */
VOID_T setup_scr_control(UINT8_T volume, UINT8_T brightness, UINT8_T alarm_vol)
{
    (VOID_T)alarm_vol;
    volume = tuya_ai_toy_volume_get();
    s_ctrl_ui.prev_scr = lv_scr_act();

    s_ctrl_ui.ctrl_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_ctrl_ui.ctrl_scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_scrollbar_mode(s_ctrl_ui.ctrl_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_ctrl_ui.ctrl_scr, lv_color_hex(CTRL_BG_COLOR), 0);
    lv_obj_set_style_pad_all(s_ctrl_ui.ctrl_scr, 0, 0);

    /* ---- Transparent title bar with centered title ---- */
    s_ctrl_ui.title_bar = lv_obj_create(s_ctrl_ui.ctrl_scr);
    lv_obj_set_pos(s_ctrl_ui.title_bar, 0, 0);
    lv_obj_set_size(s_ctrl_ui.title_bar, CTRL_TITLE_BAR_W, CTRL_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(s_ctrl_ui.title_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ctrl_ui.title_bar, 0, 0);
    lv_obj_set_style_pad_all(s_ctrl_ui.title_bar, 0, 0);
    lv_obj_clear_flag(s_ctrl_ui.title_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_label = lv_label_create(s_ctrl_ui.title_bar);
    lv_label_set_text(title_label, "个人中心");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_center(title_label);

    /* ---- Transparent content bar (height fits children) ---- */
    s_ctrl_ui.content_bar = lv_obj_create(s_ctrl_ui.ctrl_scr);
    lv_obj_set_pos(s_ctrl_ui.content_bar, 0, CTRL_CONTENT_BAR_Y);
    lv_obj_set_width(s_ctrl_ui.content_bar, CTRL_CONTENT_BAR_W);
    lv_obj_set_height(s_ctrl_ui.content_bar, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_ctrl_ui.content_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ctrl_ui.content_bar, 0, 0);
    lv_obj_set_style_pad_all(s_ctrl_ui.content_bar, 0, 0);
    lv_obj_set_scrollbar_mode(s_ctrl_ui.content_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_ctrl_ui.content_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* ---- Device mode entry button aligned to content top ---- */
    lv_coord_t entry_y = CTRL_MODE_ENTRY_TOP_PAD;
    __ctrl_create_mode_entry(s_ctrl_ui.content_bar, CTRL_SLIDER_X, entry_y);

    /* ---- Shortcut cards below mode entry button ---- */
    lv_coord_t card_y = entry_y + CTRL_MODE_ENTRY_H + CTRL_CARD_TOP_PAD;
    lv_coord_t card_w = (CTRL_SLIDER_W - CTRL_CARD_GAP * (CTRL_CARD_COLS - 1)) / CTRL_CARD_COLS;
    UINT32_T i;

    for (i = 0; i < CTRL_CARD_COUNT; i++) {
        UINT32_T col = i % CTRL_CARD_COLS;
        UINT32_T row = i / CTRL_CARD_COLS;
        lv_coord_t cx = CTRL_SLIDER_X + col * (card_w + CTRL_CARD_GAP);
        lv_coord_t cy = card_y + row * (CTRL_CARD_H + CTRL_CARD_GAP);
        __ctrl_create_card(s_ctrl_ui.content_bar, &s_card_cfgs[i],
                           cx, cy, card_w, CTRL_CARD_H);
    }

    /* ---- Two horizontal sliders placed below shortcut cards ---- */
    lv_coord_t sli_y0 = card_y + CTRL_CARD_H + CTRL_CARD_BOTTOM_GAP;
    lv_coord_t sli_y1 = sli_y0 + CTRL_SLIDER_H + CTRL_SLIDER_GAP;

    __ctrl_create_slider(s_ctrl_ui.content_bar, &s_ctrl_ui.volume_sli,
                         CTRL_SLIDER_X, sli_y0, &icon_volume, volume);
    __ctrl_create_slider(s_ctrl_ui.content_bar, &s_ctrl_ui.brightness_sli,
                         CTRL_SLIDER_X, sli_y1, &icon_brightness, brightness);

    /* ---- Bottom dismiss arrow (swipe-up hint) ---- */
    s_ctrl_ui.up_icon = lv_img_create(s_ctrl_ui.ctrl_scr);
    lv_img_set_src(s_ctrl_ui.up_icon, &icon_up);
    lv_obj_align(s_ctrl_ui.up_icon, LV_ALIGN_BOTTOM_MID, 0, -30);

    s_ctrl_ui.up_btn = lv_btn_create(s_ctrl_ui.ctrl_scr);
    lv_obj_remove_style_all(s_ctrl_ui.up_btn);
    lv_obj_set_size(s_ctrl_ui.up_btn, 100, 40);
    lv_obj_align(s_ctrl_ui.up_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(s_ctrl_ui.up_btn, __ctrl_dismiss_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_add_event_cb(s_ctrl_ui.ctrl_scr, __ctrl_gesture_cb, LV_EVENT_GESTURE, NULL);

    lv_obj_update_layout(s_ctrl_ui.ctrl_scr);
    lv_scr_load(s_ctrl_ui.ctrl_scr);
    lv_indev_wait_release(lv_indev_get_act());
}

/**
 * @brief Check whether control center is currently visible
 * @return TRUE if visible, FALSE otherwise
 */
BOOL_T ui_control_is_active(VOID_T)
{
    return (s_ctrl_ui.ctrl_scr != NULL) ? TRUE : FALSE;
}

/**
 * @brief Gesture callback that opens control center on swipe-down
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __ctrl_open_gesture_cb(lv_event_t *e)
{
    (VOID_T)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_BOTTOM && !ui_control_is_active()) {
        setup_scr_control(50, 50, 50);
    }
}

/**
 * @brief Register swipe-down gesture on any screen to open control center
 * @param[in] scr screen object to register gesture on
 * @return none
 * @note Call this in every screen's setup function after creating the screen
 */
VOID_T ui_control_register_gesture(lv_obj_t *scr)
{
    if (scr) {
        lv_obj_add_event_cb(scr, __ctrl_open_gesture_cb, LV_EVENT_GESTURE, NULL);
    }
}
