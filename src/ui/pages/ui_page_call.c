#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_comp_popup.h"
#include "ui_svc_call.h"      /* call control — no business SDK headers in pages */
#include "ui_page_ids.h"
#include "tal_time_service.h" /* call-duration clock (same source as home) */
#include <stdio.h>

LV_IMG_DECLARE(icon_back_24_24);

/* ---------------------------------------------------------------------------
 * Constants
 * -------------------------------------------------------------------------*/
#define CALL_STATUSBAR_H   32
#define CALL_TITLEBAR_H    48
#define CALL_BTN_SIZE      72

#define CALL_HANGUP_COLOR  0xFF675C   /* coral-red  */
#define CALL_DIAL_COLOR    0x008060   /* darkened teal, >=5:1 contrast on white */

/* ---------------------------------------------------------------------------
 * Internal state (page-local; resets on re-create — see RULES §5.1)
 * -------------------------------------------------------------------------*/
static lv_obj_t       *s_screen     = NULL;
static lv_obj_t       *s_title      = NULL;
static lv_obj_t       *s_status_lbl = NULL;
static lv_obj_t       *s_dial_btn   = NULL;
static lv_obj_t       *s_hangup_btn = NULL;
static ui_call_state_t s_status     = UI_CALL_STATE_IDLE;
static uint32_t        s_call_start_sec = 0;   /* posix seconds at IN_CALL start */

/* ---------------------------------------------------------------------------
 * Forward declarations
 * -------------------------------------------------------------------------*/
static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_leave(void);
static void on_destroy(void);
static void back_click_cb(lv_event_t *e);
static void gesture_cb(lv_event_t *e);
static void dial_cb(lv_event_t *e);
static void hangup_cb(lv_event_t *e);
static void on_call_state(ui_call_state_t state);
static void refresh_ui(void);
static void render_status_label(void);

/* ---------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------*/

/* A solid coloured circular action button with centred white text. Reuses the
 * shared button widget (press feedback / disabled handling) and overrides the
 * page-specific fill + shape. */
static lv_obj_t *make_action_btn(lv_obj_t *parent, uint32_t color,
                                 ui_i18n_key_t text_key, ui_comp_btn_cb_t cb)
{
    lv_obj_t *btn = ui_comp_btn_create_styled(parent, ui_i18n_text(text_key),
                                              UI_COMP_BTN_TEXT, cb);
    lv_obj_set_size(btn, ui_adapt(CALL_BTN_SIZE), ui_adapt(CALL_BTN_SIZE));
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(btn, lv_color_white(), 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    /* create_styled does not lay out the label; centre it inside the circle. */
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl) {
        lv_obj_center(lbl);
    }
    return btn;
}

/* ---------------------------------------------------------------------------
 * Page lifecycle
 * -------------------------------------------------------------------------*/
static void on_create(void *parent)
{
    (void)parent;

    /* Sync to the authoritative state (a call may already be up). */
    s_status = ui_svc_call_get_state();
    if (s_status == UI_CALL_STATE_IN_CALL) {
        /* Connected before this page existed — anchor the clock to now so the
         * duration starts from 00:00 rather than from an unknown origin. */
        s_call_start_sec = (uint32_t)tal_time_get_posix();
    }

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
    lv_obj_set_style_pad_top(s_screen, ui_adapt(CALL_STATUSBAR_H), 0);

    /* Title bar: back button + centred title */
    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(CALL_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_click_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);

    s_title = lv_label_create(titlebar);
    const char *peer = ui_svc_call_get_peer_name();
    lv_label_set_text(s_title, (peer && peer[0]) ? peer : ui_i18n_text(UI_TEXT_CALL_TITLE));
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_title, ui_adapt(200));   /* leave room for back button + safe margins */
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    /* Content: status label + button row are BOTH centre-aligned (absolute),
     * not stacked in a flex flow — so the status text appearing/disappearing
     * (or switching to the duration clock) never shifts the buttons. */
    lv_obj_t *content = lv_obj_create(s_screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    /* Status / duration text — fixed position above the buttons. Always present
     * (empty string when idle) so it reserves its slot. */
    s_status_lbl = lv_label_create(content);
    lv_label_set_text(s_status_lbl, "");
    lv_obj_set_style_text_color(s_status_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_align(s_status_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_CENTER, 0, -ui_adapt(72));

    lv_obj_t *btn_row = lv_obj_create(content);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, LV_PCT(100), ui_adapt(CALL_BTN_SIZE));
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(btn_row, LV_ALIGN_CENTER, 0, 0);

    s_hangup_btn = make_action_btn(btn_row, CALL_HANGUP_COLOR, UI_TEXT_CALL_HANGUP, hangup_cb);
    s_dial_btn   = make_action_btn(btn_row, CALL_DIAL_COLOR,   UI_TEXT_CALL_DIAL,   dial_cb);

    refresh_ui();

    /* Register last so the callback only fires against a fully-built tree. */
    ui_svc_call_set_cb(on_call_state);
}

static void on_enter(uint32_t dirty)
{
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        /* Language may have changed: re-resolve i18n text.  The title bar
         * normally shows the peer name (a raw string, not i18n); only fall
         * back to UI_TEXT_CALL_TITLE when no peer name is set. */
        const char *peer = ui_svc_call_get_peer_name();
        if (s_title) {
            lv_label_set_text(s_title,
                              (peer && peer[0]) ? peer
                                                : ui_i18n_text(UI_TEXT_CALL_TITLE));
        }
        if (s_hangup_btn) {
            ui_comp_btn_set_text(s_hangup_btn, ui_i18n_text(UI_TEXT_CALL_HANGUP));
        }
        if (s_dial_btn) {
            ui_comp_btn_set_text(s_dial_btn, ui_i18n_text(UI_TEXT_CALL_DIAL));
        }
        refresh_ui();
    }

    /* Per-second tick (SYSTEM timestamp) drives the in-call duration clock. */
    if ((dirty & (1u << UI_STATE_GROUP_SYSTEM)) && s_status == UI_CALL_STATE_IN_CALL) {
        render_status_label();
    }
}

static void on_leave(void)
{
    /* Leaving the foreground ends the call rather than leaving it running with no
     * visible indicator. An unanswered incoming call is rejected (busy); an
     * active/dialing call is hung up. */
    if (s_status == UI_CALL_STATE_INCOMING) {
        ui_svc_call_reject();
        s_status = UI_CALL_STATE_IDLE;
    } else if (s_status != UI_CALL_STATE_IDLE) {
        ui_svc_call_hangup();
        s_status = UI_CALL_STATE_IDLE;
    }
}

static void on_destroy(void)
{
    ui_svc_call_set_cb(NULL);

    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
        s_screen     = NULL;
        s_title      = NULL;
        s_status_lbl = NULL;
        s_dial_btn   = NULL;
        s_hangup_btn = NULL;
    }
    s_status = UI_CALL_STATE_IDLE;
}

/* ---------------------------------------------------------------------------
 * Refresh
 * -------------------------------------------------------------------------*/
/* Render the fixed status line: "呼叫中..." while dialing, a running MM:SS
 * clock while in call, empty otherwise. Driven per-second via on_enter(SYSTEM)
 * while in call. The label is centre-aligned (not in flex flow) so its content
 * never moves the buttons. */
static void render_status_label(void)
{
    if (!s_status_lbl) {
        return;
    }

    if (s_status == UI_CALL_STATE_CALLING) {
        lv_label_set_text(s_status_lbl, ui_i18n_text(UI_TEXT_CALL_DIALING));
    } else if (s_status == UI_CALL_STATE_INCOMING) {
        lv_label_set_text(s_status_lbl, ui_i18n_text(UI_TEXT_CALL_INCOMING));
    } else if (s_status == UI_CALL_STATE_IN_CALL) {
        uint32_t now = (uint32_t)tal_time_get_posix();
        uint32_t elapsed = (now >= s_call_start_sec) ? (now - s_call_start_sec) : 0;
        char buf[8];
        snprintf(buf, sizeof(buf), "%02u:%02u",
                 (unsigned)((elapsed / 60) % 100), (unsigned)(elapsed % 60));
        lv_label_set_text(s_status_lbl, buf);
    } else {
        lv_label_set_text(s_status_lbl, "");
    }
}

static void refresh_ui(void)
{
    render_status_label();

    /* The two circular buttons are repurposed by state:
     *  - INCOMING (ringing): green = 接听 (answer), red = 拒接 (reject), both enabled
     *  - otherwise:          green = 呼叫 (dial),   red = 挂断 (hang up)
     * Colours are fixed at create (green dial / red hangup); only the labels and
     * enabled-state change here. */
    bool incoming = (s_status == UI_CALL_STATE_INCOMING);
    bool active   = (s_status != UI_CALL_STATE_IDLE);

    if (s_dial_btn) {
        ui_comp_btn_set_text(s_dial_btn,
                             ui_i18n_text(incoming ? UI_TEXT_CALL_ANSWER : UI_TEXT_CALL_DIAL));
        lv_obj_center(lv_obj_get_child(s_dial_btn, 0));
        /* Dial enabled only when idle; Answer enabled while ringing. */
        ui_comp_btn_set_enabled(s_dial_btn, incoming || !active);
    }
    if (s_hangup_btn) {
        ui_comp_btn_set_text(s_hangup_btn,
                             ui_i18n_text(incoming ? UI_TEXT_CALL_REJECT : UI_TEXT_CALL_HANGUP));
        lv_obj_center(lv_obj_get_child(s_hangup_btn, 0));
        /* Reject/Hang-up enabled whenever a call is up or ringing. */
        ui_comp_btn_set_enabled(s_hangup_btn, active);
    }
}

/* ---------------------------------------------------------------------------
 * Service callback (runs in UI thread — marshalled by ui_svc_call)
 * -------------------------------------------------------------------------*/
/* 通话终局统一回 home(语音与视频对讲共用本页,视频上行由适配层随挂断自动停):
 * toast 挂在 lv_layer_top,清栈后仍续显;s_status 先置 IDLE,退栈触发的
 * on_leave 才不会二次挂断。返回键/右滑(on_leave 路径)不在此列,维持
 * "回下层页"的全局导航语义。 */
static void end_call_go_home(ui_i18n_key_t toast_key)
{
    if (toast_key != UI_TEXT_MAX) {
        ui_comp_popup_toast(ui_i18n_text(toast_key), 1500);
    }
    s_status = UI_CALL_STATE_IDLE;
    ui_route_reset(UI_PAGE_HOME);
}

static void on_call_state(ui_call_state_t state)
{
    if (state == UI_CALL_STATE_FAILED) {
        end_call_go_home(UI_TEXT_CALL_FAILED);
        return;
    }
    if (state == UI_CALL_STATE_ENDED) {
        /* 对端挂断 / 通话中断(对端已挂,无需再发挂断)。 */
        end_call_go_home(UI_TEXT_CALL_ENDED);
        return;
    }
    if (state == UI_CALL_STATE_ENDED_BY_OTHERS) {
        end_call_go_home(UI_TEXT_CALL_ENDED_BY_OTHERS);
        return;
    }

    /* Start the duration clock the moment the call connects. */
    if (state == UI_CALL_STATE_IN_CALL && s_status != UI_CALL_STATE_IN_CALL) {
        s_call_start_sec = (uint32_t)tal_time_get_posix();
    }

    s_status = state;
    refresh_ui();
}

/* ---------------------------------------------------------------------------
 * Callbacks
 * -------------------------------------------------------------------------*/
static void back_click_cb(lv_event_t *e)
{
    (void)e;
    ui_route_pop();   /* on_leave hangs up an active call */
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

/* Green button: dial when idle, answer when a call is ringing. */
static void dial_cb(lv_event_t *e)
{
    (void)e;
    if (s_status == UI_CALL_STATE_INCOMING) {
        /* Answer: stay until the media-start event promotes us to IN_CALL. */
        ui_svc_call_answer();
        return;
    }
    if (s_status != UI_CALL_STATE_IDLE) {
        return;
    }
    /* Dialing needs the P2P stack up. If the user hasn't enabled P2P, prompt
     * them to turn it on in Settings rather than silently failing. */
    if (!ui_svc_call_enabled_get()) {
        ui_comp_popup_toast(ui_i18n_text(UI_TEXT_CALL_P2P_DISABLED), 1500);
        return;
    }
    ui_svc_call_dial();
    s_status = UI_CALL_STATE_CALLING;
    refresh_ui();
}

/* Red button: reject when ringing, hang up when a call is active/dialing.
 * Either way the call task is over — go home (no toast; the tap itself is
 * the feedback). */
static void hangup_cb(lv_event_t *e)
{
    (void)e;
    if (s_status == UI_CALL_STATE_INCOMING) {
        ui_svc_call_reject();
        end_call_go_home(UI_TEXT_MAX);
        return;
    }
    if (s_status == UI_CALL_STATE_IDLE) {
        return;
    }
    ui_svc_call_hangup();
    end_call_go_home(UI_TEXT_MAX);
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/
const ui_page_entry_t ui_page_call_entry = {
    .id = UI_PAGE_CALL,
    .name = "call",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
