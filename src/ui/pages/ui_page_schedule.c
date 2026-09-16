#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_page_ids.h"

#include "ui_svc_tm.h"
#include "ui_comp_popup.h"
#include "ty_cJSON.h"
#include "tal_memory.h"
#include "tal_time_service.h"
#include <string.h>
#include <stdio.h>

/* Title-bar back icon (24x24) */
LV_IMG_DECLARE(icon_back_24_24);

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_title_lbl = NULL;     /* captured in on_create for i18n re-resolve */
static lv_obj_t *s_list = NULL;
static char s_pending_del_id[40];
static char (*s_ids)[40] = NULL;         /* page-owned id table to avoid cJSON UAF */
static int  s_id_count = 0;

static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_leave(void);
static void on_destroy(void);
static void back_click_cb(lv_event_t *e);
static void gesture_cb(lv_event_t *e);
static void build_list(void);

static void free_ids(void)
{
    if (s_ids) {
        tal_free(s_ids);
        s_ids = NULL;
    }
    s_id_count = 0;
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
    lv_label_set_text(title, ui_i18n_text(UI_TEXT_SCHEDULE_TITLE));
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
    s_title_lbl = title;

    build_list();
}

/* ---- Delete ---- */

static void del_confirm_cb(bool ok)
{
    if (ok && s_pending_del_id[0]) {
        ui_svc_tm_reminder_remove(s_pending_del_id);
        build_list();
    }
}

static void row_longpress_cb(lv_event_t *e)
{
    const char *id = (const char *)lv_obj_get_user_data(lv_event_get_target(e));
    strncpy(s_pending_del_id, id ? id : "", sizeof(s_pending_del_id) - 1);
    s_pending_del_id[sizeof(s_pending_del_id) - 1] = '\0';
    ui_comp_popup_show(UI_COMP_POPUP_CONFIRM, NULL,
                       ui_i18n_text(UI_TEXT_TM_DELETE_CONFIRM), del_confirm_cb);
}

/* ---- List builder ---- */

/* Calendar-day equality for two POSIX_TM_S (year/mon/mday). */
static bool tm_same_day(const POSIX_TM_S *a, const POSIX_TM_S *b)
{
    return a->tm_year == b->tm_year && a->tm_mon == b->tm_mon && a->tm_mday == b->tm_mday;
}

/* Emit a left-aligned date group header into s_list for the given local time,
 * relative to "now". Shows 今天/明天 (Today/Tomorrow) or a localized M月D日 (M/D). */
static void add_day_header(const POSIX_TM_S *lt, const POSIX_TM_S *now_tm, long ts)
{
    char buf[24];
    const char *text = buf;

    if (tm_same_day(lt, now_tm)) {
        text = ui_i18n_text(UI_TEXT_SCHEDULE_TODAY);
    } else {
        POSIX_TM_S prev;   /* the reminder's day minus 24h -> if == today, it's tomorrow */
        if (tal_time_get_local_time_custom((TIME_T)(ts - 86400), &prev) == OPRT_OK &&
            tm_same_day(&prev, now_tm)) {
            text = ui_i18n_text(UI_TEXT_SCHEDULE_TOMORROW);
        } else {
            snprintf(buf, sizeof(buf), ui_i18n_text(UI_TEXT_SCHEDULE_DATE_FMT),
                     (int)(lt->tm_mon + 1), (int)lt->tm_mday);
        }
    }

    lv_obj_t *hdr = lv_label_create(s_list);
    lv_label_set_text(hdr, text);
    lv_obj_set_style_text_color(hdr, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_pad_top(hdr, ui_adapt(6), 0);
    lv_obj_set_style_pad_left(hdr, ui_adapt(2), 0);
}

static void build_list(void)
{
    char *json = NULL;
    ty_cJSON *root = NULL, *arr = NULL;
    int n = 0;
    POSIX_TM_S now_tm;
    bool have_now = (tal_time_get_local_time_custom(0, &now_tm) == OPRT_OK);
    bool have_prev_day = false;
    POSIX_TM_S prev_day;

    if (s_list) {
        lv_obj_del(s_list);
        s_list = NULL;
    }
    free_ids();

    /* Bound the list BELOW the title bar (y≈86), NOT full-size: a full-size
     * scrollable list layered over the title bar swallows the back button's
     * touches. Start it under the title bar with a bounded height instead. */
    s_list = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_height(s_list, ui_adapt_screen_h() - ui_adapt(32) - ui_adapt(86));
    lv_obj_align(s_list, LV_ALIGN_TOP_MID, 0, ui_adapt(86));
    lv_obj_set_style_pad_hor(s_list, ui_adapt(14), 0);
    lv_obj_set_style_pad_bottom(s_list, ui_adapt(14), 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, ui_adapt(8), 0);

    if (ui_svc_tm_reminder_list(&json) != 0 || json == NULL) {
        goto empty;
    }
    root = ty_cJSON_Parse(json);
    arr = root ? ty_cJSON_GetObjectItem(root, "reminders") : NULL;
    n = arr ? ty_cJSON_GetArraySize(arr) : 0;
    if (n == 0) {
        goto empty;
    }

    s_ids = (char (*)[40])tal_malloc(sizeof(char[40]) * n);
    if (s_ids) {
        memset(s_ids, 0, sizeof(char[40]) * n);
    }

    /* Display ascending by start_time. Build a sorted index over the array
     * (selection sort over a small list; backend returns storage order). */
    int *order = (int *)tal_malloc(sizeof(int) * n);
    if (order) {
        for (int i = 0; i < n; i++) {
            order[i] = i;
        }
        for (int i = 0; i < n - 1; i++) {
            for (int j = i + 1; j < n; j++) {
                ty_cJSON *a = ty_cJSON_GetArrayItem(arr, order[i]);
                ty_cJSON *b = ty_cJSON_GetArrayItem(arr, order[j]);
                ty_cJSON *ta = a ? ty_cJSON_GetObjectItem(a, "start_time") : NULL;
                ty_cJSON *tb = b ? ty_cJSON_GetObjectItem(b, "start_time") : NULL;
                long va = ta ? (long)ta->valuedouble : 0;
                long vb = tb ? (long)tb->valuedouble : 0;
                if (vb < va) {
                    int tmp = order[i];
                    order[i] = order[j];
                    order[j] = tmp;
                }
            }
        }
    }

    for (int i = 0; i < n; i++) {
        int idx = order ? order[i] : i;
        ty_cJSON *it = ty_cJSON_GetArrayItem(arr, idx);
        ty_cJSON *jid = ty_cJSON_GetObjectItem(it, "id");
        ty_cJSON *jts = ty_cJSON_GetObjectItem(it, "start_time");
        ty_cJSON *jmsg = ty_cJSON_GetObjectItem(it, "message");
        const char *id = (jid && jid->valuestring) ? jid->valuestring : "";
        long ts = jts ? (long)jts->valuedouble : 0;
        const char *msg = (jmsg && jmsg->valuestring) ? jmsg->valuestring : "";
        char *row_id = "";
        if (s_ids) {
            strncpy(s_ids[i], id, 39);
            s_id_count = i + 1;
            row_id = s_ids[i];
        }

        POSIX_TM_S lt;
        char hm[8] = "--:--";
        bool have_lt = (tal_time_get_local_time_custom((TIME_T)ts, &lt) == OPRT_OK);
        if (have_lt) {
            snprintf(hm, sizeof(hm), "%02d:%02d", lt.tm_hour, lt.tm_min);
        }

        bool is_today = (have_now && have_lt && tm_same_day(&lt, &now_tm));

        /* Emit a date group header whenever the calendar day changes. */
        if (have_now && have_lt &&
            (!have_prev_day || !tm_same_day(&lt, &prev_day))) {
            add_day_header(&lt, &now_tm, ts);
            prev_day = lt;
            have_prev_day = true;
        }

        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), ui_adapt(50));
        lv_obj_set_style_bg_color(row, UI_COLOR_BG_CARD, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, ui_adapt(9), 0);
        lv_obj_set_style_pad_hor(row, ui_adapt(12), 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, ui_adapt(10), 0);
        lv_obj_set_user_data(row, (void *)row_id);
        lv_obj_add_event_cb(row, row_longpress_cb, LV_EVENT_LONG_PRESSED, NULL);

        /* Colored status dot: primary for today's items, muted for later days. */
        lv_obj_t *dot = lv_obj_create(row);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, ui_adapt(8), ui_adapt(8));
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, is_today ? UI_COLOR_PRIMARY : UI_COLOR_TEXT_SEC, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *l = lv_label_create(row);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(l, 1);
        lv_label_set_text_fmt(l, "%s  %s", hm, msg);
        lv_obj_set_style_text_color(l, UI_COLOR_TEXT, 0);
    }

    if (order) {
        tal_free(order);
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
        /* Centered empty state: just the muted "暂无日程" label. */
        lv_obj_t *box = lv_obj_create(s_list);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, LV_PCT(100), LV_PCT(100));
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        lv_obj_t *e = lv_label_create(box);
        lv_label_set_text(e, ui_i18n_text(UI_TEXT_TM_EMPTY_SCHEDULE));
        lv_obj_set_style_text_color(e, UI_COLOR_TEXT_SEC, 0);
    }
}

/* ---- Lifecycle ---- */

static void on_enter(uint32_t dirty)
{
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        if (s_title_lbl) {
            lv_label_set_text(s_title_lbl, ui_i18n_text(UI_TEXT_SCHEDULE_TITLE));
        }
        build_list();   /* re-resolve empty-state text + reformat */
    }
}

static void on_leave(void)
{
}

static void on_destroy(void)
{
    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
        s_screen = NULL;
    }
    free_ids();
    s_list = NULL;
    s_title_lbl = NULL;
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

const ui_page_entry_t ui_page_schedule_entry = {
    .id = UI_PAGE_SCHEDULE,
    .name = "schedule",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
