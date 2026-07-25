/**
 * @file ui_call.c
 * @brief Voice call screen for T5AI_BOARD (320x480)
 *
 * Outbound device-to-app voice call. The user enters from the control-center
 * "通话" card, taps the green "呼叫" button to initiate, sees the call go
 * through the IDLE -> CALLING -> IN_CALL state machine, and ends with the
 * red "挂断" button or the back arrow.
 *
 * Status transitions are driven by the SDK media-stream events published by
 * tuya_p2p_app.c on TUYA_IPC_CALL: LIVE_AUDIO_START moves us into IN_CALL,
 * LIVE_AUDIO_STOP moves us back to IDLE. A 30-second UI timer flags an
 * unanswered call as "呼叫失败" (the SDK has its own 30s timeout but emits
 * no user-visible failure cue, so we keep this layer).
 *
 * Inbound calls are auto-answered by the SDK layer (tuya_sdk_call.c
 * __call_handler) and never reach this screen as a UI event.
 *
 * Lifecycle follows view's "lazy create + persistent screen" convention
 * (see ui_music.c / ui_record.c): setup_scr_call() runs once on first show,
 * the lv_obj tree lives for the rest of the app session. Per-visit state
 * (status, event subscription, timer activity) is reset in ui_call_show /
 * ui_call_hide so a returning user always lands on a clean IDLE screen.
 *
 * @version 1.0
 * @date 2026-05-18
 * @copyright Copyright (c) Tuya Inc.
 */
#include <string.h>
#include "ui_common.h"
#include "ui_call.h"
#include "base_event.h"
#include "tuya_ipc_media_stream_event.h"
#include "tuya_sdk_call.h"

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define CALL_HALF_W                 (LV_HOR_RES / 2)
#define CALL_CONTENT_Y              UI_TITLE_BAR_H
#define CALL_CONTENT_H              (LV_VER_RES - UI_TITLE_BAR_H)
#define CALL_IMG_SIZE               64
#define CALL_STATUS_LABEL_Y         8         /* relative to content top */

#define CALL_HANGUP_COLOR           0xFF675C  /* coral-red, from call_hangup.png */
#define CALL_ANSWER_COLOR           0x008060  /* darkened teal, contrast >= 5:1 with white */

#define CALL_OUTBOUND_TIMEOUT_MS    30000
#define CALL_FAIL_HIDE_MS           1500

#define CALL_EVT_SUBSCRIBER         "view_call"

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef enum {
    CALL_STATUS_IDLE = 0,
    CALL_STATUS_CALLING,
    CALL_STATUS_IN_CALL,
} CALL_STATUS_E;

typedef struct {
    lv_obj_t      *scr;
    lv_obj_t      *title_bar;
    lv_obj_t      *title_lbl;
    lv_obj_t      *back_btn;
    lv_obj_t      *content;
    lv_obj_t      *status_label;
    lv_obj_t      *answer_btn;
    lv_obj_t      *hangup_btn;
    lv_timer_t    *timeout_timer;
    lv_timer_t    *fail_hide_timer;
    CALL_STATUS_E  status;
    BOOL_T         evt_subscribed;
} CALL_UI_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC CALL_UI_T s_call = {0};

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T setup_scr_call(VOID_T);
STATIC VOID_T __call_build_title_bar(VOID_T);
STATIC VOID_T __call_build_content(VOID_T);
STATIC VOID_T __call_make_action_item(lv_obj_t *parent, lv_color_t icon_color,
                                       CONST CHAR_T *label_text, lv_event_cb_t btn_cb,
                                       lv_obj_t **btn_out, lv_coord_t x_offset);
STATIC VOID_T __call_update_status_label(VOID_T);
STATIC VOID_T __call_cancel_timers(VOID_T);
STATIC VOID_T __call_back_cb(lv_event_t *e);
STATIC VOID_T __call_answer_cb(lv_event_t *e);
STATIC VOID_T __call_hangup_cb(lv_event_t *e);
STATIC VOID_T __call_timeout_cb(lv_timer_t *timer);
STATIC VOID_T __call_fail_hide_cb(lv_timer_t *timer);
STATIC INT_T  __call_status_event_cb(VOID_T *data);

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Cancel both UI timers without deleting them
 * @return none
 * @note lv_timer objects live for the screen's lifetime (paused when idle).
 *       Pausing instead of deleting matches view's persistent-screen model.
 */
STATIC VOID_T __call_cancel_timers(VOID_T)
{
    if (s_call.timeout_timer != NULL) {
        lv_timer_pause(s_call.timeout_timer);
    }
    if (s_call.fail_hide_timer != NULL) {
        lv_timer_pause(s_call.fail_hide_timer);
    }
}

/**
 * @brief Refresh the status label text/visibility based on current status
 * @return none
 */
STATIC VOID_T __call_update_status_label(VOID_T)
{
    if (s_call.status_label == NULL) {
        return;
    }

    switch (s_call.status) {
        case CALL_STATUS_CALLING:
            lv_label_set_text(s_call.status_label, "呼叫中...");
            lv_obj_clear_flag(s_call.status_label, LV_OBJ_FLAG_HIDDEN);
            break;
        case CALL_STATUS_IN_CALL:
            lv_label_set_text(s_call.status_label, "通话中...");
            lv_obj_clear_flag(s_call.status_label, LV_OBJ_FLAG_HIDDEN);
            break;
        case CALL_STATUS_IDLE:
        default:
            lv_obj_add_flag(s_call.status_label, LV_OBJ_FLAG_HIDDEN);
            break;
    }
}

/**
 * @brief Build the standard view title bar (back button + centered "通话")
 * @return none
 */
STATIC VOID_T __call_build_title_bar(VOID_T)
{
    s_call.title_bar = lv_obj_create(s_call.scr);
    lv_obj_remove_style_all(s_call.title_bar);
    lv_obj_set_pos(s_call.title_bar, 0, 0);
    lv_obj_set_size(s_call.title_bar, LV_HOR_RES, UI_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(s_call.title_bar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_call.title_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_call.back_btn = lv_btn_create(s_call.title_bar);
    lv_obj_remove_style_all(s_call.back_btn);
    lv_obj_set_size(s_call.back_btn, UI_TITLE_BTN_W, UI_TITLE_BAR_H);
    lv_obj_set_pos(s_call.back_btn, 0, 0);
    lv_obj_set_style_bg_opa(s_call.back_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_call.back_btn, __call_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_icon = lv_img_create(s_call.back_btn);
    lv_img_set_src(back_icon, &icon_back_24_24);
    lv_obj_set_size(back_icon, UI_TITLE_ICON_SIZE, UI_TITLE_ICON_SIZE);
    lv_obj_center(back_icon);

    s_call.title_lbl = lv_label_create(s_call.title_bar);
    lv_label_set_text(s_call.title_lbl, "通话");
    lv_obj_set_style_text_font(s_call.title_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_call.title_lbl, lv_color_white(), 0);
    lv_obj_center(s_call.title_lbl);
}

/**
 * @brief Create one action cluster: a circular coloured button with a centred label.
 *
 * Replaces the former img + transparent-btn overlay pattern.  The lv_btn
 * serves as both the visual circle (bg_color + LV_RADIUS_CIRCLE) and the
 * click target; no separate transparent overlay is needed.
 *
 * See ADR 0001 for the rationale (flash optimisation, WCAG contrast).
 *
 * @param[in]  parent      content container the cluster belongs to
 * @param[in]  icon_color  circle background colour
 * @param[in]  label_text  text rendered in the circle centre
 * @param[in]  btn_cb      click handler
 * @param[out] btn_out     returns the btn object (stored in CALL_UI_T)
 * @param[in]  x_offset    horizontal position inside content
 */
STATIC VOID_T __call_make_action_item(lv_obj_t *parent, lv_color_t icon_color,
                                       CONST CHAR_T *label_text, lv_event_cb_t btn_cb,
                                       lv_obj_t **btn_out, lv_coord_t x_offset)
{
    lv_obj_t *slot = lv_obj_create(parent);
    lv_obj_remove_style_all(slot);
    lv_obj_set_size(slot, CALL_HALF_W, CALL_CONTENT_H);
    lv_obj_set_pos(slot, x_offset, 0);
    lv_obj_set_style_bg_opa(slot, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(slot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn = lv_btn_create(slot);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, CALL_IMG_SIZE, CALL_IMG_SIZE);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(btn, icon_color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_font(label, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);

    if (btn_out != NULL) {
        *btn_out = btn;
    }
}

/**
 * @brief Build the content area below the title bar: status label + two action clusters
 * @return none
 * @note Content fills (LV_HOR_RES x (LV_VER_RES - UI_TITLE_BAR_H)). On the
 *       320x480 T5AI_BOARD that is 320x430, twice the height of the desktop
 *       320x240 layout the screen was originally designed for - the action
 *       clusters are vertically centered in the larger canvas (Q6 decision).
 */
STATIC VOID_T __call_build_content(VOID_T)
{
    s_call.content = lv_obj_create(s_call.scr);
    lv_obj_remove_style_all(s_call.content);
    lv_obj_set_pos(s_call.content, 0, CALL_CONTENT_Y);
    lv_obj_set_size(s_call.content, LV_HOR_RES, CALL_CONTENT_H);
    lv_obj_set_style_bg_opa(s_call.content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_call.content, 0, 0);
    lv_obj_set_scrollbar_mode(s_call.content, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_call.content, LV_OBJ_FLAG_SCROLLABLE);

    s_call.status_label = lv_label_create(s_call.content);
    lv_label_set_text(s_call.status_label, "");
    lv_obj_set_style_text_font(s_call.status_label,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_call.status_label, lv_color_white(), 0);
    lv_obj_align(s_call.status_label, LV_ALIGN_TOP_MID, 0, CALL_STATUS_LABEL_Y);
    lv_obj_add_flag(s_call.status_label, LV_OBJ_FLAG_HIDDEN);

    /* Left half: hangup (red), Right half: answer/dial (teal). */
    __call_make_action_item(s_call.content, lv_color_hex(CALL_HANGUP_COLOR), "挂断",
                            __call_hangup_cb, &s_call.hangup_btn, 0);
    __call_make_action_item(s_call.content, lv_color_hex(CALL_ANSWER_COLOR), "呼叫",
                            __call_answer_cb, &s_call.answer_btn, CALL_HALF_W);
}

/**
 * @brief Create the call screen + per-screen lv_timers (idempotent)
 * @return none
 * @note lv_timers are created paused once and stay around for the screen's
 *       lifetime; ui_call_hide just pauses them. This keeps the create/destroy
 *       cost off the show/hide hot path while still respecting the
 *       "no work while invisible" constraint.
 */
STATIC VOID_T setup_scr_call(VOID_T)
{
    if (s_call.scr != NULL) {
        return;
    }

    memset(&s_call, 0, sizeof(s_call));

    s_call.scr = lv_obj_create(NULL);
    lv_obj_set_size(s_call.scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_call.scr, lv_color_hex(UI_BG_COLOR_DARK), 0);
    lv_obj_set_style_bg_opa(s_call.scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_call.scr, 0, 0);
    lv_obj_set_scrollbar_mode(s_call.scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_call.scr, LV_OBJ_FLAG_SCROLLABLE);

    __call_build_title_bar();
    __call_build_content();

    ui_control_center_register_gesture(s_call.scr);

    s_call.timeout_timer = lv_timer_create(__call_timeout_cb,
                                           CALL_OUTBOUND_TIMEOUT_MS, NULL);
    lv_timer_set_repeat_count(s_call.timeout_timer, 1);
    lv_timer_pause(s_call.timeout_timer);

    s_call.fail_hide_timer = lv_timer_create(__call_fail_hide_cb,
                                             CALL_FAIL_HIDE_MS, NULL);
    lv_timer_set_repeat_count(s_call.fail_hide_timer, 1);
    lv_timer_pause(s_call.fail_hide_timer);

    lv_obj_update_layout(s_call.scr);
}

/**
 * @brief Show the call screen (creates lazily on first call)
 * @return none
 * @note Each visit re-subscribes to TUYA_IPC_CALL and resets the state to
 *       IDLE so the user always lands on a clean screen.
 */
VOID_T ui_call_show(VOID_T)
{
    if (s_call.scr == NULL) {
        setup_scr_call();
    }

    s_call.status = CALL_STATUS_IDLE;
    __call_update_status_label();
    __call_cancel_timers();

    if (!s_call.evt_subscribed) {
        if (ty_subscribe_event(TUYA_IPC_CALL, CALL_EVT_SUBSCRIBER,
                               __call_status_event_cb,
                               SUBSCRIBE_TYPE_NORMAL) == OPRT_OK) {
            s_call.evt_subscribed = TRUE;
        } else {
            PR_ERR("call: subscribe TUYA_IPC_CALL failed");
        }
    }

    if (lv_scr_act() != s_call.scr) {
        lv_scr_load(s_call.scr);
    }
}

/**
 * @brief Hide the call screen, hang up if a call is active, release per-visit state
 * @return none
 * @note If the user navigates away mid-call we hang up rather than leave the
 *       call running in a state with no visible indicator (Q7 decision).
 */
VOID_T ui_call_hide(VOID_T)
{
    __call_cancel_timers();

    if (s_call.status != CALL_STATUS_IDLE) {
        PR_INFO("call: leaving while not idle, hanging up");
        TUYA_IPC_hangup();
        s_call.status = CALL_STATUS_IDLE;
        __call_update_status_label();
    }

    if (s_call.evt_subscribed) {
        ty_unsubscribe_event(TUYA_IPC_CALL, CALL_EVT_SUBSCRIBER,
                             __call_status_event_cb);
        s_call.evt_subscribed = FALSE;
    }
}

/**
 * @brief Get the call screen object
 * @return call screen pointer, NULL if not created yet
 */
lv_obj_t *ui_call_get_scr(VOID_T)
{
    return s_call.scr;
}

/* ---------------------------------------------------------------------------
 * Event handlers
 * --------------------------------------------------------------------------- */

/**
 * @brief Back button: route through dispatch so ui_nav_back drives ui_call_hide
 * @param[in] e LVGL event (unused)
 * @return none
 */
STATIC VOID_T __call_back_cb(lv_event_t *e)
{
    (VOID_T)e;
    PR_DEBUG("call: back");
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_CLOSE_CALL);
}

/**
 * @brief "呼叫" button: only valid in IDLE; initiates outbound call
 * @param[in] e LVGL event (unused)
 * @return none
 */
STATIC VOID_T __call_answer_cb(lv_event_t *e)
{
    (VOID_T)e;

    if (s_call.status != CALL_STATUS_IDLE) {
        return;
    }

    PR_INFO("call: answer/dial pressed");
    s_call.status = CALL_STATUS_CALLING;
    __call_update_status_label();

    TUYA_IPC_call_app();

    if (s_call.timeout_timer != NULL) {
        lv_timer_reset(s_call.timeout_timer);
        lv_timer_resume(s_call.timeout_timer);
    }
}

/**
 * @brief "挂断" button: ignored when already idle, otherwise hangs up
 * @param[in] e LVGL event (unused)
 * @return none
 */
STATIC VOID_T __call_hangup_cb(lv_event_t *e)
{
    (VOID_T)e;

    if (s_call.status == CALL_STATUS_IDLE) {
        return;
    }

    PR_INFO("call: hangup pressed");
    __call_cancel_timers();
    s_call.status = CALL_STATUS_IDLE;
    __call_update_status_label();

    TUYA_IPC_hangup();
}

/* ---------------------------------------------------------------------------
 * Timer / event callbacks
 * --------------------------------------------------------------------------- */

/**
 * @brief 30-second outbound timeout: surfaces "呼叫失败" and cleans up
 * @param[in] timer firing timer (auto-paused since repeat_count was 1)
 * @return none
 */
STATIC VOID_T __call_timeout_cb(lv_timer_t *timer)
{
    (VOID_T)timer;
    PR_WARN("call: outbound timeout");

    s_call.status = CALL_STATUS_IDLE;

    if (s_call.status_label != NULL) {
        lv_label_set_text(s_call.status_label, "呼叫失败");
        lv_obj_clear_flag(s_call.status_label, LV_OBJ_FLAG_HIDDEN);
    }

    TUYA_IPC_hangup();

    if (s_call.fail_hide_timer != NULL) {
        lv_timer_reset(s_call.fail_hide_timer);
        lv_timer_resume(s_call.fail_hide_timer);
    }
}

/**
 * @brief 1.5-second fail-label auto-hide: clears the failure cue
 * @param[in] timer firing timer
 * @return none
 */
STATIC VOID_T __call_fail_hide_cb(lv_timer_t *timer)
{
    (VOID_T)timer;
    if (s_call.status_label != NULL) {
        lv_obj_add_flag(s_call.status_label, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief TUYA_IPC_CALL event handler: drive status from media-stream lifecycle
 * @param[in] data pointer to MEDIA_STREAM_EVENT_E published by tuya_p2p_app.c
 * @return OPRT_OK
 */
STATIC INT_T __call_status_event_cb(VOID_T *data)
{
    if (data == NULL) {
        return OPRT_COM_ERROR;
    }

    MEDIA_STREAM_EVENT_E event = *(MEDIA_STREAM_EVENT_E *)data;

    switch (event) {
        case MEDIA_STREAM_LIVE_AUDIO_START:
            __call_cancel_timers();
            s_call.status = CALL_STATUS_IN_CALL;
            __call_update_status_label();
            break;
        case MEDIA_STREAM_LIVE_AUDIO_STOP:
            __call_cancel_timers();
            s_call.status = CALL_STATUS_IDLE;
            __call_update_status_label();
            break;
        default:
            break;
    }

    return OPRT_OK;
}
