#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_svc_recording.h"
#include "ui_page_ids.h"
#include <stdint.h>

LV_IMG_DECLARE(icon_back_24_24);
LV_IMG_DECLARE(icon_record_list);

#define REC_STATUSBAR_H  32
#define REC_TITLEBAR_H   48
#define REC_RED          0xEC5C5C
#define REC_ACCENT       0xF3E55D   /* recording-state timer colour (matches the list page accent) */

static lv_obj_t *s_screen   = NULL;
static lv_obj_t *s_title    = NULL;
static lv_obj_t *s_list_btn = NULL;
static lv_obj_t *s_timer    = NULL;
static lv_obj_t *s_rec_btn  = NULL;
static lv_obj_t *s_rec_icon = NULL;
static int       s_shown_sec = -1;

static void back_cb(lv_event_t *e)    { (void)e; ui_route_pop(); }
static void gesture_cb(lv_event_t *e);
static void list_btn_cb(lv_event_t *e);
static void rec_btn_cb(lv_event_t *e);
static void apply_cap_visual(ui_rec_cap_state_t st);
static void refresh_timer(void);

/* Swipe right anywhere on the page = go back (same as the back button). */
static void gesture_cb(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());   /* swallow rest of touch so the release doesn't click the page below */
        ui_route_pop();
    }
}

/* Right-top list icon — enter the list page. Disabled while recording. */
static void list_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_recording_status_t st;
    ui_svc_recording_get_status(&st);
    if (st.cap_state != UI_REC_CAP_IDLE) return;
    ui_route_push(UI_PAGE_RECORDING_LIST);
}

/* Two-state toggle: start (red disc) ↔ stop (red square). No pause. */
static void rec_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_recording_status_t st;
    ui_svc_recording_get_status(&st);
    if (st.cap_state == UI_REC_CAP_IDLE) {
        ui_svc_recording_capture_start();
        apply_cap_visual(UI_REC_CAP_RECORDING);
    } else {
        ui_svc_recording_capture_stop();
        apply_cap_visual(UI_REC_CAP_IDLE);
    }
    s_shown_sec = -1;
    refresh_timer();
}

/* Render the record-button glyph for the given state and toggle the list
 * button's availability (no list navigation while recording). Mutates the
 * existing s_rec_icon in place — never deletes/recreates it. */
static void apply_cap_visual(ui_rec_cap_state_t st)
{
    if (!s_rec_icon || !s_rec_btn) return;
    if (st == UI_REC_CAP_RECORDING) {
        lv_obj_set_size(s_rec_btn, ui_adapt(52), ui_adapt(52));    /* ring tightens while recording */
        lv_obj_set_size(s_rec_icon, ui_adapt(22), ui_adapt(22));   /* red square = stop */
        lv_obj_set_style_radius(s_rec_icon, ui_adapt(5), 0);
        /* Hide the list entry entirely while recording — can't browse files
         * mid-capture. */
        if (s_list_btn) lv_obj_add_flag(s_list_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_size(s_rec_btn, ui_adapt(64), ui_adapt(64));    /* larger ring at rest */
        lv_obj_set_size(s_rec_icon, ui_adapt(34), ui_adapt(34));   /* red disc = record */
        lv_obj_set_style_radius(s_rec_icon, LV_RADIUS_CIRCLE, 0);
        if (s_list_btn) lv_obj_clear_flag(s_list_btn, LV_OBJ_FLAG_HIDDEN);
    }
    /* Re-anchor after the size change so the button stays centred. */
    lv_obj_align(s_rec_btn, LV_ALIGN_CENTER, 0, ui_adapt(60));
    lv_obj_center(s_rec_icon);
}

/* Per-second timer refresh driven by SYSTEM dirty (no lv_timer). */
static void refresh_timer(void)
{
    ui_recording_status_t st;
    if (!s_timer) return;
    ui_svc_recording_get_status(&st);
    if (st.cap_state != UI_REC_CAP_RECORDING) {
        if (st.cap_state == UI_REC_CAP_IDLE && s_shown_sec != 0) {
            s_shown_sec = 0;
            lv_label_set_text(s_timer, "00:00:00");
            lv_obj_set_style_text_color(s_timer, UI_COLOR_TEXT, 0);
        }
        return;
    }
    if ((int)st.cap_elapsed_sec == s_shown_sec) return;
    s_shown_sec = (int)st.cap_elapsed_sec;
    lv_label_set_text_fmt(s_timer, "%02u:%02u:%02u",
                          st.cap_elapsed_sec / 3600,
                          (st.cap_elapsed_sec % 3600) / 60,
                          st.cap_elapsed_sec % 60);
    lv_obj_set_style_text_color(s_timer, lv_color_hex(REC_ACCENT), 0);
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
    /* Clear the global statusbar band so the title bar renders below it. */
    lv_obj_set_style_pad_top(s_screen, ui_adapt(REC_STATUSBAR_H), 0);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);   /* swipe-right = back */

    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(REC_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);

    s_title = lv_label_create(titlebar);
    lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_RECORDING_TITLE));
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    s_list_btn = ui_comp_btn_icon_create(titlebar, &icon_record_list, list_btn_cb);
    lv_obj_align(s_list_btn, LV_ALIGN_RIGHT_MID, -ui_adapt(8), 0);

    s_timer = lv_label_create(s_screen);
    lv_label_set_text(s_timer, "00:00:00");
    lv_obj_set_style_text_font(s_timer, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_timer, UI_COLOR_TEXT, 0);
    lv_obj_align(s_timer, LV_ALIGN_CENTER, 0, -ui_adapt(30));

    s_rec_btn = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_rec_btn);
    lv_obj_set_size(s_rec_btn, ui_adapt(54), ui_adapt(54));
    lv_obj_set_style_radius(s_rec_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_rec_btn, ui_adapt(2), 0);
    lv_obj_set_style_border_color(s_rec_btn, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_rec_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(s_rec_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_rec_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(s_rec_btn, LV_ALIGN_CENTER, 0, ui_adapt(60));
    lv_obj_add_event_cb(s_rec_btn, rec_btn_cb, LV_EVENT_CLICKED, NULL);

    /* Inner red shape, created ONCE. apply_cap_visual only mutates its size/
     * shape — never deletes/recreates it (doing that inside the click handler
     * dropped subsequent taps). Non-clickable so taps fall through to the
     * button. */
    s_rec_icon = lv_obj_create(s_rec_btn);
    lv_obj_remove_style_all(s_rec_icon);
    lv_obj_set_style_bg_color(s_rec_icon, lv_color_hex(REC_RED), 0);
    lv_obj_set_style_bg_opa(s_rec_icon, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_rec_icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(s_rec_icon);

    /* Session bound to page lifecycle: open here, close in on_destroy. */
    ui_svc_recording_capture_open();
    apply_cap_visual(UI_REC_CAP_IDLE);
    s_shown_sec = 0;
}

static void on_enter(uint32_t dirty)
{
    if (dirty & (1u << UI_STATE_GROUP_SYSTEM))   refresh_timer();
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        if (s_title) lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_RECORDING_TITLE));
        /* list button is now an icon — no text to re-resolve on language change */
    }
}

/* Intentionally empty: keep the capture session open while the list page is
 * on top (recording is blocked mid-capture, so only an IDLE session lingers),
 * so returning resumes instantly without re-acquiring the mic. The session is
 * closed in on_destroy when the page actually leaves the stack. */
static void on_leave(void) {}

static void on_destroy(void)
{
    ui_svc_recording_capture_close();
    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
        s_screen = NULL; s_title = NULL; s_list_btn = NULL;
        s_timer = NULL; s_rec_btn = NULL; s_rec_icon = NULL;
    }
    s_shown_sec = -1;
}

const ui_page_entry_t ui_page_recording_entry = {
    .id = UI_PAGE_RECORDING,
    .name = "recording",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
