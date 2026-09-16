#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_comp_tabview.h"
#include "ui_page_ids.h"

#include "ui_svc_tm.h"
#include "ui_comp_popup.h"
#include "ty_cJSON.h"
#include "tal_memory.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* Title-bar back icon (24x24) */
LV_IMG_DECLARE(icon_back_24_24);

typedef enum {
    TAB_ALARM = 0,
    TAB_COUNTDOWN,
    TAB_STOPWATCH,
    TAB_POMODORO,
    TAB_COUNT,
} clock_tab_t;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_title_lbl = NULL;
static lv_obj_t *s_tabview = NULL;
static lv_obj_t *s_tab_pages[TAB_COUNT] = { NULL };
static lv_obj_t *s_cd_timer_face = NULL;
static lv_obj_t *s_cd_arc = NULL;
static lv_obj_t *s_cd_roller_h = NULL;  /* countdown idle picker: hours roller */
static lv_obj_t *s_cd_roller_m = NULL;  /* countdown idle picker: minutes roller */
static lv_obj_t *s_cd_roller_s = NULL;  /* countdown idle picker: seconds roller */
static lv_obj_t *s_sw_timer_face = NULL;
static lv_obj_t *s_sw_centis_face = NULL;
static lv_obj_t *s_pomo_chip_lbl = NULL;
static lv_obj_t *s_pomo_arc = NULL;
static lv_obj_t *s_pomo_face = NULL;
static lv_obj_t *s_pomo_cycle_markers[12] = { NULL };
static unsigned s_pomo_cycle_marker_count = 0;
static lv_obj_t *s_pomo_btn_primary = NULL;
static lv_obj_t *s_pomo_btn_stop = NULL;
static lv_obj_t *s_pomo_roller = NULL;
static clock_tab_t s_tab = TAB_ALARM;
static long s_last_cd_shown_sec = -1;
static int s_cd_face_large = -1;
static bool s_cd_custom_mode = false;
static long s_last_sw_shown_sec = -1;
static int s_last_sw_shown_centis = -1;
static int s_sw_view_active = -1;
static int s_sw_view_paused = -1;
static bool s_sw_anim_active = false;
static lv_anim_t s_sw_anim;
static long s_last_pomo_shown_sec = -1;
static int16_t s_last_pomo_progress = -1;
static int s_last_pomo_visual_phase = -1;
static int s_last_pomo_action_key = -1;
static unsigned s_last_pomo_cycle = 0xFFFFFFFFu;
static unsigned s_last_pomo_cycle_total = 0xFFFFFFFFu;
static unsigned s_last_pomo_completed = 0xFFFFFFFFu;
static int s_pomo_view_active = -1;
static int s_pomo_edit_phase = -1;
static char (*s_alarm_ids)[40] = NULL;  /* page-owned id table to avoid cJSON UAF */
static int s_alarm_id_count = 0;
static char s_pending_del_id[40];

#define STOPWATCH_LAP_VISIBLE_MAX 4

typedef struct {
    unsigned number;
    uint64_t elapsed_ms;
} stopwatch_lap_t;

static stopwatch_lap_t s_sw_laps[STOPWATCH_LAP_VISIBLE_MAX];
static unsigned s_sw_lap_count = 0;
static unsigned s_sw_lap_seq = 0;

static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_leave(void);
static void on_destroy(void);
static void back_click_cb(lv_event_t *e);
static void gesture_cb(lv_event_t *e);
static void tab_change_cb(lv_event_t *e);
static void rebuild_selected_tab_async(void *data);
static void rebuild_tab(clock_tab_t tab);
static void rebuild_all_tabs(void);
static void refresh_i18n_labels(void);
static void build_pomodoro_tab(lv_obj_t *content);
static void refresh_pomodoro_view(void);
static void stop_stopwatch_animation(void);

static const ui_svc_tm_pomodoro_cfg_t POMODORO_DEFAULT_CFG = { 25, 5, 15, 4 };
static ui_svc_tm_pomodoro_cfg_t s_pomo_cfg = { 25, 5, 15, 4 };

static bool clock_tab_from_request(ui_svc_tm_clock_tab_t requested,
                                   clock_tab_t *tab)
{
    if (!tab) {
        return false;
    }
    switch (requested) {
        case UI_SVC_TM_CLOCK_TAB_ALARM:     *tab = TAB_ALARM; return true;
        case UI_SVC_TM_CLOCK_TAB_COUNTDOWN: *tab = TAB_COUNTDOWN; return true;
        case UI_SVC_TM_CLOCK_TAB_STOPWATCH: *tab = TAB_STOPWATCH; return true;
        case UI_SVC_TM_CLOCK_TAB_POMODORO:  *tab = TAB_POMODORO; return true;
        default: return false;
    }
}

static void activate_clock_tab(clock_tab_t tab)
{
    if (tab < TAB_ALARM || tab >= TAB_COUNT || tab == s_tab) {
        return;
    }

    if (s_tab == TAB_STOPWATCH) {
        stop_stopwatch_animation();
    } else if (s_tab == TAB_POMODORO) {
        s_pomo_edit_phase = -1;
    }

    s_tab = tab;
    if (s_tab == TAB_COUNTDOWN) {
        s_last_cd_shown_sec = -1;
    } else if (s_tab == TAB_STOPWATCH) {
        s_last_sw_shown_sec = -1;
    } else if (s_tab == TAB_POMODORO) {
        s_last_pomo_shown_sec = -1;
    }
    if (s_tabview) {
        lv_tabview_set_act(s_tabview, (uint32_t)s_tab, LV_ANIM_OFF);
    }
}

static void free_alarm_ids(void)
{
    if (s_alarm_ids) {
        tal_free(s_alarm_ids);
        s_alarm_ids = NULL;
    }
    s_alarm_id_count = 0;
}

static void on_create(void *parent)
{
    (void)parent;

    s_pomo_cfg = POMODORO_DEFAULT_CFG;
    s_pomo_edit_phase = -1;

    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);   /* swipe-right = back */
    /* Reserve the global statusbar band (lv_layer_top pill, height 32). */
    lv_obj_set_style_pad_top(s_screen, ui_adapt(32), 0);

    /* Title bar: icon-only back (left), page title centered */
    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(48));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_click_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);

    lv_obj_t *title = lv_label_create(titlebar);
    lv_label_set_text(title, ui_i18n_text(UI_TEXT_CLOCK_TITLE));
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
    s_title_lbl = title;

    /* A cross-page shortcut takes priority; otherwise surface an active timer
     * (including one created via MCP/MQTT) instead of the remembered/default
     * tab. Must run before the content + tab strip are built, since both read
     * s_tab. */
    {
        ui_svc_tm_clock_tab_t requested = ui_svc_tm_clock_tab_take();
        clock_tab_t requested_tab;
        ui_svc_tm_countdown_t cd;
        ui_svc_tm_stopwatch_t sw;
        ui_svc_tm_pomodoro_t pomo;
        if (clock_tab_from_request(requested, &requested_tab)) {
            s_tab = requested_tab;
        } else if (ui_svc_tm_countdown_query(&cd) == 0 && cd.active) {
            s_tab = TAB_COUNTDOWN;
        } else if (ui_svc_tm_stopwatch_query(&sw) == 0 && sw.active) {
            s_tab = TAB_STOPWATCH;
        } else if (ui_svc_tm_pomodoro_query(&pomo) == 0 && pomo.active) {
            s_tab = TAB_POMODORO;
        }
    }

    /* Shared native tabview: it owns the bottom segmented control and preserves
     * LVGL's click/drag tab switching. Its total height includes the tab strip. */
    const ui_comp_tabview_props_t tabview_props = {
        .tab_pos = LV_DIR_BOTTOM,
        .tab_size = ui_adapt(48),
    };
    s_tabview = ui_comp_tabview_create(s_screen, &tabview_props);
    lv_obj_set_width(s_tabview, LV_PCT(100));
    lv_obj_set_height(s_tabview,
                      ui_adapt_screen_h() - ui_adapt(32) - ui_adapt(48));
    lv_obj_align(s_tabview, LV_ALIGN_TOP_MID, 0, ui_adapt(48));

    s_tab_pages[TAB_ALARM] = lv_tabview_add_tab(
        s_tabview, ui_i18n_text(UI_TEXT_CLOCK_TAB_ALARM));
    s_tab_pages[TAB_COUNTDOWN] = lv_tabview_add_tab(
        s_tabview, ui_i18n_text(UI_TEXT_CLOCK_TAB_COUNTDOWN));
    s_tab_pages[TAB_STOPWATCH] = lv_tabview_add_tab(
        s_tabview, ui_i18n_text(UI_TEXT_CLOCK_TAB_STOPWATCH));
    s_tab_pages[TAB_POMODORO] = lv_tabview_add_tab(
        s_tabview, ui_i18n_text(UI_TEXT_POMODORO_TITLE));

    lv_tabview_set_act(s_tabview, (uint32_t)s_tab, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_tabview, tab_change_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    rebuild_all_tabs();
}

/* ---- Tab selection ---- */

static void tab_change_cb(lv_event_t *e)
{
    (void)e;
    if (!s_tabview) {
        return;
    }

    uint16_t active = lv_tabview_get_tab_act(s_tabview);
    if (active < TAB_COUNT && active != (uint16_t)s_tab) {
        if (s_tab == TAB_STOPWATCH) {
            stop_stopwatch_animation();
        } else if (s_tab == TAB_POMODORO) {
            s_pomo_edit_phase = -1;
        }
        s_tab = (clock_tab_t)active;
        if (s_tab == TAB_COUNTDOWN) {
            s_last_cd_shown_sec = -1;
        } else if (s_tab == TAB_STOPWATCH) {
            s_last_sw_shown_sec = -1;
        } else if (s_tab == TAB_POMODORO) {
            s_last_pomo_shown_sec = -1;
        }
        /* VALUE_CHANGED can be nested inside tabview's scroll event. Defer the
         * child-tree rebuild until that event chain has completed. */
        lv_async_call(rebuild_selected_tab_async,
                      (void *)(uintptr_t)s_tab);
    }
}

static void rebuild_selected_tab_async(void *data)
{
    clock_tab_t tab = (clock_tab_t)(uintptr_t)data;
    if (!s_tabview || tab < TAB_ALARM || tab >= TAB_COUNT || tab != s_tab ||
        lv_tabview_get_tab_act(s_tabview) != (uint16_t)tab) {
        return;
    }

    /* Refresh from the service and preserve the previous reset-on-switch
     * behavior for picker/control-local state. */
    rebuild_tab(tab);
}

/* ---- Helpers ---- */

static const char *repeat_text(int repeat_type)
{
    switch (repeat_type) {
        case 1:  return ui_i18n_text(UI_TEXT_REPEAT_DAILY);
        case 2:  return ui_i18n_text(UI_TEXT_REPEAT_WEEKLY);
        case 3:  return ui_i18n_text(UI_TEXT_REPEAT_MONTHLY);
        default: return ui_i18n_text(UI_TEXT_REPEAT_ONCE);
    }
}

static void fmt_hms(char *buf, size_t n, long sec)
{
    if (sec < 0) {
        sec = 0;
    }
    long h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
    if (h > 0) {
        snprintf(buf, n, "%ld:%02ld:%02ld", h, m, s);
    } else {
        snprintf(buf, n, "%02ld:%02ld", m, s);
    }
}

/* ---- Alarm tab ---- */

static void alarm_row_set_enabled(lv_obj_t *row, bool enabled)
{
    if (!row || lv_obj_get_child_cnt(row) < 3) {
        return;
    }

    lv_obj_t *rail = lv_obj_get_child(row, 0);
    lv_obj_t *time_l = lv_obj_get_child(row, 1);
    lv_obj_t *sub = lv_obj_get_child(row, 2);

    lv_obj_set_style_bg_opa(row, enabled ? LV_OPA_COVER : LV_OPA_70, 0);
    lv_obj_set_style_border_color(row,
                                  enabled ? UI_COLOR_WARNING : UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_border_opa(row, enabled ? LV_OPA_40 : LV_OPA_10, 0);
    lv_obj_set_style_bg_color(rail,
                              enabled ? UI_COLOR_WARNING : UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_bg_opa(rail, enabled ? LV_OPA_COVER : LV_OPA_30, 0);
    lv_obj_set_style_text_color(time_l,
                                enabled ? UI_COLOR_TEXT : UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_text_opa(sub, enabled ? LV_OPA_COVER : LV_OPA_60, 0);
}

static void alarm_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    const char *id = (const char *)lv_obj_get_user_data(sw);
    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ui_svc_tm_alarm_enable_set(id, enabled);
    alarm_row_set_enabled(lv_obj_get_parent(sw), enabled);
}

static void alarm_del_confirm_cb(bool ok)
{
    if (ok && s_pending_del_id[0]) {
        ui_svc_tm_alarm_remove(s_pending_del_id);
        rebuild_tab(TAB_ALARM);
    }
}

static void alarm_row_longpress_cb(lv_event_t *e)
{
    const char *id = (const char *)lv_obj_get_user_data(lv_event_get_target(e));
    strncpy(s_pending_del_id, id ? id : "", sizeof(s_pending_del_id) - 1);
    s_pending_del_id[sizeof(s_pending_del_id) - 1] = '\0';
    ui_comp_popup_show(UI_COMP_POPUP_CONFIRM, NULL,
                       ui_i18n_text(UI_TEXT_TM_DELETE_CONFIRM), alarm_del_confirm_cb);
}

static void build_alarm_tab(lv_obj_t *content)
{
    char *json = NULL;
    ty_cJSON *root = NULL, *arr = NULL;
    int n = 0;

    if (ui_svc_tm_alarm_list(&json) != 0 || json == NULL) {
        goto empty;
    }
    root = ty_cJSON_Parse(json);
    arr = root ? ty_cJSON_GetObjectItem(root, "alarms") : NULL;
    n = arr ? ty_cJSON_GetArraySize(arr) : 0;
    if (n == 0) {
        goto empty;
    }

    s_alarm_ids = (char (*)[40])tal_malloc(sizeof(char[40]) * n);
    if (s_alarm_ids) {
        memset(s_alarm_ids, 0, sizeof(char[40]) * n);
    }

    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(content, ui_adapt(14), 0);
    lv_obj_set_style_pad_bottom(content, ui_adapt(14), 0);
    lv_obj_set_style_pad_row(content, ui_adapt(12), 0);
    lv_obj_set_style_pad_hor(content, ui_adapt(14), 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < n; i++) {
        ty_cJSON *it = ty_cJSON_GetArrayItem(arr, i);
        ty_cJSON *jid = ty_cJSON_GetObjectItem(it, "id");
        ty_cJSON *jtime = ty_cJSON_GetObjectItem(it, "time");        /* preformatted string */
        ty_cJSON *jen = ty_cJSON_GetObjectItem(it, "enabled");
        ty_cJSON *jrt = ty_cJSON_GetObjectItem(it, "repeat_type");
        ty_cJSON *jmsg = ty_cJSON_GetObjectItem(it, "message");
        const char *id = (jid && jid->valuestring) ? jid->valuestring : "";
        const char *time_str = (jtime && jtime->valuestring) ? jtime->valuestring : "--:--";
        int en = jen ? jen->valueint : 0;
        int rt = jrt ? jrt->valueint : 0;
        const char *msg = (jmsg && jmsg->valuestring) ? jmsg->valuestring : "";
        const char *time_display = time_str;
        char once_time[6] = {0};
        char once_date[11] = {0};
        const char *iso_sep = strchr(time_str, 'T');
        if (rt == 0 && strlen(time_str) >= 16 && iso_sep == time_str + 10) {
            memcpy(once_date, time_str, 10);
            memcpy(once_time, iso_sep + 1, 5);
            time_display = once_time;
        }
        char *row_id = "";
        if (s_alarm_ids && i < n) {
            strncpy(s_alarm_ids[i], id, 39);
            s_alarm_id_count = i + 1;
            row_id = s_alarm_ids[i];
        }

        lv_obj_t *row = lv_obj_create(content);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), ui_adapt(84));
        lv_obj_set_style_bg_color(row, lv_color_hex(0x24262B), 0);
        lv_obj_set_style_bg_opa(row, en ? LV_OPA_COVER : LV_OPA_70, 0);
        lv_obj_set_style_radius(row, ui_adapt(16), 0);
        lv_obj_set_style_border_width(row, ui_adapt(1), 0);
        lv_obj_set_style_bg_color(row, UI_COLOR_WARNING, LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_10, LV_STATE_PRESSED);
        lv_obj_set_style_pad_hor(row, ui_adapt(16), 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(row, (void *)row_id);
        lv_obj_add_event_cb(row, alarm_row_longpress_cb, LV_EVENT_LONG_PRESSED, NULL);

        /* A slim status rail lets enabled alarms stand out without adding text
         * or changing the switch interaction. */
        lv_obj_t *rail = lv_obj_create(row);
        lv_obj_remove_style_all(rail);
        lv_obj_set_size(rail, ui_adapt(4), ui_adapt(50));
        lv_obj_set_style_radius(rail, LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(rail, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(rail, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *time_l = lv_label_create(row);
        lv_label_set_long_mode(time_l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(time_l, ui_adapt(190));
        lv_label_set_text(time_l, time_display);
        lv_obj_set_style_text_font(time_l, &lv_font_montserrat_28, 0);
        lv_obj_align(time_l, LV_ALIGN_LEFT_MID, ui_adapt(16), ui_adapt(-14));

        lv_obj_t *sub = lv_label_create(row);
        lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
        lv_obj_set_width(sub, ui_adapt(190));
        if (once_date[0]) {
            lv_label_set_text_fmt(sub, "%s %s%s%s", once_date, repeat_text(rt),
                                  (msg[0] ? "  " : ""), msg);
        } else {
            lv_label_set_text_fmt(sub, "%s%s%s", repeat_text(rt),
                                  (msg[0] ? "  " : ""), msg);
        }
        lv_obj_set_style_text_color(sub, UI_COLOR_TEXT_SEC, 0);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, ui_adapt(16), ui_adapt(20));

        lv_obj_t *sw = lv_switch_create(row);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_size(sw, ui_adapt(42), ui_adapt(24));
        lv_obj_set_style_bg_color(sw, UI_COLOR_TEXT_SEC, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(sw, LV_OPA_30, LV_PART_MAIN);
        if (en) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
        lv_obj_set_style_bg_color(sw, UI_COLOR_WARNING,
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, UI_COLOR_TEXT, LV_PART_KNOB);
        lv_obj_set_style_bg_color(sw, UI_COLOR_BG,
                                  LV_PART_KNOB | LV_STATE_CHECKED);
        lv_obj_set_user_data(sw, (void *)row_id);
        lv_obj_add_event_cb(sw, alarm_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

        alarm_row_set_enabled(row, en != 0);
    }
    ty_cJSON_Delete(root);
    ui_svc_tm_json_free(json);
    return;

empty:
    if (root) {
        ty_cJSON_Delete(root);
    }
    if (json) {
        ui_svc_tm_json_free(json);
    }
    {
        lv_obj_t *empty_card = lv_obj_create(content);
        lv_obj_remove_style_all(empty_card);
        lv_obj_set_size(empty_card, ui_adapt(244), ui_adapt(104));
        lv_obj_set_style_bg_color(empty_card, lv_color_hex(0x24262B), 0);
        lv_obj_set_style_bg_opa(empty_card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(empty_card, ui_adapt(16), 0);
        lv_obj_set_style_border_width(empty_card, ui_adapt(1), 0);
        lv_obj_set_style_border_color(empty_card, UI_COLOR_WARNING, 0);
        lv_obj_set_style_border_opa(empty_card, LV_OPA_20, 0);
        lv_obj_clear_flag(empty_card,
                          LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_center(empty_card);

        lv_obj_t *accent = lv_obj_create(empty_card);
        lv_obj_remove_style_all(accent);
        lv_obj_set_size(accent, ui_adapt(42), ui_adapt(4));
        lv_obj_set_style_radius(accent, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(accent, UI_COLOR_WARNING, 0);
        lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
        lv_obj_clear_flag(accent,
                          LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(accent, LV_ALIGN_TOP_MID, 0, ui_adapt(24));

        lv_obj_t *l = lv_label_create(empty_card);
        lv_label_set_text(l, ui_i18n_text(UI_TEXT_TM_EMPTY_ALARM));
        lv_obj_set_style_text_color(l, UI_COLOR_TEXT_SEC, 0);
        lv_obj_align(l, LV_ALIGN_CENTER, 0, ui_adapt(16));
    }
}

/* ---- Countdown tab ---- */

/* Build "00\n01\n...\n(count-1)" zero-padded newline-separated roller options. */
static void fill_roller_opts(char *buf, size_t cap, int count)
{
    size_t pos = 0;
    for (int i = 0; i < count && pos < cap; i++) {
        int n = snprintf(buf + pos, cap - pos, (i == 0) ? "%02d" : "\n%02d", i);
        if (n < 0 || (size_t)n >= cap - pos) {
            break;
        }
        pos += (size_t)n;
    }
}

/* Compact dark roller with the selected value outlined in the timer accent. */
static void style_roller(lv_obj_t *r)
{
    lv_obj_set_style_bg_color(r, lv_color_hex(0x17181B), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_radius(r, ui_adapt(14), 0);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(r, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_text_color(r, UI_COLOR_WARNING, LV_PART_SELECTED);
    lv_obj_set_style_bg_color(r, UI_COLOR_WARNING, LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(r, LV_OPA_10, LV_PART_SELECTED);
    lv_obj_set_style_radius(r, ui_adapt(10), LV_PART_SELECTED);
    lv_obj_set_style_border_width(r, ui_adapt(1), LV_PART_SELECTED);
    lv_obj_set_style_border_color(r, UI_COLOR_WARNING, LV_PART_SELECTED);
    lv_obj_set_style_border_opa(r, LV_OPA_70, LV_PART_SELECTED);
}

static lv_obj_t *create_timer_arc(lv_obj_t *parent, lv_coord_t diameter,
                                  lv_coord_t top_y, lv_color_t accent)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, diameter, diameter);
    lv_obj_align(arc, LV_ALIGN_TOP_MID, 0, top_y);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_range(arc, 0, 1000);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x34373C), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, ui_adapt(6), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, accent, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, ui_adapt(8), LV_PART_INDICATOR);
    return arc;
}

static void set_timer_button_accent(lv_obj_t *btn, lv_color_t accent,
                                    bool filled)
{
    if (!btn) {
        return;
    }
    lv_obj_set_style_bg_color(btn, accent, 0);
    lv_obj_set_style_bg_opa(btn, filled ? LV_OPA_COVER : LV_OPA_10, 0);
    lv_obj_set_style_border_color(btn, accent, 0);
    lv_obj_set_style_border_opa(btn, filled ? LV_OPA_TRANSP : LV_OPA_40, 0);
    lv_obj_set_style_border_width(btn, filled ? 0 : ui_adapt(1), 0);
    lv_color_t text_color = filled ? UI_COLOR_TEXT : accent;
    lv_obj_set_style_text_color(btn, text_color, 0);
    if (lv_obj_get_child_cnt(btn) > 0) {
        lv_obj_set_style_text_color(lv_obj_get_child(btn, 0), text_color, 0);
    }
}

static lv_obj_t *create_timer_action_button(lv_obj_t *parent, const char *text,
                                            lv_color_t accent, bool filled,
                                            ui_comp_btn_cb_t cb)
{
    lv_obj_t *btn = ui_comp_btn_create_styled(
        parent, text, filled ? UI_COMP_BTN_PRIMARY : UI_COMP_BTN_SECONDARY, cb);
    lv_obj_set_size(btn, ui_adapt(112), ui_adapt(44));
    lv_obj_set_style_radius(btn, ui_adapt(22), 0);
    set_timer_button_accent(btn, accent, filled);
    return btn;
}

static void schedule_countdown_rebuild(void)
{
    lv_async_call(rebuild_selected_tab_async,
                  (void *)(uintptr_t)TAB_COUNTDOWN);
}

static void start_countdown(int hours, int minutes, int seconds)
{
    if (hours == 0 && minutes == 0 && seconds == 0) {
        return;
    }
    ui_svc_tm_countdown_create(hours, minutes, seconds);
    s_cd_custom_mode = false;
    s_last_cd_shown_sec = -1;
    schedule_countdown_rebuild();
}

static void cd_preset_cb(lv_event_t *e)
{
    int minutes = (int)(uintptr_t)lv_event_get_user_data(e);
    start_countdown(minutes / 60, minutes % 60, 0);
}

static void cd_custom_cb(lv_event_t *e)
{
    (void)e;
    s_cd_custom_mode = true;
    schedule_countdown_rebuild();
}

static void cd_presets_cb(lv_event_t *e)
{
    (void)e;
    s_cd_custom_mode = false;
    schedule_countdown_rebuild();
}

static void cd_start_cb(lv_event_t *e)
{
    (void)e;
    int h = s_cd_roller_h ? (int)lv_roller_get_selected(s_cd_roller_h) : 0;
    int m = s_cd_roller_m ? (int)lv_roller_get_selected(s_cd_roller_m) : 0;
    int s = s_cd_roller_s ? (int)lv_roller_get_selected(s_cd_roller_s) : 0;
    start_countdown(h, m, s);
}

static void cd_pause_cb(lv_event_t *e)
{
    (void)e;
    ui_svc_tm_countdown_t s;
    if (ui_svc_tm_countdown_query(&s) == 0 && s.active) {
        if (s.paused) {
            ui_svc_tm_countdown_resume();
        } else {
            ui_svc_tm_countdown_pause();
        }
    }
    s_last_cd_shown_sec = -1;
    schedule_countdown_rebuild();
}

static void cd_del_cb(lv_event_t *e)
{
    (void)e;
    ui_svc_tm_countdown_delete();
    s_cd_custom_mode = false;
    s_last_cd_shown_sec = -1;
    schedule_countdown_rebuild();
}

static void center_countdown_button_label(lv_obj_t *btn, lv_color_t color)
{
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_text_color(btn, color, 0);
    if (lv_obj_get_child_cnt(btn) > 0) {
        lv_obj_t *label = lv_obj_get_child(btn, 0);
        lv_obj_set_style_text_color(label, color, 0);
    }
}

static void create_timer_editor_header(lv_obj_t *content, const char *title,
                                       lv_color_t title_color,
                                       ui_comp_btn_cb_t cancel_cb)
{
    lv_obj_t *cancel = ui_comp_btn_create_styled(
        content, ui_i18n_text(UI_TEXT_CANCEL), UI_COMP_BTN_TEXT, cancel_cb);
    lv_obj_set_size(cancel, ui_adapt(64), ui_adapt(36));
    lv_obj_set_style_pad_all(cancel, 0, 0);
    center_countdown_button_label(cancel, UI_COLOR_TEXT_SEC);
    lv_obj_align(cancel, LV_ALIGN_TOP_LEFT, ui_adapt(12), ui_adapt(2));

    lv_obj_t *title_lbl = lv_label_create(content);
    lv_label_set_text(title_lbl, title);
    lv_obj_set_style_text_color(title_lbl, title_color, 0);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_MID, 0, ui_adapt(10));
}

static lv_obj_t *create_countdown_preset_button(lv_obj_t *parent, int minutes)
{
    char text[20];
    snprintf(text, sizeof(text), ui_i18n_text(UI_TEXT_TM_MINUTES_FMT),
             minutes);
    lv_obj_t *btn = ui_comp_btn_create_styled(
        parent, text, UI_COMP_BTN_SECONDARY, NULL);
    lv_obj_set_size(btn, ui_adapt(89), ui_adapt(44));
    lv_obj_set_style_radius(btn, ui_adapt(22), 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_80, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    center_countdown_button_label(btn, UI_COLOR_TEXT);
    lv_obj_add_event_cb(btn, cd_preset_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)minutes);
    return btn;
}

static void build_countdown_presets(lv_obj_t *content)
{
    static const int preset_minutes[] = { 1, 5, 10, 30, 45, 60 };

    lv_obj_t *panel = lv_obj_create(content);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, ui_adapt(284), ui_adapt(156));
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, ui_adapt(10), 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, ui_adapt(34));

    for (int row_idx = 0; row_idx < 2; row_idx++) {
        lv_obj_t *row = lv_obj_create(panel);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, ui_adapt(284), ui_adapt(44));
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, ui_adapt(8), 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        for (int col = 0; col < 3; col++) {
            create_countdown_preset_button(
                row, preset_minutes[row_idx * 3 + col]);
        }
    }

    lv_obj_t *custom = ui_comp_btn_create_styled(
        panel, ui_i18n_text(UI_TEXT_TM_CUSTOM), UI_COMP_BTN_SECONDARY,
        cd_custom_cb);
    lv_obj_set_size(custom, ui_adapt(284), ui_adapt(48));
    lv_obj_set_style_radius(custom, ui_adapt(24), 0);
    lv_obj_set_style_bg_color(custom, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(custom, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(custom, 0, 0);
    center_countdown_button_label(custom, UI_COLOR_TEXT);
}

static void create_countdown_roller(lv_obj_t *picker, lv_obj_t **roller,
                                    int count, const char *unit)
{
    char opts[256];
    fill_roller_opts(opts, sizeof(opts), count);
    *roller = lv_roller_create(picker);
    lv_roller_set_options(*roller, opts, LV_ROLLER_MODE_NORMAL);
    lv_obj_set_width(*roller, ui_adapt(58));
    style_roller(*roller);
    lv_roller_set_visible_row_count(*roller, 3);

    lv_obj_t *unit_lbl = lv_label_create(picker);
    lv_label_set_text(unit_lbl, unit);
    lv_obj_set_style_text_color(unit_lbl, UI_COLOR_TEXT_SEC, 0);
}

static void build_countdown_custom(lv_obj_t *content)
{
    create_timer_editor_header(
        content, ui_i18n_text(UI_TEXT_TM_CUSTOM), UI_COLOR_WARNING,
        cd_presets_cb);

    lv_obj_t *picker = lv_obj_create(content);
    lv_obj_remove_style_all(picker);
    lv_obj_set_size(picker, ui_adapt(292), ui_adapt(170));
    lv_obj_set_flex_flow(picker, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(picker, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(picker, ui_adapt(5), 0);
    lv_obj_clear_flag(picker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(picker, LV_ALIGN_TOP_MID, 0, ui_adapt(52));

    create_countdown_roller(picker, &s_cd_roller_h, 24,
                            ui_i18n_text(UI_TEXT_TM_HOUR_UNIT));
    create_countdown_roller(picker, &s_cd_roller_m, 60,
                            ui_i18n_text(UI_TEXT_TM_MIN_UNIT));
    create_countdown_roller(picker, &s_cd_roller_s, 60,
                            ui_i18n_text(UI_TEXT_TM_SEC_UNIT));

    lv_obj_t *go = create_timer_action_button(
        content, ui_i18n_text(UI_TEXT_TM_START), UI_COLOR_WARNING, true,
        cd_start_cb);
    lv_obj_set_width(go, ui_adapt(172));
    center_countdown_button_label(go, UI_COLOR_BG);
    lv_obj_align(go, LV_ALIGN_BOTTOM_MID, 0, ui_adapt(-24));
}

static void build_countdown_tab(lv_obj_t *content)
{
    ui_svc_tm_countdown_t s;
    bool active = (ui_svc_tm_countdown_query(&s) == 0 && s.active);

    if (active) {
        s_cd_custom_mode = false;
        s_cd_arc = create_timer_arc(
            content, ui_adapt(180), ui_adapt(26), UI_COLOR_WARNING);
        lv_obj_set_style_arc_rounded(s_cd_arc, true, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(s_cd_arc, true, LV_PART_INDICATOR);

        /* Countdown ring depletes: full at the start, empty at zero (value =
         * remaining/total, not elapsed/total). */
        int32_t progress = 0;
        if (s.duration_sec > 0) {
            long rem = s.remaining_sec;
            if (rem < 0) {
                rem = 0;
            }
            if (rem > s.duration_sec) {
                rem = s.duration_sec;
            }
            progress = (int32_t)(rem * 1000 / s.duration_sec);
        }
        lv_arc_set_value(s_cd_arc, progress);

        /* Remaining time in the center of the ring. */
        s_cd_timer_face = lv_label_create(content);
        s_cd_face_large = s.remaining_sec < 3600;
        lv_obj_set_style_text_font(
            s_cd_timer_face,
            s_cd_face_large ? &lv_font_montserrat_48 : &lv_font_montserrat_28,
            0);
        lv_obj_set_style_text_color(
            s_cd_timer_face, s.paused ? UI_COLOR_TEXT_SEC : UI_COLOR_TEXT, 0);
        char b[16];
        fmt_hms(b, sizeof(b), s.remaining_sec);
        lv_label_set_text(s_cd_timer_face, b);
        lv_obj_align_to(s_cd_timer_face, s_cd_arc, LV_ALIGN_CENTER, 0, 0);
        s_last_cd_shown_sec = s.remaining_sec;

        lv_obj_t *p = create_timer_action_button(
            content,
            ui_i18n_text(s.paused ? UI_TEXT_TM_RESUME : UI_TEXT_TM_PAUSE),
            s.paused ? UI_COLOR_SUCCESS : UI_COLOR_WARNING, true, cd_pause_cb);
        lv_obj_set_width(p, ui_adapt(100));
        center_countdown_button_label(p, UI_COLOR_TEXT);
        lv_obj_align(p, LV_ALIGN_TOP_MID, ui_adapt(56), ui_adapt(224));
        lv_obj_t *d = create_timer_action_button(
            content, ui_i18n_text(UI_TEXT_CANCEL), UI_COLOR_TEXT_SEC, false,
            cd_del_cb);
        lv_obj_set_width(d, ui_adapt(100));
        center_countdown_button_label(d, UI_COLOR_TEXT_SEC);
        lv_obj_align(d, LV_ALIGN_TOP_MID, ui_adapt(-56), ui_adapt(224));
    } else if (s_cd_custom_mode) {
        s_last_cd_shown_sec = -1;
        build_countdown_custom(content);
    } else {
        s_last_cd_shown_sec = -1;
        build_countdown_presets(content);
    }
}

/* ---- Stopwatch tab ---- */

static void clear_stopwatch_laps(void)
{
    memset(s_sw_laps, 0, sizeof(s_sw_laps));
    s_sw_lap_count = 0;
    s_sw_lap_seq = 0;
}

static void fmt_stopwatch_main(char *buf, size_t n, uint64_t elapsed_ms)
{
    uint64_t sec = elapsed_ms / 1000u;
    uint64_t h = sec / 3600u, m = (sec % 3600u) / 60u, s = sec % 60u;
    if (h > 0) {
        snprintf(buf, n, "%llu:%02llu:%02llu",
                 (unsigned long long)h, (unsigned long long)m,
                 (unsigned long long)s);
    } else {
        snprintf(buf, n, "%02llu:%02llu", (unsigned long long)m,
                 (unsigned long long)s);
    }
}

static void fmt_stopwatch(char *buf, size_t n, uint64_t elapsed_ms)
{
    char main[20];
    fmt_stopwatch_main(main, sizeof(main), elapsed_ms);
    unsigned centis = (unsigned)((elapsed_ms / 10u) % 100u);
    snprintf(buf, n, "%s.%02u", main, centis);
}

static void refresh_stopwatch_time(uint64_t elapsed_ms)
{
    long sec = (long)(elapsed_ms / 1000u);
    if (s_sw_timer_face && sec != s_last_sw_shown_sec) {
        char main[20];
        fmt_stopwatch_main(main, sizeof(main), elapsed_ms);
        lv_label_set_text(s_sw_timer_face, main);
        s_last_sw_shown_sec = sec;
    }

    int centis = (int)((elapsed_ms / 10u) % 100u);
    if (s_sw_centis_face && centis != s_last_sw_shown_centis) {
        char fraction[3];
        snprintf(fraction, sizeof(fraction), "%02d", centis);
        lv_label_set_text(s_sw_centis_face, fraction);
        s_last_sw_shown_centis = centis;
    }
}

static void stopwatch_anim_set(void *obj, int32_t value)
{
    (void)value;
    if (obj != s_sw_centis_face || s_tab != TAB_STOPWATCH) {
        return;
    }

    ui_svc_tm_stopwatch_t s;
    if (ui_svc_tm_stopwatch_query(&s) != 0 || !s.active || s.paused) {
        return;
    }
    refresh_stopwatch_time(s.elapsed_ms);
}

static void stop_stopwatch_animation(void)
{
    if (s_sw_anim_active && s_sw_centis_face) {
        lv_anim_del(s_sw_centis_face, stopwatch_anim_set);
    }
    s_sw_anim_active = false;
}

static void start_stopwatch_animation(void)
{
    if (s_sw_anim_active || !s_sw_centis_face || s_tab != TAB_STOPWATCH ||
        !s_sw_view_active || s_sw_view_paused) {
        return;
    }

    lv_anim_init(&s_sw_anim);
    lv_anim_set_var(&s_sw_anim, s_sw_centis_face);
    lv_anim_set_exec_cb(&s_sw_anim, stopwatch_anim_set);
    lv_anim_set_values(&s_sw_anim, 0, 1000);
    lv_anim_set_time(&s_sw_anim, 1000);
    lv_anim_set_repeat_count(&s_sw_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&s_sw_anim);
    s_sw_anim_active = true;
}

static void schedule_stopwatch_rebuild(void)
{
    lv_async_call(rebuild_selected_tab_async,
                  (void *)(uintptr_t)TAB_STOPWATCH);
}

static void sw_start_cb(lv_event_t *e)
{
    (void)e;
    ui_svc_tm_stopwatch_t s;
    if (ui_svc_tm_stopwatch_query(&s) == 0 && s.active) {
        if (s.paused) {
            ui_svc_tm_stopwatch_resume();
        } else {
            ui_svc_tm_stopwatch_pause();
        }
    } else {
        clear_stopwatch_laps();
        ui_svc_tm_stopwatch_start();
    }
    s_last_sw_shown_sec = -1;
    s_last_sw_shown_centis = -1;
    schedule_stopwatch_rebuild();
}

static void sw_lap_cb(lv_event_t *e)
{
    (void)e;
    ui_svc_tm_stopwatch_t s;
    if (ui_svc_tm_stopwatch_query(&s) != 0 || !s.active || s.paused) {
        return;
    }

    unsigned move_count = s_sw_lap_count;
    if (move_count >= STOPWATCH_LAP_VISIBLE_MAX) {
        move_count = STOPWATCH_LAP_VISIBLE_MAX - 1;
    }
    if (move_count > 0) {
        memmove(&s_sw_laps[1], &s_sw_laps[0],
                move_count * sizeof(s_sw_laps[0]));
    }
    s_sw_laps[0].number = ++s_sw_lap_seq;
    s_sw_laps[0].elapsed_ms = s.elapsed_ms;
    if (s_sw_lap_count < STOPWATCH_LAP_VISIBLE_MAX) {
        s_sw_lap_count++;
    }
    schedule_stopwatch_rebuild();
}

static void sw_reset_cb(lv_event_t *e)
{
    (void)e;
    ui_svc_tm_stopwatch_reset();
    clear_stopwatch_laps();
    s_last_sw_shown_sec = -1;
    s_last_sw_shown_centis = -1;
    schedule_stopwatch_rebuild();
}

static void build_stopwatch_tab(lv_obj_t *content)
{
    ui_svc_tm_stopwatch_t s;
    bool active = (ui_svc_tm_stopwatch_query(&s) == 0 && s.active);
    if (!active) {
        clear_stopwatch_laps();
    } else if (s_sw_lap_count > 0 &&
               s.elapsed_ms < s_sw_laps[0].elapsed_ms) {
        clear_stopwatch_laps();
    }

    lv_obj_t *time_stage = lv_obj_create(content);
    lv_obj_remove_style_all(time_stage);
    lv_obj_set_size(time_stage, ui_adapt(290), ui_adapt(52));
    lv_obj_clear_flag(time_stage,
                      LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(time_stage, LV_ALIGN_TOP_MID, 0, ui_adapt(12));

    s_sw_timer_face = lv_label_create(time_stage);
    lv_obj_set_width(s_sw_timer_face, ui_adapt(190));
    lv_obj_set_style_text_font(s_sw_timer_face, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_sw_timer_face, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_align(s_sw_timer_face, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_sw_timer_face, LV_ALIGN_LEFT_MID, 0, 0);

    /* Render the decimal separator as a shape instead of a font glyph. The
     * timer-face subset is intentionally minimal, so this also avoids future
     * fallback/missing-glyph regressions. */
    lv_obj_t *separator = lv_obj_create(time_stage);
    lv_obj_remove_style_all(separator);
    lv_obj_set_size(separator, ui_adapt(5), ui_adapt(5));
    lv_obj_set_style_radius(separator, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(separator, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
    lv_obj_clear_flag(separator,
                      LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(separator, LV_ALIGN_LEFT_MID, ui_adapt(200), ui_adapt(13));

    s_sw_centis_face = lv_label_create(time_stage);
    lv_obj_set_width(s_sw_centis_face, ui_adapt(70));
    lv_obj_set_style_text_font(s_sw_centis_face, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_sw_centis_face, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_text_align(s_sw_centis_face, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(s_sw_centis_face, LV_ALIGN_LEFT_MID,
                 ui_adapt(212), ui_adapt(7));

    s_last_sw_shown_sec = -1;
    s_last_sw_shown_centis = -1;
    refresh_stopwatch_time(active ? s.elapsed_ms : 0);

    lv_obj_t *lap_list = lv_obj_create(content);
    lv_obj_remove_style_all(lap_list);
    lv_obj_set_size(lap_list, ui_adapt(210), ui_adapt(112));
    lv_obj_set_flex_flow(lap_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(lap_list, ui_adapt(2), 0);
    lv_obj_clear_flag(lap_list, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(lap_list, LV_ALIGN_TOP_MID, 0, ui_adapt(88));

    for (unsigned i = 0; i < s_sw_lap_count; i++) {
        lv_obj_t *row = lv_obj_create(lap_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, ui_adapt(26));
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        char lap_name[20];
        snprintf(lap_name, sizeof(lap_name),
                 ui_i18n_text(UI_TEXT_TM_LAP_FMT), s_sw_laps[i].number);
        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, lap_name);
        lv_obj_set_style_text_color(name, UI_COLOR_TEXT_SEC, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);

        char lap_time[20];
        fmt_stopwatch(lap_time, sizeof(lap_time), s_sw_laps[i].elapsed_ms);
        lv_obj_t *time = lv_label_create(row);
        lv_label_set_text(time, lap_time);
        lv_obj_set_style_text_color(time, UI_COLOR_TEXT, 0);
        lv_obj_align(time, LV_ALIGN_RIGHT_MID, 0, 0);
    }

    s_sw_view_active = active;
    s_sw_view_paused = active ? s.paused : 0;

    const char *left_text = active && s.paused
                                ? ui_i18n_text(UI_TEXT_CANCEL)
                                : ui_i18n_text(UI_TEXT_TM_LAP);
    lv_obj_t *left = ui_comp_btn_circle_text_create(
        content, left_text, UI_COLOR_TEXT_SEC,
        active ? (s.paused ? sw_reset_cb : sw_lap_cb) : NULL);
    lv_obj_align(left, LV_ALIGN_BOTTOM_MID, ui_adapt(-92), ui_adapt(-16));
    if (!active) {
        ui_comp_btn_set_enabled(left, false);
    }

    int action_key = !active ? UI_TEXT_TM_START
                     : s.paused ? UI_TEXT_TM_RESUME
                                : UI_TEXT_TM_PAUSE;
    lv_color_t action_color = (active && !s.paused) ? UI_COLOR_ERROR
                                                     : UI_COLOR_SUCCESS;
    lv_obj_t *action = ui_comp_btn_circle_text_create(
        content, ui_i18n_text(action_key), action_color, sw_start_cb);
    lv_obj_align(action, LV_ALIGN_BOTTOM_MID, ui_adapt(92), ui_adapt(-16));

    if (active && !s.paused) {
        start_stopwatch_animation();
    }
}

/* ---- Pomodoro tab ---- */

#define POMODORO_PHASE_WORK  0
#define POMODORO_PHASE_SHORT 1
#define POMODORO_PHASE_LONG  2
#define POMODORO_CYCLE_MARKER_MAX 12

static const char *pomodoro_phase_text(int phase)
{
    switch (phase) {
        case POMODORO_PHASE_SHORT:
            return ui_i18n_text(UI_TEXT_POMODORO_PHASE_SHORT);
        case POMODORO_PHASE_LONG:
            return ui_i18n_text(UI_TEXT_POMODORO_PHASE_LONG);
        default: return ui_i18n_text(UI_TEXT_POMODORO_PHASE_WORK);
    }
}

static lv_color_t pomodoro_phase_color(int phase)
{
    if (phase == POMODORO_PHASE_SHORT) {
        return lv_color_hex(0x35B8C4);
    }
    if (phase == POMODORO_PHASE_LONG) {
        return lv_color_hex(0x329DDE);
    }
    return lv_color_hex(0xE85A5A);
}

static long pomodoro_phase_total_sec(int phase,
                                     const ui_svc_tm_pomodoro_cfg_t *cfg)
{
    int mins = (phase == POMODORO_PHASE_SHORT) ? cfg->short_break_duration
             : (phase == POMODORO_PHASE_LONG) ? cfg->long_break_duration
                                              : cfg->work_duration;
    return (long)mins * 60;
}

static int pomodoro_phase_minutes(int phase,
                                  const ui_svc_tm_pomodoro_cfg_t *cfg)
{
    if (phase == POMODORO_PHASE_SHORT) {
        return cfg->short_break_duration;
    }
    if (phase == POMODORO_PHASE_LONG) {
        return cfg->long_break_duration;
    }
    return cfg->work_duration;
}

static int pomodoro_phase_max_minutes(int phase)
{
    return phase == POMODORO_PHASE_SHORT ? 30 :
           phase == POMODORO_PHASE_LONG ? 60 : 120;
}

static int pomodoro_phase_min_minutes(int phase)
{
    return phase == POMODORO_PHASE_LONG ? 5 : 1;
}

static void pomodoro_phase_minutes_set(int phase, int minutes)
{
    if (phase == POMODORO_PHASE_SHORT) {
        s_pomo_cfg.short_break_duration = minutes;
    } else if (phase == POMODORO_PHASE_LONG) {
        s_pomo_cfg.long_break_duration = minutes;
    } else {
        s_pomo_cfg.work_duration = minutes;
    }
}

static int16_t pomodoro_phase_progress(
    int phase, long remaining_sec, const ui_svc_tm_pomodoro_cfg_t *cfg)
{
    long total = pomodoro_phase_total_sec(phase, cfg);
    if (total <= 0) {
        return 0;
    }
    if (remaining_sec < 0) {
        remaining_sec = 0;
    } else if (remaining_sec > total) {
        remaining_sec = total;
    }
    return (int16_t)((remaining_sec * 1000) / total);
}

static void schedule_pomodoro_rebuild(void)
{
    lv_async_call(rebuild_selected_tab_async,
                  (void *)(uintptr_t)TAB_POMODORO);
}

static void pomodoro_primary_cb(lv_event_t *e)
{
    (void)e;
    ui_svc_tm_pomodoro_t s;
    if (ui_svc_tm_pomodoro_query(&s) == 0 && s.active) {
        if (s.paused) {
            ui_svc_tm_pomodoro_resume();
        } else {
            ui_svc_tm_pomodoro_pause();
        }
    } else {
        ui_svc_tm_pomodoro_start(&s_pomo_cfg);
    }
    s_last_pomo_shown_sec = -1;
    schedule_pomodoro_rebuild();
}

static void pomodoro_stop_cb(lv_event_t *e)
{
    (void)e;
    ui_svc_tm_pomodoro_stop();
    s_last_pomo_shown_sec = -1;
    schedule_pomodoro_rebuild();
}

static void pomodoro_edit_cb(lv_event_t *e)
{
    s_pomo_edit_phase = (int)(uintptr_t)lv_event_get_user_data(e);
    schedule_pomodoro_rebuild();
}

static void pomodoro_edit_cancel_cb(lv_event_t *e)
{
    (void)e;
    s_pomo_edit_phase = -1;
    schedule_pomodoro_rebuild();
}

static void pomodoro_edit_done_cb(lv_event_t *e)
{
    (void)e;
    if (s_pomo_edit_phase >= POMODORO_PHASE_WORK &&
        s_pomo_edit_phase <= POMODORO_PHASE_LONG && s_pomo_roller) {
        int minutes = (int)lv_roller_get_selected(s_pomo_roller) +
                      pomodoro_phase_min_minutes(s_pomo_edit_phase);
        pomodoro_phase_minutes_set(s_pomo_edit_phase, minutes);
    }
    s_pomo_edit_phase = -1;
    schedule_pomodoro_rebuild();
}

static void refresh_pomodoro_cycle_markers(
    const ui_svc_tm_pomodoro_t *s)
{
    unsigned total = (unsigned)s->cfg.work_sessions_before_long_break;
    if (total == 0) {
        total = 1;
    } else if (total > POMODORO_CYCLE_MARKER_MAX) {
        total = POMODORO_CYCLE_MARKER_MAX;
    }
    if (s_last_pomo_cycle == s->cycle &&
        s_last_pomo_cycle_total == total &&
        s_last_pomo_completed == s->completed) {
        return;
    }

    unsigned completed = total ? (s->completed % total) : 0;
    if (s->phase == POMODORO_PHASE_LONG && s->completed > 0 &&
        completed == 0) {
        completed = total;
    }
    unsigned current = total && s->cycle > 0 ? ((s->cycle - 1) % total) : 0;
    lv_color_t phase_color = pomodoro_phase_color(s->phase);

    for (unsigned i = 0; i < s_pomo_cycle_marker_count; i++) {
        lv_obj_t *marker = s_pomo_cycle_markers[i];
        if (!marker) {
            continue;
        }
        bool done = i < completed;
        bool now = s->phase == POMODORO_PHASE_WORK && i == current;
        lv_color_t accent = done ? UI_COLOR_WARNING :
                            now ? phase_color : UI_COLOR_TEXT_SEC;
        lv_obj_set_style_bg_color(marker, accent, 0);
        lv_obj_set_style_bg_opa(marker, done ? LV_OPA_30 :
                                        now ? LV_OPA_20 : LV_OPA_10, 0);
        lv_obj_set_style_border_color(marker, accent, 0);
        lv_obj_set_style_border_opa(marker,
                                    (done || now) ? LV_OPA_COVER : LV_OPA_30,
                                    0);
        if (lv_obj_get_child_cnt(marker) > 0) {
            lv_obj_set_style_text_color(lv_obj_get_child(marker, 0), accent, 0);
        }
    }
    s_last_pomo_cycle = s->cycle;
    s_last_pomo_cycle_total = total;
    s_last_pomo_completed = s->completed;
}

static void refresh_pomodoro_view(void)
{
    ui_svc_tm_pomodoro_t s;
    bool ok = (ui_svc_tm_pomodoro_query(&s) == 0);
    bool active = ok && s.active;
    if (!active) {
        return;
    }
    const ui_svc_tm_pomodoro_cfg_t *cfg = &s.cfg;

    int visual_phase = s.phase + 1;
    if (visual_phase != s_last_pomo_visual_phase) {
        lv_color_t c = pomodoro_phase_color(s.phase);
        if (s_pomo_chip_lbl) {
            lv_label_set_text(s_pomo_chip_lbl, pomodoro_phase_text(s.phase));
            lv_obj_set_style_text_color(s_pomo_chip_lbl, c, 0);
        }
        if (s_pomo_arc) {
            lv_obj_set_style_arc_color(s_pomo_arc, c, LV_PART_INDICATOR);
        }
        s_last_pomo_visual_phase = visual_phase;
    }
    if (s_pomo_arc) {
        int16_t progress =
            pomodoro_phase_progress(s.phase, s.remaining_sec, cfg);
        if (progress != s_last_pomo_progress) {
            lv_arc_set_value(s_pomo_arc, progress);
            s_last_pomo_progress = progress;
        }
    }
    if (s_pomo_face) {
        long sec = s.remaining_sec;
        if (sec != s_last_pomo_shown_sec) {
            char b[16];
            snprintf(b, sizeof(b), "%02ld:%02ld", sec / 60, sec % 60);
            lv_label_set_text(s_pomo_face, b);
            lv_obj_align_to(s_pomo_face, s_pomo_arc, LV_ALIGN_CENTER,
                            0, ui_adapt(11));
            s_last_pomo_shown_sec = sec;
        }
    }
    refresh_pomodoro_cycle_markers(&s);
    if (s_pomo_btn_primary) {
        int key = s.paused ? UI_TEXT_TM_RESUME : UI_TEXT_TM_PAUSE;
        if (key != s_last_pomo_action_key) {
            ui_comp_btn_set_text(s_pomo_btn_primary, ui_i18n_text(key));
            s_last_pomo_action_key = key;
        }
    }
    if (s_pomo_btn_primary) {
        lv_color_t action = s.paused ? UI_COLOR_SUCCESS : UI_COLOR_WARNING;
        lv_obj_set_style_bg_color(s_pomo_btn_primary, action, 0);
        lv_obj_set_style_border_color(s_pomo_btn_primary, action, 0);
        if (lv_obj_get_child_cnt(s_pomo_btn_primary) > 0) {
            lv_obj_set_style_text_color(
                lv_obj_get_child(s_pomo_btn_primary, 0), action, 0);
        }
    }
}

static lv_obj_t *create_pomodoro_duration_card(lv_obj_t *parent, int phase,
                                                int minutes)
{
    lv_color_t accent = pomodoro_phase_color(phase);
    lv_obj_t *btn = ui_comp_btn_create_styled(
        parent, NULL, UI_COMP_BTN_SECONDARY, NULL);
    lv_obj_set_size(btn, ui_adapt(88), ui_adapt(122));
    lv_obj_set_style_radius(btn, ui_adapt(16), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x24262B), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, ui_adapt(1), 0);
    lv_obj_set_style_border_color(btn, accent, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, pomodoro_edit_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)phase);

    lv_obj_t *line = lv_obj_create(btn);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, ui_adapt(34), ui_adapt(4));
    lv_obj_set_style_radius(line, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(line, accent, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, ui_adapt(13));

    char time[12];
    snprintf(time, sizeof(time), "%02d:00", minutes);
    lv_obj_t *time_lbl = lv_label_create(btn);
    lv_label_set_text(time_lbl, time);
    lv_obj_set_style_text_font(time_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(time_lbl, accent, 0);
    lv_obj_align(time_lbl, LV_ALIGN_TOP_MID, 0, ui_adapt(33));

    lv_obj_t *phase_lbl = lv_label_create(btn);
    lv_label_set_text(phase_lbl, pomodoro_phase_text(phase));
    lv_obj_set_style_text_color(phase_lbl, UI_COLOR_TEXT, 0);
    lv_obj_align(phase_lbl, LV_ALIGN_BOTTOM_MID, 0, ui_adapt(-15));
    return btn;
}

static void build_pomodoro_idle(lv_obj_t *content)
{
    lv_obj_t *row = lv_obj_create(content);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, ui_adapt(280), ui_adapt(126));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, ui_adapt(8), 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, ui_adapt(36));

    create_pomodoro_duration_card(
        row, POMODORO_PHASE_WORK, s_pomo_cfg.work_duration);
    create_pomodoro_duration_card(
        row, POMODORO_PHASE_SHORT, s_pomo_cfg.short_break_duration);
    create_pomodoro_duration_card(
        row, POMODORO_PHASE_LONG, s_pomo_cfg.long_break_duration);

    s_pomo_btn_primary = create_timer_action_button(
        content, ui_i18n_text(UI_TEXT_TM_START), UI_COLOR_WARNING, true,
        pomodoro_primary_cb);
    lv_obj_set_width(s_pomo_btn_primary, ui_adapt(172));
    center_countdown_button_label(s_pomo_btn_primary, UI_COLOR_BG);
    lv_obj_align(s_pomo_btn_primary, LV_ALIGN_BOTTOM_MID, 0, ui_adapt(-24));
}

static void fill_pomodoro_minute_opts(char *buf, size_t cap,
                                      int min_minutes, int max_minutes)
{
    size_t pos = 0;
    for (int i = min_minutes; i <= max_minutes && pos < cap; i++) {
        int n = snprintf(buf + pos, cap - pos,
                         i == min_minutes ? "%02d" : "\n%02d", i);
        if (n < 0 || (size_t)n >= cap - pos) {
            break;
        }
        pos += (size_t)n;
    }
}

static void build_pomodoro_editor(lv_obj_t *content)
{
    int phase = s_pomo_edit_phase;
    lv_color_t accent = pomodoro_phase_color(phase);

    create_timer_editor_header(
        content, pomodoro_phase_text(phase), accent,
        pomodoro_edit_cancel_cb);

    lv_obj_t *picker = lv_obj_create(content);
    lv_obj_remove_style_all(picker);
    lv_obj_set_size(picker, ui_adapt(178), ui_adapt(154));
    lv_obj_set_style_bg_color(picker, lv_color_hex(0x24262B), 0);
    lv_obj_set_style_bg_opa(picker, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(picker, ui_adapt(18), 0);
    lv_obj_set_style_border_width(picker, ui_adapt(1), 0);
    lv_obj_set_style_border_color(picker, accent, 0);
    lv_obj_set_style_border_opa(picker, LV_OPA_30, 0);
    lv_obj_clear_flag(picker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(picker, LV_ALIGN_TOP_MID, 0, ui_adapt(52));

    char opts[512];
    fill_pomodoro_minute_opts(
        opts, sizeof(opts), pomodoro_phase_min_minutes(phase),
        pomodoro_phase_max_minutes(phase));
    s_pomo_roller = lv_roller_create(picker);
    lv_roller_set_options(s_pomo_roller, opts, LV_ROLLER_MODE_NORMAL);
    lv_obj_set_size(s_pomo_roller, ui_adapt(104), ui_adapt(126));
    style_roller(s_pomo_roller);
    lv_obj_set_style_border_color(s_pomo_roller, accent, LV_PART_SELECTED);
    lv_obj_set_style_text_color(s_pomo_roller, accent, LV_PART_SELECTED);
    lv_obj_set_style_bg_color(s_pomo_roller, accent, LV_PART_SELECTED);
    lv_roller_set_visible_row_count(s_pomo_roller, 3);
    int selected = pomodoro_phase_minutes(phase, &s_pomo_cfg) -
                   pomodoro_phase_min_minutes(phase);
    lv_roller_set_selected(s_pomo_roller, (uint16_t)selected, LV_ANIM_OFF);
    lv_obj_align(s_pomo_roller, LV_ALIGN_CENTER, ui_adapt(-15), 0);

    lv_obj_t *unit = lv_label_create(picker);
    lv_label_set_text(unit, ui_i18n_text(UI_TEXT_TM_MIN_UNIT));
    lv_obj_set_style_text_color(unit, UI_COLOR_TEXT_SEC, 0);
    lv_obj_align(unit, LV_ALIGN_RIGHT_MID, ui_adapt(-13), 0);

    lv_obj_t *done = create_timer_action_button(
        content, ui_i18n_text(UI_TEXT_TM_DONE), UI_COLOR_WARNING, true,
        pomodoro_edit_done_cb);
    lv_obj_set_width(done, ui_adapt(172));
    center_countdown_button_label(done, UI_COLOR_BG);
    lv_obj_align(done, LV_ALIGN_BOTTOM_MID, 0, ui_adapt(-24));
}

static void build_pomodoro_cycle_row(lv_obj_t *content,
                                      const ui_svc_tm_pomodoro_t *s)
{
    unsigned total = (unsigned)s->cfg.work_sessions_before_long_break;
    if (total == 0) {
        total = 1;
    } else if (total > POMODORO_CYCLE_MARKER_MAX) {
        total = POMODORO_CYCLE_MARKER_MAX;
    }
    lv_coord_t marker_size = ui_adapt(total <= 6 ? 28 : total <= 9 ? 24 : 20);
    lv_coord_t gap = ui_adapt(total <= 8 ? 7 : 3);

    lv_obj_t *row = lv_obj_create(content);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, ui_adapt(292), ui_adapt(30));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, gap, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, ui_adapt(8));

    for (unsigned i = 0; i < total; i++) {
        lv_obj_t *marker = lv_obj_create(row);
        lv_obj_remove_style_all(marker);
        lv_obj_set_size(marker, marker_size, marker_size);
        lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(marker, ui_adapt(1), 0);
        lv_obj_clear_flag(marker,
                          LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        char number[4];
        snprintf(number, sizeof(number), "%u", i + 1);
        lv_obj_t *label = lv_label_create(marker);
        lv_label_set_text(label, number);
        lv_obj_center(label);
        s_pomo_cycle_markers[i] = marker;
    }
    s_pomo_cycle_marker_count = total;
}

static void build_pomodoro_running(lv_obj_t *content,
                                    const ui_svc_tm_pomodoro_t *s)
{
    lv_color_t accent = pomodoro_phase_color(s->phase);
    build_pomodoro_cycle_row(content, s);

    s_pomo_arc = create_timer_arc(
        content, ui_adapt(178), ui_adapt(45), accent);
    lv_arc_set_bg_angles(s_pomo_arc, 5, 355);
    lv_obj_set_style_arc_rounded(s_pomo_arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_pomo_arc, true, LV_PART_INDICATOR);

    s_pomo_chip_lbl = lv_label_create(content);
    lv_label_set_text(s_pomo_chip_lbl, pomodoro_phase_text(s->phase));
    lv_obj_set_style_text_color(s_pomo_chip_lbl, accent, 0);
    lv_obj_align_to(s_pomo_chip_lbl, s_pomo_arc, LV_ALIGN_CENTER,
                    0, ui_adapt(-31));

    s_pomo_face = lv_label_create(content);
    lv_label_set_text(s_pomo_face, "00:00");
    lv_obj_set_style_text_font(s_pomo_face, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_pomo_face, UI_COLOR_TEXT, 0);
    lv_obj_align_to(s_pomo_face, s_pomo_arc, LV_ALIGN_CENTER,
                    0, ui_adapt(11));

    s_pomo_btn_stop = ui_comp_btn_circle_text_create(
        content, ui_i18n_text(UI_TEXT_CANCEL), UI_COLOR_TEXT_SEC,
        pomodoro_stop_cb);
    lv_obj_align(s_pomo_btn_stop, LV_ALIGN_BOTTOM_MID,
                 ui_adapt(-92), ui_adapt(-18));

    s_pomo_btn_primary = ui_comp_btn_circle_text_create(
        content,
        ui_i18n_text(s->paused ? UI_TEXT_TM_RESUME : UI_TEXT_TM_PAUSE),
        s->paused ? UI_COLOR_SUCCESS : UI_COLOR_WARNING,
        pomodoro_primary_cb);
    lv_obj_align(s_pomo_btn_primary, LV_ALIGN_BOTTOM_MID,
                 ui_adapt(92), ui_adapt(-18));
}

static void build_pomodoro_tab(lv_obj_t *content)
{
    ui_svc_tm_pomodoro_t s;
    bool active = (ui_svc_tm_pomodoro_query(&s) == 0 && s.active);
    s_pomo_view_active = active ? 1 : 0;

    if (active) {
        s_pomo_edit_phase = -1;
        build_pomodoro_running(content, &s);
    } else if (s_pomo_edit_phase >= POMODORO_PHASE_WORK &&
               s_pomo_edit_phase <= POMODORO_PHASE_LONG) {
        build_pomodoro_editor(content);
    } else {
        build_pomodoro_idle(content);
    }

    refresh_pomodoro_view();
}

/* ---- Tab content rebuild ---- */

static void rebuild_tab(clock_tab_t tab)
{
    if (tab < TAB_ALARM || tab >= TAB_COUNT || !s_tab_pages[tab]) {
        return;
    }

    lv_obj_t *content = s_tab_pages[tab];

    /* Null cached child pointers in the same operation that frees their parent
     * subtree (RULES.md section 7.1). */
    if (tab == TAB_COUNTDOWN) {
        s_cd_timer_face = NULL;
        s_cd_arc = NULL;
        s_cd_roller_h = NULL;
        s_cd_roller_m = NULL;
        s_cd_roller_s = NULL;
        s_last_cd_shown_sec = -1;
        s_cd_face_large = -1;
    } else if (tab == TAB_STOPWATCH) {
        stop_stopwatch_animation();
        s_sw_timer_face = NULL;
        s_sw_centis_face = NULL;
        s_last_sw_shown_sec = -1;
        s_last_sw_shown_centis = -1;
        s_sw_view_active = -1;
        s_sw_view_paused = -1;
    } else if (tab == TAB_POMODORO) {
        s_pomo_chip_lbl = NULL;
        s_pomo_arc = NULL;
        s_pomo_face = NULL;
        for (unsigned i = 0; i < POMODORO_CYCLE_MARKER_MAX; i++) {
            s_pomo_cycle_markers[i] = NULL;
        }
        s_pomo_cycle_marker_count = 0;
        s_pomo_btn_primary = NULL;
        s_pomo_btn_stop = NULL;
        s_pomo_roller = NULL;
        s_last_pomo_shown_sec = -1;
        s_last_pomo_progress = -1;
        s_last_pomo_visual_phase = -1;
        s_last_pomo_action_key = -1;
        s_last_pomo_cycle = 0xFFFFFFFFu;
        s_last_pomo_cycle_total = 0xFFFFFFFFu;
        s_last_pomo_completed = 0xFFFFFFFFu;
        s_pomo_view_active = -1;
    }
    lv_obj_clean(content);
    if (tab == TAB_ALARM) {
        free_alarm_ids();
    }

    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    /* The alarm tab applies a COLUMN flex layout and padding to its tab page.
     * Reset those persistent container styles before another build. */
    lv_obj_set_layout(content, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_row(content, 0, 0);

    if (tab == TAB_ALARM) {
        build_alarm_tab(content);
    } else if (tab == TAB_COUNTDOWN) {
        build_countdown_tab(content);
    } else if (tab == TAB_STOPWATCH) {
        build_stopwatch_tab(content);
    } else {
        build_pomodoro_tab(content);
    }
}

static void rebuild_all_tabs(void)
{
    for (int i = 0; i < TAB_COUNT; i++) {
        rebuild_tab((clock_tab_t)i);
    }
}

/* ---- i18n ---- */

static void refresh_i18n_labels(void)
{
    const int keys[TAB_COUNT] = {
        UI_TEXT_CLOCK_TAB_ALARM,
        UI_TEXT_CLOCK_TAB_COUNTDOWN,
        UI_TEXT_CLOCK_TAB_STOPWATCH,
        UI_TEXT_POMODORO_TITLE,
    };
    if (s_title_lbl) {
        lv_label_set_text(s_title_lbl, ui_i18n_text(UI_TEXT_CLOCK_TITLE));
    }
    if (s_tabview) {
        for (int i = 0; i < TAB_COUNT; i++) {
            lv_tabview_rename_tab(s_tabview, (uint32_t)i,
                                  ui_i18n_text(keys[i]));
        }
    }
}

/* ---- Lifecycle ---- */

static void on_enter(uint32_t dirty)
{
    if (ui_dirty_is_enter(dirty)) {
        clock_tab_t requested_tab;
        if (clock_tab_from_request(ui_svc_tm_clock_tab_take(),
                                   &requested_tab)) {
            activate_clock_tab(requested_tab);
        }
    }
    if (dirty & (1u << UI_STATE_GROUP_SYSTEM)) {
        if (s_tab == TAB_COUNTDOWN) {
            ui_svc_tm_countdown_t s;
            bool active = (ui_svc_tm_countdown_query(&s) == 0 && s.active);
            bool active_view = s_cd_timer_face != NULL;
            if (active != active_view) {
                s_cd_custom_mode = false;
                rebuild_tab(TAB_COUNTDOWN);
            } else if (active && s.remaining_sec != s_last_cd_shown_sec) {
                if (s_cd_arc && s.duration_sec > 0) {
                    long rem = s.remaining_sec;
                    if (rem < 0) {
                        rem = 0;
                    }
                    if (rem > s.duration_sec) {
                        rem = s.duration_sec;
                    }
                    lv_arc_set_value(s_cd_arc,
                                     (int32_t)(rem * 1000 / s.duration_sec));
                }
                char b[16];
                fmt_hms(b, sizeof(b), s.remaining_sec);
                int large = s.remaining_sec < 3600;
                if (large != s_cd_face_large) {
                    lv_obj_set_style_text_font(
                        s_cd_timer_face,
                        large ? &lv_font_montserrat_48
                              : &lv_font_montserrat_28,
                        0);
                    s_cd_face_large = large;
                }
                lv_label_set_text(s_cd_timer_face, b);
                lv_obj_align_to(s_cd_timer_face, s_cd_arc,
                                LV_ALIGN_CENTER, 0, 0);
                s_last_cd_shown_sec = s.remaining_sec;
            }
        } else if (s_tab == TAB_STOPWATCH) {
            ui_svc_tm_stopwatch_t s;
            bool active = (ui_svc_tm_stopwatch_query(&s) == 0 && s.active);
            bool paused = active && s.paused;
            if ((int)active != s_sw_view_active ||
                (active && (int)paused != s_sw_view_paused)) {
                rebuild_tab(TAB_STOPWATCH);
            } else if (active) {
                refresh_stopwatch_time(s.elapsed_ms);
                if (!paused) {
                    start_stopwatch_animation();
                }
            }
        } else if (s_tab == TAB_POMODORO) {
            ui_svc_tm_pomodoro_t s;
            bool active =
                (ui_svc_tm_pomodoro_query(&s) == 0 && s.active);
            if ((int)active != s_pomo_view_active) {
                rebuild_tab(TAB_POMODORO);
            } else if (active && s_pomo_face) {
                refresh_pomodoro_view();
            }
        }
    }
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        refresh_i18n_labels();
        rebuild_all_tabs();
    }
}

static void on_leave(void)
{
    stop_stopwatch_animation();
}

static void on_destroy(void)
{
    free_alarm_ids();
    stop_stopwatch_animation();
    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
        s_screen = NULL;
    }
    s_title_lbl = NULL;
    s_tabview = NULL;
    for (int i = 0; i < TAB_COUNT; i++) {
        s_tab_pages[i] = NULL;
    }
    s_cd_timer_face = NULL;
    s_cd_arc = NULL;
    s_cd_roller_h = NULL;
    s_cd_roller_m = NULL;
    s_cd_roller_s = NULL;
    s_cd_custom_mode = false;
    s_cd_face_large = -1;
    s_sw_timer_face = NULL;
    s_sw_centis_face = NULL;
    s_last_sw_shown_sec = -1;
    s_last_sw_shown_centis = -1;
    s_sw_view_active = -1;
    s_sw_view_paused = -1;
    clear_stopwatch_laps();
    s_pomo_chip_lbl = NULL;
    s_pomo_arc = NULL;
    s_pomo_face = NULL;
    for (unsigned i = 0; i < POMODORO_CYCLE_MARKER_MAX; i++) {
        s_pomo_cycle_markers[i] = NULL;
    }
    s_pomo_cycle_marker_count = 0;
    s_pomo_btn_primary = NULL;
    s_pomo_btn_stop = NULL;
    s_pomo_roller = NULL;
    s_last_pomo_shown_sec = -1;
    s_last_pomo_progress = -1;
    s_last_pomo_visual_phase = -1;
    s_last_pomo_action_key = -1;
    s_last_pomo_cycle = 0xFFFFFFFFu;
    s_last_pomo_cycle_total = 0xFFFFFFFFu;
    s_last_pomo_completed = 0xFFFFFFFFu;
    s_pomo_view_active = -1;
    s_pomo_edit_phase = -1;
}

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

const ui_page_entry_t ui_page_clock_entry = {
    .id = UI_PAGE_CLOCK,
    .name = "clock",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
