#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_svc_netmon.h"
#include "ui_page_ids.h"

/* Title-bar back icon (24x24) */
LV_IMG_DECLARE(icon_back_24_24);

/* ---------------------------------------------------------------------------
 * Network status — read-only live network info page under Diagnostics.
 *
 * Same info-row look as the System-status page. Values refresh on the SYSTEM
 * dirty bit: ui_tick marks it every second, and ui_state_set_online() marks
 * it on cloud on/offline flips — no page-local lv_timer needed.
 * -------------------------------------------------------------------------*/

/* ---------------------------------------------------------------------------
 * Style constants
 * -------------------------------------------------------------------------*/
#define NET_STATUS_STATUSBAR_H   32   /* global statusbar height (ui_comp_statusbar) */
#define NET_STATUS_TITLEBAR_H    48
#define NET_STATUS_ROW_MIN_H     48

/* ---------------------------------------------------------------------------
 * Internal state
 * -------------------------------------------------------------------------*/
static lv_obj_t *s_screen       = NULL;
static lv_obj_t *s_content      = NULL;
static lv_obj_t *s_title        = NULL;   /* page title (i18n) */
static lv_obj_t *s_wifi_label   = NULL;   /* "WiFi 状态" row label */
static lv_obj_t *s_wifi_value   = NULL;   /* value is i18n too (已连接/未连接) */
static lv_obj_t *s_ssid_label   = NULL;
static lv_obj_t *s_ssid_value   = NULL;
static lv_obj_t *s_rssi_label   = NULL;
static lv_obj_t *s_rssi_value   = NULL;
static lv_obj_t *s_ip_label     = NULL;
static lv_obj_t *s_ip_value     = NULL;
static lv_obj_t *s_mac_label    = NULL;
static lv_obj_t *s_mac_value    = NULL;
static lv_obj_t *s_cloud_label  = NULL;   /* "云连接" row label */
static lv_obj_t *s_cloud_value  = NULL;   /* value is i18n too (在线/离线) */

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

/* Rewrite a value label only when the text actually changed — the network
 * strings (SSID/IP/MAC) are stable, no point invalidating them every second. */
static void value_set_if_changed(lv_obj_t *value, const char *text)
{
    if (value && strcmp(lv_label_get_text(value), text) != 0) {
        lv_label_set_text(value, text);
    }
}

/* One read-only row: label (left) + value (right, secondary color).
 * Same look as the About / System-status rows. */
static void info_row_create(lv_obj_t *parent, ui_i18n_key_t label_key,
                            lv_obj_t **out_label, lv_obj_t **out_value)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, ui_adapt(NET_STATUS_ROW_MIN_H), 0);
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
    lv_label_set_text(val, "--");
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
    lv_obj_set_style_pad_top(s_screen, ui_adapt(NET_STATUS_STATUSBAR_H), 0);

    /* Title bar: icon-only back (left), page title centered */
    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(NET_STATUS_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_click_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);

    s_title = lv_label_create(titlebar);
    lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_DIAG_NET_STATUS));
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    /* Content area — vertical scrollable list of read-only info rows. */
    s_content = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_width(s_content, LV_PCT(100));
    lv_obj_set_flex_grow(s_content, 1);
    /* Six rows can exceed a short screen — allow vertical scroll. */
    lv_obj_add_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_content, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_row(s_content, ui_adapt(UI_SPACE_SM), 0);

    info_row_create(s_content, UI_TEXT_NET_WIFI_STATUS, &s_wifi_label,  &s_wifi_value);
    info_row_create(s_content, UI_TEXT_NET_SSID,        &s_ssid_label,  &s_ssid_value);
    info_row_create(s_content, UI_TEXT_NET_RSSI,        &s_rssi_label,  &s_rssi_value);
    info_row_create(s_content, UI_TEXT_NET_IP,          &s_ip_label,    &s_ip_value);
    info_row_create(s_content, UI_TEXT_NET_MAC,         &s_mac_label,   &s_mac_value);
    info_row_create(s_content, UI_TEXT_NET_CLOUD,       &s_cloud_label, &s_cloud_value);

    refresh_stats();
}

static void on_enter(uint32_t dirty)
{
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        refresh_lang();
        refresh_stats();   /* the WiFi/cloud VALUES are i18n texts too */
    }
    /* ui_tick marks SYSTEM dirty on every second change (and on cloud
     * online/offline flips) — our refresh clock. */
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
        s_screen      = NULL;
        s_content     = NULL;
        s_title       = NULL;
        s_wifi_label  = NULL;
        s_wifi_value  = NULL;
        s_ssid_label  = NULL;
        s_ssid_value  = NULL;
        s_rssi_label  = NULL;
        s_rssi_value  = NULL;
        s_ip_label    = NULL;
        s_ip_value    = NULL;
        s_mac_label   = NULL;
        s_mac_value   = NULL;
        s_cloud_label = NULL;
        s_cloud_value = NULL;
    }
}

/* ---------------------------------------------------------------------------
 * Refresh
 * -------------------------------------------------------------------------*/

/* Re-read the live network info and update the value labels. Every value goes
 * through the changed-guard — only the RSSI genuinely moves between ticks. */
static void refresh_stats(void)
{
    char buf[16];
    bool connected = ui_svc_netmon_wifi_connected();

    value_set_if_changed(s_wifi_value,
                         ui_i18n_text(connected ? UI_TEXT_WLAN_CONNECTED
                                                : UI_TEXT_WLAN_NOT_CONNECTED));
    value_set_if_changed(s_ssid_value, ui_svc_netmon_ssid());

    int8_t rssi = ui_svc_netmon_rssi();
    if (rssi != 0) {
        snprintf(buf, sizeof(buf), "%d dBm", (int)rssi);
        value_set_if_changed(s_rssi_value, buf);
    } else {
        value_set_if_changed(s_rssi_value, "--");
    }

    value_set_if_changed(s_ip_value,  ui_svc_netmon_ip());
    value_set_if_changed(s_mac_value, ui_svc_netmon_mac());

    value_set_if_changed(s_cloud_value,
                         ui_i18n_text(ui_state_get_system()->is_online
                                          ? UI_TEXT_NET_ONLINE
                                          : UI_TEXT_NET_OFFLINE));
}

/* Re-apply language-dependent texts (RULES: never cache ui_i18n_text() pointers
 * across a language switch). The WiFi/cloud values are re-resolved by the
 * refresh_stats() call that follows in on_enter. */
static void refresh_lang(void)
{
    if (s_title) {
        lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_DIAG_NET_STATUS));
        lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);
    }
    if (s_wifi_label) {
        lv_label_set_text(s_wifi_label, ui_i18n_text(UI_TEXT_NET_WIFI_STATUS));
    }
    if (s_ssid_label) {
        lv_label_set_text(s_ssid_label, ui_i18n_text(UI_TEXT_NET_SSID));
    }
    if (s_rssi_label) {
        lv_label_set_text(s_rssi_label, ui_i18n_text(UI_TEXT_NET_RSSI));
    }
    if (s_ip_label) {
        lv_label_set_text(s_ip_label, ui_i18n_text(UI_TEXT_NET_IP));
    }
    if (s_mac_label) {
        lv_label_set_text(s_mac_label, ui_i18n_text(UI_TEXT_NET_MAC));
    }
    if (s_cloud_label) {
        lv_label_set_text(s_cloud_label, ui_i18n_text(UI_TEXT_NET_CLOUD));
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
const ui_page_entry_t ui_page_net_status_entry = {
    .id = UI_PAGE_NET_STATUS,
    .name = "net_status",
    .lifecycle = { on_create, on_enter, NULL, on_destroy }
};
