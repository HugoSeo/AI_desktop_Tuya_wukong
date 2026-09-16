#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_comp_btn.h"
#include "ui_comp_bar.h"
#include "ui_comp_popup.h"
#include "ui_comp_slider.h"
#include "ui_comp_statusbar.h"
#include "ui_feature.h"
#include "ui_svc_dev_ctrl.h"
#include "ui_svc_ota.h"
#include "ui_svc_tm.h"
#include "ui_svc_wlan.h"
#include "ui_state.h"
#include "ui_page_ids.h"
#include "lvgl.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Style constants
 * -------------------------------------------------------------------------*/
#define PULLDOWN_GESTURE_ZONE_H     40
#define PULLDOWN_ENTRY_H            75
#define PULLDOWN_ENTRY_GAP          12

#define PULLDOWN_TIMER_COUNT        3
#define PULLDOWN_PHASE_WORK         0
#define PULLDOWN_PHASE_SHORT        1
#define PULLDOWN_PHASE_LONG         2

/* Nothing rendered yet — distinct from the service's -1 "percent unknown". */
#define PULLDOWN_OTA_PERCENT_NONE   (-2)

/* ---------------------------------------------------------------------------
 * Icon declarations
 * -------------------------------------------------------------------------*/
LV_IMG_DECLARE(icon_volume);
LV_IMG_DECLARE(icon_brightness);

/* ---------------------------------------------------------------------------
 * Internal state
 * -------------------------------------------------------------------------*/
typedef struct {
    lv_obj_t *btn;
    lv_obj_t *title;
    lv_obj_t *status;
    char last_title[40];
    char last_status[40];
    int last_style_key;
} pulldown_timer_card_t;

typedef struct {
    lv_obj_t *overlay;
    lv_obj_t *panel;
    lv_obj_t *volume_sli;
    lv_obj_t *brightness_sli;
    lv_obj_t *ota_container;
    lv_obj_t *ota_label;
    lv_obj_t *ota_bar;
    lv_obj_t *wlan_btn;
    lv_obj_t *wlan_title;
    lv_obj_t *wlan_status;
    pulldown_timer_card_t timer_cards[PULLDOWN_TIMER_COUNT];
    lv_obj_t *gesture_zone;
    bool      visible;
    bool      gesture_enabled;
    bool      ota_anim_running;
    int       ota_last_percent;
} pulldown_state_t;

static pulldown_state_t s_pd = {0};

/* ---------------------------------------------------------------------------
 * Forward declarations
 * -------------------------------------------------------------------------*/
static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_leave(void);
static void on_destroy(void);

static void dismiss(void);
static void gesture_dismiss_cb(lv_event_t *e);
static void volume_cb(lv_event_t *e);
static void volume_released_cb(lv_event_t *e);
static void brightness_cb(lv_event_t *e);
static void brightness_released_cb(lv_event_t *e);
static void wlan_click_cb(lv_event_t *e);
static void timer_click_cb(lv_event_t *e);
static void open_gesture_cb(lv_event_t *e);

static void ota_bar_anim_set(void *obj, int32_t value)
{
    ui_comp_bar_set_value((lv_obj_t *)obj, value);
}

static void ota_anim_stop(void)
{
    if (!s_pd.ota_anim_running) {
        return;
    }
    lv_anim_del(s_pd.ota_bar, ota_bar_anim_set);
    s_pd.ota_anim_running = false;
}

static void ota_anim_start(void)
{
    if (!s_pd.ota_bar || s_pd.ota_anim_running) {
        return;
    }

    ui_comp_bar_set_value(s_pd.ota_bar, 8);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_pd.ota_bar);
    lv_anim_set_exec_cb(&anim, ota_bar_anim_set);
    lv_anim_set_values(&anim, 8, 92);
    lv_anim_set_time(&anim, 1400);
    lv_anim_set_playback_time(&anim, 1400);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_start(&anim);
    s_pd.ota_anim_running = true;
}

static void render_ota_progress(void)
{
    if (!s_pd.ota_container) {
        return;
    }

    if (!ui_svc_ota_is_upgrading()) {
        ota_anim_stop();
        s_pd.ota_last_percent = PULLDOWN_OTA_PERCENT_NONE;
        lv_obj_add_flag(s_pd.ota_container, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(s_pd.ota_container, LV_OBJ_FLAG_HIDDEN);

    /* Progress arrives via the service callback (every ~2s while downloading);
     * dedup so repeated renders (page entry, language change) don't rewrite the
     * label or restart the bar animation. */
    int percent = ui_svc_ota_get_percent();
    if (percent == s_pd.ota_last_percent) {
        return;
    }
    s_pd.ota_last_percent = percent;

    if (percent < 0) {
        /* Upgrade started but no data block reported yet: sweep indeterminately. */
        lv_label_set_text(s_pd.ota_label, ui_i18n_text(UI_TEXT_OTA_UPGRADING));
        ota_anim_start();
        return;
    }

    ota_anim_stop();    /* determinate from the first reported percent on */
    char text[48];
    snprintf(text, sizeof(text), "%s %d%%",
             ui_i18n_text(UI_TEXT_OTA_UPGRADING), percent);
    lv_label_set_text(s_pd.ota_label, text);
    ui_comp_bar_set_value_anim(s_pd.ota_bar, percent, 300);
}

/* Upgrade state or download percent changed (already on the UI thread). */
static void ota_state_cb(bool upgrading)
{
    (void)upgrading;
    if (s_pd.overlay && ui_route_current() == UI_PAGE_PULLDOWN) {
        render_ota_progress();
    }
}

static void format_timer_value(char *buf, size_t cap, long total_sec)
{
    if (total_sec < 0) {
        total_sec = 0;
    }

    long hours = total_sec / 3600;
    long minutes = (total_sec / 60) % 60;
    long seconds = total_sec % 60;
    if (hours > 0) {
        snprintf(buf, cap, "%02ld:%02ld:%02ld", hours, minutes, seconds);
    } else {
        snprintf(buf, cap, "%02ld:%02ld", minutes, seconds);
    }
}

static void timer_card_set_label(lv_obj_t *label, char *cache,
                                 size_t cache_size, const char *text)
{
    if (!label || !cache || !text || strcmp(cache, text) == 0) {
        return;
    }
    lv_label_set_text(label, text);
    snprintf(cache, cache_size, "%s", text);
}

static void timer_card_render(pulldown_timer_card_t *card,
                              const char *title, bool active, bool paused,
                              long value_sec, lv_color_t accent,
                              int accent_key)
{
    if (!card || !card->btn || !card->title || !card->status) {
        return;
    }

    char status[40];
    if (!active) {
        snprintf(status, sizeof(status), "%s",
                 ui_i18n_text(UI_TEXT_TM_NOT_RUNNING));
    } else {
        char value[24];
        format_timer_value(value, sizeof(value), value_sec);
        if (paused) {
            snprintf(status, sizeof(status), "%s %s",
                     ui_i18n_text(UI_TEXT_TM_PAUSE), value);
        } else {
            snprintf(status, sizeof(status), "%s", value);
        }
    }

    timer_card_set_label(card->title, card->last_title,
                         sizeof(card->last_title), title);
    timer_card_set_label(card->status, card->last_status,
                         sizeof(card->last_status), status);

    int style_key = accent_key * 4 + (active ? 2 : 0) + (paused ? 1 : 0);
    if (style_key == card->last_style_key) {
        return;
    }

    lv_color_t card_color = active ? accent : UI_COLOR_TEXT_SEC;
    lv_obj_set_style_text_color(card->status, card_color, 0);
    lv_obj_set_style_bg_color(card->btn, card_color, 0);
    lv_obj_set_style_bg_opa(card->btn,
                            active ? (paused ? LV_OPA_10 : LV_OPA_20)
                                   : LV_OPA_10,
                            0);
    lv_obj_set_style_border_color(card->btn, card_color, 0);
    lv_obj_set_style_border_opa(card->btn,
                                active ? (paused ? LV_OPA_40 : LV_OPA_60)
                                       : LV_OPA_20,
                                0);
    lv_obj_set_style_border_width(card->btn, ui_adapt(1), 0);
    card->last_style_key = style_key;
}

static const char *pomodoro_card_title(int phase)
{
    if (phase == PULLDOWN_PHASE_SHORT) {
        return ui_i18n_text(UI_TEXT_PULLDOWN_POMODORO_SHORT);
    }
    if (phase == PULLDOWN_PHASE_LONG) {
        return ui_i18n_text(UI_TEXT_PULLDOWN_POMODORO_LONG);
    }
    return ui_i18n_text(UI_TEXT_PULLDOWN_POMODORO_WORK);
}

static lv_color_t pomodoro_card_color(int phase)
{
    if (phase == PULLDOWN_PHASE_SHORT) {
        return lv_color_hex(0x2CB8C2);
    }
    if (phase == PULLDOWN_PHASE_LONG) {
        return lv_color_hex(0x3598D4);
    }
    return lv_color_hex(0xE9585B);
}

static void render_timer_entries(void)
{
    ui_svc_tm_countdown_t countdown = {0};
    ui_svc_tm_stopwatch_t stopwatch = {0};
    ui_svc_tm_pomodoro_t pomodoro = {0};

    bool countdown_active =
        ui_svc_tm_countdown_query(&countdown) == 0 && countdown.active;
    bool stopwatch_active =
        ui_svc_tm_stopwatch_query(&stopwatch) == 0 && stopwatch.active;
    bool pomodoro_active =
        ui_svc_tm_pomodoro_query(&pomodoro) == 0 && pomodoro.active;

    timer_card_render(&s_pd.timer_cards[0],
                      ui_i18n_text(UI_TEXT_CLOCK_TAB_COUNTDOWN),
                      countdown_active,
                      countdown_active && countdown.paused,
                      countdown_active ? countdown.remaining_sec : 0,
                      UI_COLOR_WARNING, 0);
    timer_card_render(&s_pd.timer_cards[1],
                      ui_i18n_text(UI_TEXT_CLOCK_TAB_STOPWATCH),
                      stopwatch_active,
                      stopwatch_active && stopwatch.paused,
                      stopwatch_active ? stopwatch.elapsed_sec : 0,
                      UI_COLOR_SUCCESS, 1);
    timer_card_render(&s_pd.timer_cards[2],
                      pomodoro_active
                          ? pomodoro_card_title(pomodoro.phase)
                          : ui_i18n_text(UI_TEXT_POMODORO_TITLE),
                      pomodoro_active,
                      pomodoro_active && pomodoro.paused,
                      pomodoro_active ? pomodoro.remaining_sec : 0,
                      pomodoro_card_color(pomodoro.phase),
                      2 + (pomodoro_active ? pomodoro.phase : 0));
}

static void setup_entry_button(lv_obj_t *btn, lv_coord_t width)
{
    lv_obj_set_size(btn, width, ui_adapt(PULLDOWN_ENTRY_H));
    lv_obj_set_style_radius(btn, ui_adapt(UI_RADIUS_LG), 0);
    lv_obj_set_style_pad_row(btn, ui_adapt(UI_SPACE_XS), 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_entry_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), ui_adapt(PULLDOWN_ENTRY_H));
    lv_obj_set_layout(row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, ui_adapt(PULLDOWN_ENTRY_GAP), 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

static void create_timer_entry(lv_obj_t *parent,
                               pulldown_timer_card_t *card,
                               lv_coord_t width,
                               ui_svc_tm_clock_tab_t target)
{
    memset(card, 0, sizeof(*card));
    card->last_style_key = -1;

    card->btn = ui_comp_btn_create_styled(parent, NULL,
                                           UI_COMP_BTN_TEXT, NULL);
    setup_entry_button(card->btn, width);
    lv_obj_add_event_cb(card->btn, timer_click_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)target);

    card->title = lv_label_create(card->btn);
    lv_label_set_text(card->title, "");
    lv_obj_set_style_text_color(card->title, UI_COLOR_TEXT, 0);

    card->status = lv_label_create(card->btn);
    lv_label_set_text(card->status, "");
}

static void render_wlan_entry(void)
{
    if (!s_pd.wlan_btn || !s_pd.wlan_title || !s_pd.wlan_status) {
        return;
    }

    const ui_svc_wlan_result_t *result = ui_svc_wlan_get();
    lv_color_t accent;
    const char *status_text;
    if (result->connect_state == UI_SVC_WLAN_CONNECTING) {
        accent = UI_COLOR_WARNING;
        status_text = ui_i18n_text(UI_TEXT_WLAN_CONNECTING);
    } else if (result->connected) {
        accent = UI_COLOR_SUCCESS;
        status_text = ui_i18n_text(UI_TEXT_WLAN_CONNECTED);
    } else {
        accent = UI_COLOR_ERROR;
        status_text = ui_i18n_text(UI_TEXT_WLAN_NOT_CONNECTED);
    }

    lv_label_set_text(s_pd.wlan_title,
                      ui_i18n_text(UI_TEXT_WLAN_TITLE));
    lv_label_set_text(s_pd.wlan_status, status_text);
    lv_obj_set_style_text_color(s_pd.wlan_status, accent, 0);
    lv_obj_set_style_bg_color(s_pd.wlan_btn, accent, 0);
    lv_obj_set_style_bg_opa(s_pd.wlan_btn, LV_OPA_20, 0);
    lv_obj_set_style_border_color(s_pd.wlan_btn, accent, 0);
    lv_obj_set_style_border_opa(s_pd.wlan_btn, LV_OPA_60, 0);
    lv_obj_set_style_border_width(s_pd.wlan_btn, ui_adapt(1), 0);
}

static void wlan_result_cb(const ui_svc_wlan_result_t *result)
{
    (void)result;
    if (s_pd.overlay && ui_route_current() == UI_PAGE_PULLDOWN) {
        render_wlan_entry();
    }
}

/* ---------------------------------------------------------------------------
 * Page lifecycle
 * -------------------------------------------------------------------------*/
static void on_create(void *parent)
{
    (void)parent;
    uint16_t sw = ui_adapt_screen_w();
    uint16_t sh = ui_adapt_screen_h();

    /* Hide gesture zone while page is active */
    if (s_pd.gesture_zone) {
        lv_obj_add_flag(s_pd.gesture_zone, LV_OBJ_FLAG_HIDDEN);
    }

    /* Semi-transparent overlay (hidden during construction to avoid partial rendering) */
    s_pd.overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_pd.overlay);
    lv_obj_set_size(s_pd.overlay, sw, sh);
    lv_obj_set_style_bg_color(s_pd.overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_pd.overlay, LV_OPA_50, 0);
    lv_obj_add_flag(s_pd.overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_pd.overlay, LV_OBJ_FLAG_SCROLLABLE);

    /* Main panel */
    s_pd.panel = lv_obj_create(s_pd.overlay);
    lv_obj_remove_style_all(s_pd.panel);
    lv_obj_set_size(s_pd.panel, sw, sh);
    lv_obj_set_pos(s_pd.panel, 0, 0);
    lv_obj_set_style_bg_color(s_pd.panel, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_pd.panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_pd.panel, 0, 0);
    lv_obj_set_style_pad_hor(s_pd.panel, ui_adapt(16), 0);
    /* statusbar band (32) + a gap below the floating statusbar pill, so the
     * volume slider isn't flush against it */
    lv_obj_set_style_pad_top(s_pd.panel, ui_adapt(32) + ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_bottom(s_pd.panel, ui_adapt(8), 0);  /* hint bar hugs the bottom edge */
    lv_obj_set_style_pad_row(s_pd.panel, ui_adapt(12), 0);
    lv_obj_clear_flag(s_pd.panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(s_pd.panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_layout(s_pd.panel, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_pd.panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_pd.panel,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_pd.panel, ui_adapt(12), 0);
    lv_obj_add_event_cb(s_pd.panel, gesture_dismiss_cb, LV_EVENT_GESTURE, NULL);

    /* Volume slider */
    s_pd.volume_sli = ui_comp_slider_create(s_pd.panel, &icon_volume, ui_svc_dev_ctrl_volume_get());
    lv_obj_add_event_cb(s_pd.volume_sli, volume_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_pd.volume_sli, volume_released_cb, LV_EVENT_RELEASED, NULL);

    /* Brightness slider */
    s_pd.brightness_sli = ui_comp_slider_create(s_pd.panel, &icon_brightness, ui_svc_dev_ctrl_brightness_get());
    lv_obj_add_flag(s_pd.brightness_sli, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_pd.brightness_sli, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_pd.brightness_sli, brightness_released_cb, LV_EVENT_RELEASED, NULL);

    /* OTA row: determinate once the SDK reports a download percent
     * (EVENT_OTA_PROGRESS_NOTIFY via ui_svc_ota), indeterminate sweep until
     * then. Keep the row out of layout when idle. */
    s_pd.ota_last_percent = PULLDOWN_OTA_PERCENT_NONE;
    s_pd.ota_container = lv_obj_create(s_pd.panel);
    lv_obj_remove_style_all(s_pd.ota_container);
    lv_obj_set_size(s_pd.ota_container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(s_pd.ota_container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_pd.ota_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_pd.ota_container, ui_adapt(UI_SPACE_XS), 0);
    lv_obj_clear_flag(s_pd.ota_container, LV_OBJ_FLAG_SCROLLABLE);

    s_pd.ota_label = lv_label_create(s_pd.ota_container);
    lv_label_set_text(s_pd.ota_label, "");
    lv_obj_set_style_text_color(s_pd.ota_label, UI_COLOR_TEXT_SEC, 0);

    s_pd.ota_bar = ui_comp_bar_create(s_pd.ota_container, 0, 100);
    lv_obj_add_flag(s_pd.ota_container, LV_OBJ_FLAG_HIDDEN);
    render_ota_progress();

    /* WLAN + timer status shortcuts share one stable 2x2 card grid. */
    lv_coord_t entry_w = (sw - ui_adapt(16) * 2 - ui_adapt(PULLDOWN_ENTRY_GAP)) / 2;
    lv_obj_t *entry_row_top = create_entry_row(s_pd.panel);
    s_pd.wlan_btn = ui_comp_btn_create_styled(entry_row_top,
                                              NULL,
                                              UI_COMP_BTN_TEXT,
                                              wlan_click_cb);
    setup_entry_button(s_pd.wlan_btn, entry_w);

    s_pd.wlan_title = lv_label_create(s_pd.wlan_btn);
    lv_label_set_text(s_pd.wlan_title, ui_i18n_text(UI_TEXT_WLAN_TITLE));
    lv_obj_set_style_text_color(s_pd.wlan_title, UI_COLOR_TEXT, 0);

    s_pd.wlan_status = lv_label_create(s_pd.wlan_btn);
    render_wlan_entry();

    create_timer_entry(entry_row_top, &s_pd.timer_cards[0], entry_w,
                       UI_SVC_TM_CLOCK_TAB_COUNTDOWN);

    lv_obj_t *entry_row_bottom = create_entry_row(s_pd.panel);
    create_timer_entry(entry_row_bottom, &s_pd.timer_cards[1], entry_w,
                       UI_SVC_TM_CLOCK_TAB_STOPWATCH);
    create_timer_entry(entry_row_bottom, &s_pd.timer_cards[2], entry_w,
                       UI_SVC_TM_CLOCK_TAB_POMODORO);
    render_timer_entries();

    /* Application launchers now live exclusively in App Center. Keep a flex
     * spacer here so the dismiss hint remains pinned to the bottom. */
    lv_obj_t *spacer = lv_obj_create(s_pd.panel);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_size(spacer, LV_PCT(100), 0);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_clear_flag(spacer, LV_OBJ_FLAG_SCROLLABLE);

    /* Dismiss hint as a normal flex child at the end of the column; the spacer
     * above grows to keep this bar flush with the bottom edge. */
    lv_obj_t *hint_bar = lv_obj_create(s_pd.panel);
    lv_obj_remove_style_all(hint_bar);
    lv_obj_set_width(hint_bar, LV_PCT(100));
    lv_obj_set_height(hint_bar, LV_SIZE_CONTENT);
    lv_obj_clear_flag(hint_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(hint_bar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hint_bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *up_icon = lv_label_create(hint_bar);
    lv_label_set_text(up_icon, LV_SYMBOL_UP);
    lv_obj_set_style_text_font(up_icon, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(up_icon, lv_color_hex(0x666666), 0);

    lv_obj_t *hint = lv_label_create(hint_bar);
    lv_label_set_text(hint, ui_i18n_text(UI_TEXT_SWIPE_UP_TO_EXIT));
    lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);

    /* Force layout calculation then show everything in one frame */
    lv_obj_update_layout(s_pd.overlay);
    lv_obj_clear_flag(s_pd.overlay, LV_OBJ_FLAG_HIDDEN);

    /* Float the global statusbar above this overlay — the overlay was created
     * after the statusbar on lv_layer_top(), so without this it would render
     * on top of (and hide) the statusbar that now occupies the panel's top band. */
    ui_comp_statusbar_raise();

    s_pd.visible = true;
}

static void on_enter(uint32_t dirty)
{
    if (ui_dirty_is_enter(dirty)) {
        ui_svc_wlan_set_cb(wlan_result_cb);
        ui_svc_ota_set_cb(ota_state_cb);
        ui_svc_wlan_refresh_status();
        render_ota_progress();
    }

    /* External volume changes (cloud/app DP, hardware net keys) arrive via the
     * display-message bridge → ui_state_set_volume() → SYSTEM dirty. Track them
     * live on the slider while the panel is open. Skip while the user is
     * dragging the knob (don't yank it from under the finger); the value
     * reconciles on release / next open. Same-value sets are no-ops (LVGL bar
     * early-returns), which also absorbs ui_tick's per-second SYSTEM marking. */
    if (dirty & (1u << UI_STATE_GROUP_SYSTEM)) {
        if (s_pd.volume_sli &&
            !lv_obj_has_state(s_pd.volume_sli, LV_STATE_PRESSED)) {
            ui_comp_slider_set_value(s_pd.volume_sli, ui_svc_dev_ctrl_volume_get());
        }
    }
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        render_wlan_entry();
        /* Language may have changed: drop the dedup guard so the "upgrading NN%"
         * label is reformatted with the new locale. */
        s_pd.ota_last_percent = PULLDOWN_OTA_PERCENT_NONE;
        render_ota_progress();
    }
    if (dirty & ((1u << UI_STATE_GROUP_SYSTEM) |
                 (1u << UI_STATE_GROUP_SETTINGS))) {
        render_timer_entries();
    }
}

static void on_leave(void)
{
    ui_svc_wlan_set_cb(NULL);
    ui_svc_ota_set_cb(NULL);
    ota_anim_stop();
    ui_svc_dev_ctrl_save();
}

static void on_destroy(void)
{
    ui_svc_wlan_set_cb(NULL);
    ui_svc_ota_set_cb(NULL);
    ota_anim_stop();
    if (s_pd.overlay) {
        /* on_destroy runs inside the event-callback chain (dismiss -> ui_route_pop);
         * hide immediately then defer deletion to avoid freeing the object while
         * LVGL is still processing events on it. */
        lv_obj_add_flag(s_pd.overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_pd.overlay);
        s_pd.overlay = NULL;
        s_pd.panel = NULL;
        s_pd.volume_sli = NULL;
        s_pd.brightness_sli = NULL;
        s_pd.ota_container = NULL;
        s_pd.ota_label = NULL;
        s_pd.ota_bar = NULL;
        s_pd.ota_last_percent = PULLDOWN_OTA_PERCENT_NONE;
        s_pd.wlan_btn = NULL;
        s_pd.wlan_title = NULL;
        s_pd.wlan_status = NULL;
    }
    memset(s_pd.timer_cards, 0, sizeof(s_pd.timer_cards));

    s_pd.visible = false;

    if (s_pd.gesture_zone && s_pd.gesture_enabled) {
        lv_obj_clear_flag(s_pd.gesture_zone, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------------------------------------------------------------------------
 * Callbacks
 * -------------------------------------------------------------------------*/
static void dismiss_async_cb(void *unused);

static void dismiss(void)
{
    if (ui_route_current() == UI_PAGE_PULLDOWN) {
        ui_route_pop();
    }
}

/**
 * Deferred dismiss — avoids leaking gesture/click events to the page underneath.
 * Calling ui_route_pop() synchronously inside a gesture/click callback would
 * destroy the overlay while LVGL is still processing the input sequence
 * (e.g. the finger hasn't lifted yet), so the release lands on whatever is
 * below.  Deferring to the next timer tick lets LVGL finish the current
 * input before we tear down the overlay.
 */
static void dismiss_async_cb(void *unused)
{
    (void)unused;
    dismiss();
}

static void gesture_dismiss_cb(lv_event_t *e)
{
    (void)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_TOP) {
        /* Swallow the rest of this touch so the release doesn't land as a click on
         * the page revealed underneath once the overlay is dismissed. The async
         * dismiss below still defers the actual teardown to the next tick. */
        lv_indev_wait_release(lv_indev_get_act());
        lv_async_call(dismiss_async_cb, NULL);
    }
}

static void volume_cb(lv_event_t *e)
{
    lv_obj_t *sli = lv_event_get_target(e);
    ui_svc_dev_ctrl_volume_set((uint8_t)lv_slider_get_value(sli));
}

static void volume_released_cb(lv_event_t *e)
{
    (void)e;
    /* Drag/tap finished: push the final volume to cloud (DP) + KV via the
     * device-control service (scheduled off the UI thread). Live audio already
     * tracked the gesture through volume_cb; commit dedups against the toy value
     * so a release that didn't change anything is a no-op. */
    ui_svc_dev_ctrl_volume_commit();
}

static void brightness_cb(lv_event_t *e)
{
    lv_obj_t *sli = lv_event_get_target(e);
    ui_svc_dev_ctrl_brightness_set((uint8_t)lv_slider_get_value(sli));
}

static void brightness_released_cb(lv_event_t *e)
{
    (void)e;
    /* Drag/tap finished: persist the final brightness to the ui_settings KV
     * (scheduled off the UI thread). Live backlight already tracked the gesture
     * through brightness_cb; save() dedups so a release that changed nothing is
     * a no-op. */
    ui_svc_dev_ctrl_save();
}

static void wlan_click_cb(lv_event_t *e)
{
    (void)e;
    /* Replace the overlay page so its lv_layer_top() tree is hidden before the
     * regular WLAN page is shown. Back from WLAN returns to the page below. */
    ui_route_replace(UI_PAGE_WLAN);
}

static void timer_click_cb(lv_event_t *e)
{
    ui_svc_tm_clock_tab_t target =
        (ui_svc_tm_clock_tab_t)(uintptr_t)lv_event_get_user_data(e);
    if (!ui_feature_available(UI_FEATURE_ID_TIME)) {
        ui_comp_popup_toast(ui_i18n_text(UI_TEXT_FEATURE_UNAVAILABLE), 1600);
        return;
    }

    ui_svc_tm_clock_tab_request(target);
    if (ui_route_previous() == UI_PAGE_CLOCK) {
        /* Reuse the Clock instance directly below this overlay. Its on_enter
         * consumes the requested target, preserving page-local state such as
         * stopwatch laps and preventing duplicate page-static LVGL pointers. */
        ui_route_pop();
    } else {
        /* No Clock below the overlay: replace only the pulldown, so Back from
         * the new Clock still returns to the page that opened the panel. */
        ui_route_replace(UI_PAGE_CLOCK);
    }
}

static void open_gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_BOTTOM && s_pd.gesture_enabled && !s_pd.visible) {
        ui_route_push(UI_PAGE_PULLDOWN);
    }
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/
const ui_page_entry_t ui_page_pulldown_entry = {
    .id = UI_PAGE_PULLDOWN,
    .name = "pulldown",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};

void ui_page_pulldown_init(void)
{
    s_pd.visible = false;
    s_pd.gesture_enabled = true;

    uint16_t sw = ui_adapt_screen_w();
    s_pd.gesture_zone = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_pd.gesture_zone);
    lv_obj_set_pos(s_pd.gesture_zone, 0, 0);
    lv_obj_set_size(s_pd.gesture_zone, sw, ui_adapt(PULLDOWN_GESTURE_ZONE_H));
    lv_obj_set_style_bg_opa(s_pd.gesture_zone, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(s_pd.gesture_zone, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_pd.gesture_zone, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_pd.gesture_zone, open_gesture_cb, LV_EVENT_GESTURE, NULL);
}

void ui_page_pulldown_set_gesture_enabled(bool enabled)
{
    s_pd.gesture_enabled = enabled;
    if (!s_pd.gesture_zone) {
        return;
    }

    if (enabled && !s_pd.visible) {
        lv_obj_clear_flag(s_pd.gesture_zone, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_pd.gesture_zone, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ui_page_pulldown_is_visible(void)
{
    return s_pd.visible;
}
