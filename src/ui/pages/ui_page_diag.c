#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_svc_dev_ctrl.h"
#include "ui_svc_audio_diag.h"
#include "ui_page_ids.h"
LV_IMG_DECLARE(icon_back_24_24);

/* ---------------------------------------------------------------------------
 * Diagnostics — launcher page for on-device test/diagnostic items, reached
 * from the App Center. Hosts the developer-facing rows that used to live in
 * Settings (Show-FPS overlay, audio diagnostics); new test items go here.
 * -------------------------------------------------------------------------*/

/* ---------------------------------------------------------------------------
 * Style constants
 * -------------------------------------------------------------------------*/
#define DIAG_STATUSBAR_H    32   /* global statusbar height (ui_comp_statusbar) */
#define DIAG_TITLEBAR_H     48
#define DIAG_ROW_H          56

/* ---------------------------------------------------------------------------
 * Internal state
 * -------------------------------------------------------------------------*/
static lv_obj_t *s_screen           = NULL;
static lv_obj_t *s_content          = NULL;
static lv_obj_t *s_title            = NULL;   /* centered page title */
static lv_obj_t *s_sys_status_label = NULL;   /* "系统状态" row label */
static lv_obj_t *s_net_status_label = NULL;   /* "网络诊断" row label */
static lv_obj_t *s_screen_test_label = NULL;  /* "屏幕测试" row label */
static lv_obj_t *s_show_fps_label   = NULL;   /* "显示帧率" row label (memory-only) */
static lv_obj_t *s_ap_stat_label    = NULL;   /* "播放统计" row label (memory-only, 编入时才有) */
static lv_obj_t *s_audio_diag_label = NULL;   /* "音频诊断" row label */

/* ---------------------------------------------------------------------------
 * Forward declarations
 * -------------------------------------------------------------------------*/
static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_destroy(void);
static void back_click_cb(lv_event_t *e);
static void gesture_cb(lv_event_t *e);
static void show_fps_cb(lv_event_t *e);
static void ap_stat_cb(lv_event_t *e);
static void audio_diag_click_cb(lv_event_t *e);
static void sys_status_click_cb(lv_event_t *e);
static void net_status_click_cb(lv_event_t *e);
static void screen_test_click_cb(lv_event_t *e);
static void refresh_lang(void);

/* ---------------------------------------------------------------------------
 * Row helpers (same look as the Settings rows)
 * -------------------------------------------------------------------------*/

/* Create a tappable navigation row: label (left) + ">" chevron (right). */
static lv_obj_t *nav_row_create(lv_obj_t *parent, ui_i18n_key_t label_key,
                                lv_obj_t **out_label, lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, ui_adapt(DIAG_ROW_H));
    lv_obj_set_style_bg_color(row, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_set_style_pad_hor(row, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, ui_adapt(UI_SPACE_SM), 0);

    /* Title — flex_grow pushes the chevron to the right edge. */
    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, ui_i18n_text(label_key));
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT, 0);
    lv_obj_set_flex_grow(label, 1);
    if (out_label) {
        *out_label = label;
    }

    lv_obj_t *chevron = lv_label_create(row);
    lv_label_set_text(chevron, ">");
    lv_obj_set_style_text_color(chevron, UI_COLOR_TEXT_SEC, 0);

    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, NULL);
    return row;
}

/* Create a row with a label (left) + lv_switch (right). */
static lv_obj_t *switch_row_create(lv_obj_t *parent, ui_i18n_key_t label_key,
                                   lv_obj_t **out_label, bool checked,
                                   lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, ui_adapt(DIAG_ROW_H));
    lv_obj_set_style_bg_color(row, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_set_style_pad_hor(row, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, ui_i18n_text(label_key));
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT, 0);
    if (out_label) {
        *out_label = label;
    }

    lv_obj_t *sw = lv_switch_create(row);
    if (checked) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_set_style_bg_color(sw, UI_COLOR_PRIMARY, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
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
    lv_obj_set_style_pad_top(s_screen, ui_adapt(DIAG_STATUSBAR_H), 0);

    /* Title bar: icon-only back (left), page title centered */
    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(DIAG_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_click_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);

    s_title = lv_label_create(titlebar);
    lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_APP_DIAG));
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    /* Content area — vertical list of diagnostic rows. */
    s_content = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_width(s_content, LV_PCT(100));
    lv_obj_set_flex_grow(s_content, 1);
    lv_obj_add_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_content, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_row(s_content, ui_adapt(UI_SPACE_SM), 0);

    /* --- 系统状态: 内存水位/运行时长/复位原因只读页 --- */
    nav_row_create(s_content, UI_TEXT_DIAG_SYS_STATUS, &s_sys_status_label,
                   sys_status_click_cb);

    /* --- 网络诊断: WiFi/IP/RSSI/云连接只读页 --- */
    nav_row_create(s_content, UI_TEXT_DIAG_NET_STATUS, &s_net_status_label,
                   net_status_click_cb);

    /* --- 屏幕测试: 纯色/彩条/触摸画线全屏测试页 --- */
    nav_row_create(s_content, UI_TEXT_DIAG_SCREEN_TEST, &s_screen_test_label,
                   screen_test_click_cb);

    /* --- Show-FPS toggle: memory-only LVGL render FPS/CPU overlay --- */
    switch_row_create(s_content, UI_TEXT_DIAG_SHOW_FPS, &s_show_fps_label,
                      ui_svc_dev_ctrl_fps_overlay_get(), show_fps_cb);

    /* --- 播放统计(AP-STAT): 仅当统计代码编入(AI_PLAYER_DEBUG_STATS=y)时显示;
     * 内存态默认关, 开启后播放期间每 2s 输出一行 [AP-STAT] 日志 --- */
    if (ui_svc_dev_ctrl_ap_stat_available()) {
        switch_row_create(s_content, UI_TEXT_DIAG_AP_STAT, &s_ap_stat_label,
                          ui_svc_dev_ctrl_ap_stat_get(), ap_stat_cb);
    }

    /* --- 音频诊断: 仅当 dump 功能编译可用时显示 --- */
    if (ui_svc_audio_diag_available()) {
        nav_row_create(s_content, UI_TEXT_DIAG_AUDIO_DIAG, &s_audio_diag_label,
                       audio_diag_click_cb);
    }
}

static void on_enter(uint32_t dirty)
{
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        refresh_lang();
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
        s_screen           = NULL;
        s_content          = NULL;
        s_title            = NULL;
        s_sys_status_label = NULL;
        s_net_status_label = NULL;
        s_screen_test_label = NULL;
        s_show_fps_label   = NULL;
        s_ap_stat_label    = NULL;
        s_audio_diag_label = NULL;
    }
}

/* ---------------------------------------------------------------------------
 * Refresh
 * -------------------------------------------------------------------------*/

/* Re-apply language-dependent texts (RULES: never cache ui_i18n_text() pointers
 * across a language switch). */
static void refresh_lang(void)
{
    if (s_title) {
        lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_APP_DIAG));
        lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);
    }
    if (s_sys_status_label) {
        lv_label_set_text(s_sys_status_label, ui_i18n_text(UI_TEXT_DIAG_SYS_STATUS));
    }
    if (s_net_status_label) {
        lv_label_set_text(s_net_status_label, ui_i18n_text(UI_TEXT_DIAG_NET_STATUS));
    }
    if (s_screen_test_label) {
        lv_label_set_text(s_screen_test_label, ui_i18n_text(UI_TEXT_DIAG_SCREEN_TEST));
    }
    if (s_show_fps_label) {
        lv_label_set_text(s_show_fps_label, ui_i18n_text(UI_TEXT_DIAG_SHOW_FPS));
    }
    if (s_ap_stat_label) {
        lv_label_set_text(s_ap_stat_label, ui_i18n_text(UI_TEXT_DIAG_AP_STAT));
    }
    if (s_audio_diag_label) {
        lv_label_set_text(s_audio_diag_label, ui_i18n_text(UI_TEXT_DIAG_AUDIO_DIAG));
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

/* Show-FPS toggle changed. Memory-only — drives the port-layer FPS/CPU overlay
 * via the dev-ctrl forwarder; no KV persistence (power-cycle resets to OFF). */
static void show_fps_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ui_svc_dev_ctrl_fps_overlay_set(on);
}

/* AP-STAT toggle changed. Memory-only — drives the audio player's runtime
 * stats switch via the dev-ctrl forwarder; no KV persistence. */
static void ap_stat_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ui_svc_dev_ctrl_ap_stat_set(on);
}

/* "音频诊断" row tapped — open the audio diagnostics sub-page. */
static void audio_diag_click_cb(lv_event_t *e)
{
    (void)e;
    ui_route_push(UI_PAGE_AUDIO_DIAG);
}

/* "系统状态" row tapped — open the live system-status sub-page. */
static void sys_status_click_cb(lv_event_t *e)
{
    (void)e;
    ui_route_push(UI_PAGE_SYS_STATUS);
}

/* "网络诊断" row tapped — open the live network-status sub-page. */
static void net_status_click_cb(lv_event_t *e)
{
    (void)e;
    ui_route_push(UI_PAGE_NET_STATUS);
}

/* "屏幕测试" row tapped — open the full-screen LCD/touch test page. */
static void screen_test_click_cb(lv_event_t *e)
{
    (void)e;
    ui_route_push(UI_PAGE_SCREEN_TEST);
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/
const ui_page_entry_t ui_page_diag_entry = {
    .id = UI_PAGE_DIAG,
    .name = "diag",
    .lifecycle = { on_create, on_enter, NULL, on_destroy }
};
