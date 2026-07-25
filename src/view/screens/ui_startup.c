/**
 * @file ui_startup.c
 * @brief Startup welcome screen for T5AI_BOARD (320x480)
 *
 * One-shot splash that loads at boot and self-navigates to the home
 * screen via ui_nav after a 1-second delay.
 *
 * @version 1.0
 * @date 2026-05-13
 * @copyright Copyright (c) Tuya Inc.
 */
#include "ui_common.h"

/* ---------------------------------------------------------------------------
 * Font declarations
 * --------------------------------------------------------------------------- */
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular30);

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    lv_obj_t *startup_scr;
} STARTUP_UI_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC STARTUP_UI_T s_startup = {0};

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Startup welcome timer callback, transitions to home screen
 * @param[in] timer LVGL timer handle
 * @return none
 */
STATIC VOID_T __startup_welcome_timer_cb(lv_timer_t *timer)
{
    ui_nav_to(UI_SCR_HOME);

    if (timer) {
        lv_timer_del(timer);
    }
}

/**
 * @brief Build and show the startup welcome screen
 * @return none
 */
VOID_T setup_scr_startup(VOID_T)
{
    s_startup.startup_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_startup.startup_scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_scrollbar_mode(s_startup.startup_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_startup.startup_scr, lv_color_hex(UI_BG_COLOR_DARK), 0);
    lv_obj_set_style_pad_all(s_startup.startup_scr, 0, 0);

    lv_obj_t *welcome_text = lv_label_create(s_startup.startup_scr);
    lv_label_set_text(welcome_text, "Welcome");
    lv_obj_set_style_text_font(welcome_text, &AlibabaPuHuiTi3_Regular30, 0);
    lv_obj_set_style_text_color(welcome_text, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(welcome_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(welcome_text);

    lv_obj_update_layout(s_startup.startup_scr);
    lv_scr_load(s_startup.startup_scr);
    lv_timer_create(__startup_welcome_timer_cb, 1000, NULL);
}
