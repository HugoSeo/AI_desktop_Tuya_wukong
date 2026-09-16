#include <stdio.h>
#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_svc_sysmon.h"
#include "ui_page_ids.h"

/* Title-bar back icon (24x24) */
LV_IMG_DECLARE(icon_back_24_24);

/* ---------------------------------------------------------------------------
 * System status — read-only live stats page under Diagnostics.
 *
 * Same info-row look as the About page, but the memory/uptime values refresh
 * every second: ui_tick marks the SYSTEM group dirty on each second change,
 * so on_enter re-reads the stats — no page-local lv_timer needed.
 * -------------------------------------------------------------------------*/

/* ---------------------------------------------------------------------------
 * Style constants
 * -------------------------------------------------------------------------*/
#define SYS_STATUS_STATUSBAR_H   32   /* global statusbar height (ui_comp_statusbar) */
#define SYS_STATUS_TITLEBAR_H    48
#define SYS_STATUS_ROW_MIN_H     48

/* ---------------------------------------------------------------------------
 * Internal state
 * -------------------------------------------------------------------------*/
static lv_obj_t *s_screen        = NULL;
static lv_obj_t *s_content       = NULL;
static lv_obj_t *s_title         = NULL;   /* page title (i18n) */
static lv_obj_t *s_sram_label    = NULL;   /* "SRAM 剩余" row label */
static lv_obj_t *s_sram_value    = NULL;
static lv_obj_t *s_psram_label   = NULL;   /* "PSRAM 剩余" row label (PSRAM boards only) */
static lv_obj_t *s_psram_value   = NULL;
static lv_obj_t *s_uptime_label  = NULL;   /* "运行时长" row label */
static lv_obj_t *s_uptime_value  = NULL;
static lv_obj_t *s_reset_label   = NULL;   /* "上次复位原因" row label (value is boot-constant) */

/* Last shown values — skip the label write when unchanged (uptime changes
 * every tick anyway; the heap values often don't). */
static int32_t s_last_sram  = -1;
static int32_t s_last_psram = -1;

/* ---------------------------------------------------------------------------
 * Forward declarations
 * -------------------------------------------------------------------------*/
static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_destroy(void);
static void back_click_cb(lv_event_t *e);
static void gesture_cb(lv_event_t *e);
static void refresh_stats(void);
static void refresh_lang(void);

/* ---------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------*/

/* Format a byte count as "xxx KB" below 1 MB, "x.x MB" from there up. */
static void fmt_bytes(char *buf, size_t len, int32_t bytes)
{
    if (bytes < 0) {
        snprintf(buf, len, "--");
    } else if (bytes < 1024 * 1024) {
        snprintf(buf, len, "%d KB", (int)(bytes / 1024));
    } else {
        snprintf(buf, len, "%d.%d MB", (int)(bytes / (1024 * 1024)),
                 (int)((bytes % (1024 * 1024)) * 10 / (1024 * 1024)));
    }
}

/* One read-only row: label (left) + value (right, secondary color).
 * Same look as the About page rows. */
static void info_row_create(lv_obj_t *parent, ui_i18n_key_t label_key, const char *value,
                            lv_obj_t **out_label, lv_obj_t **out_value)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, ui_adapt(SYS_STATUS_ROW_MIN_H), 0);
    lv_obj_set_style_bg_color(row, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_set_style_pad_hor(row, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_ver(row, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, ui_adapt(UI_SPACE_MD), 0);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, ui_i18n_text(label_key));
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT, 0);
    if (out_label) {
        *out_label = label;
    }

    lv_obj_t *val = lv_label_create(row);
    lv_label_set_text(val, value ? value : "--");
    lv_obj_set_style_text_color(val, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
    /* Take the remaining width and wrap long values instead of overflowing. */
    lv_obj_set_flex_grow(val, 1);
    lv_label_set_long_mode(val, LV_LABEL_LONG_WRAP);
    if (out_value) {
        *out_value = val;
    }
}

/* ---------------------------------------------------------------------------
 * Page lifecycle
 * -------------------------------------------------------------------------*/
static void on_create(void *parent)
{
    (void)parent;

    /* Full-screen root, column layout: title bar on top, content below */
    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);   /* swipe-right = back */
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    /* Clear the global statusbar (lv_layer_top pill, height 32) so the title
     * bar sits below it — same offset the other sub-pages use. */
    lv_obj_set_style_pad_top(s_screen, ui_adapt(SYS_STATUS_STATUSBAR_H), 0);

    /* Title bar: icon-only back (left), page title centered */
    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(SYS_STATUS_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_click_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);

    s_title = lv_label_create(titlebar);
    lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_DIAG_SYS_STATUS));
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    /* Content area — vertical list of read-only info rows. */
    s_content = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_width(s_content, LV_PCT(100));
    lv_obj_set_flex_grow(s_content, 1);
    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_content, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_row(s_content, ui_adapt(UI_SPACE_SM), 0);

    info_row_create(s_content, UI_TEXT_SYS_SRAM_FREE, NULL,
                    &s_sram_label, &s_sram_value);
    /* PSRAM row only on boards that have PSRAM (getter returns <0 otherwise). */
    if (ui_svc_sysmon_psram_free() >= 0) {
        info_row_create(s_content, UI_TEXT_SYS_PSRAM_FREE, NULL,
                        &s_psram_label, &s_psram_value);
    }
    info_row_create(s_content, UI_TEXT_SYS_UPTIME, NULL,
                    &s_uptime_label, &s_uptime_value);
    /* Boot-constant: painted once here, never refreshed. Numeric code — the
     * describe string isn't reliably populated on this platform. */
    char reason[12];
    snprintf(reason, sizeof(reason), "%d", (int)ui_svc_sysmon_reset_reason());
    info_row_create(s_content, UI_TEXT_SYS_RESET_REASON, reason,
                    &s_reset_label, NULL);

    /* Force-paint every dynamic value for the first show. */
    s_last_sram  = -1;
    s_last_psram = -1;
    refresh_stats();
}

static void on_enter(uint32_t dirty)
{
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        refresh_lang();
    }
    /* ui_tick marks SYSTEM dirty on every second change — our 1s refresh clock. */
    if (dirty & (1u << UI_STATE_GROUP_SYSTEM)) {
        refresh_stats();
    }
}

static void on_destroy(void)
{
    if (s_screen) {
        /* on_destroy runs inside the event-callback chain (back -> ui_route_pop);
         * hide immediately then defer deletion to avoid freeing the object while
         * LVGL is still processing events on it. */
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
        s_screen        = NULL;
        s_content       = NULL;
        s_title         = NULL;
        s_sram_label    = NULL;
        s_sram_value    = NULL;
        s_psram_label   = NULL;
        s_psram_value   = NULL;
        s_uptime_label  = NULL;
        s_uptime_value  = NULL;
        s_reset_label   = NULL;
    }
}

/* ---------------------------------------------------------------------------
 * Refresh
 * -------------------------------------------------------------------------*/

/* Re-read the live stats and update the value labels. Heap labels are only
 * rewritten when the value actually changed; uptime changes every call. */
static void refresh_stats(void)
{
    char buf[32];

    int32_t sram = ui_svc_sysmon_sram_free();
    if (s_sram_value && sram != s_last_sram) {
        s_last_sram = sram;
        fmt_bytes(buf, sizeof(buf), sram);
        lv_label_set_text(s_sram_value, buf);
    }

    int32_t psram = ui_svc_sysmon_psram_free();
    if (s_psram_value && psram != s_last_psram) {
        s_last_psram = psram;
        fmt_bytes(buf, sizeof(buf), psram);
        lv_label_set_text(s_psram_value, buf);
    }

    if (s_uptime_value) {
        uint32_t sec = ui_svc_sysmon_uptime_sec();
        snprintf(buf, sizeof(buf), "%ud %02u:%02u:%02u",
                 (unsigned)(sec / 86400u),
                 (unsigned)(sec % 86400u / 3600u),
                 (unsigned)(sec % 3600u / 60u),
                 (unsigned)(sec % 60u));
        lv_label_set_text(s_uptime_value, buf);
    }
}

/* Re-apply language-dependent texts (RULES: never cache ui_i18n_text() pointers
 * across a language switch). Values are numeric / not translated. */
static void refresh_lang(void)
{
    if (s_title) {
        lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_DIAG_SYS_STATUS));
        lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);
    }
    if (s_sram_label) {
        lv_label_set_text(s_sram_label, ui_i18n_text(UI_TEXT_SYS_SRAM_FREE));
    }
    if (s_psram_label) {
        lv_label_set_text(s_psram_label, ui_i18n_text(UI_TEXT_SYS_PSRAM_FREE));
    }
    if (s_uptime_label) {
        lv_label_set_text(s_uptime_label, ui_i18n_text(UI_TEXT_SYS_UPTIME));
    }
    if (s_reset_label) {
        lv_label_set_text(s_reset_label, ui_i18n_text(UI_TEXT_SYS_RESET_REASON));
    }
}

/* ---------------------------------------------------------------------------
 * Callbacks
 * -------------------------------------------------------------------------*/
static void back_click_cb(lv_event_t *e)
{
    (void)e;
    ui_route_pop();
}

/* Swipe right anywhere on the page = go back (same as the back button). */
static void gesture_cb(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());   /* swallow rest of touch so the release doesn't click the page below */
        ui_route_pop();
    }
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/
const ui_page_entry_t ui_page_sys_status_entry = {
    .id = UI_PAGE_SYS_STATUS,
    .name = "sys_status",
    .lifecycle = { on_create, on_enter, NULL, on_destroy }
};
