/**
 * @file ui_home.c
 * @brief Startup welcome screen and home date screen for T5AI_BOARD (320x480)
 * @version 1.0
 * @date 2025-04-02
 * @copyright Copyright (c) Tuya Inc.
 */
#include "ui_common.h"
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Font declarations
 * --------------------------------------------------------------------------- */
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular16);
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular30);
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular40);
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular65);
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular120);

/* ---------------------------------------------------------------------------
 * Icon declarations
 * --------------------------------------------------------------------------- */
LV_IMG_DECLARE(icon_weather);
LV_IMG_DECLARE(icon_calendar);
LV_IMG_DECLARE(icon_clock);
LV_IMG_DECLARE(icon_wifi_24_24);
LV_IMG_DECLARE(icon_battery_icon);
LV_IMG_DECLARE(icon_point_1);
LV_IMG_DECLARE(icon_point_2);

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    UINT8_T month;
    UINT8_T day;
    CHAR_T weekday[16];
    UINT8_T cal_count;
    UINT8_T alarm_count;
} UI_HOME_CTX_T;

typedef struct {
    lv_obj_t *startup_scr;
} STARTUP_UI_T;

typedef struct {
    lv_obj_t *home_scr;
    lv_obj_t *calendar_num;
    lv_obj_t *clock_num;
    lv_obj_t *wifi_icon;
    lv_obj_t *battery_bar;
    lv_obj_t *date_month;
    lv_obj_t *date_month_text;
    lv_obj_t *date_day;
    lv_obj_t *date_week;
} HOME_UI_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC STARTUP_UI_T s_startup_ui = {0};
STATIC HOME_UI_T s_home_ui = {0};
STATIC UI_HOME_CTX_T s_home_ctx = {
    .month = 6,
    .day = 20,
    .weekday = "星期三",
    .cal_count = 2,
    .alarm_count = 4,
};

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __home_gesture_cb(lv_event_t *e);

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

VOID_T ui_home_set_ctx(UINT8_T month, UINT8_T day, CONST CHAR_T *weekday,
                       UINT8_T cal_count, UINT8_T alarm_count)
{
    s_home_ctx.month = month;
    s_home_ctx.day = day;
    s_home_ctx.cal_count = cal_count;
    s_home_ctx.alarm_count = alarm_count;

    if (weekday != NULL) {
        strncpy(s_home_ctx.weekday, weekday, sizeof(s_home_ctx.weekday) - 1);
        s_home_ctx.weekday[sizeof(s_home_ctx.weekday) - 1] = '\0';
    }
}

VOID_T ui_home_show(VOID_T)
{
    setup_scr_home(s_home_ctx.month, s_home_ctx.day, s_home_ctx.weekday,
                   s_home_ctx.cal_count, s_home_ctx.alarm_count);
}

/**
 * @brief Home screen gesture callback, swipe-left to enter chat page
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __home_gesture_cb(lv_event_t *e)
{
    (VOID_T)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_LEFT) {
        ui_nav_to(UI_SCR_CHAT);
    }
}

/**
 * @brief Build and show the date/time home screen
 * @param[in] month month number (1-12)
 * @param[in] day day number (1-31)
 * @param[in] weekday weekday string, e.g. "星期三"
 * @param[in] cal_count calendar event count shown in status bar
 * @param[in] alarm_count alarm count shown in status bar
 * @return none
 */
void setup_scr_home(UINT8_T month, UINT8_T day, CONST CHAR_T *weekday,
                    UINT8_T cal_count, UINT8_T alarm_count)
{
    CHAR_T buf[16];

    s_home_ui.home_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_home_ui.home_scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_scrollbar_mode(s_home_ui.home_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_home_ui.home_scr, lv_color_hex(0x25262A), 0);
    lv_obj_set_style_pad_all(s_home_ui.home_scr, 0, 0);

    /* ---- Top status bar (rounded pill) ---- */
    lv_obj_t *status_bar = lv_obj_create(s_home_ui.home_scr);
    lv_obj_set_pos(status_bar, 16, 12);
    lv_obj_set_size(status_bar, 162, 28);
    lv_obj_set_scrollbar_mode(status_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_radius(status_bar, 14, 0);
    lv_obj_set_style_bg_opa(status_bar, 28, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_pad_all(status_bar, 0, 0);

    /* Vertical dividers inside status bar */
    lv_obj_t *line1 = lv_line_create(status_bar);
    STATIC lv_point_t line_pts1[] = {{0, 0}, {0, 14}};
    lv_line_set_points(line1, line_pts1, 2);
    lv_obj_set_pos(line1, 54, 7);
    lv_obj_set_style_line_width(line1, 2, 0);
    lv_obj_set_style_line_color(line1, lv_color_hex(0x757575), 0);
    lv_obj_set_style_line_rounded(line1, true, 0);

    lv_obj_t *line2 = lv_line_create(status_bar);
    STATIC lv_point_t line_pts2[] = {{0, 0}, {0, 14}};
    lv_line_set_points(line2, line_pts2, 2);
    lv_obj_set_pos(line2, 108, 7);
    lv_obj_set_style_line_width(line2, 2, 0);
    lv_obj_set_style_line_color(line2, lv_color_hex(0x757575), 0);
    lv_obj_set_style_line_rounded(line2, true, 0);

    /* Weather icon */
    lv_obj_t *weather_img = lv_img_create(status_bar);
    lv_img_set_src(weather_img, &icon_weather);
    lv_obj_set_pos(weather_img, 19, 6);

    /* Calendar icon + count */
    lv_obj_t *cal_img = lv_img_create(status_bar);
    lv_img_set_src(cal_img, &icon_calendar);
    lv_obj_set_pos(cal_img, 66, 6);

    s_home_ui.calendar_num = lv_label_create(status_bar);
    snprintf(buf, sizeof(buf), "%d", cal_count);
    lv_label_set_text(s_home_ui.calendar_num, buf);
    lv_obj_set_pos(s_home_ui.calendar_num, 86, 7);
    lv_obj_set_style_text_color(s_home_ui.calendar_num, lv_color_hex(0xFFF37B), 0);
    lv_obj_set_style_text_font(s_home_ui.calendar_num, &AlibabaPuHuiTi3_Regular16, 0);

    /* Clock/alarm icon + count */
    lv_obj_t *clock_img = lv_img_create(status_bar);
    lv_img_set_src(clock_img, &icon_clock);
    lv_obj_set_pos(clock_img, 120, 6);

    s_home_ui.clock_num = lv_label_create(status_bar);
    snprintf(buf, sizeof(buf), "%d", alarm_count);
    lv_label_set_text(s_home_ui.clock_num, buf);
    lv_obj_set_pos(s_home_ui.clock_num, 140, 7);
    lv_obj_set_style_text_color(s_home_ui.clock_num, lv_color_hex(0xFFF37B), 0);
    lv_obj_set_style_text_font(s_home_ui.clock_num, &AlibabaPuHuiTi3_Regular16, 0);

    /* WiFi icon (right side of screen) */
    s_home_ui.wifi_icon = lv_img_create(s_home_ui.home_scr);
    lv_img_set_src(s_home_ui.wifi_icon, &icon_wifi_24_24);
    lv_obj_set_pos(s_home_ui.wifi_icon, 252, 14);

    /* Battery bar with icon background */
    s_home_ui.battery_bar = lv_bar_create(s_home_ui.home_scr);
    lv_obj_remove_style_all(s_home_ui.battery_bar);
    lv_bar_set_mode(s_home_ui.battery_bar, LV_BAR_MODE_NORMAL);
    lv_bar_set_range(s_home_ui.battery_bar, 0, 100);
    lv_bar_set_value(s_home_ui.battery_bar, 100, LV_ANIM_OFF);
    lv_obj_set_pos(s_home_ui.battery_bar, 283, 19);
    lv_obj_set_size(s_home_ui.battery_bar, 19, 11);
    lv_obj_set_style_bg_opa(s_home_ui.battery_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_img_src(s_home_ui.battery_bar, &icon_battery_icon, 0);
    lv_obj_set_style_pad_all(s_home_ui.battery_bar, 2, 0);
    lv_obj_set_style_bg_opa(s_home_ui.battery_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_home_ui.battery_bar, lv_color_hex(0x4CD964), LV_PART_INDICATOR);

    /* ---- Date area (vertically centered for 320x480) ---- */

    /* Day number card with rounded background (right side) */
    s_home_ui.date_day = lv_label_create(s_home_ui.home_scr);
    snprintf(buf, sizeof(buf), "%d", day);
    lv_label_set_text(s_home_ui.date_day, buf);
    lv_obj_set_pos(s_home_ui.date_day, 152, 140);
    lv_obj_set_size(s_home_ui.date_day, 153, 220);
    lv_obj_set_style_radius(s_home_ui.date_day, 30, 0);
    lv_obj_set_style_text_color(s_home_ui.date_day, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_home_ui.date_day, &AlibabaPuHuiTi3_Regular120, 0);
    lv_obj_set_style_bg_opa(s_home_ui.date_day, 28, 0);
    lv_obj_set_style_bg_color(s_home_ui.date_day, lv_color_hex(0xB8BDDE), 0);
    lv_obj_set_style_text_align(s_home_ui.date_day, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(s_home_ui.date_day, 45, 0);

    /* Month number "06" (left side, large) */
    s_home_ui.date_month = lv_label_create(s_home_ui.home_scr);
    snprintf(buf, sizeof(buf), "%02d", month);
    lv_label_set_text(s_home_ui.date_month, buf);
    lv_obj_set_pos(s_home_ui.date_month, 20, 180);
    lv_obj_set_style_text_color(s_home_ui.date_month, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_home_ui.date_month, &AlibabaPuHuiTi3_Regular65, 0);

    /* "月" character (baseline-aligned to month number) */
    s_home_ui.date_month_text = lv_label_create(s_home_ui.home_scr);
    lv_label_set_text(s_home_ui.date_month_text, "月");
    lv_obj_set_style_text_color(s_home_ui.date_month_text, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_home_ui.date_month_text, &AlibabaPuHuiTi3_Regular40, 0);
    lv_obj_update_layout(s_home_ui.date_month);
    lv_obj_align_to(s_home_ui.date_month_text, s_home_ui.date_month,
                    LV_ALIGN_OUT_RIGHT_BOTTOM, 4, 0);

    /* Weekday "星期三" (left side, below month) */
    s_home_ui.date_week = lv_label_create(s_home_ui.home_scr);
    lv_label_set_text(s_home_ui.date_week, weekday);
    lv_label_set_long_mode(s_home_ui.date_week, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_home_ui.date_week, 2, 268);
    lv_obj_set_size(s_home_ui.date_week, 155, 50);
    lv_obj_set_style_text_color(s_home_ui.date_week, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_home_ui.date_week, &AlibabaPuHuiTi3_Regular40, 0);
    lv_obj_set_style_text_align(s_home_ui.date_week, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_update_layout(s_home_ui.home_scr);
    ui_control_register_gesture(s_home_ui.home_scr);
    lv_obj_add_event_cb(s_home_ui.home_scr, __home_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_scr_load(s_home_ui.home_scr);
}

/**
 * @brief Startup welcome timer callback, transitions to home screen
 * @param[in] timer LVGL timer handle
 * @return none
 */
STATIC VOID_T handle_startup_welcome_timer(lv_timer_t *timer)
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
void setup_scr_startup(VOID_T)
{
    s_startup_ui.startup_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_startup_ui.startup_scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_scrollbar_mode(s_startup_ui.startup_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_startup_ui.startup_scr, lv_color_hex(0x25262A), 0);
    lv_obj_set_style_pad_all(s_startup_ui.startup_scr, 0, 0);

    lv_obj_t *welcome_text = lv_label_create(s_startup_ui.startup_scr);
    lv_label_set_text(welcome_text, "Welcome");
    lv_obj_set_style_text_font(welcome_text, &AlibabaPuHuiTi3_Regular30, 0);
    lv_obj_set_style_text_color(welcome_text, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(welcome_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(welcome_text);

    lv_obj_update_layout(s_startup_ui.startup_scr);
    lv_scr_load(s_startup_ui.startup_scr);
    lv_timer_create(handle_startup_welcome_timer, 1000, NULL);
}
