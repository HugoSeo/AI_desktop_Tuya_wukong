/**
 * @file ui_control_center.c
 * @brief Control center UI for T5AI_BOARD (320x480), triggered by swipe-down
 * @version 1.0
 * @date 2025-04-02
 * @copyright Copyright (c) Tuya Inc.
 */
#include <stdio.h>
#include <string.h>
#include "ui_common.h"
#include "ui_dispatch.h"
#include "tuya_ai_display.h"
#include "uni_log.h"
#include "wukong_ai_mode.h"
#include "tuya_app_gui_gw_core0.h"

extern OPERATE_RET tuya_ai_toy_volume_set(UINT8_T value);
extern UINT8_T tuya_ai_toy_volume_get(VOID);

/* ---------------------------------------------------------------------------
 * Font declarations
 * --------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
 * Icon declarations
 * --------------------------------------------------------------------------- */
LV_IMG_DECLARE(icon_volume);
LV_IMG_DECLARE(icon_brightness);
LV_IMG_DECLARE(icon_up);
LV_IMG_DECLARE(icon_photo_app);
LV_IMG_DECLARE(icon_camera_app);
LV_IMG_DECLARE(icon_record_app);
LV_IMG_DECLARE(icon_music_app);
LV_IMG_DECLARE(icon_call_app);
LV_IMG_DECLARE(icon_detection_app);
LV_IMG_DECLARE(icon_settings_app);
LV_IMG_DECLARE(icon_arrow_yellow);

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define CONTROL_SLIDER_BG          0xB8BDDE
#define CONTROL_SLIDER_FILL        0xFFF37B
#define CONTROL_SLIDER_BG_OPA      28

#define CONTROL_TITLE_BAR_W        320
#define CONTROL_CONTENT_BAR_W      320
#define CONTROL_CONTENT_BAR_Y      UI_TITLE_BAR_H

#define CONTROL_SLIDER_W           288
#define CONTROL_SLIDER_H           45
#define CONTROL_SLIDER_RADIUS      (CONTROL_SLIDER_H / 2)
#define CONTROL_SLIDER_TOP_PAD     12
#define CONTROL_SLIDER_GAP         12
#define CONTROL_SLIDER_X           ((CONTROL_CONTENT_BAR_W - CONTROL_SLIDER_W) / 2)
#define CONTROL_SLIDER_ICON_PAD    12
#define CONTROL_ICON_SIZE          24

#define CONTROL_MODE_ENTRY_W       CONTROL_SLIDER_W
#define CONTROL_MODE_ENTRY_H       64
#define CONTROL_MODE_ENTRY_RADIUS  32
#define CONTROL_MODE_ENTRY_BG      0x005CC4
#define CONTROL_MODE_ENTRY_TOP_PAD 0
#define CONTROL_MODE_ENTRY_LBL_PAD 20
#define CONTROL_MODE_ENTRY_ARROW_PAD 16

#define CONTROL_CARD_TOP_PAD       15
#define CONTROL_CARD_H             75
#define CONTROL_CARD_GAP           12
#define CONTROL_CARD_BOTTOM_GAP    15
#define CONTROL_CARD_RADIUS        16
#define CONTROL_CARD_BG            0xB8BDDE
#define CONTROL_CARD_BG_OPA        28
#define CONTROL_CARD_PAD           12
#define CONTROL_CARD_COLS          3   /* 应用卡片列数 */
#define CONTROL_BOTTOM_RESERVE     50  /* dismiss_bar height; also = vertical room reserved below content_bar */

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    CONST lv_img_dsc_t *icon;
    CONST CHAR_T *label;
    TY_DISPLAY_ACTION_E action;
} CONTROL_CARD_CFG_T;

typedef struct {
    lv_obj_t *ctrl_scr;
    lv_obj_t *prev_scr;
    lv_obj_t *title_bar;
    lv_obj_t *content_bar;
    lv_obj_t *dismiss_bar;
    lv_obj_t *volume_sli;
    lv_obj_t *brightness_sli;
    lv_obj_t *mode_entry_btn;
    lv_obj_t *mode_entry_lbl;
    lv_obj_t *up_icon;
    lv_obj_t *up_btn;
} CONTROL_UI_T;

/* External getters (no public header in current project layout) */
extern AI_DEVICE_MODE_E tuya_ai_toy_device_mode_get(VOID);
extern AI_CHAT_SUB_MODE_E tuya_ai_toy_trigger_mode_get(VOID);

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC CONTROL_UI_T s_control = {0};

STATIC CONST CONTROL_CARD_CFG_T s_card_cfgs[] = {
    { &icon_photo_app,     "相册", TY_DISP_ACT_OPEN_ALBUM },
    { &icon_camera_app,    "相机", TY_DISP_ACT_OPEN_CAMERA },
    { &icon_record_app,    "录音", TY_DISP_ACT_OPEN_RECORD },
    { &icon_music_app,     "音乐", TY_DISP_ACT_OPEN_MUSIC },
    { &icon_call_app,      "通话", TY_DISP_ACT_OPEN_CALL },
    { &icon_detection_app, "侦测", TY_DISP_ACT_OPEN_DETECTION },
    { &icon_settings_app,  "设置", TY_DISP_ACT_OPEN_SETTINGS },
};

#define CONTROL_CARD_COUNT (sizeof(s_card_cfgs) / sizeof(s_card_cfgs[0]))

STATIC CONST CHAR_T *s_chat_sub_mode_names[] = {
    "长按", "按键", "唤醒", "自由",
};

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __control_slider_event_cb(lv_event_t *e);
STATIC VOID_T __control_dismiss(VOID_T);
STATIC VOID_T __control_dispatch_and_close(TY_DISPLAY_ACTION_E action);
STATIC VOID_T __control_dismiss_cb(lv_event_t *e);
STATIC VOID_T __control_gesture_cb(lv_event_t *e);
STATIC VOID_T __control_open_gesture_cb(lv_event_t *e);
STATIC VOID_T __control_card_click_cb(lv_event_t *e);
STATIC VOID_T __control_mode_entry_cb(lv_event_t *e);
STATIC CONST CHAR_T *__control_get_mode_text(VOID_T);
STATIC VOID_T __control_create_slider(lv_obj_t *parent, lv_obj_t **sli,
                                   lv_coord_t x, lv_coord_t y,
                                   CONST lv_img_dsc_t *icon, INT_T value);
STATIC lv_obj_t *__control_create_card(lv_obj_t *parent,
                                    CONST CONTROL_CARD_CFG_T *cfg,
                                    lv_coord_t x, lv_coord_t y,
                                    lv_coord_t w, lv_coord_t h);
STATIC VOID_T __control_create_mode_entry(lv_obj_t *parent, lv_coord_t x,
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
STATIC VOID_T __control_create_slider(lv_obj_t *parent, lv_obj_t **sli,
                                   lv_coord_t x, lv_coord_t y,
                                   CONST lv_img_dsc_t *icon, INT_T value)
{
    *sli = lv_slider_create(parent);
    lv_slider_set_range(*sli, 0, 100);
    lv_slider_set_value(*sli, value, LV_ANIM_OFF);
    lv_obj_set_pos(*sli, x, y);
    lv_obj_set_size(*sli, CONTROL_SLIDER_W, CONTROL_SLIDER_H);

    lv_obj_set_style_radius(*sli, CONTROL_SLIDER_RADIUS, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(*sli, CONTROL_SLIDER_BG_OPA, LV_PART_MAIN);
    lv_obj_set_style_bg_color(*sli, lv_color_hex(CONTROL_SLIDER_BG), LV_PART_MAIN);

    lv_obj_set_style_radius(*sli, CONTROL_SLIDER_RADIUS, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(*sli, lv_color_hex(CONTROL_SLIDER_FILL), LV_PART_INDICATOR);

    lv_obj_set_style_bg_opa(*sli, LV_OPA_TRANSP, LV_PART_KNOB);

    lv_obj_clear_flag(*sli, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(*sli, __control_slider_event_cb, LV_EVENT_RELEASED, NULL);

    if (icon != NULL) {
        lv_obj_t *img = lv_img_create(*sli);
        lv_img_set_src(img, icon);
        lv_obj_set_pos(img, CONTROL_SLIDER_ICON_PAD,
                       (CONTROL_SLIDER_H - CONTROL_ICON_SIZE) / 2);
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
STATIC lv_obj_t *__control_create_card(lv_obj_t *parent,
                                    CONST CONTROL_CARD_CFG_T *cfg,
                                    lv_coord_t x, lv_coord_t y,
                                    lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, CONTROL_CARD_RADIUS, 0);
    lv_obj_set_style_bg_opa(card, CONTROL_CARD_BG_OPA, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(CONTROL_CARD_BG), 0);
    lv_obj_set_style_pad_all(card, 0, 0);

    lv_obj_t *icon = lv_img_create(card);
    lv_img_set_src(icon, cfg->icon);
    lv_obj_set_pos(icon, CONTROL_CARD_PAD, CONTROL_CARD_PAD);

    lv_obj_t *arrow = lv_img_create(card);
    lv_img_set_src(arrow, &icon_arrow_yellow);
    lv_obj_set_pos(arrow, w - CONTROL_CARD_PAD - CONTROL_ICON_SIZE, CONTROL_CARD_PAD);

    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, cfg->label);
    lv_obj_set_pos(label, CONTROL_CARD_PAD, h - 18 - CONTROL_CARD_PAD);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &AlibabaPuHuiTi3_Regular18_Static, 0);

    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, __control_card_click_cb, LV_EVENT_CLICKED, (VOID_T *)cfg);

    return card;
}

/**
 * @brief Build display text for the current device / chat sub-mode
 * @return pointer to a static buffer holding the text
 * @note For CHAT mode the text is "闲聊模式: <sub-mode>"; otherwise the
 *       device mode name is returned.
 */
STATIC CONST CHAR_T *__control_get_mode_text(VOID_T)
{
    STATIC CHAR_T s_buf[64];
    AI_DEVICE_MODE_E cur = tuya_ai_toy_device_mode_get();

    TAL_PR_DEBUG("ctrl mode text: device=%d", cur);

    switch (cur) {
    case AI_DEVICE_MODE_CHAT: {
        AI_CHAT_SUB_MODE_E sub = tuya_ai_toy_trigger_mode_get();
        CONST CHAR_T *sub_name = (sub < AI_CHAT_SUB_MAX)
                                 ? s_chat_sub_mode_names[sub] : "未知";
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
STATIC VOID_T __control_create_mode_entry(lv_obj_t *parent, lv_coord_t x,
                                       lv_coord_t y)
{
    s_control.mode_entry_btn = lv_btn_create(parent);
    lv_obj_remove_style_all(s_control.mode_entry_btn);
    lv_obj_set_size(s_control.mode_entry_btn,
                    CONTROL_MODE_ENTRY_W, CONTROL_MODE_ENTRY_H);
    lv_obj_set_pos(s_control.mode_entry_btn, x, y);
    lv_obj_set_style_radius(s_control.mode_entry_btn,
                            CONTROL_MODE_ENTRY_RADIUS, 0);
    lv_obj_set_style_bg_color(s_control.mode_entry_btn,
                              lv_color_hex(CONTROL_MODE_ENTRY_BG), 0);
    lv_obj_set_style_bg_opa(s_control.mode_entry_btn, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_control.mode_entry_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_control.mode_entry_btn, __control_mode_entry_cb,
                        LV_EVENT_CLICKED, NULL);

    s_control.mode_entry_lbl = lv_label_create(s_control.mode_entry_btn);
    lv_obj_set_style_text_color(s_control.mode_entry_lbl,
                                lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_control.mode_entry_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_label_set_text(s_control.mode_entry_lbl, __control_get_mode_text());
    lv_obj_align(s_control.mode_entry_lbl, LV_ALIGN_LEFT_MID,
                 CONTROL_MODE_ENTRY_LBL_PAD, 0);

    lv_obj_t *arrow = lv_img_create(s_control.mode_entry_btn);
    lv_img_set_src(arrow, &icon_arrow_yellow);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -CONTROL_MODE_ENTRY_ARROW_PAD, 0);
    lv_obj_clear_flag(arrow, LV_OBJ_FLAG_CLICKABLE);
}

/**
 * @brief Slider released callback that applies the final value
 * @param[in] e LVGL event
 * @return none
 * @note Bound to LV_EVENT_RELEASED so the set interface is only invoked once
 *       when the knob is released, not continuously while dragging. The visual
 *       fill still tracks the drag because LVGL updates the slider internally.
 */
STATIC VOID_T __control_slider_event_cb(lv_event_t *e)
{
    lv_obj_t *sli = lv_event_get_target(e);
    INT_T val = lv_slider_get_value(sli);

    if (sli == s_control.volume_sli) {
        TAL_PR_DEBUG("volume: %d", val);
        tuya_ai_toy_volume_set((UINT8_T)val);
    } else if (sli == s_control.brightness_sli) {
        TAL_PR_DEBUG("brightness: %d", val);
        tuya_disp_lcd_backlight_set((UINT8_T)val);
    }
}

/**
 * @brief Card click callback, dismisses control center and posts action
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __control_card_click_cb(lv_event_t *e)
{
    CONST CONTROL_CARD_CFG_T *cfg = (CONST CONTROL_CARD_CFG_T *)lv_event_get_user_data(e);
    if (cfg == NULL) {
        return;
    }

    TAL_PR_DEBUG("card clicked: %s, action: %d", cfg->label, cfg->action);
    __control_dispatch_and_close(cfg->action);
}

/**
 * @brief Mode entry click callback, dismisses control center and opens
 *        the device mode selection page.
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __control_mode_entry_cb(lv_event_t *e)
{
    (VOID_T)e;
    TAL_PR_DEBUG("mode entry clicked");
    __control_dispatch_and_close(TY_DISP_ACT_OPEN_DEVICE_MODE);
}

/**
 * @brief Switch back to previous screen and delete control center
 * @return none
 */
STATIC VOID_T __control_dismiss(VOID_T)
{
    if (s_control.ctrl_scr == NULL) {
        return;
    }

    if (s_control.prev_scr) {
        lv_scr_load(s_control.prev_scr);
    }

    lv_obj_del(s_control.ctrl_scr);
    memset(&s_control, 0, sizeof(s_control));
}

/**
 * @brief Close the control center while routing through a UI dispatch action.
 *        Drives the dispatcher synchronously so the target screen takes over
 *        before ctrl_scr is deleted, avoiding a one-frame flash of prev_scr
 *        (the home/date screen) that the queue-based async path produced.
 * @param[in] action UI action to dispatch (typically TY_DISP_ACT_OPEN_*)
 * @return none
 */
STATIC VOID_T __control_dispatch_and_close(TY_DISPLAY_ACTION_E action)
{
    lv_obj_t *to_del = s_control.ctrl_scr;
    lv_obj_t *prev_scr = s_control.prev_scr;

    if (to_del == NULL) {
        tuya_ai_display_action_post(NULL, 0, action);
        return;
    }

    memset(&s_control, 0, sizeof(s_control));

    lv_scr_load(prev_scr);
    lv_obj_del(to_del);
    ui_dispatch_action(action, NULL, 0);
}

/**
 * @brief Dismiss button click callback
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __control_dismiss_cb(lv_event_t *e)
{
    (VOID_T)e;
    __control_dismiss();
}

/**
 * @brief Control center gesture callback, swipe-up to dismiss
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __control_gesture_cb(lv_event_t *e)
{
    (VOID_T)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_TOP) {
        __control_dismiss();
    }
}

/**
 * @brief Build and show the control center overlay screen
 * @param[in] volume current volume (0-100)
 * @param[in] brightness current brightness (0-100)
 * @param[in] alarm_vol current alarm volume (0-100)
 * @return none
 */
VOID_T setup_scr_control_center(UINT8_T volume, UINT8_T brightness, UINT8_T alarm_vol)
{
    (VOID_T)alarm_vol;
    volume = tuya_ai_toy_volume_get();
    s_control.prev_scr = lv_scr_act();

    s_control.ctrl_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_control.ctrl_scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_scrollbar_mode(s_control.ctrl_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_control.ctrl_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_control.ctrl_scr, lv_color_hex(UI_BG_COLOR_DARK), 0);
    lv_obj_set_style_pad_all(s_control.ctrl_scr, 0, 0);

    /* ---- Transparent title bar with centered title ---- */
    s_control.title_bar = lv_obj_create(s_control.ctrl_scr);
    lv_obj_set_pos(s_control.title_bar, 0, 0);
    lv_obj_set_size(s_control.title_bar, CONTROL_TITLE_BAR_W, UI_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(s_control.title_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_control.title_bar, 0, 0);
    lv_obj_set_style_pad_all(s_control.title_bar, 0, 0);
    lv_obj_clear_flag(s_control.title_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_label = lv_label_create(s_control.title_bar);
    lv_label_set_text(title_label, "个人中心");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_center(title_label);

    /* ---- Scrollable content bar (fixed viewport, internally scrollable) ---- */
    s_control.content_bar = lv_obj_create(s_control.ctrl_scr);
    lv_obj_set_pos(s_control.content_bar, 0, CONTROL_CONTENT_BAR_Y);
    lv_obj_set_width(s_control.content_bar, CONTROL_CONTENT_BAR_W);
    lv_obj_set_height(s_control.content_bar,
                      LV_VER_RES - CONTROL_CONTENT_BAR_Y - CONTROL_BOTTOM_RESERVE);
    lv_obj_set_style_bg_opa(s_control.content_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_control.content_bar, 0, 0);
    lv_obj_set_style_pad_all(s_control.content_bar, 0, 0);
    lv_obj_set_scrollbar_mode(s_control.content_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(s_control.content_bar, LV_DIR_VER);

    /* ---- Device mode entry button aligned to content top ---- */
    lv_coord_t entry_y = 0;
    __control_create_mode_entry(s_control.content_bar, CONTROL_SLIDER_X, entry_y);

    /* ---- Volume / brightness sliders below the mode entry,
     *      top-aligned to the original cards row ---- */
    lv_coord_t sli_y0 = entry_y + CONTROL_MODE_ENTRY_H + CONTROL_CARD_TOP_PAD;
    lv_coord_t sli_y1 = sli_y0 + CONTROL_SLIDER_H + CONTROL_SLIDER_GAP;

    __control_create_slider(s_control.content_bar, &s_control.volume_sli,
                         CONTROL_SLIDER_X, sli_y0, &icon_volume, volume);
    __control_create_slider(s_control.content_bar, &s_control.brightness_sli,
                         CONTROL_SLIDER_X, sli_y1, &icon_brightness, brightness);

    /* ---- Shortcut cards below sliders ---- */
    lv_coord_t card_y = sli_y1 + CONTROL_SLIDER_H + CONTROL_CARD_BOTTOM_GAP;
    lv_coord_t card_w = (CONTROL_SLIDER_W - CONTROL_CARD_GAP * (CONTROL_CARD_COLS - 1)) / CONTROL_CARD_COLS;
    UINT32_T i;

    for (i = 0; i < CONTROL_CARD_COUNT; i++) {
        UINT32_T col = i % CONTROL_CARD_COLS;
        UINT32_T row = i / CONTROL_CARD_COLS;
        lv_coord_t cx = CONTROL_SLIDER_X + col * (card_w + CONTROL_CARD_GAP);
        lv_coord_t cy = card_y + row * (CONTROL_CARD_H + CONTROL_CARD_GAP);
        __control_create_card(s_control.content_bar, &s_card_cfgs[i],
                           cx, cy, card_w, CONTROL_CARD_H);
    }

    /* ---- Dismiss bar: container + swipe-up hint ----
     * Bottom-anchored 50px-tall transparent strip; up_icon and up_btn live
     * inside it and center themselves. Lets a single CONTROL_BOTTOM_RESERVE
     * drive both the content_bar viewport and the dismiss area geometry.
     */
    s_control.dismiss_bar = lv_obj_create(s_control.ctrl_scr);
    lv_obj_remove_style_all(s_control.dismiss_bar);
    lv_obj_set_size(s_control.dismiss_bar, LV_HOR_RES, CONTROL_BOTTOM_RESERVE);
    lv_obj_align(s_control.dismiss_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_control.dismiss_bar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_control.dismiss_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_control.up_icon = lv_img_create(s_control.dismiss_bar);
    lv_img_set_src(s_control.up_icon, &icon_up);
    lv_obj_center(s_control.up_icon);

    s_control.up_btn = lv_btn_create(s_control.dismiss_bar);
    lv_obj_remove_style_all(s_control.up_btn);
    lv_obj_set_size(s_control.up_btn, 100, 40);
    lv_obj_center(s_control.up_btn);
    lv_obj_add_event_cb(s_control.up_btn, __control_dismiss_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_add_event_cb(s_control.ctrl_scr, __control_gesture_cb, LV_EVENT_GESTURE, NULL);

    lv_obj_update_layout(s_control.ctrl_scr);
    lv_scr_load(s_control.ctrl_scr);
    lv_indev_wait_release(lv_indev_get_act());
}

/**
 * @brief Check whether control center is currently visible
 * @return TRUE if visible, FALSE otherwise
 */
BOOL_T ui_control_center_is_active(VOID_T)
{
    return (s_control.ctrl_scr != NULL) ? TRUE : FALSE;
}

/**
 * @brief Refresh the mode entry label to reflect the current device / chat
 *        sub-mode. No-op when the control center is not visible.
 * @return none
 */
VOID_T ui_control_center_refresh_mode(VOID_T)
{
    if (s_control.mode_entry_lbl == NULL) {
        return;
    }
    lv_label_set_text(s_control.mode_entry_lbl, __control_get_mode_text());
    lv_obj_align(s_control.mode_entry_lbl, LV_ALIGN_LEFT_MID,
                 CONTROL_MODE_ENTRY_LBL_PAD, 0);
}

/**
 * @brief Gesture callback that opens control center on swipe-down
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __control_open_gesture_cb(lv_event_t *e)
{
    (VOID_T)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_BOTTOM && !ui_control_center_is_active()) {
        setup_scr_control_center(50, 50, 50);
    }
}

/**
 * @brief Register swipe-down gesture on any screen to open control center
 * @param[in] scr screen object to register gesture on
 * @return none
 * @note Call this in every screen's setup function after creating the screen
 */
VOID_T ui_control_center_register_gesture(lv_obj_t *scr)
{
    if (scr) {
        lv_obj_add_event_cb(scr, __control_open_gesture_cb, LV_EVENT_GESTURE, NULL);
    }
}
