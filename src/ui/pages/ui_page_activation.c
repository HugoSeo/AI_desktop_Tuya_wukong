#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_comp_popup.h"
#include "ui_svc_activate.h"
#include "ui_svc_dev_ctrl.h"
#include "ui_svc_wlan.h"
#include "ui_page_ids.h"
#include "uni_log.h"
#include <string.h>

LV_IMG_DECLARE(icon_back_24_24);

/* One page owns the complete first-boot activation wizard. Language, mode and
 * QR are page-local steps; WLAN remains a covered sub-page for credential
 * entry and pops back here after a successful manual connection. */
#define ACTIVATION_STATUSBAR_H  32
#define ACTIVATION_TITLEBAR_H   48
#define ACTIVATION_PROGRESS_H   20
#define ACTIVATION_LANG_ROW_H   64
#define ACTIVATION_OPTION_ROW_H 84
#define ACTIVATION_PRIMARY_H    48
#define ACTIVATION_QR_SIZE      184
#define ACTIVATION_QR_CARD_PAD  12

typedef enum {
    ACTIVATION_STEP_LANGUAGE = 0,
    ACTIVATION_STEP_MODE,
    ACTIVATION_STEP_QR,
    ACTIVATION_STEP_MAX
} activation_step_t;

typedef enum {
    ACTIVATION_OPTION_APP = 0,
    ACTIVATION_OPTION_MANUAL,
    ACTIVATION_OPTION_MAX
} activation_option_t;

typedef struct {
    ui_i18n_key_t title_key;
    ui_i18n_key_t desc_key;
} activation_option_def_t;

static const ui_i18n_key_t s_step_title_keys[ACTIVATION_STEP_MAX] = {
    [ACTIVATION_STEP_LANGUAGE] = UI_TEXT_BOOT_LANG_TITLE,
    [ACTIVATION_STEP_MODE]     = UI_TEXT_ACTIVATE_TITLE,
    [ACTIVATION_STEP_QR]       = UI_TEXT_QRCODE_TITLE,
};

/* Row index maps 1:1 to ui_lang_t. Both labels are native names in every
 * language table, so the choices remain recognizable while switching. */
static const ui_i18n_key_t s_lang_keys[UI_LANG_MAX] = {
    [UI_LANG_ZH_CN] = UI_TEXT_LANG_ZH,
    [UI_LANG_EN]    = UI_TEXT_LANG_EN,
};

static const activation_option_def_t s_options[ACTIVATION_OPTION_MAX] = {
    [ACTIVATION_OPTION_APP] = {
        UI_TEXT_ACTIVATE_BY_APP, UI_TEXT_ACTIVATE_BY_APP_DESC
    },
    [ACTIVATION_OPTION_MANUAL] = {
        UI_TEXT_ACTIVATE_MANUAL, UI_TEXT_ACTIVATE_MANUAL_DESC
    },
};

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_title = NULL;
static lv_obj_t *s_skip_btn = NULL;
static lv_obj_t *s_step_views[ACTIVATION_STEP_MAX];
static lv_obj_t *s_progress_steps[ACTIVATION_STEP_MAX];

static lv_obj_t *s_lang_rows[UI_LANG_MAX];
static lv_obj_t *s_lang_labels[UI_LANG_MAX];
static lv_obj_t *s_lang_marks[UI_LANG_MAX];
static lv_obj_t *s_lang_mark_dots[UI_LANG_MAX];
static lv_obj_t *s_continue_btn = NULL;

static lv_obj_t *s_mode_hint = NULL;
static lv_obj_t *s_option_rows[ACTIVATION_OPTION_MAX];
static lv_obj_t *s_option_titles[ACTIVATION_OPTION_MAX];
static lv_obj_t *s_option_descs[ACTIVATION_OPTION_MAX];
static lv_obj_t *s_option_marks[ACTIVATION_OPTION_MAX];
static lv_obj_t *s_option_mark_dots[ACTIVATION_OPTION_MAX];
static lv_obj_t *s_mode_continue_btn = NULL;

static lv_obj_t *s_qr_card = NULL;
static lv_obj_t *s_qr = NULL;
static lv_obj_t *s_qr_placeholder = NULL;
static lv_obj_t *s_qr_hint = NULL;

static activation_step_t s_step = ACTIVATION_STEP_MAX;
static uint8_t s_rendered_lang = UI_LANG_MAX;
static activation_option_t s_selected_option = ACTIVATION_OPTION_APP;
static activation_option_t s_rendered_option = ACTIVATION_OPTION_MAX;
static bool s_manual_flow_pending = false;
static bool s_qr_ready = false;
static char s_rendered_url[UI_SVC_ACTIVATE_URL_MAX_LEN + 1];

static void refresh_qr(void);
static void activate_cb(ui_svc_activate_event_t event);
static void go_home_async_cb(void *data);
static void refresh_option_selection(void);

static lv_obj_t *create_step_view(lv_obj_t *parent)
{
    lv_obj_t *view = lv_obj_create(parent);
    lv_obj_remove_style_all(view);
    lv_obj_set_width(view, LV_PCT(100));
    lv_obj_set_flex_grow(view, 1);
    lv_obj_clear_flag(view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(view, LV_OBJ_FLAG_HIDDEN);
    return view;
}

static void refresh_progress(void)
{
    if (s_step >= ACTIVATION_STEP_MAX) {
        return;
    }

    for (uint8_t i = 0; i < ACTIVATION_STEP_MAX; i++) {
        lv_obj_t *step = s_progress_steps[i];
        if (!step) {
            continue;
        }
        bool current = i == (uint8_t)s_step;
        bool complete = i < (uint8_t)s_step;
        lv_obj_set_size(step,
                        ui_adapt(current ? UI_SPACE_XL : UI_SPACE_SM),
                        ui_adapt(UI_SPACE_SM));
        lv_obj_set_style_bg_color(step,
                                  current || complete ? UI_COLOR_PRIMARY :
                                                        UI_COLOR_TEXT_SEC,
                                  0);
        lv_obj_set_style_bg_opa(step,
                                current ? LV_OPA_COVER :
                                (complete ? LV_OPA_50 : LV_OPA_30),
                                0);
    }
}

static void set_step(activation_step_t step)
{
    if (step >= ACTIVATION_STEP_MAX || step == s_step) {
        return;
    }

    if (s_step == ACTIVATION_STEP_QR) {
        ui_svc_activate_set_cb(NULL);
    }

    s_step = step;
    for (uint8_t i = 0; i < ACTIVATION_STEP_MAX; i++) {
        if (!s_step_views[i]) {
            continue;
        }
        if (i == (uint8_t)step) {
            lv_obj_clear_flag(s_step_views[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_step_views[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_title) {
        lv_label_set_text(s_title, ui_i18n_text(s_step_title_keys[step]));
    }
    refresh_progress();

    if (step == ACTIVATION_STEP_QR &&
        ui_route_current() == UI_PAGE_ACTIVATION) {
        if (ui_svc_activate_is_complete()) {
            lv_async_call(go_home_async_cb, NULL);
            return;
        }
        ui_svc_activate_set_cb(activate_cb);
        refresh_qr();
    }
}

static void lang_row_set_selected(uint8_t idx, bool selected)
{
    lv_obj_t *row = s_lang_rows[idx];
    lv_obj_t *label = s_lang_labels[idx];
    lv_obj_t *mark = s_lang_marks[idx];
    lv_obj_t *dot = s_lang_mark_dots[idx];
    if (!row || !label || !mark || !dot) {
        return;
    }

    if (selected) {
        lv_obj_set_style_bg_color(row, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_20, 0);
        lv_obj_set_style_border_color(row, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(label, UI_COLOR_TEXT, 0);
        lv_obj_set_style_bg_color(mark, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(mark, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_border_opa(mark, LV_OPA_COVER, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_style_bg_color(row, UI_COLOR_BG_CARD, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(row, UI_COLOR_TEXT_SEC, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_20, 0);
        lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SEC, 0);
        lv_obj_set_style_bg_opa(mark, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(mark, UI_COLOR_TEXT_SEC, 0);
        lv_obj_set_style_border_opa(mark, LV_OPA_50, 0);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_language_selection(void)
{
    uint8_t current = ui_svc_dev_ctrl_language_get();
    if (current >= UI_LANG_MAX || current == s_rendered_lang) {
        return;
    }
    for (uint8_t i = 0; i < UI_LANG_MAX; i++) {
        lang_row_set_selected(i, i == current);
    }
    s_rendered_lang = current;
}

static void option_row_set_selected(uint8_t idx, bool selected)
{
    lv_obj_t *row = s_option_rows[idx];
    lv_obj_t *title = s_option_titles[idx];
    lv_obj_t *desc = s_option_descs[idx];
    lv_obj_t *mark = s_option_marks[idx];
    lv_obj_t *dot = s_option_mark_dots[idx];
    if (!row || !title || !desc || !mark || !dot) {
        return;
    }

    if (selected) {
        lv_obj_set_style_bg_color(row, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_20, 0);
        lv_obj_set_style_border_color(row, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_color(desc, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_opa(desc, LV_OPA_70, 0);
        lv_obj_set_style_bg_color(mark, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(mark, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_border_opa(mark, LV_OPA_COVER, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_style_bg_color(row, UI_COLOR_BG_CARD, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(row, UI_COLOR_TEXT_SEC, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_20, 0);
        lv_obj_set_style_text_color(title, UI_COLOR_TEXT_SEC, 0);
        lv_obj_set_style_text_color(desc, UI_COLOR_TEXT_SEC, 0);
        lv_obj_set_style_text_opa(desc, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(mark, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(mark, UI_COLOR_TEXT_SEC, 0);
        lv_obj_set_style_border_opa(mark, LV_OPA_50, 0);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_option_selection(void)
{
    if (s_selected_option >= ACTIVATION_OPTION_MAX ||
        s_selected_option == s_rendered_option) {
        return;
    }
    for (uint8_t i = 0; i < ACTIVATION_OPTION_MAX; i++) {
        option_row_set_selected(i, i == (uint8_t)s_selected_option);
    }
    s_rendered_option = s_selected_option;
}

static void set_qr_ready(bool ready)
{
    if (!s_qr_card || !s_qr || !s_qr_placeholder || ready == s_qr_ready) {
        return;
    }

    s_qr_ready = ready;
    if (ready) {
        lv_obj_set_style_bg_color(s_qr_card, lv_color_white(), 0);
        lv_obj_set_style_border_opa(s_qr_card, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(s_qr_placeholder, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_qr, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_style_bg_color(s_qr_card, UI_COLOR_BG_CARD, 0);
        lv_obj_set_style_border_opa(s_qr_card, LV_OPA_30, 0);
        lv_obj_add_flag(s_qr, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_qr_placeholder, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_qr(void)
{
    if (!s_qr_card || !s_qr || !s_qr_hint) {
        return;
    }

    const char *url = ui_svc_activate_shorturl();
    if (url[0] == '\0') {
        set_qr_ready(false);
        lv_label_set_text(s_qr_hint, ui_i18n_text(UI_TEXT_QRCODE_WAITING));
        s_rendered_url[0] = '\0';
        return;
    }

    if (strcmp(url, s_rendered_url) != 0) {
        if (lv_qrcode_update(s_qr, url, strlen(url)) != LV_RES_OK) {
            PR_WARN("qrcode update failed, url len=%u", (unsigned)strlen(url));
            s_rendered_url[0] = '\0';
            set_qr_ready(false);
            lv_label_set_text(s_qr_hint,
                              ui_i18n_text(UI_TEXT_QRCODE_WAITING));
            return;
        }
        strncpy(s_rendered_url, url, sizeof(s_rendered_url) - 1);
        s_rendered_url[sizeof(s_rendered_url) - 1] = '\0';
    }

    set_qr_ready(true);
    lv_label_set_text(s_qr_hint, ui_i18n_text(UI_TEXT_QRCODE_HINT));
}

static void refresh_texts(void)
{
    if (s_title && s_step < ACTIVATION_STEP_MAX) {
        lv_label_set_text(s_title, ui_i18n_text(s_step_title_keys[s_step]));
    }
    if (s_skip_btn) {
        ui_comp_btn_set_text(s_skip_btn, ui_i18n_text(UI_TEXT_BOOT_SKIP));
    }
    for (uint8_t i = 0; i < UI_LANG_MAX; i++) {
        if (s_lang_labels[i]) {
            lv_label_set_text(s_lang_labels[i], ui_i18n_text(s_lang_keys[i]));
        }
    }
    if (s_continue_btn) {
        ui_comp_btn_set_text(s_continue_btn,
                             ui_i18n_text(UI_TEXT_BOOT_LANG_CONTINUE));
    }
    if (s_mode_hint) {
        lv_label_set_text(s_mode_hint, ui_i18n_text(UI_TEXT_ACTIVATE_HINT));
    }
    for (uint8_t i = 0; i < ACTIVATION_OPTION_MAX; i++) {
        if (s_option_titles[i]) {
            lv_label_set_text(s_option_titles[i],
                              ui_i18n_text(s_options[i].title_key));
        }
        if (s_option_descs[i]) {
            lv_label_set_text(s_option_descs[i],
                              ui_i18n_text(s_options[i].desc_key));
        }
    }
    if (s_mode_continue_btn) {
        ui_comp_btn_set_text(s_mode_continue_btn,
                             ui_i18n_text(UI_TEXT_BOOT_LANG_CONTINUE));
    }
    if (s_step == ACTIVATION_STEP_QR) {
        refresh_qr();
    }
}

static void go_home_activated(void)
{
    ui_comp_popup_toast(ui_i18n_text(UI_TEXT_QRCODE_ACTIVATED), 2500);
    ui_route_push(UI_PAGE_HOME);
}

static void go_home_async_cb(void *data)
{
    (void)data;
    if (ui_route_current() == UI_PAGE_ACTIVATION &&
        ui_svc_activate_is_complete()) {
        go_home_activated();
    }
}

static void activate_cb(ui_svc_activate_event_t event)
{
    if (!s_screen || s_step != ACTIVATION_STEP_QR ||
        ui_route_current() != UI_PAGE_ACTIVATION) {
        return;
    }
    if (event == UI_SVC_ACTIVATE_EVT_ACTIVATED) {
        go_home_activated();
        return;
    }
    refresh_qr();
}

static void navigate_back(void)
{
    if (s_step > ACTIVATION_STEP_LANGUAGE &&
        s_step < ACTIVATION_STEP_MAX) {
        set_step((activation_step_t)(s_step - 1));
        return;
    }
    ui_route_pop();
}

static void back_click_cb(lv_event_t *e)
{
    (void)e;
    navigate_back();
}

static void skip_click_cb(lv_event_t *e)
{
    (void)e;
    ui_route_push(UI_PAGE_HOME);
}

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        navigate_back();
    }
}

static void lang_row_cb(lv_event_t *e)
{
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (idx >= UI_LANG_MAX || idx == ui_svc_dev_ctrl_language_get()) {
        return;
    }
    ui_svc_dev_ctrl_language_set(idx);
    refresh_language_selection();
}

static void continue_click_cb(lv_event_t *e)
{
    (void)e;
    ui_svc_dev_ctrl_save();
    set_step(ACTIVATION_STEP_MODE);
}

static void option_row_cb(lv_event_t *e)
{
    activation_option_t option =
        (activation_option_t)(uintptr_t)lv_event_get_user_data(e);
    if (option >= ACTIVATION_OPTION_MAX || option == s_selected_option) {
        return;
    }
    s_selected_option = option;
    refresh_option_selection();
}

static void mode_continue_click_cb(lv_event_t *e)
{
    (void)e;
    activation_option_t option = s_selected_option;
    if (option == ACTIVATION_OPTION_APP) {
        ui_route_push(UI_PAGE_HOME);
        return;
    }
    if (option != ACTIVATION_OPTION_MANUAL) {
        return;
    }

    const ui_svc_wlan_result_t *wlan = ui_svc_wlan_get();
    if (wlan && wlan->connected) {
        set_step(ACTIVATION_STEP_QR);
        return;
    }

    s_manual_flow_pending = true;
    ui_route_push(UI_PAGE_WLAN);
}

static void create_progress(lv_obj_t *parent)
{
    lv_obj_t *indicator = lv_obj_create(parent);
    lv_obj_remove_style_all(indicator);
    lv_obj_set_size(indicator, LV_PCT(100), ui_adapt(ACTIVATION_PROGRESS_H));
    lv_obj_set_flex_flow(indicator, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(indicator,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(indicator, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_clear_flag(indicator, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = 0; i < ACTIVATION_STEP_MAX; i++) {
        lv_obj_t *step = lv_obj_create(indicator);
        lv_obj_remove_style_all(step);
        lv_obj_set_size(step, ui_adapt(UI_SPACE_SM), ui_adapt(UI_SPACE_SM));
        lv_obj_set_style_radius(step, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(step, UI_COLOR_TEXT_SEC, 0);
        lv_obj_set_style_bg_opa(step, LV_OPA_30, 0);
        lv_obj_clear_flag(step, LV_OBJ_FLAG_SCROLLABLE);
        s_progress_steps[i] = step;
    }
}

static void create_language_row(lv_obj_t *parent, uint8_t idx)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), ui_adapt(ACTIVATION_LANG_ROW_H));
    lv_obj_set_style_radius(row, ui_adapt(UI_RADIUS_LG), 0);
    lv_obj_set_style_border_width(row, ui_adapt(2), 0);
    lv_obj_set_style_bg_color(row, UI_COLOR_PRIMARY_DARK, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(row, ui_adapt(UI_SPACE_LG), 0);
    lv_obj_set_style_pad_ver(row, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_set_style_pad_column(row, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, lang_row_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)idx);
    s_lang_rows[idx] = row;

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, ui_i18n_text(s_lang_keys[idx]));
    lv_obj_set_width(label, 0);
    lv_obj_set_flex_grow(label, 1);
    s_lang_labels[idx] = label;

    lv_obj_t *mark = lv_obj_create(row);
    lv_obj_remove_style_all(mark);
    lv_obj_set_size(mark, ui_adapt(20), ui_adapt(20));
    lv_obj_set_style_radius(mark, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(mark, ui_adapt(2), 0);
    lv_obj_clear_flag(mark, LV_OBJ_FLAG_SCROLLABLE);
    s_lang_marks[idx] = mark;

    lv_obj_t *dot = lv_obj_create(mark);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, ui_adapt(UI_SPACE_SM), ui_adapt(UI_SPACE_SM));
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, UI_COLOR_TEXT, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_center(dot);
    s_lang_mark_dots[idx] = dot;
}

static void create_language_step(lv_obj_t *parent)
{
    lv_obj_t *view = create_step_view(parent);
    s_step_views[ACTIVATION_STEP_LANGUAGE] = view;
    lv_obj_set_flex_flow(view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(view, ui_adapt(UI_SPACE_LG), 0);
    lv_obj_set_style_pad_top(view, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_bottom(view, ui_adapt(UI_SPACE_LG), 0);
    lv_obj_set_style_pad_row(view, ui_adapt(UI_SPACE_MD), 0);

    for (uint8_t i = 0; i < UI_LANG_MAX; i++) {
        create_language_row(view, i);
    }

    lv_obj_t *spacer = lv_obj_create(view);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_width(spacer, LV_PCT(100));
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    s_continue_btn = ui_comp_btn_create_styled(
        view, ui_i18n_text(UI_TEXT_BOOT_LANG_CONTINUE),
        UI_COMP_BTN_PRIMARY, continue_click_cb);
    lv_obj_set_size(s_continue_btn, LV_PCT(100),
                    ui_adapt(ACTIVATION_PRIMARY_H));
    lv_obj_set_style_radius(s_continue_btn, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_set_flex_flow(s_continue_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_continue_btn,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
}

static void create_option_row(lv_obj_t *parent, uint8_t idx)
{
    lv_obj_t *row = ui_comp_btn_create_styled(
        parent, NULL, UI_COMP_BTN_SECONDARY, NULL);
    lv_obj_set_size(row, LV_PCT(100), ui_adapt(ACTIVATION_OPTION_ROW_H));
    lv_obj_set_style_bg_color(row, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, ui_adapt(UI_RADIUS_LG), 0);
    lv_obj_set_style_border_width(row, ui_adapt(2), 0);
    lv_obj_set_style_bg_color(row, UI_COLOR_PRIMARY_DARK, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(row, LV_OPA_50, LV_STATE_PRESSED);
    lv_obj_set_style_pad_hor(row, ui_adapt(UI_SPACE_LG), 0);
    lv_obj_set_style_pad_ver(row, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_column(row, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(row, option_row_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)idx);
    s_option_rows[idx] = row;

    lv_obj_t *text_col = lv_obj_create(row);
    lv_obj_remove_style_all(text_col);
    lv_obj_set_width(text_col, 0);
    lv_obj_set_height(text_col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(text_col, 1);
    lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(text_col, ui_adapt(UI_SPACE_XS), 0);
    lv_obj_clear_flag(text_col,
                      LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(text_col);
    lv_label_set_text(title, ui_i18n_text(s_options[idx].title_key));
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    s_option_titles[idx] = title;

    lv_obj_t *desc = lv_label_create(text_col);
    lv_obj_set_width(desc, LV_PCT(100));
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_label_set_text(desc, ui_i18n_text(s_options[idx].desc_key));
    lv_obj_set_style_text_color(desc, UI_COLOR_TEXT_SEC, 0);
    s_option_descs[idx] = desc;

    lv_obj_t *mark = lv_obj_create(row);
    lv_obj_remove_style_all(mark);
    lv_obj_set_size(mark, ui_adapt(20), ui_adapt(20));
    lv_obj_set_style_radius(mark, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(mark, ui_adapt(2), 0);
    lv_obj_clear_flag(mark,
                      LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    s_option_marks[idx] = mark;

    lv_obj_t *dot = lv_obj_create(mark);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, ui_adapt(UI_SPACE_SM), ui_adapt(UI_SPACE_SM));
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, UI_COLOR_TEXT, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(dot,
                      LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(dot);
    s_option_mark_dots[idx] = dot;
}

static void create_mode_step(lv_obj_t *parent)
{
    lv_obj_t *view = create_step_view(parent);
    s_step_views[ACTIVATION_STEP_MODE] = view;
    lv_obj_set_flex_flow(view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(view, ui_adapt(UI_SPACE_LG), 0);
    lv_obj_set_style_pad_top(view, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_bottom(view, ui_adapt(UI_SPACE_LG), 0);
    lv_obj_set_style_pad_row(view, ui_adapt(UI_SPACE_MD), 0);

    s_mode_hint = lv_label_create(view);
    lv_obj_set_width(s_mode_hint, LV_PCT(100));
    lv_label_set_long_mode(s_mode_hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_mode_hint, ui_i18n_text(UI_TEXT_ACTIVATE_HINT));
    lv_obj_set_style_text_align(s_mode_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_mode_hint, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_pad_bottom(s_mode_hint, ui_adapt(UI_SPACE_MD), 0);

    for (uint8_t i = 0; i < ACTIVATION_OPTION_MAX; i++) {
        create_option_row(view, i);
    }

    lv_obj_t *spacer = lv_obj_create(view);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_width(spacer, LV_PCT(100));
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    s_mode_continue_btn = ui_comp_btn_create_styled(
        view, ui_i18n_text(UI_TEXT_BOOT_LANG_CONTINUE),
        UI_COMP_BTN_PRIMARY, mode_continue_click_cb);
    lv_obj_set_size(s_mode_continue_btn, LV_PCT(100),
                    ui_adapt(ACTIVATION_PRIMARY_H));
    lv_obj_set_style_radius(s_mode_continue_btn, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_set_flex_flow(s_mode_continue_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_mode_continue_btn,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
}

static void create_qr_step(lv_obj_t *parent)
{
    lv_obj_t *view = create_step_view(parent);
    s_step_views[ACTIVATION_STEP_QR] = view;
    lv_obj_set_flex_flow(view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(view,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(view, ui_adapt(UI_SPACE_LG), 0);
    lv_obj_set_style_pad_top(view, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_bottom(view, ui_adapt(UI_SPACE_LG), 0);
    lv_obj_set_style_pad_row(view, ui_adapt(UI_SPACE_XL), 0);

    s_qr_card = lv_obj_create(view);
    lv_obj_remove_style_all(s_qr_card);
    lv_obj_set_size(s_qr_card,
                    ui_adapt(ACTIVATION_QR_SIZE + 2 * ACTIVATION_QR_CARD_PAD),
                    ui_adapt(ACTIVATION_QR_SIZE + 2 * ACTIVATION_QR_CARD_PAD));
    lv_obj_set_style_bg_color(s_qr_card, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(s_qr_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_qr_card, ui_adapt(UI_RADIUS_LG), 0);
    lv_obj_set_style_border_color(s_qr_card, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_opa(s_qr_card, LV_OPA_30, 0);
    lv_obj_set_style_border_width(s_qr_card, ui_adapt(1), 0);
    lv_obj_clear_flag(s_qr_card, LV_OBJ_FLAG_SCROLLABLE);

    s_qr_placeholder = lv_obj_create(s_qr_card);
    lv_obj_remove_style_all(s_qr_placeholder);
    lv_obj_set_size(s_qr_placeholder, ui_adapt(72), ui_adapt(72));
    lv_obj_set_style_radius(s_qr_placeholder, ui_adapt(UI_RADIUS_LG), 0);
    lv_obj_set_style_border_color(s_qr_placeholder, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_border_opa(s_qr_placeholder, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_qr_placeholder, ui_adapt(2), 0);
    lv_obj_clear_flag(s_qr_placeholder, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(s_qr_placeholder);

    lv_obj_t *placeholder_dot = lv_obj_create(s_qr_placeholder);
    lv_obj_remove_style_all(placeholder_dot);
    lv_obj_set_size(placeholder_dot, ui_adapt(UI_SPACE_MD),
                    ui_adapt(UI_SPACE_MD));
    lv_obj_set_style_radius(placeholder_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(placeholder_dot, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_opa(placeholder_dot, LV_OPA_COVER, 0);
    lv_obj_center(placeholder_dot);

    s_qr = lv_qrcode_create(s_qr_card, ui_adapt(ACTIVATION_QR_SIZE),
                            lv_color_black(), lv_color_white());
    lv_obj_center(s_qr);
    lv_obj_add_flag(s_qr, LV_OBJ_FLAG_HIDDEN);

    s_qr_hint = lv_label_create(view);
    lv_obj_set_width(s_qr_hint, LV_PCT(90));
    lv_label_set_long_mode(s_qr_hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_qr_hint, ui_i18n_text(UI_TEXT_QRCODE_WAITING));
    lv_obj_set_style_text_align(s_qr_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_qr_hint, UI_COLOR_TEXT_SEC, 0);
}

static void on_create(void *parent)
{
    (void)parent;

    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen,
                      LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(s_screen, ui_adapt(ACTIVATION_STATUSBAR_H), 0);

    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(ACTIVATION_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24,
                                              back_click_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(UI_SPACE_SM), 0);

    s_title = lv_label_create(titlebar);
    lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_BOOT_LANG_TITLE));
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    s_skip_btn = ui_comp_btn_create_styled(
        titlebar, ui_i18n_text(UI_TEXT_BOOT_SKIP), UI_COMP_BTN_TEXT,
        skip_click_cb);
    lv_obj_set_size(s_skip_btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_skip_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(s_skip_btn, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_pad_hor(s_skip_btn, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_set_style_pad_ver(s_skip_btn, ui_adapt(UI_SPACE_XS), 0);
    lv_obj_align(s_skip_btn, LV_ALIGN_RIGHT_MID, -ui_adapt(UI_SPACE_SM), 0);

    create_progress(s_screen);

    lv_obj_t *body = lv_obj_create(s_screen);
    lv_obj_remove_style_all(body);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    create_language_step(body);
    create_mode_step(body);
    create_qr_step(body);

    s_step = ACTIVATION_STEP_MAX;
    s_rendered_lang = UI_LANG_MAX;
    s_selected_option = ACTIVATION_OPTION_APP;
    s_rendered_option = ACTIVATION_OPTION_MAX;
    s_manual_flow_pending = false;
    s_qr_ready = false;
    s_rendered_url[0] = '\0';
    set_step(ACTIVATION_STEP_LANGUAGE);
    refresh_language_selection();
    refresh_option_selection();
}

static void on_enter(uint32_t dirty)
{
    if (ui_dirty_is_enter(dirty)) {
        if (ui_svc_activate_is_complete()) {
            lv_async_call(go_home_async_cb, NULL);
            return;
        }

        activation_step_t step_before = s_step;
        if (s_manual_flow_pending) {
            s_manual_flow_pending = false;
            const ui_svc_wlan_result_t *wlan = ui_svc_wlan_get();
            if (wlan && wlan->connected) {
                set_step(ACTIVATION_STEP_QR);
            }
        }

        if (s_step == ACTIVATION_STEP_QR && step_before == s_step) {
            ui_svc_activate_set_cb(activate_cb);
            refresh_qr();
        }
    }

    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        refresh_texts();
        refresh_language_selection();
    }
}

static void on_leave(void)
{
    ui_svc_activate_set_cb(NULL);
    ui_svc_dev_ctrl_save();
}

static void on_destroy(void)
{
    ui_svc_activate_set_cb(NULL);
    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
    }

    s_screen = NULL;
    s_title = NULL;
    s_skip_btn = NULL;
    s_continue_btn = NULL;
    s_mode_hint = NULL;
    s_mode_continue_btn = NULL;
    s_qr_card = NULL;
    s_qr = NULL;
    s_qr_placeholder = NULL;
    s_qr_hint = NULL;
    for (uint8_t i = 0; i < ACTIVATION_STEP_MAX; i++) {
        s_step_views[i] = NULL;
        s_progress_steps[i] = NULL;
    }
    for (uint8_t i = 0; i < UI_LANG_MAX; i++) {
        s_lang_rows[i] = NULL;
        s_lang_labels[i] = NULL;
        s_lang_marks[i] = NULL;
        s_lang_mark_dots[i] = NULL;
    }
    for (uint8_t i = 0; i < ACTIVATION_OPTION_MAX; i++) {
        s_option_rows[i] = NULL;
        s_option_titles[i] = NULL;
        s_option_descs[i] = NULL;
        s_option_marks[i] = NULL;
        s_option_mark_dots[i] = NULL;
    }
    s_step = ACTIVATION_STEP_MAX;
    s_rendered_lang = UI_LANG_MAX;
    s_selected_option = ACTIVATION_OPTION_APP;
    s_rendered_option = ACTIVATION_OPTION_MAX;
    s_manual_flow_pending = false;
    s_qr_ready = false;
    s_rendered_url[0] = '\0';
}

const ui_page_entry_t ui_page_activation_entry = {
    .id = UI_PAGE_ACTIVATION,
    .name = "activation",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
