#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_statusbar.h"
#include "ui_comp_btn.h"
#include "ui_page_ids.h"
#include "ui_svc_weather.h"
#include "ui_svc_call.h"
#include "ui_svc_camera.h"
#include "ui_svc_tm.h"
#include "ui_feature.h"
#include "ui_comp_popup.h"
#include "ty_cJSON.h"
#include "tal_time_service.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

LV_IMG_DECLARE(icon_camera_app);
LV_IMG_DECLARE(icon_call_app);

/* --- Props --- */

typedef struct {
    uint8_t  alarm_hour;
    uint8_t  alarm_min;
    char     alarm_label[32];
} ui_page_home_props_t;

/* --- State --- */

static lv_obj_t *s_screen    = NULL;
static lv_obj_t *s_lbl_time  = NULL;
static lv_obj_t *s_lbl_date  = NULL;
static lv_obj_t *s_card_weather = NULL;
static lv_obj_t *s_lbl_weather_main = NULL;
static lv_obj_t *s_lbl_weather_range = NULL;
static lv_obj_t *s_card_alarm = NULL;
static lv_obj_t *s_lbl_alarm_title = NULL;
static lv_obj_t *s_lbl_alarm_detail = NULL;

static ui_page_home_props_t s_props = {
    .alarm_hour = 0xFF,
    .alarm_min = 0,
    .alarm_label = "",
};

/* Dedup guard: avoid redundant lv_label_set_text when content hasn't changed */
static uint8_t s_last_hour = 0xFF;
static uint8_t s_last_min  = 0xFF;
static uint8_t s_last_mon  = 0xFF;
static uint8_t s_last_mday = 0xFF;

/* Dedup guard for the next-alarm refresh: only re-parse when the alarm count
 * changes (0xFF = never refreshed yet / invalidated). */
static uint8_t s_last_alarm_count = 0xFF;

/* Weekday i18n keys indexed by tm_wday (0=Sunday, 1=Monday, ..., 6=Saturday) */
static const ui_i18n_key_t s_weekday_keys[] = {
    UI_TEXT_WEEKDAY_SUN, UI_TEXT_WEEKDAY_MON, UI_TEXT_WEEKDAY_TUE,
    UI_TEXT_WEEKDAY_WED, UI_TEXT_WEEKDAY_THU, UI_TEXT_WEEKDAY_FRI,
    UI_TEXT_WEEKDAY_SAT,
};

/* --- Card builder --- */

static lv_obj_t *create_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, UI_COLOR_STATUSBAR_BG, 0);
    lv_obj_set_style_bg_opa(card, UI_COLOR_STATUSBAR_BG_OPA * 2, 0);
    lv_obj_set_style_radius(card, UI_RADIUS_LG, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, ui_adapt(12), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, ui_adapt(4), 0);
    return card;
}

/* Lock-screen style circular icon shortcut. Uses the framework's circular
 * button (frosted translucent fill + pressed feedback); the page only adds the
 * gesture-bubble flag so a swipe still propagates to the screen for navigation. */
static lv_obj_t *create_shortcut_btn(lv_obj_t *parent, const void *icon_src,
                                     ui_comp_btn_cb_t cb)
{
    lv_obj_t *btn = ui_comp_btn_circle_create(parent, icon_src, NULL, cb);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return btn;
}

/* --- Refresh helpers --- */

static void refresh_datetime(void)
{
    POSIX_TM_S tm = {0};
    if (tal_time_get_local_time_custom(0, &tm) != OPRT_OK) {
        if (s_lbl_time && s_last_hour == 0xFF) {
            lv_label_set_text(s_lbl_time, "--:--");
        }
        return;
    }

    uint8_t h = (uint8_t)tm.tm_hour;
    uint8_t m = (uint8_t)tm.tm_min;

    if (s_lbl_time && (h != s_last_hour || m != s_last_min)) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
        lv_label_set_text(s_lbl_time, buf);
        s_last_hour = h;
        s_last_min = m;
    }

    uint8_t mday = (uint8_t)tm.tm_mday;
    uint8_t mon = (uint8_t)(tm.tm_mon + 1);
    if (s_lbl_date && (mday != s_last_mday || mon != s_last_mon)) {
        int wday = tm.tm_wday;
        if (wday < 0 || wday > 6) wday = 0;
        const char *weekday = ui_i18n_text(s_weekday_keys[wday]);
        char buf[32];
        snprintf(buf, sizeof(buf), ui_i18n_text(UI_TEXT_HOME_DATE_FMT),
                 mon, mday, weekday);
        lv_label_set_text(s_lbl_date, buf);
        s_last_mon = mon;
        s_last_mday = mday;
    }
}

static void refresh_weather(void)
{
    if (!s_card_weather) return;
    if (!ui_svc_weather_available()) {
        lv_obj_add_flag(s_card_weather, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const ui_svc_weather_t *w = ui_svc_weather_get();
    lv_obj_clear_flag(s_card_weather, LV_OBJ_FLAG_HIDDEN);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d°  %s", w->temp, w->desc);
    lv_label_set_text(s_lbl_weather_main, buf);
    snprintf(buf, sizeof(buf), ui_i18n_text(UI_TEXT_HOME_WEATHER_RANGE_FMT),
             w->high, w->low);
    lv_label_set_text(s_lbl_weather_range, buf);
}

static void refresh_alarm(void)
{
    if (!s_card_alarm) return;
    if (s_props.alarm_hour == 0xFF) {
        lv_obj_add_flag(s_card_alarm, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s_card_alarm, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_lbl_alarm_title, ui_i18n_text(UI_TEXT_HOME_NEXT_ALARM));
    char buf[48];
    snprintf(buf, sizeof(buf), "%02d:%02d  %s",
             s_props.alarm_hour, s_props.alarm_min, s_props.alarm_label);
    lv_label_set_text(s_lbl_alarm_detail, buf);
}

/* Parse the alarm list and feed the soonest ENABLED alarm (by time-of-day) to
 * the alarm card. Alarm items carry a "time" STRING: recurring alarms use
 * "HH:MM"; once-alarms use an ISO8601 datetime ("YYYY-MM-DDTHH:MM:SS+HH:MM").
 * 0xFF hour hides the card. */
static void refresh_next_alarm(void)
{
    char *json = NULL;
    int best_h = 0xFF, best_m = 0;
    char best_lbl[32] = "";
    if (ui_svc_tm_alarm_list(&json) == 0 && json) {
        ty_cJSON *root = ty_cJSON_Parse(json);
        ty_cJSON *arr = root ? ty_cJSON_GetObjectItem(root, "alarms") : NULL;
        int n = arr ? ty_cJSON_GetArraySize(arr) : 0;
        int best_key = 24 * 60 + 1;
        for (int i = 0; i < n; i++) {
            ty_cJSON *it = ty_cJSON_GetArrayItem(arr, i);
            ty_cJSON *jen = ty_cJSON_GetObjectItem(it, "enabled");
            if (!jen || !jen->valueint) continue;
            ty_cJSON *jt = ty_cJSON_GetObjectItem(it, "time");
            const char *ts = (jt && jt->valuestring) ? jt->valuestring : NULL;
            int hh = 0, mm = 0;
            if (!ts) continue;
            /* ISO8601 once-alarms embed HH:MM after the 'T'; parse there so the
               trailing "+08:00" timezone is not read as the time. Recurring
               "HH:MM" strings have no 'T' and parse from the front. */
            const char *tp = strchr(ts, 'T');
            const char *hm = tp ? tp + 1 : ts;
            if (sscanf(hm, "%d:%d", &hh, &mm) != 2) continue;
            if (hh < 0 || hh > 23 || mm < 0 || mm > 59) continue;
            int key = hh * 60 + mm;
            if (key < best_key) {
                best_key = key; best_h = hh; best_m = mm;
                ty_cJSON *jmsg = ty_cJSON_GetObjectItem(it, "message");
                if (jmsg && jmsg->valuestring) {
                    strncpy(best_lbl, jmsg->valuestring, sizeof(best_lbl) - 1);
                    best_lbl[sizeof(best_lbl) - 1] = '\0';
                }
            }
        }
        ty_cJSON_Delete(root);
        ui_svc_tm_json_free(json);
    }
    ui_page_home_set_alarm((uint8_t)best_h, (uint8_t)best_m, best_lbl);   /* 0xFF hides the card */
}

/* --- Gesture --- */

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_RIGHT) {
        ui_route_push(UI_PAGE_CHAT);
    } else if (dir == LV_DIR_LEFT) {
        ui_route_push(UI_PAGE_APP_CENTER);
    }
}

/* --- Camera shortcut --- */

static void camera_click_cb(lv_event_t *e)
{
    (void)e;
    if (ui_svc_camera_available()) {
        ui_route_push(UI_PAGE_CAMERA);
    } else {
        ui_comp_popup_toast(ui_i18n_text(UI_TEXT_FEATURE_UNAVAILABLE), 2000);
    }
}

/* --- Call shortcut --- */

static void call_click_cb(lv_event_t *e)
{
    (void)e;
    if (ui_feature_available(UI_FEATURE_ID_CALL)) {
        ui_route_push(UI_PAGE_CONTACTS);   /* 先选联系人(App / 设备),再进呼叫页 */
    } else {
        ui_comp_popup_toast(ui_i18n_text(UI_TEXT_FEATURE_UNAVAILABLE), 2000);
    }
}

/* --- Weather callback --- */

static void on_weather_updated(const ui_svc_weather_t *weather)
{
    (void)weather;
    refresh_weather();
}

/* --- Lifecycle --- */

static void on_create(void *parent)
{
    (void)parent;

    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);

    /* Main content area — flex column below the status row */
    lv_obj_t *content = lv_obj_create(s_screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_top(content, ui_adapt(32), 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(content, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, ui_adapt(8), 0);
    lv_obj_set_style_pad_hor(content, ui_adapt(UI_SPACE_LG), 0);

    /* Spacer above time */
    lv_obj_t *spacer_top = lv_obj_create(content);
    lv_obj_remove_style_all(spacer_top);
    lv_obj_set_size(spacer_top, 1, ui_adapt(30));
    lv_obj_clear_flag(spacer_top, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(spacer_top, LV_OBJ_FLAG_GESTURE_BUBBLE);

    /* Time "HH:MM" */
    s_lbl_time = lv_label_create(content);
    lv_obj_set_style_text_font(s_lbl_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_lbl_time, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_letter_space(s_lbl_time, 2, 0);
    lv_label_set_text(s_lbl_time, "--:--");

    /* Date "M月D日 星期X" */
    s_lbl_date = lv_label_create(content);
    lv_obj_set_style_text_color(s_lbl_date, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_pad_top(s_lbl_date, ui_adapt(4), 0);
    lv_label_set_text(s_lbl_date, "");

    /* Spacer between date and cards */
    lv_obj_t *spacer_mid = lv_obj_create(content);
    lv_obj_remove_style_all(spacer_mid);
    lv_obj_set_size(spacer_mid, 1, ui_adapt(24));
    lv_obj_clear_flag(spacer_mid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(spacer_mid, LV_OBJ_FLAG_GESTURE_BUBBLE);

    /* Weather card */
    s_card_weather = create_card(content);
    s_lbl_weather_main = lv_label_create(s_card_weather);
    lv_obj_set_style_text_color(s_lbl_weather_main, UI_COLOR_TEXT, 0);

    s_lbl_weather_range = lv_label_create(s_card_weather);
    lv_obj_set_style_text_color(s_lbl_weather_range, UI_COLOR_TEXT_SEC, 0);
    refresh_weather();

    /* Alarm card */
    s_card_alarm = create_card(content);
    s_lbl_alarm_title = lv_label_create(s_card_alarm);
    lv_obj_set_style_text_color(s_lbl_alarm_title, UI_COLOR_TEXT, 0);

    s_lbl_alarm_detail = lv_label_create(s_card_alarm);
    lv_obj_set_style_text_color(s_lbl_alarm_detail, UI_COLOR_TEXT_SEC, 0);
    refresh_alarm();

    /* Bottom shortcut buttons (iPhone lock-screen style) */
    lv_obj_t *spacer_bottom = lv_obj_create(content);
    lv_obj_remove_style_all(spacer_bottom);
    lv_obj_set_width(spacer_bottom, 1);
    lv_obj_set_flex_grow(spacer_bottom, 1);
    lv_obj_clear_flag(spacer_bottom, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(spacer_bottom, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *btn_row = lv_obj_create(content);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(btn_row, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_pad_bottom(btn_row, ui_adapt(16), 0);

    /* Camera shortcut — greyed out when the camera feature is compiled out */
    lv_obj_t *camera_btn = create_shortcut_btn(btn_row, &icon_camera_app, camera_click_cb);
    if (!ui_svc_camera_available()) {
        ui_comp_btn_set_enabled(camera_btn, false);
    }

    /* Call shortcut — greyed out when the P2P/call feature is compiled out */
    lv_obj_t *call_btn = create_shortcut_btn(btn_row, &icon_call_app, call_click_cb);
    if (!ui_feature_available(UI_FEATURE_ID_CALL)) {
        ui_comp_btn_set_enabled(call_btn, false);
    }

    /* Gesture */
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);

    /* Disable scroll on lv_scr_act() to prevent it from consuming swipe as scroll */
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);

    /* Initial time/date read */
    refresh_datetime();

    /* Initial next-alarm read */
    refresh_next_alarm();

    /* Register weather service callback */
    ui_svc_weather_set_cb(on_weather_updated);
}

static void on_enter(uint32_t dirty)
{
    if (ui_dirty_is_enter(dirty)) {
        /* Entry: (re)assert the home statusbar look and do the one-time alarm
         * paint. Weather liveness is driven by on_weather_updated() (registered
         * in on_create), so it needs no per-tick refresh here. */
        ui_comp_statusbar_set_mode(UI_STATUSBAR_MODE_MINIMAL);
        ui_comp_statusbar_set_visible(true);
        refresh_alarm();
    }
    if (dirty & (1u << UI_STATE_GROUP_SYSTEM)) {
        refresh_datetime();
        /* Re-parse the next alarm only when the alarm count changed. */
        const ui_state_system_t *sys = ui_state_get_system();
        if (sys->alarm_count != s_last_alarm_count) {
            s_last_alarm_count = sys->alarm_count;
            refresh_next_alarm();
        }
    }
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        /* Language changed: invalidate dedup guard so the date string is
         * reformatted with the new locale, and re-resolve the weather range
         * string (UI_TEXT_HOME_WEATHER_RANGE_FMT is i18n). */
        s_last_mon  = 0xFF;
        s_last_mday = 0xFF;
        refresh_datetime();
        refresh_weather();
        refresh_alarm();   /* alarm card title (UI_TEXT_HOME_NEXT_ALARM) is i18n too */
    }
}

static void on_leave(void)
{
    ui_comp_statusbar_set_mode(UI_STATUSBAR_MODE_FULL);
}

static void on_destroy(void)
{
    ui_svc_weather_set_cb(NULL);
    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
    }
    s_screen = NULL;
    s_lbl_time = NULL;
    s_lbl_date = NULL;
    s_card_weather = NULL;
    s_lbl_weather_main = NULL;
    s_lbl_weather_range = NULL;
    s_card_alarm = NULL;
    s_lbl_alarm_title = NULL;
    s_lbl_alarm_detail = NULL;
    s_last_hour = 0xFF;
    s_last_min  = 0xFF;
    s_last_mon  = 0xFF;
    s_last_mday = 0xFF;
    s_last_alarm_count = 0xFF;
}

const ui_page_entry_t ui_page_home_entry = {
    .id = UI_PAGE_HOME,
    .name = "home",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};

/* --- Public API --- */

void ui_page_home_set_alarm(uint8_t hour, uint8_t min, const char *label)
{
    s_props.alarm_hour = hour;
    s_props.alarm_min = min;
    if (label) {
        strncpy(s_props.alarm_label, label, sizeof(s_props.alarm_label) - 1);
        s_props.alarm_label[sizeof(s_props.alarm_label) - 1] = '\0';
    }
    refresh_alarm();
}
