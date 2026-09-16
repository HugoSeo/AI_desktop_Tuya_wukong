#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_comp_popup.h"
#include "ui_svc_dev_ctrl.h"
#include "ui_svc_call.h"      /* auto-answer toggle + availability */
#include "tuya_app_config.h"
#if defined(ENABLE_TUYA_NFC) && (ENABLE_TUYA_NFC == 1)
#include "ui_svc_nfc.h"
#endif
#include "ui_feature.h"
#include "ui_page_ids.h"
LV_IMG_DECLARE(icon_back_24_24);

/* ---------------------------------------------------------------------------
 * Style constants
 * -------------------------------------------------------------------------*/
#define SETTINGS_STATUSBAR_H    32   /* global statusbar height (ui_comp_statusbar) */
#define SETTINGS_TITLEBAR_H     48
#define SETTINGS_ROW_H          56

/* ---------------------------------------------------------------------------
 * Internal state
 *
 * Object handles kept so on_enter() can refresh texts / segment highlight when
 * the language changes (UI_STATE_GROUP_SETTINGS dirty). Single source of truth
 * for the current language is ui_i18n_get_lang().
 * -------------------------------------------------------------------------*/
static lv_obj_t *s_screen     = NULL;
static lv_obj_t *s_content    = NULL;
static lv_obj_t *s_title      = NULL;   /* centered page title */
static lv_obj_t *s_lang_label       = NULL;   /* "语言/Language" row label */
static lv_obj_t *s_about_label      = NULL;   /* "关于/About" row label */
static lv_obj_t *s_reset_label      = NULL;   /* "重置/Reset" row label */
static lv_obj_t *s_p2p_label         = NULL;  /* "P2P 通话" row label (call feature only) */
static lv_obj_t *s_auto_answer_label = NULL;  /* "来电自动接通" row label (call feature only) */
static lv_obj_t *s_auto_answer_sw    = NULL;  /* "来电自动接通" switch — row hidden while P2P is off */
#if defined(ENABLE_TUYA_NFC) && (ENABLE_TUYA_NFC == 1)
static lv_obj_t *s_nfc_label          = NULL;  /* persisted NFC switch label */
static lv_obj_t *s_nfc_switch         = NULL;  /* refreshed if async NFC init fails */
#endif
static lv_obj_t *s_seg_zh     = NULL;   /* segment: 中文 */
static lv_obj_t *s_seg_en     = NULL;   /* segment: English */

/* ---------------------------------------------------------------------------
 * Forward declarations
 * -------------------------------------------------------------------------*/
static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_destroy(void);
static void back_click_cb(lv_event_t *e);
static void gesture_cb(lv_event_t *e);
static void lang_seg_cb(lv_event_t *e);
static void p2p_cb(lv_event_t *e);
#if ENABLE_UI_CALL_AUTO_ANSWER
static void auto_answer_cb(lv_event_t *e);
#endif
#if defined(ENABLE_TUYA_NFC) && (ENABLE_TUYA_NFC == 1)
static void nfc_cb(lv_event_t *e);
#endif
static void about_click_cb(lv_event_t *e);
static void reset_click_cb(lv_event_t *e);
static void refresh_lang(void);
static void auto_answer_row_refresh(void);
#if defined(ENABLE_TUYA_NFC) && (ENABLE_TUYA_NFC == 1)
static void refresh_nfc_switch(void);
#endif

/* ---------------------------------------------------------------------------
 * Segmented control helpers (page-local — settings-only, not a shared widget)
 * -------------------------------------------------------------------------*/

/* Paint one segment as selected (filled primary) or unselected (transparent).
 * Page-specific look applied as an override on top of the TEXT base style. */
static void seg_set_selected(lv_obj_t *seg, bool selected)
{
    if (!seg) {
        return;
    }
    if (selected) {
        lv_obj_set_style_bg_color(seg, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(seg, UI_COLOR_TEXT, 0);
    } else {
        lv_obj_set_style_bg_opa(seg, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(seg, UI_COLOR_TEXT_SEC, 0);
    }
}

/* Create one segment button. cb=NULL at create time; we attach our own handler
 * with user_data = lang so the two instances are distinguishable (per RULES). */
static lv_obj_t *seg_create(lv_obj_t *parent, ui_i18n_key_t text_key, ui_lang_t lang)
{
    lv_obj_t *seg = ui_comp_btn_create_styled(parent, ui_i18n_text(text_key),
                                              UI_COMP_BTN_TEXT, NULL);
    lv_obj_set_style_radius(seg, ui_adapt(UI_RADIUS_SM), 0);
    lv_obj_set_style_pad_hor(seg, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_ver(seg, ui_adapt(UI_SPACE_XS), 0);
    lv_obj_add_event_cb(seg, lang_seg_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)lang);
    return seg;
}

/* Create a tappable navigation row: label (left) + optional value text + ">" chevron (right).
 * out_label returns the left label; out_value_label (optional) returns the secondary
 * value label shown between the title and the chevron. */
static lv_obj_t *nav_row_create(lv_obj_t *parent, ui_i18n_key_t label_key,
                                lv_obj_t **out_label, lv_obj_t **out_value_label,
                                lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, ui_adapt(SETTINGS_ROW_H));
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

    /* Title — flex_grow pushes everything after it to the right edge. */
    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, ui_i18n_text(label_key));
    lv_obj_set_style_text_color(label, UI_COLOR_TEXT, 0);
    lv_obj_set_flex_grow(label, 1);
    if (out_label) {
        *out_label = label;
    }

    /* Optional value text (shown right of title, before chevron). */
    if (out_value_label) {
        lv_obj_t *val = lv_label_create(row);
        lv_label_set_text(val, "");
        lv_obj_set_style_text_color(val, UI_COLOR_TEXT_SEC, 0);
        *out_value_label = val;
    }

    lv_obj_t *chevron = lv_label_create(row);
    lv_label_set_text(chevron, ">");
    lv_obj_set_style_text_color(chevron, UI_COLOR_TEXT_SEC, 0);

    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, NULL);
    return row;
}

/* Create a row with a label (left) + lv_switch (right). Page-local — the only
 * toggle row on this page, so it's not promoted to a shared widget (RULES §3).
 * out_label returns the label for i18n refresh on language change. */
static lv_obj_t *switch_row_create(lv_obj_t *parent, ui_i18n_key_t label_key,
                                   lv_obj_t **out_label, bool checked,
                                   lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, ui_adapt(SETTINGS_ROW_H));
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
     * bar sits below it — same offset chat/home use. Without this the title
     * bar renders under the statusbar and the back button can't be tapped. */
    lv_obj_set_style_pad_top(s_screen, ui_adapt(SETTINGS_STATUSBAR_H), 0);

    /* Title bar: icon-only back (left), page title centered */
    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(SETTINGS_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    /* Icon-only back button (no "返回" text), left-aligned. */
    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_click_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);

    /* Centered page title */
    s_title = lv_label_create(titlebar);
    lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_APP_SETTINGS));
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    /* Content area — vertical list of setting rows. */
    s_content = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_width(s_content, LV_PCT(100));
    lv_obj_set_flex_grow(s_content, 1);
    /* Vertical scroll for the row list (rows can exceed one screen); horizontal
     * is left to the page-root swipe-right = back gesture. */
    lv_obj_add_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(s_content, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_row(s_content, ui_adapt(UI_SPACE_SM), 0);

    /* --- Language row: label (left) + segmented control (right) --- */
    lv_obj_t *row = lv_obj_create(s_content);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, ui_adapt(SETTINGS_ROW_H));
    lv_obj_set_style_bg_color(row, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_set_style_pad_hor(row, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    s_lang_label = lv_label_create(row);
    lv_label_set_text(s_lang_label, ui_i18n_text(UI_TEXT_SETTINGS_LANGUAGE));
    lv_obj_set_style_text_color(s_lang_label, UI_COLOR_TEXT, 0);

    /* Segmented track holding the two language buttons */
    lv_obj_t *seg_track = lv_obj_create(row);
    lv_obj_remove_style_all(seg_track);
    lv_obj_set_size(seg_track, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_clear_flag(seg_track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(seg_track, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(seg_track, ui_adapt(UI_SPACE_XS), 0);

    s_seg_zh = seg_create(seg_track, UI_TEXT_LANG_ZH, UI_LANG_ZH_CN);
    s_seg_en = seg_create(seg_track, UI_TEXT_LANG_EN, UI_LANG_EN);

#if defined(ENABLE_TUYA_NFC) && (ENABLE_TUYA_NFC == 1)
    /* Persisted one-way NFC control: ON initializes PN532; OFF does not
     * deinitialize it. The switch value is restored by dev-ctrl on startup. */
    s_nfc_switch = switch_row_create(s_content, UI_TEXT_SETTINGS_NFC, &s_nfc_label,
                                     ui_svc_nfc_switch_get(), nfc_cb);
#endif

    /* --- P2P toggle: only when the call feature is compiled in (it is the
     * prerequisite — it starts the call stack). --- */
    if (ui_feature_available(UI_FEATURE_ID_CALL)) {
        switch_row_create(s_content, UI_TEXT_SETTINGS_P2P, &s_p2p_label,
                          ui_svc_call_enabled_get(), p2p_cb);
#if ENABLE_UI_CALL_AUTO_ANSWER   /* off by default: incoming calls ring for
                                  * manual answer — see ui_svc_call.h */
        s_auto_answer_sw = switch_row_create(s_content, UI_TEXT_SETTINGS_AUTO_ANSWER,
                                             &s_auto_answer_label,
                                             ui_svc_call_auto_answer_get(), auto_answer_cb);
        auto_answer_row_refresh();
#endif
    }

    /* --- About row: navigates to the About page --- */
    nav_row_create(s_content, UI_TEXT_SETTINGS_ABOUT, &s_about_label, NULL, about_click_cb);

    /* --- Reset row: confirms, then unbinds the device --- */
    nav_row_create(s_content, UI_TEXT_SETTINGS_RESET, &s_reset_label, NULL, reset_click_cb);

    /* Initial paint from current language (don't rely on dirty for first enter) */
    refresh_lang();
}

static void on_enter(uint32_t dirty)
{
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        refresh_lang();
#if defined(ENABLE_TUYA_NFC) && (ENABLE_TUYA_NFC == 1)
        refresh_nfc_switch();
#endif
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
        s_screen     = NULL;
        s_content    = NULL;
        s_title      = NULL;
        s_lang_label       = NULL;
        s_about_label      = NULL;
        s_reset_label      = NULL;
        s_p2p_label         = NULL;
        s_auto_answer_label = NULL;
        s_auto_answer_sw    = NULL;
#if defined(ENABLE_TUYA_NFC) && (ENABLE_TUYA_NFC == 1)
        s_nfc_label          = NULL;
        s_nfc_switch         = NULL;
#endif
        s_seg_zh     = NULL;
        s_seg_en     = NULL;
    }
}

/* ---------------------------------------------------------------------------
 * Refresh
 * -------------------------------------------------------------------------*/

/* Re-apply language-dependent texts and segment highlight from current state. */
static void refresh_lang(void)
{
    ui_lang_t lang = ui_i18n_get_lang();

    if (s_title) {
        lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_APP_SETTINGS));
        lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);
    }
    if (s_lang_label) {
        lv_label_set_text(s_lang_label, ui_i18n_text(UI_TEXT_SETTINGS_LANGUAGE));
    }
    if (s_about_label) {
        lv_label_set_text(s_about_label, ui_i18n_text(UI_TEXT_SETTINGS_ABOUT));
    }
    if (s_reset_label) {
        lv_label_set_text(s_reset_label, ui_i18n_text(UI_TEXT_SETTINGS_RESET));
    }
    if (s_p2p_label) {
        lv_label_set_text(s_p2p_label, ui_i18n_text(UI_TEXT_SETTINGS_P2P));
    }
    if (s_auto_answer_label) {
        lv_label_set_text(s_auto_answer_label, ui_i18n_text(UI_TEXT_SETTINGS_AUTO_ANSWER));
    }
#if defined(ENABLE_TUYA_NFC) && (ENABLE_TUYA_NFC == 1)
    if (s_nfc_label) {
        lv_label_set_text(s_nfc_label, ui_i18n_text(UI_TEXT_SETTINGS_NFC));
    }
#endif
    seg_set_selected(s_seg_zh, lang == UI_LANG_ZH_CN);
    seg_set_selected(s_seg_en, lang == UI_LANG_EN);
}

/* Auto-answer only makes sense while P2P is on: hide the whole row while P2P
 * is off (flex reflows automatically). The switch value is kept as-is so the
 * user's choice is restored when P2P is turned back on. */
static void auto_answer_row_refresh(void)
{
    if (!s_auto_answer_sw) {
        return;
    }
    lv_obj_t *row = lv_obj_get_parent(s_auto_answer_sw);
    if (ui_svc_call_enabled_get()) {
        lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }
}

#if defined(ENABLE_TUYA_NFC) && (ENABLE_TUYA_NFC == 1)
static void refresh_nfc_switch(void)
{
    if (!s_nfc_switch) {
        return;
    }
    if (ui_svc_nfc_switch_get()) {
        lv_obj_add_state(s_nfc_switch, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(s_nfc_switch, LV_STATE_CHECKED);
    }
}
#endif

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

/* "关于" row tapped — open the next-level About page. */
static void about_click_cb(lv_event_t *e)
{
    (void)e;
    ui_route_push(UI_PAGE_ABOUT);
}

/* Deferred reset: swap the dismissed confirm dialog for a loading spinner and
 * kick off the (async, reboot-on-completion) unbind. Run from lv_async_call so
 * it executes AFTER the confirm popup's own dismiss completes — showing the
 * loading popup inside the confirm callback would get torn down immediately. */
static void reset_async_cb(void *unused)
{
    (void)unused;
    ui_comp_popup_show(UI_COMP_POPUP_LOADING, NULL,
                       ui_i18n_text(UI_TEXT_RESETTING), NULL);
    ui_svc_dev_ctrl_reset();
}

/* Confirm dialog result. Only proceed on explicit confirm. */
static void reset_confirm_cb(bool confirmed)
{
    if (confirmed) {
        lv_async_call(reset_async_cb, NULL);
    }
}

/* "重置" row tapped — ask for confirmation before unbinding the device. */
static void reset_click_cb(lv_event_t *e)
{
    (void)e;
    ui_comp_popup_show(UI_COMP_POPUP_CONFIRM,
                       ui_i18n_text(UI_TEXT_SETTINGS_RESET),
                       ui_i18n_text(UI_TEXT_RESET_CONFIRM_MSG),
                       reset_confirm_cb);
}

/* A language segment was tapped. Mutate state + i18n and persist immediately;
 * on_enter() (driven by the SETTINGS dirty bit) repaints the UI. */
static void lang_seg_cb(lv_event_t *e)
{
    ui_lang_t lang = (ui_lang_t)(uintptr_t)lv_event_get_user_data(e);
    ui_svc_dev_ctrl_language_set((uint8_t)lang);
    ui_svc_dev_ctrl_save();
}

/* P2P toggle changed. Turning on starts the P2P/call stack immediately (one-way
 * — no de-init on off). Persist the switch state to KV right away so it survives
 * a reboot even if the page is never left. */
static void p2p_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ui_svc_call_enabled_set(on);
    auto_answer_row_refresh();
    ui_svc_dev_ctrl_save();
}

#if ENABLE_UI_CALL_AUTO_ANSWER
/* Auto-answer toggle changed. Update the runtime value (call layer) and persist
 * to KV right away. */
static void auto_answer_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ui_svc_call_auto_answer_set(on);
    ui_svc_dev_ctrl_save();
}
#endif

#if defined(ENABLE_TUYA_NFC) && (ENABLE_TUYA_NFC == 1)
/* NFC is one-way at the hardware layer. ON schedules PN532 initialization off
 * the UI thread; OFF changes no NFC hardware state. Persist the switch state to
 * KV right away, matching the P2P switch. */
static void nfc_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    ui_svc_nfc_switch_set(lv_obj_has_state(sw, LV_STATE_CHECKED));
    ui_svc_dev_ctrl_save();
}
#endif

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/
const ui_page_entry_t ui_page_settings_entry = {
    .id = UI_PAGE_SETTINGS,
    .name = "settings",
    .lifecycle = { on_create, on_enter, NULL, on_destroy }
};
