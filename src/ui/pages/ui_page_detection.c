/**
 * @file ui_page_detection.c
 * @brief Detection record list page (UI_PAGE_DETECTION)
 *
 * Read-only browser for cloud-pushed AI detection alerts. Ported from the
 * legacy view screen (src/view/screens/ui_detection.c) onto the Vue-inspired
 * UI framework. The HTTP fetch + manual trigger live in ui_svc_detection;
 * this page only renders and wires user interactions (RULES §6/§8).
 *
 * Layout (below the global statusbar band):
 *   title bar : [back]      侦测记录      [一键总结]
 *   list      : scrollable detection record cards (title + datetime)
 *   page bar  : [上一页]   cur/total   [下一页]
 */
#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_comp_popup.h"
#include "ui_comp_picture.h"
#include "ui_svc_detection.h"
#include "ui_page_ids.h"
#include "tuya_ai_display.h"
#include <stdio.h>
#include <stdint.h>

/* Title-bar back icon (24x24) */
LV_IMG_DECLARE(icon_back_24_24);

/* ---------------------------------------------------------------------------
 * Style constants
 * -------------------------------------------------------------------------*/
#define DET_STATUSBAR_H   32   /* global statusbar height (ui_comp_statusbar) */
#define DET_TITLEBAR_H    48
#define DET_PAGEBAR_H     52
#define DET_ITEM_H        58   /* two text lines: title + datetime */

/* ---------------------------------------------------------------------------
 * Internal state
 * -------------------------------------------------------------------------*/
static lv_obj_t *s_screen      = NULL;
static lv_obj_t *s_title_lbl   = NULL;
static lv_obj_t *s_summary_btn = NULL;
static lv_obj_t *s_list        = NULL;
static lv_obj_t *s_pagebar     = NULL;
static lv_obj_t *s_prev_btn    = NULL;
static lv_obj_t *s_next_btn    = NULL;
static lv_obj_t *s_page_lbl    = NULL;

static bool          s_showing_placeholder = false;
static ui_i18n_key_t s_placeholder_key     = UI_TEXT_LOADING;

/* Image viewer overlay (page-local modal on lv_layer_top; see RULES §4). */
static lv_obj_t *s_ov_root    = NULL;   /* full-screen dim backdrop (tap = close) */
static lv_obj_t *s_ov_spinner = NULL;   /* loading indicator */
static lv_obj_t *s_ov_pic     = NULL;   /* ui_comp_picture (owns the RGB565 buffer) */
static lv_obj_t *s_ov_err     = NULL;   /* "图片加载失败" label */
static uint32_t  s_ov_seq     = 0;      /* request seq (latest-wins guard) */

/* ---------------------------------------------------------------------------
 * Forward declarations
 * -------------------------------------------------------------------------*/
static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_leave(void);
static void on_destroy(void);

static void back_click_cb(lv_event_t *e);
static void gesture_cb(lv_event_t *e);
static void summary_click_cb(lv_event_t *e);
static void prev_click_cb(lv_event_t *e);
static void next_click_cb(lv_event_t *e);

static void on_detection_data(bool ok);
static void show_placeholder(ui_i18n_key_t key);
static void build_items(void);
static void make_item(const ui_svc_detection_item_t *it, int index);
static void refresh_pagebar(void);
static void set_pagebar_visible(bool visible);

static void item_click_cb(lv_event_t *e);
static void open_image_overlay(const char *url);
static void close_image_overlay(void);
static void overlay_dismiss_cb(lv_event_t *e);
static void overlay_gesture_cb(lv_event_t *e);
static void on_detection_image(const ui_svc_detection_image_t *img);

/* ---------------------------------------------------------------------------
 * Rendering helpers (page-local — not shared widgets)
 * -------------------------------------------------------------------------*/

/* Clear the list and show a single centred hint (loading / empty). */
static void show_placeholder(ui_i18n_key_t key)
{
    if (!s_list) {
        return;
    }
    lv_obj_clean(s_list);
    s_showing_placeholder = true;
    s_placeholder_key = key;

    lv_obj_t *lbl = lv_label_create(s_list);
    lv_label_set_text(lbl, ui_i18n_text(key));
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_SEC, 0);
    lv_obj_center(lbl);
}

/* One detection record card: title (top) + datetime (bottom, secondary).
 * Tapping a card opens its attached picture (download + decrypt + decode). */
static void make_item(const ui_svc_detection_item_t *it, int index)
{
    lv_obj_t *item = lv_obj_create(s_list);
    lv_obj_remove_style_all(item);
    lv_obj_set_size(item, LV_PCT(100), ui_adapt(DET_ITEM_H));
    lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(item, ui_adapt(UI_RADIUS_LG), 0);
    lv_obj_set_style_bg_opa(item, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(item, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_pad_hor(item, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_ver(item, ui_adapt(UI_SPACE_XS), 0);

    /* Clickable only when the record actually carries a picture URL. The index
     * (stable until the next fetch rebuilds the list) is read back on click. */
    if (it->attachPics[0] != '\0') {
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(item, (void *)(intptr_t)index);
        lv_obj_add_event_cb(item, item_click_cb, LV_EVENT_CLICKED, NULL);
    }

    /* Fixed (adaptive) label width, NOT LV_PCT: LV_LABEL_LONG_DOT bakes the
     * "…" using the label's width at set_text time, and a percent width is
     * still 0 while the flex layout is pending — which would blank the label
     * (RULES §9). 260 sits inside the item's inner width at every scale. */
    lv_obj_t *title = lv_label_create(item);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, ui_adapt(260));
    lv_label_set_text(title, it->title);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);

    lv_obj_t *date = lv_label_create(item);
    lv_label_set_long_mode(date, LV_LABEL_LONG_DOT);
    lv_obj_set_width(date, ui_adapt(260));
    lv_label_set_text(date, it->datetime);
    lv_obj_align(date, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_text_color(date, UI_COLOR_TEXT_SEC, 0);
}

/* Replace placeholder/old cards with the current page's records. */
static void build_items(void)
{
    if (!s_list) {
        return;
    }
    lv_obj_clean(s_list);
    s_showing_placeholder = false;

    const ui_svc_detection_item_t *items = ui_svc_detection_items();
    int count = items ? ui_svc_detection_item_count() : 0;
    for (int i = 0; i < count; i++) {
        make_item(&items[i], i);
    }
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
}

static void set_pagebar_visible(bool visible)
{
    if (!s_pagebar) {
        return;
    }
    if (visible) {
        lv_obj_clear_flag(s_pagebar, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_pagebar, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Update the cur/total indicator and enable/disable the prev/next buttons. */
static void refresh_pagebar(void)
{
    if (!s_pagebar) {
        return;
    }
    int cur = ui_svc_detection_current_page();
    int total = ui_svc_detection_total_pages();

    char buf[16];
    snprintf(buf, sizeof(buf), "%d/%d", cur, total);
    lv_label_set_text(s_page_lbl, buf);

    ui_comp_btn_set_enabled(s_prev_btn, cur > 1);
    ui_comp_btn_set_enabled(s_next_btn, cur < total);
    set_pagebar_visible(true);
}

/* ---------------------------------------------------------------------------
 * Service callback (already marshalled onto the UI thread by the service)
 * -------------------------------------------------------------------------*/
static void on_detection_data(bool ok)
{
    if (!s_screen) {
        return;   /* page torn down; callback should already be cleared */
    }
    if (!ok || ui_svc_detection_item_count() <= 0) {
        show_placeholder(UI_TEXT_DETECTION_EMPTY);
        set_pagebar_visible(false);
        return;
    }
    build_items();
    refresh_pagebar();
}

/* ---------------------------------------------------------------------------
 * Page lifecycle
 * -------------------------------------------------------------------------*/
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
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    /* Clear the global statusbar band so the title bar renders below it. */
    lv_obj_set_style_pad_top(s_screen, ui_adapt(DET_STATUSBAR_H), 0);

    /* --- Title bar: back (left) + centered title + 一键总结 (right) --- */
    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(DET_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_click_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);

    s_title_lbl = lv_label_create(titlebar);
    lv_label_set_text(s_title_lbl, ui_i18n_text(UI_TEXT_DETECTION_TITLE));
    lv_obj_set_style_text_color(s_title_lbl, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title_lbl, LV_ALIGN_CENTER, 0, 0);

    s_summary_btn = ui_comp_btn_create_styled(titlebar, ui_i18n_text(UI_TEXT_DETECTION_SUMMARY),
                                              UI_COMP_BTN_TEXT, summary_click_cb);
    lv_obj_align(s_summary_btn, LV_ALIGN_RIGHT_MID, -ui_adapt(8), 0);

    /* --- Scrollable record list (fills the space between bars) --- */
    s_list = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_hor(s_list, ui_adapt(UI_SPACE_LG), 0);
    lv_obj_set_style_pad_ver(s_list, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_set_style_pad_row(s_list, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_OFF);

    /* --- Bottom page bar: [上一页]  cur/total  [下一页] --- */
    s_pagebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_pagebar);
    lv_obj_set_size(s_pagebar, LV_PCT(100), ui_adapt(DET_PAGEBAR_H));
    lv_obj_set_style_bg_opa(s_pagebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_pagebar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_pagebar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_pagebar,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_pagebar, ui_adapt(UI_SPACE_LG), 0);

    s_prev_btn = ui_comp_btn_create_styled(s_pagebar, ui_i18n_text(UI_TEXT_PREV_PAGE),
                                           UI_COMP_BTN_TEXT, prev_click_cb);

    s_page_lbl = lv_label_create(s_pagebar);
    lv_label_set_text(s_page_lbl, "1/1");
    lv_obj_set_style_text_color(s_page_lbl, UI_COLOR_TEXT_SEC, 0);

    s_next_btn = ui_comp_btn_create_styled(s_pagebar, ui_i18n_text(UI_TEXT_NEXT_PAGE),
                                           UI_COMP_BTN_TEXT, next_click_cb);

    /* The service's item page is only needed while this page is open. */
    ui_svc_detection_acquire();

    /* Register before fetch; the service marshals the results back to us. */
    ui_svc_detection_set_cb(on_detection_data);

    /* Pull page 1 on every visit; show a static loading hint while it lands. */
    show_placeholder(UI_TEXT_LOADING);
    set_pagebar_visible(false);
    ui_svc_detection_fetch(1);
}

static void on_enter(uint32_t dirty)
{
    /* Language change: re-resolve all i18n text (RULES §5). */
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        if (s_title_lbl) {
            lv_label_set_text(s_title_lbl, ui_i18n_text(UI_TEXT_DETECTION_TITLE));
        }
        if (s_summary_btn) {
            ui_comp_btn_set_text(s_summary_btn, ui_i18n_text(UI_TEXT_DETECTION_SUMMARY));
        }
        if (s_prev_btn) {
            ui_comp_btn_set_text(s_prev_btn, ui_i18n_text(UI_TEXT_PREV_PAGE));
        }
        if (s_next_btn) {
            ui_comp_btn_set_text(s_next_btn, ui_i18n_text(UI_TEXT_NEXT_PAGE));
        }
        if (s_showing_placeholder) {
            show_placeholder(s_placeholder_key);
        }
    }
}

static void on_leave(void)
{
}

static void on_destroy(void)
{
    /* Tear down the image overlay if still open (clears its image cb + aborts
     * the in-flight download). */
    close_image_overlay();

    /* Drops the service callback (so late async results become no-ops) and
     * releases the item page — deferred internally if a query is in flight. */
    ui_svc_detection_release();

    if (s_screen) {
        /* on_destroy runs inside the event-callback chain (back/gesture ->
         * ui_route_pop); hide immediately then defer deletion. */
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
        s_screen      = NULL;
        s_title_lbl   = NULL;
        s_summary_btn = NULL;
        s_list        = NULL;
        s_pagebar     = NULL;
        s_prev_btn    = NULL;
        s_next_btn    = NULL;
        s_page_lbl    = NULL;
    }
    s_showing_placeholder = false;
}

/* ---------------------------------------------------------------------------
 * Callbacks
 * -------------------------------------------------------------------------*/
static void back_click_cb(lv_event_t *e)
{
    (void)e;
    ui_route_pop();
}

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());   /* swallow rest of touch so the release doesn't click the page below */
        ui_route_pop();
    }
}

static void summary_click_cb(lv_event_t *e)
{
    (void)e;
    /* Ask the device to detect once now; the cloud pushes a new record
     * asynchronously — it appears on the next fetch, so we don't auto-refresh. */
    ui_svc_detection_trigger();
}

static void prev_click_cb(lv_event_t *e)
{
    (void)e;
    int cur = ui_svc_detection_current_page();
    if (cur > 1) {
        show_placeholder(UI_TEXT_LOADING);
        set_pagebar_visible(false);
        ui_svc_detection_fetch(cur - 1);
    }
}

static void next_click_cb(lv_event_t *e)
{
    (void)e;
    int cur = ui_svc_detection_current_page();
    if (cur < ui_svc_detection_total_pages()) {
        show_placeholder(UI_TEXT_LOADING);
        set_pagebar_visible(false);
        ui_svc_detection_fetch(cur + 1);
    }
}

/* ---------------------------------------------------------------------------
 * Image viewer overlay
 *
 * Page-local modal on lv_layer_top (RULES §4): tapping a record downloads +
 * decrypts + decodes its picture via ui_svc_detection (off the UI thread); the
 * RGB565 result is rendered by ui_comp_picture. Tap anywhere or swipe-right to
 * close. Single image at a time.
 * -------------------------------------------------------------------------*/
static void item_click_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (index < 0 || index >= ui_svc_detection_item_count()) {
        return;
    }
    const ui_svc_detection_item_t *it = &ui_svc_detection_items()[index];
    if (it->attachPics[0] == '\0') {
        return;
    }
    open_image_overlay(it->attachPics);
}

static void open_image_overlay(const char *url)
{
    if (s_ov_root) {
        return;   /* already open — single image at a time */
    }
    /* Full-screen dim backdrop on the top layer; tap or swipe-right closes it. */
    s_ov_root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_ov_root);
    lv_obj_set_size(s_ov_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_ov_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ov_root, LV_OPA_90, 0);
    lv_obj_clear_flag(s_ov_root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(s_ov_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ov_root, overlay_dismiss_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ov_root, overlay_gesture_cb, LV_EVENT_GESTURE, NULL);

    s_ov_spinner = lv_spinner_create(s_ov_root, 1000, 60);
    lv_obj_set_size(s_ov_spinner, ui_adapt(48), ui_adapt(48));
    lv_obj_center(s_ov_spinner);

    s_ov_pic = NULL;
    s_ov_err = NULL;

    /* Register the result callback, then kick the async fetch. */
    ui_svc_detection_set_image_cb(on_detection_image);
    ui_svc_detection_image_request(url, ++s_ov_seq);
}

static void close_image_overlay(void)
{
    if (!s_ov_root) {
        return;
    }
    /* Stop listening + abort the download first, so a late result is a no-op. */
    ui_svc_detection_set_image_cb(NULL);
    ui_svc_detection_image_cancel();

    /* Deleting the overlay tree also deletes s_ov_pic, which releases its owned
     * RGB565 buffer through the free_fn passed to ui_comp_picture_set_rgb565. */
    lv_obj_add_flag(s_ov_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_del_async(s_ov_root);
    s_ov_root    = NULL;
    s_ov_spinner = NULL;
    s_ov_pic     = NULL;
    s_ov_err     = NULL;
}

static void overlay_dismiss_cb(lv_event_t *e)
{
    (void)e;
    close_image_overlay();
}

static void overlay_gesture_cb(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        close_image_overlay();
    }
}

/* Result callback — already on the UI thread (service marshalled it). Ownership
 * of img->data transfers to us: hand it to the picture widget, or release it. */
static void on_detection_image(const ui_svc_detection_image_t *img)
{
    if (!s_ov_root) {
        if (img && img->data) {
            ui_svc_detection_free_rgb565(img->data);   /* overlay gone — drop it */
        }
        return;
    }
    if (s_ov_spinner) {
        lv_obj_del(s_ov_spinner);
        s_ov_spinner = NULL;
    }
    if (img && img->ok && img->data) {
        s_ov_pic = ui_comp_picture_create(s_ov_root);
        if (s_ov_pic) {
            ui_comp_picture_set_rgb565(s_ov_pic, img->width, img->height,
                                       img->data, ui_svc_detection_free_rgb565);
        } else {
            ui_svc_detection_free_rgb565(img->data);
        }
    } else {
        s_ov_err = lv_label_create(s_ov_root);
        lv_label_set_text(s_ov_err, ui_i18n_text(UI_TEXT_DETECTION_IMG_FAIL));
        lv_obj_set_style_text_color(s_ov_err, UI_COLOR_TEXT, 0);
        lv_obj_center(s_ov_err);
    }
}

/* ---------------------------------------------------------------------------
 * Display-message handler
 *
 * Called from tuya_ai_display_stub.c on the UI thread (LVGL lock already held)
 * and only while this page is the current route. The detection summary
 * round-trip (一键总结 → cloud) saves its result picture to the album and
 * signals completion via TY_DISPLAY_TP_AI_IMAGE — we surface a toast pointing
 * the user to the album. The filename payload is unused here.
 * -------------------------------------------------------------------------*/
void ui_page_detection_on_msg(const uint8_t *msg, int len, int display_tp)
{
    (void)msg;
    (void)len;
    if (!s_screen) {
        return;   /* page torn down; ignore late async messages */
    }
    if (display_tp == TY_DISPLAY_TP_AI_IMAGE) {
        ui_comp_popup_toast(ui_i18n_text(UI_TEXT_DETECTION_SUMMARY_DONE), 2000);
    }
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/
const ui_page_entry_t ui_page_detection_entry = {
    .id = UI_PAGE_DETECTION,
    .name = "detection",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
