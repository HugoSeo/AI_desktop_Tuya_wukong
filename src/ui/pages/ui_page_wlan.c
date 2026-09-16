#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_comp_btn.h"
#include "ui_comp_keyboard.h"
#include "ui_comp_popup.h"
#include "ui_comp_statusbar.h"
#include "ui_svc_wlan.h"
#include "ui_svc_activate.h"
#include "ui_state.h"
#include "ui_page_ids.h"

#define WLAN_TITLEBAR_H        48
#define WLAN_ITEM_H            52
#define WLAN_SECTION_GAP       12
#define WLAN_ITEM_BG           0xB8BDDE
#define WLAN_ITEM_BG_OPA       28
#define WLAN_CONNECT_TOAST_H   142
#define WLAN_CONNECT_GAP       8

LV_IMG_DECLARE(icon_back_24_24);

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_title = NULL;
static lv_obj_t *s_skip_btn = NULL;   /* first-boot wizard only (unactivated) */
static lv_obj_t *s_list = NULL;
static lv_obj_t *s_refresh_btn = NULL;
static lv_obj_t *s_connect_overlay = NULL;
static lv_obj_t *s_connect_ssid = NULL;
static lv_obj_t *s_password_input = NULL;
static lv_obj_t *s_password_toggle_btn = NULL;
static lv_obj_t *s_connect_btn = NULL;
static lv_obj_t *s_cancel_btn = NULL;
static lv_obj_t *s_keyboard = NULL;
static char s_selected_ssid[UI_SVC_WLAN_SSID_MAX_LEN + 1];
static bool s_password_visible = false;
static ui_svc_wlan_connect_state_t s_last_connect_state =
    UI_SVC_WLAN_CONNECT_IDLE;

static void render_list(void);
static void hide_connect_panel(void);

static void center_button_content(lv_obj_t *btn)
{
    if (!btn) {
        return;
    }
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
}

static void set_password_visible(bool visible)
{
    s_password_visible = visible;
    if (s_password_input) {
        lv_textarea_set_password_mode(s_password_input, !visible);
    }
    if (s_password_toggle_btn) {
        ui_comp_btn_set_text(
            s_password_toggle_btn,
            ui_i18n_text(visible ? UI_TEXT_WLAN_PASSWORD_HIDE :
                                   UI_TEXT_WLAN_PASSWORD_SHOW));
    }
}

static const char *connect_error_text(ui_svc_wlan_connect_error_t error)
{
    switch (error) {
        case UI_SVC_WLAN_CONNECT_ERR_PASSWORD:
            return ui_i18n_text(UI_TEXT_WLAN_PASSWORD_WRONG);
        case UI_SVC_WLAN_CONNECT_ERR_NOT_FOUND:
            return ui_i18n_text(UI_TEXT_WLAN_NOT_FOUND);
        case UI_SVC_WLAN_CONNECT_ERR_DHCP:
            return ui_i18n_text(UI_TEXT_WLAN_DHCP_FAILED);
        case UI_SVC_WLAN_CONNECT_ERR_TIMEOUT:
            return ui_i18n_text(UI_TEXT_WLAN_CONNECT_TIMEOUT);
        case UI_SVC_WLAN_CONNECT_ERR_REQUEST:
        case UI_SVC_WLAN_CONNECT_ERR_NONE:
        default:
            return ui_i18n_text(UI_TEXT_WLAN_CONNECT_FAILED);
    }
}

static void back_click_cb(lv_event_t *e)
{
    (void)e;
    if (s_connect_overlay &&
        !lv_obj_has_flag(s_connect_overlay, LV_OBJ_FLAG_HIDDEN)) {
        hide_connect_panel();
        return;
    }
    ui_route_pop();
}

static void skip_click_cb(lv_event_t *e)
{
    (void)e;
    ui_route_push(UI_PAGE_HOME);   /* unwind the activation wizard back to root */
}

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        if (s_connect_overlay &&
            !lv_obj_has_flag(s_connect_overlay, LV_OBJ_FLAG_HIDDEN)) {
            hide_connect_panel();
            return;
        }
        ui_route_pop();
    }
}

static lv_obj_t *create_section_label(const char *text)
{
    lv_obj_t *label = lv_label_create(s_list);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_text(label, text);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SEC, 0);
    return label;
}

static void refresh_click_cb(lv_event_t *e)
{
    (void)e;
    ui_svc_wlan_refresh();
}

static void hide_connect_panel(void)
{
    if (s_connect_overlay) {
        lv_obj_add_flag(s_connect_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_password_input) {
        set_password_visible(false);
        lv_textarea_set_text(s_password_input, "");
    }
    s_selected_ssid[0] = '\0';
}

static void password_toggle_cb(lv_event_t *e)
{
    (void)e;
    set_password_visible(!s_password_visible);
}

static void cancel_connect_cb(lv_event_t *e)
{
    (void)e;
    hide_connect_panel();
}

static void connect_click_cb(lv_event_t *e)
{
    (void)e;
    if (!s_password_input || s_selected_ssid[0] == '\0') {
        return;
    }

    const char *password = lv_textarea_get_text(s_password_input);
    if (ui_svc_wlan_connect(s_selected_ssid, password ? password : "")) {
        ui_comp_popup_show(UI_COMP_POPUP_LOADING,
                           ui_i18n_text(UI_TEXT_WLAN_CONNECTING),
                           s_selected_ssid, NULL);
        s_last_connect_state = UI_SVC_WLAN_CONNECTING;
        hide_connect_panel();
    } else {
        ui_comp_popup_toast(ui_i18n_text(UI_TEXT_WLAN_CONNECT_FAILED),
                            2000);
    }
}

static void show_connect_panel(const char *ssid)
{
    if (!ssid || ssid[0] == '\0' || !s_connect_overlay) {
        return;
    }

    strncpy(s_selected_ssid, ssid, sizeof(s_selected_ssid) - 1);
    s_selected_ssid[sizeof(s_selected_ssid) - 1] = '\0';
    lv_label_set_text(s_connect_ssid, s_selected_ssid);
    lv_textarea_set_text(s_password_input, "");
    set_password_visible(false);
    ui_comp_keyboard_set_uppercase(s_keyboard, false);
    lv_obj_clear_flag(s_connect_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_connect_overlay);
}

static void available_item_click_cb(lv_event_t *e)
{
    const char *ssid = (const char *)lv_event_get_user_data(e);
    show_connect_panel(ssid);
}

static void create_available_header(void)
{
    lv_obj_t *header = lv_obj_create(s_list);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), ui_adapt(32));
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(header);
    lv_label_set_text(label, ui_i18n_text(UI_TEXT_WLAN_AVAILABLE));
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SEC, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

    s_refresh_btn = ui_comp_btn_create_styled(header,
                                              ui_i18n_text(UI_TEXT_REFRESH),
                                              UI_COMP_BTN_TEXT,
                                              refresh_click_cb);
    lv_obj_set_size(s_refresh_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_refresh_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_left(s_refresh_btn, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_set_style_pad_right(s_refresh_btn, 0, 0);
    lv_obj_set_style_pad_ver(s_refresh_btn, ui_adapt(UI_SPACE_XS), 0);
    lv_obj_align(s_refresh_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    ui_comp_btn_set_enabled(s_refresh_btn, !ui_svc_wlan_is_scanning());
}

static lv_obj_t *create_wlan_item(const char *ssid, bool connected)
{
    lv_obj_t *item = lv_obj_create(s_list);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, LV_PCT(100), ui_adapt(WLAN_ITEM_H));
    lv_obj_set_style_bg_color(item, lv_color_hex(WLAN_ITEM_BG), 0);
    lv_obj_set_style_bg_opa(item, connected ? LV_OPA_30 : WLAN_ITEM_BG_OPA, 0);
    lv_obj_set_style_radius(item, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_set_style_border_width(item, connected ? ui_adapt(1) : 0, 0);
    lv_obj_set_style_border_color(item, UI_COLOR_SUCCESS, 0);
    lv_obj_set_style_pad_hor(item, ui_adapt(UI_SPACE_LG), 0);
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    if (!connected) {
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(item, LV_OPA_40, LV_STATE_PRESSED);
        lv_obj_add_event_cb(item, available_item_click_cb,
                            LV_EVENT_CLICKED, (void *)ssid);
    }

    lv_obj_t *label = lv_label_create(item);
    lv_obj_set_width(label, ui_adapt_screen_w() - ui_adapt(64));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, ssid);
    lv_obj_set_style_text_color(label,
                                connected ? UI_COLOR_SUCCESS : UI_COLOR_TEXT,
                                0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    return item;
}

static void create_connect_panel(void)
{
    s_connect_overlay = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_connect_overlay);
    lv_obj_set_size(s_connect_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_connect_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_connect_overlay, LV_OPA_40, 0);
    lv_obj_clear_flag(s_connect_overlay,
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(s_connect_overlay,
                    LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_CLICKABLE |
                    LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *toast = lv_obj_create(s_connect_overlay);
    lv_obj_remove_style_all(toast);
    lv_obj_set_size(toast,
                    ui_adapt_screen_w() - ui_adapt(UI_SPACE_LG),
                    ui_adapt(WLAN_CONNECT_TOAST_H));
    lv_obj_set_style_bg_color(toast, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(toast, ui_adapt(UI_RADIUS_LG), 0);
    lv_obj_set_style_pad_all(toast, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_row(toast, ui_adapt(6), 0);
    lv_obj_set_flex_flow(toast, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(toast,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0,
                 -ui_adapt(UI_COMP_KEYBOARD_HEIGHT + WLAN_CONNECT_GAP));

    s_connect_ssid = lv_label_create(toast);
    lv_obj_set_width(s_connect_ssid, LV_PCT(100));
    lv_label_set_long_mode(s_connect_ssid, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(s_connect_ssid, UI_COLOR_TEXT, 0);

    lv_obj_t *password_row = lv_obj_create(toast);
    lv_obj_remove_style_all(password_row);
    lv_obj_set_size(password_row, LV_PCT(100), ui_adapt(38));
    lv_obj_set_flex_flow(password_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(password_row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(password_row, ui_adapt(UI_SPACE_XS), 0);
    lv_obj_clear_flag(password_row, LV_OBJ_FLAG_SCROLLABLE);

    s_password_input = lv_textarea_create(password_row);
    lv_obj_set_size(s_password_input, 0, LV_PCT(100));
    lv_obj_set_flex_grow(s_password_input, 1);
    lv_textarea_set_one_line(s_password_input, true);
    lv_textarea_set_password_mode(s_password_input, true);
    lv_textarea_set_password_bullet(s_password_input, "*");
    lv_textarea_set_max_length(s_password_input,
                               UI_SVC_WLAN_PASSWORD_MAX_LEN);
    lv_textarea_set_placeholder_text(s_password_input,
                                     ui_i18n_text(UI_TEXT_WLAN_PASSWORD_HINT));
    lv_obj_set_style_bg_color(s_password_input, UI_COLOR_STATUSBAR_BG, 0);
    lv_obj_set_style_bg_opa(s_password_input, LV_OPA_20, 0);
    lv_obj_set_style_border_color(s_password_input, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_width(s_password_input, ui_adapt(1), 0);
    lv_obj_set_style_radius(s_password_input, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_set_style_text_color(s_password_input, UI_COLOR_TEXT, 0);
    lv_obj_set_style_pad_hor(s_password_input, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_set_style_pad_ver(s_password_input, ui_adapt(8), 0);

    s_password_toggle_btn = ui_comp_btn_create_styled(
        password_row,
        ui_i18n_text(UI_TEXT_WLAN_PASSWORD_SHOW),
        UI_COMP_BTN_TEXT,
        password_toggle_cb);
    lv_obj_set_size(s_password_toggle_btn, ui_adapt(58), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_password_toggle_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(s_password_toggle_btn, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_pad_all(s_password_toggle_btn, 0, 0);
    center_button_content(s_password_toggle_btn);

    lv_obj_t *button_row = lv_obj_create(toast);
    lv_obj_remove_style_all(button_row);
    lv_obj_set_size(button_row, LV_PCT(100), ui_adapt(36));
    lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button_row,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(button_row, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_clear_flag(button_row, LV_OBJ_FLAG_SCROLLABLE);

    s_cancel_btn = ui_comp_btn_create_styled(button_row,
                                              ui_i18n_text(UI_TEXT_CANCEL),
                                              UI_COMP_BTN_SECONDARY,
                                              cancel_connect_cb);
    lv_obj_set_height(s_cancel_btn, LV_PCT(100));
    lv_obj_set_flex_grow(s_cancel_btn, 1);
    center_button_content(s_cancel_btn);

    s_connect_btn = ui_comp_btn_create_styled(button_row,
                                               ui_i18n_text(UI_TEXT_WLAN_CONNECT),
                                               UI_COMP_BTN_PRIMARY,
                                               connect_click_cb);
    lv_obj_set_height(s_connect_btn, LV_PCT(100));
    lv_obj_set_flex_grow(s_connect_btn, 1);
    center_button_content(s_connect_btn);

    const ui_comp_keyboard_props_t keyboard_props = {
        .textarea = s_password_input,
        .uppercase = false,
    };
    s_keyboard = ui_comp_keyboard_create(s_connect_overlay, &keyboard_props);
    if (s_keyboard) {
        lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
}

static void create_status_text(const char *text)
{
    lv_obj_t *label = lv_label_create(s_list);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_pad_ver(label, ui_adapt(UI_SPACE_LG), 0);
}

static void render_list(void)
{
    if (!s_list) {
        return;
    }

    s_refresh_btn = NULL;
    lv_obj_clean(s_list);

    const ui_svc_wlan_result_t *result = ui_svc_wlan_get();
    create_section_label(ui_i18n_text(UI_TEXT_WLAN_CONNECTED));
    if (result->connected && result->connected_ssid[0] != '\0') {
        create_wlan_item(result->connected_ssid, true);
    } else {
        create_status_text(ui_i18n_text(UI_TEXT_WLAN_NOT_CONNECTED));
    }

    lv_obj_t *spacer = lv_obj_create(s_list);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, LV_PCT(100), ui_adapt(WLAN_SECTION_GAP));
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    create_available_header();
    if (ui_svc_wlan_is_scanning()) {
        create_status_text(ui_i18n_text(UI_TEXT_WLAN_SCANNING));
        return;
    }
    if (!result->scan_ok) {
        create_status_text(ui_i18n_text(UI_TEXT_WLAN_SCAN_FAILED));
        return;
    }
    if (result->ap_count == 0) {
        create_status_text(ui_i18n_text(UI_TEXT_WLAN_EMPTY));
        return;
    }

    for (uint8_t i = 0; i < result->ap_count; i++) {
        create_wlan_item(result->aps[i].ssid, false);
    }
}

static void wlan_result_cb(const ui_svc_wlan_result_t *result)
{
    if (!s_screen || !result || ui_route_current() != UI_PAGE_WLAN) {
        return;
    }

    if (result->connect_state != s_last_connect_state) {
        s_last_connect_state = result->connect_state;
        if (result->connect_state == UI_SVC_WLAN_CONNECTING) {
            ui_comp_popup_show(UI_COMP_POPUP_LOADING,
                               ui_i18n_text(UI_TEXT_WLAN_CONNECTING),
                               result->connecting_ssid, NULL);
        } else if (result->connect_state == UI_SVC_WLAN_CONNECT_SUCCESS) {
            ui_comp_popup_dismiss();
            if (!ui_svc_activate_is_activated()) {
                /* Manual activation flow: reveal the covered single-page
                 * wizard. Its page-local pending flag advances to the QR step
                 * after it observes this successful WLAN result. Navigate
                 * first because on_leave dismisses the current popup. */
                ui_route_pop();
                ui_comp_popup_toast(
                    ui_i18n_text(UI_TEXT_WLAN_CONNECT_SUCCESS), 2000);
                return;
            }
            ui_comp_popup_toast(
                ui_i18n_text(UI_TEXT_WLAN_CONNECT_SUCCESS), 2000);
        } else if (result->connect_state == UI_SVC_WLAN_CONNECT_FAILED) {
            ui_comp_popup_dismiss();
            ui_comp_popup_toast(connect_error_text(result->connect_error),
                                2500);
        }
    }

    render_list();
}

static void on_create(void *parent)
{
    (void)parent;

    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    /* WLAN is intentionally immersive: its title bar begins at y=0 and the
     * global statusbar is hidden while the page is foreground. */
    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(WLAN_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_click_cb);
    lv_obj_set_size(back,
                    ui_adapt(WLAN_TITLEBAR_H),
                    ui_adapt(WLAN_TITLEBAR_H));
    center_button_content(back);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(UI_SPACE_SM), 0);

    s_title = lv_label_create(titlebar);
    lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_WLAN_TITLE));
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    /* Part of the first-boot activation wizard: offer a way straight home.
     * Normal (activated) visits from the pulldown don't get the button. */
    if (!ui_svc_activate_is_activated()) {
        s_skip_btn = ui_comp_btn_create_styled(titlebar,
                                               ui_i18n_text(UI_TEXT_BOOT_SKIP),
                                               UI_COMP_BTN_TEXT,
                                               skip_click_cb);
        lv_obj_set_size(s_skip_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(s_skip_btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(s_skip_btn, UI_COLOR_TEXT_SEC, 0);
        lv_obj_set_style_pad_hor(s_skip_btn, ui_adapt(UI_SPACE_SM), 0);
        lv_obj_set_style_pad_ver(s_skip_btn, ui_adapt(UI_SPACE_XS), 0);
        lv_obj_align(s_skip_btn, LV_ALIGN_RIGHT_MID, -ui_adapt(UI_SPACE_SM), 0);
    }

    s_list = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_layout(s_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(s_list, ui_adapt(UI_SPACE_LG), 0);
    lv_obj_set_style_pad_top(s_list, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_set_style_pad_bottom(s_list, ui_adapt(UI_SPACE_LG), 0);
    lv_obj_set_style_pad_row(s_list, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    create_connect_panel();

    lv_obj_update_layout(s_screen);
}

static void on_enter(uint32_t dirty)
{
    if (ui_dirty_is_enter(dirty)) {
        ui_comp_statusbar_set_visible(false);
        ui_svc_wlan_set_cb(wlan_result_cb);
        const ui_svc_wlan_result_t *result = ui_svc_wlan_get();
        s_last_connect_state = result->connect_state;
        if (result->connect_state == UI_SVC_WLAN_CONNECTING) {
            ui_comp_popup_show(UI_COMP_POPUP_LOADING,
                               ui_i18n_text(UI_TEXT_WLAN_CONNECTING),
                               result->connecting_ssid, NULL);
        }
        ui_svc_wlan_refresh();
    }

    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        if (s_title) {
            lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_WLAN_TITLE));
        }
        if (s_skip_btn) {
            ui_comp_btn_set_text(s_skip_btn, ui_i18n_text(UI_TEXT_BOOT_SKIP));
        }
        if (s_password_input) {
            lv_textarea_set_placeholder_text(s_password_input,
                                             ui_i18n_text(UI_TEXT_WLAN_PASSWORD_HINT));
        }
        if (s_password_toggle_btn) {
            set_password_visible(s_password_visible);
        }
        if (s_connect_btn) {
            ui_comp_btn_set_text(s_connect_btn,
                                 ui_i18n_text(UI_TEXT_WLAN_CONNECT));
        }
        if (s_cancel_btn) {
            ui_comp_btn_set_text(s_cancel_btn, ui_i18n_text(UI_TEXT_CANCEL));
        }
        if (s_keyboard) {
            ui_comp_keyboard_refresh(s_keyboard);
        }
        render_list();
    }
}

static void on_leave(void)
{
    hide_connect_panel();
    ui_comp_popup_dismiss();
    ui_svc_wlan_set_cb(NULL);
    ui_comp_statusbar_set_visible(true);
}

static void on_destroy(void)
{
    ui_svc_wlan_set_cb(NULL);
    ui_comp_statusbar_set_visible(true);

    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
    }
    s_screen = NULL;
    s_title = NULL;
    s_skip_btn = NULL;
    s_list = NULL;
    s_refresh_btn = NULL;
    s_connect_overlay = NULL;
    s_connect_ssid = NULL;
    s_password_input = NULL;
    s_password_toggle_btn = NULL;
    s_connect_btn = NULL;
    s_cancel_btn = NULL;
    s_keyboard = NULL;
    s_selected_ssid[0] = '\0';
    s_password_visible = false;
    s_last_connect_state = UI_SVC_WLAN_CONNECT_IDLE;
}

const ui_page_entry_t ui_page_wlan_entry = {
    .id = UI_PAGE_WLAN,
    .name = "wlan",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
