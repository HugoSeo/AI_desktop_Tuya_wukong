/**
 * @file ui_home.c
 * @brief Home clock screen for T5AI_BOARD (320x480): big HH:MM time with a
 *        weekday + date row beneath it, grouped in a single container.
 * @version 2.0
 * @date 2026-06-01
 * @copyright Copyright (c) Tuya Inc.
 */
#include "ui_common.h"
#include "tal_time_service.h"
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Font / icon declarations
 * --------------------------------------------------------------------------- */
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular30);
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular100);
LV_IMG_DECLARE(icon_wifi_24_24);
LV_IMG_DECLARE(icon_battery_icon);

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
/* HH:MM only changes once per minute, so a coarse 3s tick is plenty. */
#define HOME_REFRESH_MS         3000
/* Vertical gap between the weekday/date row and the big time. */
#define HOME_CLOCK_DATE_GAP     5
/* Horizontal gap between weekday and date in the centered date row
 * (~0.8x the 30px date-row font). */
#define HOME_WEEK_DATE_GAP      24
/* Whole clock block bias from the screen center (negative = up); the block is
 * vertically centered, nudged slightly up to balance the top status bar. */
#define HOME_CLOCK_Y_OFFSET     (-10)

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    lv_obj_t   *scr;
    lv_obj_t   *clock_cont;  /* groups time + date row for one-shot repositioning */
    lv_obj_t   *time_label;  /* big HH:MM */
    lv_obj_t   *week_label;  /* 周X, left-aligned in the date row */
    lv_obj_t   *date_label;  /* YYYY-MM-DD, right-aligned in the date row */
    lv_obj_t   *wifi_icon;
    lv_timer_t *refresh_timer;
} HOME_UI_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC HOME_UI_T s_home = {0};
STATIC BOOL_T    s_home_net_connected = TRUE;

/* Last rendered wall-clock components. Used to deduplicate lv_label_set_text()
 * since LVGL v8 invalidates the widget on every call regardless of content
 * (see lv_label_set_text()), which causes wasted redraws when the refresh
 * timer fires more often than the displayed fields change. The time label
 * shows minute resolution, so it is keyed on the minute, not the second. */
STATIC INT_T s_last_min  = -1;
STATIC INT_T s_last_mday = -1;

/* Short Chinese weekday strings indexed 0=Mon..6=Sun (matches __date_to_week()). */
STATIC CONST CHAR_T *CONST s_weekday_cn[] = {
    "周一", "周二", "周三", "周四", "周五", "周六", "周日",
};

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */
/**
 * @brief Compute weekday using the Kim Larson formula
 * @param[in] year  full Gregorian year, e.g. 2026
 * @param[in] month month [1-12]
 * @param[in] day   day of month [1-31]
 * @return weekday index 0=Mon ... 6=Sun
 */
STATIC INT_T __date_to_week(INT_T year, INT_T month, INT_T day)
{
    if (month == 1 || month == 2) {
        year--;
        month += 12;
    }
    return (day + 2 * month + 3 * (month + 1) / 5 + year + year / 4 - year / 100 + year / 400) % 7;
}

/**
 * @brief Refresh the HH:MM time, weekday, and date labels from the system clock
 * @return none
 * @note Silently returns if time/zone is not yet synced. Skips lv_label_set_text()
 *       when neither the minute nor the day has changed since the last call,
 *       which avoids wasteful LVGL invalidation/redraw (the label invalidates
 *       on every call, see lv_label_set_text() in LVGL v8).
 */
STATIC VOID_T __home_refresh_datetime(VOID_T)
{
    POSIX_TM_S tm = {0};
    CHAR_T     buf[16];
    INT_T      week;

    if (tal_time_check_time_sync() != OPRT_OK ||
        tal_time_check_time_zone_sync() != OPRT_OK) {
        return;
    }
    if (tal_time_get_local_time_custom(0, &tm) != OPRT_OK) {
        return;
    }

    /* Polling guard: nothing visible changed since the last render. */
    if (tm.tm_min == s_last_min && tm.tm_mday == s_last_mday) {
        return;
    }

    if (s_home.time_label != NULL && tm.tm_min != s_last_min) {
        snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
        lv_label_set_text(s_home.time_label, buf);
    }

    /* Weekday and date only change at midnight, refresh on day rollover. */
    if (tm.tm_mday != s_last_mday) {
        if (s_home.week_label != NULL) {
            week = __date_to_week(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
            if (week >= 0 && week < (INT_T)(sizeof(s_weekday_cn) / sizeof(s_weekday_cn[0]))) {
                lv_label_set_text(s_home.week_label, s_weekday_cn[week]);
            }
        }
        if (s_home.date_label != NULL) {
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
            lv_label_set_text(s_home.date_label, buf);
        }
    }

    s_last_min  = tm.tm_min;
    s_last_mday = tm.tm_mday;
}

/**
 * @brief Periodic refresh timer; no-op when the home screen is not active.
 * @param[in] timer LVGL timer handle (unused)
 * @return none
 */
STATIC VOID_T __home_refresh_timer_cb(lv_timer_t *timer)
{
    (VOID_T)timer;
    if (lv_scr_act() == s_home.scr) {
        __home_refresh_datetime();
    }
}

/**
 * @brief Swipe-left gesture handler, navigate to chat page
 * @param[in] e LVGL event (unused)
 * @return none
 */
STATIC VOID_T __home_gesture_cb(lv_event_t *e)
{
    (VOID_T)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_LEFT) {
        ui_nav_to(UI_SCR_CHAT);
    }
}

/**
 * @brief Build and load the clock home screen
 * @return none
 * @note The time and date row live in a single transparent container so the
 *       whole block can be repositioned by adjusting one lv_obj_align() call.
 */
VOID_T ui_home_show(VOID_T)
{
    lv_obj_t  *battery_bar = NULL;
    lv_obj_t  *date_row    = NULL;
    lv_coord_t time_w      = 0;

    /* Force the refresh dedup cache to miss so freshly-created placeholder
     * labels are filled on the first __home_refresh_datetime() call. */
    s_last_min  = -1;
    s_last_mday = -1;

    s_home.scr = lv_obj_create(NULL);
    lv_obj_set_size(s_home.scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_scrollbar_mode(s_home.scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_home.scr, lv_color_hex(UI_BG_COLOR_DARK), 0);
    lv_obj_set_style_pad_all(s_home.scr, 0, 0);

    /* Status bar - WiFi icon */
    s_home.wifi_icon = lv_img_create(s_home.scr);
    lv_img_set_src(s_home.wifi_icon, &icon_wifi_24_24);
    lv_obj_set_pos(s_home.wifi_icon, 252, 14);
    if (!s_home_net_connected) {
        lv_obj_add_flag(s_home.wifi_icon, LV_OBJ_FLAG_HIDDEN);
    }

    /* Status bar - battery bar */
    battery_bar = lv_bar_create(s_home.scr);
    lv_obj_remove_style_all(battery_bar);
    lv_bar_set_mode(battery_bar, LV_BAR_MODE_NORMAL);
    lv_bar_set_range(battery_bar, 0, 100);
    lv_bar_set_value(battery_bar, 100, LV_ANIM_OFF);
    lv_obj_set_pos(battery_bar, 283, 19);
    lv_obj_set_size(battery_bar, 19, 11);
    lv_obj_set_style_bg_opa(battery_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_img_src(battery_bar, &icon_battery_icon, 0);
    lv_obj_set_style_pad_all(battery_bar, 2, 0);
    lv_obj_set_style_bg_opa(battery_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(battery_bar, lv_color_hex(0x4CD964), LV_PART_INDICATOR);

    /* Clock container: stacks the weekday/date row above the big time. */
    s_home.clock_cont = lv_obj_create(s_home.scr);
    lv_obj_remove_style_all(s_home.clock_cont);
    lv_obj_clear_flag(s_home.clock_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_home.clock_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_home.clock_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_home.clock_cont, HOME_CLOCK_DATE_GAP, 0);

    /* Date row (top): weekday and date sit centered as a pair separated by a
     * fixed gap; the row spans the full time width (set below). */
    date_row = lv_obj_create(s_home.clock_cont);
    lv_obj_remove_style_all(date_row);
    lv_obj_clear_flag(date_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_height(date_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(date_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(date_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(date_row, HOME_WEEK_DATE_GAP, 0);

    s_home.week_label = lv_label_create(date_row);
    lv_label_set_text(s_home.week_label, "");
    lv_obj_set_style_text_color(s_home.week_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_home.week_label, &AlibabaPuHuiTi3_Regular30, 0);

    s_home.date_label = lv_label_create(date_row);
    lv_label_set_text(s_home.date_label, "");
    lv_obj_set_style_text_color(s_home.date_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_home.date_label, &AlibabaPuHuiTi3_Regular30, 0);

    /* Big HH:MM time (bottom). */
    s_home.time_label = lv_label_create(s_home.clock_cont);
    lv_label_set_text(s_home.time_label, "00:00");
    lv_obj_set_style_text_color(s_home.time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_home.time_label, &AlibabaPuHuiTi3_Regular100, 0);

    /* Match the date row width to the rendered time width, then center the whole
     * block on screen (nudged up slightly by HOME_CLOCK_Y_OFFSET). */
    lv_obj_update_layout(s_home.clock_cont);
    time_w = lv_obj_get_width(s_home.time_label);
    lv_obj_set_width(date_row, time_w);
    lv_obj_align(s_home.clock_cont, LV_ALIGN_CENTER, 0, HOME_CLOCK_Y_OFFSET);

    /* Refresh once so synced time shows without waiting for the first tick. */
    __home_refresh_datetime();

    if (s_home.refresh_timer != NULL) {
        lv_timer_del(s_home.refresh_timer);
        s_home.refresh_timer = NULL;
    }
    s_home.refresh_timer = lv_timer_create(__home_refresh_timer_cb, HOME_REFRESH_MS, NULL);

    lv_obj_update_layout(s_home.scr);
    ui_control_center_register_gesture(s_home.scr);
    lv_obj_add_event_cb(s_home.scr, __home_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_scr_load(s_home.scr);
}

/**
 * @brief Update the home-screen WiFi icon to reflect network connectivity
 * @param[in] connected TRUE if network is connected, FALSE otherwise
 * @return none
 * @note State is cached and re-applied each time ui_home_show() rebuilds the
 *       screen; safe to call before the home screen is created.
 */
VOID_T ui_home_set_net_state(BOOL_T connected)
{
    s_home_net_connected = connected;
    if (s_home.wifi_icon == NULL) {
        return;
    }
    if (connected) {
        lv_obj_clear_flag(s_home.wifi_icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_home.wifi_icon, LV_OBJ_FLAG_HIDDEN);
    }
}
