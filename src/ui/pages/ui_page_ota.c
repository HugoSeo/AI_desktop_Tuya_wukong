#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_bar.h"
#include "ui_comp_btn.h"
#include "ui_svc_devinfo.h"
#include "ui_svc_ota.h"
#include "ui_page_ids.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

LV_IMG_DECLARE(icon_back_24_24);

#define OTA_STATUSBAR_H  32
#define OTA_TITLEBAR_H   48

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_content = NULL;
static lv_obj_t *s_back = NULL;
static lv_obj_t *s_title = NULL;
static lv_obj_t *s_version = NULL;
static lv_obj_t *s_state_dot = NULL;
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_target_version = NULL;
static lv_obj_t *s_check_spinner = NULL;
static lv_obj_t *s_progress_percent = NULL;
static lv_obj_t *s_bar = NULL;
static lv_obj_t *s_info_card = NULL;
static lv_obj_t *s_current_label = NULL;
static lv_obj_t *s_current_value = NULL;
static lv_obj_t *s_latest_label = NULL;
static lv_obj_t *s_latest_value = NULL;
static lv_obj_t *s_size_label = NULL;
static lv_obj_t *s_size_value = NULL;
static lv_obj_t *s_desc_card = NULL;
static lv_obj_t *s_desc_title = NULL;
static lv_obj_t *s_desc_body = NULL;
static lv_obj_t *s_hint = NULL;
static lv_obj_t *s_action_row = NULL;
static lv_obj_t *s_check_btn = NULL;
static lv_obj_t *s_confirm_btn = NULL;
static bool s_spinner_running = false;
static bool s_render_valid = false;
static ui_svc_ota_state_t s_last_state = UI_SVC_OTA_IDLE;
static ui_svc_ota_check_state_t s_last_check_state = UI_SVC_OTA_CHECK_IDLE;
static ui_svc_ota_confirm_state_t s_last_confirm_state = UI_SVC_OTA_CONFIRM_IDLE;
static int s_last_percent = -1;

static void render(void);

static bool navigation_locked(void)
{
    ui_svc_ota_state_t state = ui_svc_ota_get_state();
    return state == UI_SVC_OTA_VERIFYING || state == UI_SVC_OTA_INSTALLING;
}

static void spinner_anim_set(void *obj, int32_t value)
{
    lv_arc_set_rotation((lv_obj_t *)obj, (uint16_t)value);
}

static void spinner_anim_stop(void)
{
    if (!s_spinner_running) {
        return;
    }
    lv_anim_del(s_check_spinner, spinner_anim_set);
    s_spinner_running = false;
}

static void spinner_anim_start(void)
{
    lv_anim_t anim;

    if (!s_check_spinner || s_spinner_running) {
        return;
    }

    lv_arc_set_rotation(s_check_spinner, 0);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_check_spinner);
    lv_anim_set_exec_cb(&anim, spinner_anim_set);
    lv_anim_set_values(&anim, 0, 359);
    lv_anim_set_time(&anim, 900);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    lv_anim_start(&anim);
    s_spinner_running = true;
}

static void set_visible(lv_obj_t *obj, bool visible)
{
    if (!obj) {
        return;
    }
    if (visible) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_action_layout(bool dual)
{
    lv_obj_set_width(s_check_btn, dual ? LV_PCT(48) : LV_PCT(68));
    set_visible(s_confirm_btn, dual);
}

static void set_status(ui_i18n_key_t key, lv_color_t color)
{
    lv_label_set_text(s_status, ui_i18n_text(key));
    lv_obj_set_style_text_color(s_status, color, 0);
    lv_obj_set_style_bg_color(s_state_dot, color, 0);
}

static const char *current_version(const ui_svc_ota_info_t *info)
{
    if (info != NULL && info->current_version[0] != '\0') {
        return info->current_version;
    }

    const char *version = ui_svc_devinfo_fw_version();
    return version != NULL ? version : "--";
}

static void format_file_size(char *text, size_t size, uint32_t bytes)
{
    if (bytes == 0) {
        snprintf(text, size, "--");
    } else if (bytes >= 1024u * 1024u) {
        uint32_t tenths = (uint32_t)(((uint64_t)bytes * 10u + 512u * 1024u) /
                                    (1024u * 1024u));
        snprintf(text, size, "%u.%u MB", (unsigned int)(tenths / 10u),
                 (unsigned int)(tenths % 10u));
    } else if (bytes >= 1024u) {
        uint32_t kb = (bytes + 512u) / 1024u;
        snprintf(text, size, "%u KB", (unsigned int)kb);
    } else {
        snprintf(text, size, "%u B", (unsigned int)bytes);
    }
}

static void show_progress(int percent, bool animated)
{
    char text[8];

    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }

    snprintf(text, sizeof(text), "%d%%", percent);
    lv_label_set_text(s_progress_percent, text);
    set_visible(s_progress_percent, true);
    set_visible(s_bar, true);
    if (animated) {
        ui_comp_bar_set_value_anim(s_bar, percent, 300);
    } else {
        ui_comp_bar_set_value(s_bar, percent);
    }
}

static void render_upgrade_state(ui_svc_ota_state_t state, int percent)
{
    switch (state) {
        case UI_SVC_OTA_PREPARING:
            set_status(UI_TEXT_OTA_PREPARING, UI_COLOR_PRIMARY);
            lv_label_set_text(s_hint, ui_i18n_text(UI_TEXT_OTA_KEEP_POWER));
            show_progress(0, false);
            break;

        case UI_SVC_OTA_DOWNLOADING:
            set_status(UI_TEXT_OTA_DOWNLOADING, UI_COLOR_PRIMARY);
            lv_label_set_text(s_hint, ui_i18n_text(UI_TEXT_OTA_KEEP_POWER));
            show_progress(percent, percent >= 0);
            break;

        case UI_SVC_OTA_VERIFYING:
            set_status(UI_TEXT_OTA_VERIFYING, UI_COLOR_WARNING);
            lv_label_set_text(s_hint, ui_i18n_text(UI_TEXT_OTA_KEEP_POWER));
            show_progress(percent >= 0 ? percent : 98, true);
            break;

        case UI_SVC_OTA_INSTALLING:
            set_status(UI_TEXT_OTA_INSTALLING, UI_COLOR_WARNING);
            lv_label_set_text(s_hint, ui_i18n_text(UI_TEXT_OTA_RESTARTING));
            show_progress(100, true);
            break;

        case UI_SVC_OTA_FAILED:
            set_status(UI_TEXT_OTA_FAILED, UI_COLOR_ERROR);
            lv_label_set_text(s_hint, ui_i18n_text(UI_TEXT_OTA_READY_HINT));
            ui_comp_btn_set_text(s_check_btn, ui_i18n_text(UI_TEXT_OTA_RETRY));
            set_action_layout(false);
            set_visible(s_action_row, true);
            break;

        case UI_SVC_OTA_IDLE:
        default:
            break;
    }
}

static void render_check_state(ui_svc_ota_check_state_t state,
                               ui_svc_ota_confirm_state_t confirm_state,
                               const ui_svc_ota_info_t *info)
{
    char size_text[24];
    const char *current = current_version(info);

    switch (state) {
        case UI_SVC_OTA_CHECKING:
            set_status(UI_TEXT_OTA_CHECKING, UI_COLOR_PRIMARY);
            set_visible(s_hint, false);
            set_visible(s_check_spinner, true);
            spinner_anim_start();
            break;

        case UI_SVC_OTA_CHECK_AVAILABLE:
            set_status(UI_TEXT_OTA_AVAILABLE, UI_COLOR_PRIMARY);
            lv_label_set_text(s_target_version,
                              info->version[0] != '\0' ? info->version : "--");
            lv_label_set_text(s_current_value, current);
            lv_label_set_text(s_latest_value,
                              info->version[0] != '\0' ? info->version : "--");
            format_file_size(size_text, sizeof(size_text), info->file_size);
            lv_label_set_text(s_size_value, size_text);
            ui_comp_btn_set_text(s_check_btn, ui_i18n_text(UI_TEXT_OTA_CHECK_AGAIN));
            set_visible(s_version, false);
            set_visible(s_target_version, true);
            set_visible(s_info_card, true);
            set_visible(s_hint, false);
            set_action_layout(true);
            set_visible(s_action_row, true);
            if (info->desc[0] != '\0') {
                lv_label_set_text(s_desc_body, info->desc);
                set_visible(s_desc_card, true);
            }
            if (confirm_state == UI_SVC_OTA_CONFIRMING ||
                confirm_state == UI_SVC_OTA_CONFIRM_ACCEPTED) {
                set_status(UI_TEXT_OTA_PREPARING, UI_COLOR_PRIMARY);
                ui_comp_btn_set_enabled(s_check_btn, false);
                ui_comp_btn_set_enabled(s_confirm_btn, false);
                show_progress(0, false);
            } else if (confirm_state == UI_SVC_OTA_CONFIRM_FAILED) {
                set_status(UI_TEXT_OTA_CONFIRM_FAILED, UI_COLOR_ERROR);
            }
            break;

        case UI_SVC_OTA_CHECK_UP_TO_DATE:
            set_status(UI_TEXT_OTA_UP_TO_DATE, UI_COLOR_SUCCESS);
            lv_label_set_text(s_target_version, current);
            lv_label_set_text(s_hint, ui_i18n_text(UI_TEXT_OTA_UP_TO_DATE_HINT));
            ui_comp_btn_set_text(s_check_btn, ui_i18n_text(UI_TEXT_OTA_CHECK_AGAIN));
            set_visible(s_version, false);
            set_visible(s_target_version, true);
            set_action_layout(false);
            set_visible(s_action_row, true);
            break;

        case UI_SVC_OTA_CHECK_FAILED:
            set_status(UI_TEXT_OTA_CHECK_FAILED, UI_COLOR_ERROR);
            lv_label_set_text(s_hint, ui_i18n_text(UI_TEXT_OTA_READY_HINT));
            ui_comp_btn_set_text(s_check_btn, ui_i18n_text(UI_TEXT_OTA_RETRY));
            set_action_layout(false);
            set_visible(s_action_row, true);
            break;

        case UI_SVC_OTA_CHECK_IDLE:
        default:
            set_status(UI_TEXT_OTA_CHECK, UI_COLOR_TEXT_SEC);
            lv_label_set_text(s_hint, ui_i18n_text(UI_TEXT_OTA_READY_HINT));
            ui_comp_btn_set_text(s_check_btn, ui_i18n_text(UI_TEXT_OTA_CHECK));
            set_action_layout(false);
            set_visible(s_action_row, true);
            break;
    }
}

static void render(void)
{
    ui_svc_ota_state_t state;
    ui_svc_ota_check_state_t check_state;
    ui_svc_ota_confirm_state_t confirm_state;
    const ui_svc_ota_info_t *info;
    int percent;
    char text[64];

    if (!s_screen) {
        return;
    }

    state = ui_svc_ota_get_state();
    check_state = ui_svc_ota_get_check_state();
    confirm_state = ui_svc_ota_get_confirm_state();
    percent = ui_svc_ota_get_percent();
    if (s_render_valid && state == s_last_state &&
        check_state == s_last_check_state &&
        confirm_state == s_last_confirm_state && percent == s_last_percent) {
        return;
    }
    s_render_valid = true;
    s_last_state = state;
    s_last_check_state = check_state;
    s_last_confirm_state = confirm_state;
    s_last_percent = percent;
    info = ui_svc_ota_get_info();

    lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_OTA_TITLE));
    lv_label_set_text(s_current_label, ui_i18n_text(UI_TEXT_OTA_CURRENT_LABEL));
    lv_label_set_text(s_latest_label, ui_i18n_text(UI_TEXT_OTA_LATEST_LABEL));
    lv_label_set_text(s_size_label, ui_i18n_text(UI_TEXT_OTA_SIZE_LABEL));
    lv_label_set_text(s_desc_title, ui_i18n_text(UI_TEXT_OTA_RELEASE_NOTES));
    ui_comp_btn_set_text(s_confirm_btn, ui_i18n_text(UI_TEXT_OTA_CONFIRM));
    snprintf(text, sizeof(text), ui_i18n_text(UI_TEXT_OTA_CURRENT_VERSION_FMT),
             current_version(info));
    lv_label_set_text(s_version, text);

    set_visible(s_back, !navigation_locked());
    set_visible(s_version, true);
    set_visible(s_target_version, false);
    set_visible(s_check_spinner, false);
    set_visible(s_progress_percent, false);
    set_visible(s_bar, false);
    set_visible(s_info_card, false);
    set_visible(s_desc_card, false);
    set_visible(s_hint, true);
    set_visible(s_action_row, false);
    ui_comp_btn_set_enabled(s_check_btn, true);
    ui_comp_btn_set_enabled(s_confirm_btn, true);
    spinner_anim_stop();

    if (state != UI_SVC_OTA_IDLE) {
        if (ui_svc_ota_is_upgrading() &&
            check_state == UI_SVC_OTA_CHECK_AVAILABLE) {
            ui_comp_btn_set_text(s_check_btn,
                                 ui_i18n_text(UI_TEXT_OTA_CHECK_AGAIN));
            set_action_layout(true);
            set_visible(s_action_row, true);
            ui_comp_btn_set_enabled(s_check_btn, false);
            ui_comp_btn_set_enabled(s_confirm_btn, false);
        }
        render_upgrade_state(state, percent);
    } else {
        render_check_state(check_state, confirm_state, info);
    }
}

static void ota_state_cb(bool upgrading)
{
    (void)upgrading;
    s_render_valid = false;
    render();
}

static void back_click_cb(lv_event_t *e)
{
    (void)e;
    if (!navigation_locked()) {
        ui_route_pop();
    }
}

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT &&
        !navigation_locked()) {
        lv_indev_wait_release(lv_indev_get_act());
        ui_route_pop();
    }
}

static void check_click_cb(lv_event_t *e)
{
    (void)e;
    if (ui_svc_ota_check_now() != 0) {
        s_render_valid = false;
        render();
    }
}

static void confirm_click_cb(lv_event_t *e)
{
    (void)e;
    if (ui_svc_ota_confirm_now() != 0) {
        s_render_valid = false;
        render();
    }
}

static lv_obj_t *info_row_create(lv_obj_t *parent, lv_obj_t **label_out)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    *label_out = lv_label_create(row);
    lv_obj_set_style_text_color(*label_out, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_flex_grow(*label_out, 1);

    lv_obj_t *value = lv_label_create(row);
    lv_obj_set_width(value, LV_PCT(48));
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(value, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    return value;
}

static void on_create(void *parent)
{
    lv_obj_t *state_row;

    (void)parent;

    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_set_style_pad_top(s_screen, ui_adapt(OTA_STATUSBAR_H), 0);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(OTA_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    s_back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_click_cb);
    lv_obj_align(s_back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);

    s_title = lv_label_create(titlebar);
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    s_content = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_width(s_content, LV_PCT(100));
    lv_obj_set_flex_grow(s_content, 1);
    lv_obj_set_style_pad_hor(s_content, ui_adapt(UI_SPACE_XL), 0);
    lv_obj_set_style_pad_top(s_content, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_set_style_pad_bottom(s_content, ui_adapt(UI_SPACE_LG), 0);
    lv_obj_set_style_pad_row(s_content, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_AUTO);

    s_version = lv_label_create(s_content);
    lv_obj_set_width(s_version, LV_PCT(100));
    lv_obj_set_style_text_align(s_version, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_version, UI_COLOR_TEXT_SEC, 0);

    state_row = lv_obj_create(s_content);
    lv_obj_remove_style_all(state_row);
    lv_obj_set_size(state_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(state_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(state_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(state_row, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_clear_flag(state_row, LV_OBJ_FLAG_SCROLLABLE);

    s_state_dot = lv_obj_create(state_row);
    lv_obj_remove_style_all(s_state_dot);
    lv_obj_set_size(s_state_dot, ui_adapt(12), ui_adapt(12));
    lv_obj_set_style_radius(s_state_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_state_dot, LV_OPA_COVER, 0);

    s_status = lv_label_create(state_row);

    s_target_version = lv_label_create(s_content);
    lv_obj_set_width(s_target_version, LV_PCT(100));
    lv_obj_set_style_text_align(s_target_version, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_target_version, UI_COLOR_PRIMARY, 0);

    s_check_spinner = lv_arc_create(s_content);
    lv_obj_set_size(s_check_spinner, ui_adapt(42), ui_adapt(42));
    lv_obj_remove_style(s_check_spinner, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_check_spinner, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_arc_set_bg_angles(s_check_spinner, 0, 360);
    lv_arc_set_angles(s_check_spinner, 0, 78);
    lv_obj_set_style_arc_width(s_check_spinner, ui_adapt(4), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_check_spinner, UI_COLOR_BG_CARD, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_check_spinner, ui_adapt(4), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_check_spinner, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_check_spinner, true, LV_PART_INDICATOR);

    s_info_card = lv_obj_create(s_content);
    lv_obj_set_width(s_info_card, LV_PCT(100));
    lv_obj_set_height(s_info_card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_info_card, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(s_info_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_info_card, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_set_style_border_width(s_info_card, 0, 0);
    lv_obj_set_style_pad_all(s_info_card, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_row(s_info_card, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_set_flex_flow(s_info_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_info_card, LV_OBJ_FLAG_SCROLLABLE);

    s_current_value = info_row_create(s_info_card, &s_current_label);
    s_latest_value = info_row_create(s_info_card, &s_latest_label);
    s_size_value = info_row_create(s_info_card, &s_size_label);

    s_desc_card = lv_obj_create(s_content);
    lv_obj_set_width(s_desc_card, LV_PCT(100));
    lv_obj_set_height(s_desc_card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_desc_card, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(s_desc_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_desc_card, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_set_style_border_width(s_desc_card, 0, 0);
    lv_obj_set_style_pad_all(s_desc_card, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_row(s_desc_card, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_set_flex_flow(s_desc_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(s_desc_card, LV_OBJ_FLAG_SCROLLABLE);

    s_desc_title = lv_label_create(s_desc_card);
    lv_obj_set_style_text_color(s_desc_title, UI_COLOR_TEXT, 0);
    s_desc_body = lv_label_create(s_desc_card);
    lv_obj_set_width(s_desc_body, LV_PCT(100));
    lv_label_set_long_mode(s_desc_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(s_desc_body, UI_COLOR_TEXT_SEC, 0);

    s_hint = lv_label_create(s_content);
    lv_obj_set_width(s_hint, LV_PCT(100));
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_hint, UI_COLOR_TEXT_SEC, 0);

    s_action_row = lv_obj_create(s_content);
    lv_obj_remove_style_all(s_action_row);
    lv_obj_set_size(s_action_row, LV_PCT(100), ui_adapt(40));
    lv_obj_set_flex_flow(s_action_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_action_row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_action_row, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_clear_flag(s_action_row, LV_OBJ_FLAG_SCROLLABLE);

    s_check_btn = ui_comp_btn_create_styled(s_action_row,
                                             ui_i18n_text(UI_TEXT_OTA_CHECK),
                                             UI_COMP_BTN_SECONDARY,
                                             check_click_cb);
    lv_obj_set_size(s_check_btn, LV_PCT(68), LV_PCT(100));
    lv_obj_set_flex_flow(s_check_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_check_btn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(s_check_btn, ui_adapt(20), 0);
    lv_obj_set_style_bg_color(s_check_btn, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_opa(s_check_btn, LV_OPA_10, 0);
    lv_obj_set_style_border_opa(s_check_btn, LV_OPA_70, 0);

    s_confirm_btn = ui_comp_btn_create_styled(s_action_row,
                                               ui_i18n_text(UI_TEXT_OTA_CONFIRM),
                                               UI_COMP_BTN_PRIMARY,
                                               confirm_click_cb);
    lv_obj_set_size(s_confirm_btn, LV_PCT(48), LV_PCT(100));
    lv_obj_set_flex_flow(s_confirm_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_confirm_btn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(s_confirm_btn, ui_adapt(20), 0);

    /* Keep the compact percentage and progress bar below the action row. */
    s_progress_percent = lv_label_create(s_content);
    lv_obj_set_width(s_progress_percent, LV_PCT(100));
    lv_label_set_text(s_progress_percent, "0%");
    lv_obj_set_style_text_align(s_progress_percent, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(s_progress_percent, UI_COLOR_TEXT_SEC, 0);

    s_bar = ui_comp_bar_create(s_content, 0, 100);

    s_render_valid = false;
    render();
}

static void on_enter(uint32_t dirty)
{
    if (ui_dirty_is_enter(dirty)) {
        ui_svc_ota_set_cb(ota_state_cb);
        s_render_valid = false;
        render();
    }
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        s_render_valid = false;
        render();
    }
}

static void on_leave(void)
{
    ui_svc_ota_set_cb(NULL);
    spinner_anim_stop();
}

static void on_destroy(void)
{
    ui_svc_ota_set_cb(NULL);
    spinner_anim_stop();
    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
    }
    s_screen = NULL;
    s_content = NULL;
    s_back = NULL;
    s_title = NULL;
    s_version = NULL;
    s_state_dot = NULL;
    s_status = NULL;
    s_target_version = NULL;
    s_check_spinner = NULL;
    s_progress_percent = NULL;
    s_bar = NULL;
    s_info_card = NULL;
    s_current_label = NULL;
    s_current_value = NULL;
    s_latest_label = NULL;
    s_latest_value = NULL;
    s_size_label = NULL;
    s_size_value = NULL;
    s_desc_card = NULL;
    s_desc_title = NULL;
    s_desc_body = NULL;
    s_hint = NULL;
    s_action_row = NULL;
    s_check_btn = NULL;
    s_confirm_btn = NULL;
    s_spinner_running = false;
    s_render_valid = false;
    s_last_state = UI_SVC_OTA_IDLE;
    s_last_check_state = UI_SVC_OTA_CHECK_IDLE;
    s_last_confirm_state = UI_SVC_OTA_CONFIRM_IDLE;
    s_last_percent = -1;
}

const ui_page_entry_t ui_page_ota_entry = {
    .id = UI_PAGE_OTA,
    .name = "ota",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
