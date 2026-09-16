/**
 * @file ui_page_photo.c
 * @brief Local picture page — single route page with two internal layouts
 *        (ADR-0001): a full-screen picture Viewer and a thumbnail Grid, switched
 *        via the file-scope `s_mode`. Merges the old view/ screens ui_album.c
 *        (viewer) and ui_album_grid.c (grid) into the new framework.
 *
 * Data flows through ui_svc_picture (no business/platform headers here): every
 * picture/thumbnail is decoded off the UI thread and delivered via callbacks; big
 * pictures use index + seq latest-wins so fast swiping never renders a stale frame
 * (ADR-0002). The shared ui_comp_picture widget backs both the big picture and the
 * thumbnails.
 */

#include "lvgl.h"
#include "ui_route.h"
#include "ui_state.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_comp_btn.h"
#include "ui_comp_picture.h"
#include "ui_comp_popup.h"
#include "ui_svc_picture.h"
#include "ui_page_ids.h"
#include "tal_memory.h"
#include "uni_log.h"
#include <stdio.h>
#include <string.h>

LV_IMG_DECLARE(icon_back_24_24);
LV_IMG_DECLARE(icon_delete);
LV_IMG_DECLARE(icon_choose);

/* ---------------------------------------------------------------------------
 * Layout constants (base 320x480; scaled at use sites via ui_adapt)
 * --------------------------------------------------------------------------- */
#define PIC_TOP_PAD       32   /* global statusbar band */
#define PIC_BTN_MARGIN    12
#define PIC_TOPBAR_H      52
#define PIC_TOP_BTN_H     40
#define PIC_ALL_BTN_W     108
#define PIC_ACTION_DOCK_W 296
#define PIC_ACTION_DOCK_H 60
#define PIC_ACTION_PAD    4
#define GRID_COLS         3
#define GRID_CELL         96
#define GRID_GAP          8
#define GRID_PAD          8
#define GRID_RADIUS       8
#define GRID_TOPBAR_H     48
#define GRID_BOTBAR_H     48
#define GRID_CHK_SIZE     22
#define GRID_CHK_OFFSET   6
#define PIC_IDX_LAST      0xFFFFFFFFu   /* sentinel: clamp lands on the last picture */

/* ---------------------------------------------------------------------------
 * State
 * --------------------------------------------------------------------------- */
typedef enum { PICTURE_MODE_VIEW, PICTURE_MODE_GRID } picture_mode_t;

typedef struct {
    lv_obj_t *cell;
    lv_obj_t *chk;
    char      name[UI_PICTURE_NAME_MAX + 1];
    bool      selected;
} grid_item_t;

static lv_obj_t      *s_screen;
static picture_mode_t s_mode;
/* When the viewer was opened by tapping a grid thumbnail, the top-left Back
 * returns to the grid instead of leaving the page. Set only at the two viewer
 * entry points (thumbnail tap = true, grid Back = false); on_create seeds false. */
static bool          s_viewer_from_grid;
static bool          s_ai_action_pending;

/* viewer */
static lv_obj_t *s_view_cont;
static lv_obj_t *s_picture;
static lv_obj_t *s_overlay;
static lv_obj_t *s_empty_hint;
static lv_obj_t *s_topbar;
static lv_obj_t *s_action_dock;
static lv_obj_t *s_back_btn;
static lv_obj_t *s_all_btn;
static lv_obj_t *s_delete_btn;
static lv_obj_t *s_ai_recognize_btn;
static lv_obj_t *s_image_to_image_btn;
static lv_obj_t *s_pending_action_btn;
static bool      s_controls_visible;
static uint32_t  s_count;
static uint32_t  s_idx;
static uint32_t  s_seq;

/* grid */
static lv_obj_t  *s_grid_cont;
static lv_obj_t  *s_grid_title;
static lv_obj_t  *s_choose_btn;
static lv_obj_t  *s_cancel_btn;
static lv_obj_t  *s_grid_scroll;
static lv_obj_t  *s_bottom_bar;
static lv_obj_t  *s_select_lbl;
/* Thumbnail grid slots: allocated in on_create, released in on_destroy. NULL
 * means the allocation failed — s_item_count then stays 0, so the grid renders
 * empty instead of the page failing to open.
 * NOTE: s_items is a pointer, so sizeof(s_items) is 4 — always size the block
 * via ITEMS_BYTES, never sizeof(s_items). */
static grid_item_t *s_items;
#define ITEMS_BYTES  (sizeof(*s_items) * UI_PICTURE_MAX_THUMBS)
static uint32_t  s_item_count;
static bool      s_select_mode;
static uint32_t  s_select_count;

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
static void set_mode(picture_mode_t mode);
static void update_select_label(void);
static void exit_select_mode(void);
static void update_viewer_actions(void);
static void set_viewer_controls_visible(bool visible);

/* ---------------------------------------------------------------------------
 * Viewer
 * --------------------------------------------------------------------------- */
static void view_nav(int delta)
{
    if (s_count == 0) {
        return;
    }
    uint32_t next = s_idx;
    if (delta < 0) {
        if (s_idx == 0) return;
        next = s_idx - 1;
    } else {
        if (s_idx + 1 >= s_count) return;
        next = s_idx + 1;
    }
    s_idx = next;
    ui_svc_picture_request(s_idx, ++s_seq);   /* previous frame stays until ready */
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    if (s_ai_action_pending) {
        return;
    }
    /* A viewer opened from a grid thumbnail returns to the grid; the default
     * viewer (page entry) leaves the picture page entirely. */
    if (s_viewer_from_grid) {
        set_mode(PICTURE_MODE_GRID);
    } else {
        ui_route_pop();
    }
}

static void delete_confirm_cb(bool confirmed)
{
    if (confirmed) {
        ui_svc_picture_delete_current();   /* on_count refreshes the viewer */
    }
}

static void delete_cb(lv_event_t *e)
{
    (void)e;
    if (s_count == 0 || s_ai_action_pending) {
        return;
    }
    ui_comp_popup_show(UI_COMP_POPUP_CONFIRM, NULL,
                       ui_i18n_text(UI_TEXT_PICTURE_DEL_CONFIRM), delete_confirm_cb);
}

static void open_chat_after_ai_action(void)
{
    /* Leave the album and surface chat. For image-to-image, chat consumes the
     * pending attachment preview in on_enter; recognition has already uploaded
     * its image/text turn and simply waits for the streamed cloud response. */
    ui_route_pop();
    ui_route_push(UI_PAGE_CHAT);
}

static void ai_action_done(bool ok)
{
    s_ai_action_pending = false;
    if (ok) {
        open_chat_after_ai_action();
    } else {
        s_pending_action_btn = NULL;
        if (s_ai_recognize_btn) {
            ui_comp_btn_set_text(s_ai_recognize_btn,
                                 ui_i18n_text(UI_TEXT_PICTURE_AI_RECOGNIZE));
        }
        if (s_image_to_image_btn) {
            ui_comp_btn_set_text(s_image_to_image_btn,
                                 ui_i18n_text(UI_TEXT_PICTURE_IMAGE_TO_IMAGE));
        }
        update_viewer_actions();
        ui_comp_popup_toast(ui_i18n_text(UI_TEXT_PICTURE_AI_FAILED), 2000);
    }
}

static void start_ai_action(lv_obj_t *source, bool recognize)
{
    if (s_count == 0 || s_ai_action_pending) {
        return;
    }

    s_ai_action_pending = true;
    s_pending_action_btn = source;
    ui_comp_btn_set_text(source, ui_i18n_text(UI_TEXT_LOADING));
    update_viewer_actions();

    if (recognize) {
        ui_svc_picture_recognize_current(ai_action_done);
    } else {
        ui_svc_picture_generate_from_current(ai_action_done);
    }
}

static void ai_recognize_cb(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
    start_ai_action(lv_event_get_current_target(e), true);
}

static void image_to_image_cb(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
    start_ai_action(lv_event_get_current_target(e), false);
}

static void all_pictures_cb(lv_event_t *e)
{
    (void)e;
    if (s_ai_action_pending) {
        return;
    }
    set_mode(PICTURE_MODE_GRID);
}

static void set_viewer_controls_visible(bool visible)
{
    s_controls_visible = visible;
    if (s_topbar) {
        if (visible) {
            lv_obj_clear_flag(s_topbar, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_topbar, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_action_dock) {
        if (visible) {
            lv_obj_clear_flag(s_action_dock, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_action_dock, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void viewer_click_cb(lv_event_t *e)
{
    /* Ignore a bubbled child-button click; only a tap on the photo/background
     * toggles the two control bands. Swipe gestures do not emit CLICKED. */
    if (lv_event_get_target(e) != lv_event_get_current_target(e)) {
        return;
    }
    set_viewer_controls_visible(!s_controls_visible);
}

static void style_action_button(lv_obj_t *btn, lv_color_t color)
{
    lv_obj_set_width(btn, 0);
    lv_obj_set_height(btn, LV_PCT(100));
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_radius(btn, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_set_style_bg_color(btn, color, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_20, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, color, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *label = lv_obj_get_child(btn, 0);
    if (label && lv_obj_check_type(label, &lv_label_class)) {
        lv_obj_set_width(label, ui_adapt(88));
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    }
}

static void update_viewer_actions(void)
{
    bool action_enabled = (s_count > 0) && !s_ai_action_pending;
    if (s_back_btn) {
        ui_comp_btn_set_enabled(s_back_btn, !s_ai_action_pending);
    }
    if (s_all_btn) {
        ui_comp_btn_set_enabled(s_all_btn, !s_ai_action_pending);
    }
    if (s_delete_btn) {
        ui_comp_btn_set_enabled(s_delete_btn, action_enabled);
    }
    if (s_image_to_image_btn) {
        ui_comp_btn_set_enabled(s_image_to_image_btn, action_enabled);
    }
    if (s_ai_recognize_btn) {
        ui_comp_btn_set_enabled(s_ai_recognize_btn, action_enabled);
    }
}

static void build_viewer(void)
{
    s_view_cont = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_view_cont);
    lv_obj_set_size(s_view_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_view_cont, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_view_cont, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_view_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_view_cont, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_view_cont, viewer_click_cb, LV_EVENT_CLICKED, NULL);

    s_picture = ui_comp_picture_create(s_view_cont);
    lv_obj_clear_flag(s_picture, LV_OBJ_FLAG_CLICKABLE);

    s_empty_hint = lv_label_create(s_view_cont);
    lv_label_set_text(s_empty_hint, ui_i18n_text(UI_TEXT_PICTURE_EMPTY));
    lv_obj_set_style_text_color(s_empty_hint, UI_COLOR_TEXT_SEC, 0);
    lv_obj_center(s_empty_hint);
    lv_obj_add_flag(s_empty_hint, LV_OBJ_FLAG_HIDDEN);

    /* Overlay: transparent, click-through; only its buttons are interactive. */
    s_overlay = lv_obj_create(s_view_cont);
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);

    /* A quiet translucent top band keeps navigation readable over bright photos. */
    s_topbar = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(s_topbar);
    lv_obj_set_size(s_topbar, LV_PCT(100), ui_adapt(PIC_TOPBAR_H));
    lv_obj_align(s_topbar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_topbar, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_topbar, LV_OPA_30, 0);
    lv_obj_clear_flag(s_topbar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_back_btn = ui_comp_btn_icon_create(s_topbar, &icon_back_24_24, back_cb);
    lv_obj_set_size(s_back_btn, ui_adapt(PIC_TOP_BTN_H), ui_adapt(PIC_TOP_BTN_H));
    lv_obj_set_style_radius(s_back_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_back_btn, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_back_btn, LV_OPA_40, 0);
    lv_obj_set_flex_flow(s_back_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_back_btn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(s_back_btn, LV_ALIGN_LEFT_MID, ui_adapt(PIC_BTN_MARGIN), 0);

    s_all_btn = ui_comp_btn_create_styled(s_topbar,
                                          ui_i18n_text(UI_TEXT_PICTURE_ALL),
                                          UI_COMP_BTN_TEXT, all_pictures_cb);
    lv_obj_set_size(s_all_btn, ui_adapt(PIC_ALL_BTN_W), ui_adapt(PIC_TOP_BTN_H));
    lv_obj_set_style_radius(s_all_btn, ui_adapt(PIC_TOP_BTN_H / 2), 0);
    lv_obj_set_style_bg_color(s_all_btn, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_all_btn, LV_OPA_40, 0);
    lv_obj_set_style_border_color(s_all_btn, lv_color_white(), 0);
    lv_obj_set_style_border_opa(s_all_btn, LV_OPA_20, 0);
    lv_obj_set_style_border_width(s_all_btn, 1, 0);
    lv_obj_set_style_text_color(s_all_btn, UI_COLOR_TEXT, 0);
    lv_obj_set_flex_flow(s_all_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_all_btn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(s_all_btn, LV_ALIGN_RIGHT_MID, -ui_adapt(PIC_BTN_MARGIN), 0);

    /* Three direct actions replace the former AI entry + popup. */
    s_action_dock = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(s_action_dock);
    lv_obj_set_size(s_action_dock, ui_adapt(PIC_ACTION_DOCK_W), ui_adapt(PIC_ACTION_DOCK_H));
    lv_obj_align(s_action_dock, LV_ALIGN_BOTTOM_MID, 0, -ui_adapt(PIC_BTN_MARGIN));
    lv_obj_set_style_bg_color(s_action_dock, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(s_action_dock, LV_OPA_90, 0);
    lv_obj_set_style_border_color(s_action_dock, lv_color_white(), 0);
    lv_obj_set_style_border_opa(s_action_dock, LV_OPA_20, 0);
    lv_obj_set_style_border_width(s_action_dock, 1, 0);
    lv_obj_set_style_radius(s_action_dock, ui_adapt(UI_RADIUS_MD + UI_SPACE_XS), 0);
    lv_obj_set_style_pad_all(s_action_dock, ui_adapt(PIC_ACTION_PAD), 0);
    lv_obj_set_style_pad_column(s_action_dock, ui_adapt(2), 0);
    lv_obj_set_flex_flow(s_action_dock, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_action_dock, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_action_dock, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    s_delete_btn = ui_comp_btn_create_styled(
        s_action_dock, ui_i18n_text(UI_TEXT_PICTURE_DELETE), UI_COMP_BTN_TEXT, delete_cb);
    style_action_button(s_delete_btn, UI_COLOR_ERROR);

    s_image_to_image_btn = ui_comp_btn_create_styled(
        s_action_dock, ui_i18n_text(UI_TEXT_PICTURE_IMAGE_TO_IMAGE),
        UI_COMP_BTN_TEXT, image_to_image_cb);
    style_action_button(s_image_to_image_btn, UI_COLOR_PRIMARY);

    s_ai_recognize_btn = ui_comp_btn_create_styled(
        s_action_dock, ui_i18n_text(UI_TEXT_PICTURE_AI_RECOGNIZE),
        UI_COMP_BTN_TEXT, ai_recognize_cb);
    style_action_button(s_ai_recognize_btn, UI_COLOR_PRIMARY);

    set_viewer_controls_visible(true);
    update_viewer_actions();
}

/* ---------------------------------------------------------------------------
 * Grid
 * --------------------------------------------------------------------------- */
static void update_checkbox(grid_item_t *item)
{
    if (!item->chk) {
        return;
    }
    if (item->selected) {
        lv_obj_set_style_bg_color(item->chk, UI_COLOR_WARNING, 0);
        lv_obj_set_style_bg_opa(item->chk, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(item->chk, UI_COLOR_WARNING, 0);
    } else {
        lv_obj_set_style_bg_color(item->chk, lv_color_hex(0x808080), 0);
        lv_obj_set_style_bg_opa(item->chk, LV_OPA_30, 0);
        lv_obj_set_style_border_color(item->chk, lv_color_white(), 0);
    }
}

static void cell_click_cb(lv_event_t *e)
{
    lv_obj_t *cell = lv_event_get_current_target(e);
    uint32_t idx = (uint32_t)(uintptr_t)lv_obj_get_user_data(cell);
    if (idx >= s_item_count) {
        return;
    }

    if (!s_select_mode) {
        /* Resolve by the thumbnail's stable filename. Thumbnail and viewer scan
         * order are independent, so index arithmetic can select a neighbour. */
        if (s_count == 0) {
            return;
        }
        s_viewer_from_grid = true;
        ui_comp_picture_clear(s_picture);   /* never flash the previous picture */
        set_mode(PICTURE_MODE_VIEW);
        ui_svc_picture_request_named(s_items[idx].name, ++s_seq);
        return;
    }

    grid_item_t *item = &s_items[idx];
    item->selected = !item->selected;
    if (item->selected) {
        s_select_count++;
    } else if (s_select_count > 0) {
        s_select_count--;
    }
    update_checkbox(item);
    update_select_label();
}

static void enter_select_mode(void)
{
    if (s_item_count == 0) {
        return;
    }
    s_select_mode = true;
    s_select_count = 0;

    lv_obj_add_flag(s_choose_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_cancel_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_bottom_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_bottom(s_grid_scroll, ui_adapt(GRID_BOTBAR_H + GRID_PAD), 0);

    for (uint32_t i = 0; i < s_item_count; i++) {
        grid_item_t *item = &s_items[i];
        item->selected = false;
        if (item->chk == NULL) {
            item->chk = lv_obj_create(item->cell);
            lv_obj_remove_style_all(item->chk);
            lv_obj_set_size(item->chk, ui_adapt(GRID_CHK_SIZE), ui_adapt(GRID_CHK_SIZE));
            lv_obj_set_pos(item->chk, ui_adapt(GRID_CHK_OFFSET), ui_adapt(GRID_CHK_OFFSET));
            lv_obj_set_style_radius(item->chk, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(item->chk, 2, 0);
            lv_obj_clear_flag(item->chk, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(item->chk, LV_OBJ_FLAG_SCROLLABLE);
        }
        update_checkbox(item);
    }
    update_select_label();
}

static void exit_select_mode(void)
{
    s_select_mode = false;
    s_select_count = 0;

    lv_obj_clear_flag(s_choose_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_cancel_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_bottom_bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_bottom(s_grid_scroll, ui_adapt(GRID_PAD), 0);

    for (uint32_t i = 0; i < s_item_count; i++) {
        grid_item_t *item = &s_items[i];
        item->selected = false;
        if (item->chk) {
            lv_obj_del(item->chk);
            item->chk = NULL;
        }
    }
}

static void update_select_label(void)
{
    if (!s_select_lbl) {
        return;
    }
    if (s_select_count == 0) {
        lv_label_set_text(s_select_lbl, ui_i18n_text(UI_TEXT_PICTURE_SELECT));
    } else {
        char buf[48];
        snprintf(buf, sizeof(buf), ui_i18n_text(UI_TEXT_PICTURE_SELECTED_FMT),
                 (unsigned)s_select_count);
        lv_label_set_text(s_select_lbl, buf);
    }
}

static void grid_back_cb(lv_event_t *e)
{
    (void)e;
    if (s_select_mode) {
        exit_select_mode();
    }
    s_viewer_from_grid = false;   /* returning to the default viewer */
    set_mode(PICTURE_MODE_VIEW);
}

static void choose_cb(lv_event_t *e)
{
    (void)e;
    enter_select_mode();
}

static void cancel_cb(lv_event_t *e)
{
    (void)e;
    exit_select_mode();
}

static void clear_cells(void)
{
    if (s_grid_scroll) {
        lv_obj_clean(s_grid_scroll);
    }
    /* lv_obj_clean freed every cell/chk in the subtree — clear the cached
     * pointers in the same step (RULES §7.1). */
    if (s_items) {
        memset(s_items, 0, ITEMS_BYTES);
    }
    s_item_count = 0;
    s_select_count = 0;
}

static void batch_delete_confirm_cb(bool confirmed)
{
    if (!confirmed) {
        return;
    }
    const char *names[UI_PICTURE_MAX_THUMBS];
    uint32_t n = 0;
    for (uint32_t i = 0; i < s_item_count && n < UI_PICTURE_MAX_THUMBS; i++) {
        if (s_items[i].selected && s_items[i].name[0]) {
            names[n++] = s_items[i].name;
        }
    }
    if (n == 0) {
        return;
    }
    ui_svc_picture_delete_batch(names, n);   /* service copies names synchronously */
    exit_select_mode();
    clear_cells();
    ui_svc_picture_thumbs_request();         /* rebuild from the new album state */
}

static void grid_delete_cb(lv_event_t *e)
{
    (void)e;
    if (s_select_count == 0) {
        return;
    }
    ui_comp_popup_show(UI_COMP_POPUP_CONFIRM, NULL,
                       ui_i18n_text(UI_TEXT_PICTURE_DEL_BATCH_CONFIRM), batch_delete_confirm_cb);
}

static void build_grid(void)
{
    s_grid_cont = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_grid_cont);
    lv_obj_set_size(s_grid_cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_grid_cont, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_grid_cont, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_grid_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_grid_cont, LV_FLEX_FLOW_COLUMN);

    /* Top bar */
    lv_obj_t *topbar = lv_obj_create(s_grid_cont);
    lv_obj_remove_style_all(topbar);
    lv_obj_set_size(topbar, LV_PCT(100), ui_adapt(GRID_TOPBAR_H));
    lv_obj_set_style_bg_opa(topbar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(topbar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(topbar, &icon_back_24_24, grid_back_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(PIC_BTN_MARGIN), 0);

    s_grid_title = lv_label_create(topbar);
    lv_label_set_text(s_grid_title, ui_i18n_text(UI_TEXT_APP_PICTURE));
    lv_obj_set_style_text_color(s_grid_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_grid_title, LV_ALIGN_CENTER, 0, 0);

    s_choose_btn = ui_comp_btn_icon_create(topbar, &icon_choose, choose_cb);
    lv_obj_align(s_choose_btn, LV_ALIGN_RIGHT_MID, -ui_adapt(PIC_BTN_MARGIN), 0);

    s_cancel_btn = ui_comp_btn_create_styled(topbar, ui_i18n_text(UI_TEXT_CANCEL),
                                             UI_COMP_BTN_TEXT, cancel_cb);
    lv_obj_align(s_cancel_btn, LV_ALIGN_RIGHT_MID, -ui_adapt(PIC_BTN_MARGIN), 0);
    lv_obj_add_flag(s_cancel_btn, LV_OBJ_FLAG_HIDDEN);

    /* Scrollable thumbnail area (flex wrap, 3 columns) */
    s_grid_scroll = lv_obj_create(s_grid_cont);
    lv_obj_remove_style_all(s_grid_scroll);
    lv_obj_set_width(s_grid_scroll, LV_PCT(100));
    lv_obj_set_flex_grow(s_grid_scroll, 1);
    lv_obj_set_style_bg_opa(s_grid_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_grid_scroll, ui_adapt(GRID_PAD), 0);
    lv_obj_set_style_pad_row(s_grid_scroll, ui_adapt(GRID_GAP), 0);
    lv_obj_set_style_pad_column(s_grid_scroll, ui_adapt(GRID_GAP), 0);
    lv_obj_set_flex_flow(s_grid_scroll, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scroll_dir(s_grid_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_grid_scroll, LV_SCROLLBAR_MODE_OFF);

    /* Bottom bar (selection mode, hidden by default) */
    s_bottom_bar = lv_obj_create(s_grid_cont);
    lv_obj_remove_style_all(s_bottom_bar);
    lv_obj_set_size(s_bottom_bar, LV_PCT(100), ui_adapt(GRID_BOTBAR_H));
    lv_obj_align(s_bottom_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_bottom_bar, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(s_bottom_bar, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_bottom_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_bottom_bar, LV_OBJ_FLAG_HIDDEN);

    s_select_lbl = lv_label_create(s_bottom_bar);
    lv_label_set_text(s_select_lbl, ui_i18n_text(UI_TEXT_PICTURE_SELECT));
    lv_obj_set_style_text_color(s_select_lbl, UI_COLOR_TEXT, 0);
    lv_obj_align(s_select_lbl, LV_ALIGN_LEFT_MID, ui_adapt(PIC_BTN_MARGIN), 0);

    lv_obj_t *del = ui_comp_btn_icon_create(s_bottom_bar, &icon_delete, grid_delete_cb);
    lv_obj_align(del, LV_ALIGN_RIGHT_MID, -ui_adapt(PIC_BTN_MARGIN), 0);

    lv_obj_add_flag(s_grid_cont, LV_OBJ_FLAG_HIDDEN);   /* viewer is the default */
}

/* ---------------------------------------------------------------------------
 * Mode switch
 * --------------------------------------------------------------------------- */
static void set_mode(picture_mode_t mode)
{
    s_mode = mode;
    if (mode == PICTURE_MODE_VIEW) {
        lv_obj_add_flag(s_grid_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_view_cont, LV_OBJ_FLAG_HIDDEN);
        set_viewer_controls_visible(true);
    } else {
        lv_obj_add_flag(s_view_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_grid_cont, LV_OBJ_FLAG_HIDDEN);
        if (s_select_mode) {
            exit_select_mode();
        }
        ui_svc_picture_thumbs_request();
    }
}

/* ---------------------------------------------------------------------------
 * Service callbacks (UI thread)
 * --------------------------------------------------------------------------- */
static void on_picture_count(uint32_t count)
{
    s_count = count;
    update_viewer_actions();
    if (count == 0) {
        s_idx = 0;
        ui_comp_picture_clear(s_picture);
        lv_obj_clear_flag(s_empty_hint, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (s_idx >= count) {
        s_idx = count - 1;   /* also resolves the PIC_IDX_LAST sentinel */
    }
    lv_obj_add_flag(s_empty_hint, LV_OBJ_FLAG_HIDDEN);
    ui_svc_picture_request(s_idx, ++s_seq);
}

static void on_picture(const ui_picture_t *picture)
{
    /* Drop frames that no longer match the latest request, or arrived after the
     * user left the viewer — release their buffer so nothing leaks. */
    if (s_mode != PICTURE_MODE_VIEW || picture->seq != s_seq) {
        if (picture->data) {
            ui_svc_picture_free_rgb565(picture->data);
        }
        return;
    }
    if (picture->data) {
        s_idx = picture->index;
        ui_comp_picture_set_rgb565(s_picture, picture->width, picture->height,
                                 picture->data, ui_svc_picture_free_rgb565);
        lv_obj_add_flag(s_empty_hint, LV_OBJ_FLAG_HIDDEN);
    }
    /* picture->data == NULL: decode failed; keep the previous frame on screen. */
}

static void on_picture_thumbs(const ui_picture_thumb_t *items, uint32_t count)
{
    if (!s_grid_scroll || !s_items) {
        return;   /* no grid, or slot alloc failed → nothing to populate */
    }
    clear_cells();

    if (count > UI_PICTURE_MAX_THUMBS) {
        count = UI_PICTURE_MAX_THUMBS;
    }
    for (uint32_t i = 0; i < count; i++) {
        const ui_picture_thumb_t *t = &items[i];
        grid_item_t *gi = &s_items[i];

        gi->cell = lv_obj_create(s_grid_scroll);
        lv_obj_remove_style_all(gi->cell);
        lv_obj_set_size(gi->cell, ui_adapt(GRID_CELL), ui_adapt(GRID_CELL));
        lv_obj_set_style_radius(gi->cell, ui_adapt(GRID_RADIUS), 0);
        lv_obj_set_style_bg_color(gi->cell, UI_COLOR_BG_CARD, 0);
        lv_obj_set_style_bg_opa(gi->cell, LV_OPA_COVER, 0);
        lv_obj_set_style_clip_corner(gi->cell, true, 0);
        lv_obj_clear_flag(gi->cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(gi->cell, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(gi->cell, cell_click_cb, LV_EVENT_CLICKED, NULL);

        if (t->data && t->width > 0 && t->height > 0) {
            lv_obj_t *thumb = ui_comp_picture_create(gi->cell);
            lv_obj_clear_flag(thumb, LV_OBJ_FLAG_CLICKABLE);
            /* Borrowed buffer (service owns the thumb list) → free_fn = NULL. */
            ui_comp_picture_set_rgb565(thumb, t->width, t->height, t->data, NULL);
        }

        strncpy(gi->name, t->name, UI_PICTURE_NAME_MAX);
        gi->name[UI_PICTURE_NAME_MAX] = '\0';
        gi->selected = false;
        gi->chk = NULL;
    }
    s_item_count = count;
}

/* ---------------------------------------------------------------------------
 * Gestures
 * --------------------------------------------------------------------------- */
static void gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

    if (s_mode == PICTURE_MODE_VIEW) {
        if (s_ai_action_pending) {
            return;
        }
        if (dir == LV_DIR_LEFT) {
            view_nav(+1);
        } else if (dir == LV_DIR_RIGHT) {
            view_nav(-1);
        }
    } else {   /* GRID: right-swipe returns to the viewer */
        if (dir == LV_DIR_RIGHT) {
            grid_back_cb(NULL);
        }
    }
}

/* ---------------------------------------------------------------------------
 * i18n refresh
 * --------------------------------------------------------------------------- */
static void refresh_texts(void)
{
    if (s_empty_hint) {
        lv_label_set_text(s_empty_hint, ui_i18n_text(UI_TEXT_PICTURE_EMPTY));
    }
    if (s_all_btn) {
        ui_comp_btn_set_text(s_all_btn, ui_i18n_text(UI_TEXT_PICTURE_ALL));
    }
    if (s_grid_title) {
        lv_label_set_text(s_grid_title, ui_i18n_text(UI_TEXT_APP_PICTURE));
    }
    if (s_cancel_btn) {
        ui_comp_btn_set_text(s_cancel_btn, ui_i18n_text(UI_TEXT_CANCEL));
    }
    if (s_delete_btn) {
        ui_comp_btn_set_text(s_delete_btn, ui_i18n_text(UI_TEXT_PICTURE_DELETE));
    }
    if (s_ai_recognize_btn) {
        ui_comp_btn_set_text(s_ai_recognize_btn,
                             s_pending_action_btn == s_ai_recognize_btn
                                 ? ui_i18n_text(UI_TEXT_LOADING)
                                 : ui_i18n_text(UI_TEXT_PICTURE_AI_RECOGNIZE));
    }
    if (s_image_to_image_btn) {
        ui_comp_btn_set_text(s_image_to_image_btn,
                             s_pending_action_btn == s_image_to_image_btn
                                 ? ui_i18n_text(UI_TEXT_LOADING)
                                 : ui_i18n_text(UI_TEXT_PICTURE_IMAGE_TO_IMAGE));
    }
    update_select_label();
}

/* ---------------------------------------------------------------------------
 * Page lifecycle
 * --------------------------------------------------------------------------- */
static void on_create(void *parent)
{
    (void)parent;

    s_mode = PICTURE_MODE_VIEW;
    s_viewer_from_grid = false;
    s_ai_action_pending = false;
    s_count = 0;
    s_idx = PIC_IDX_LAST;   /* first load lands on the last (newest) picture */
    s_seq = 0;
    s_select_mode = false;
    s_select_count = 0;
    s_item_count = 0;
    s_topbar = NULL;
    s_action_dock = NULL;
    s_back_btn = NULL;
    s_delete_btn = NULL;
    s_ai_recognize_btn = NULL;
    s_image_to_image_btn = NULL;
    s_pending_action_btn = NULL;
    s_controls_visible = true;

    /* Grid slots are only needed while this page lives. Failure is non-fatal —
     * see the declaration. */
    s_items = tal_malloc(ITEMS_BYTES);
    if (s_items) {
        memset(s_items, 0, ITEMS_BYTES);
    } else {
        PR_ERR("photo: grid slot alloc %d bytes failed, grid disabled", (int)ITEMS_BYTES);
    }

    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_top(s_screen, ui_adapt(PIC_TOP_PAD), 0);   /* clear statusbar */
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    build_viewer();
    build_grid();

    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);

    ui_svc_picture_set_cbs(on_picture_count, on_picture, on_picture_thumbs);
    ui_svc_picture_open();   /* async → on_picture_count → loads the last picture */
}

static void on_enter(uint32_t dirty)
{
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        refresh_texts();   /* language may have changed */
    }
}

static void on_leave(void)
{
}

static void on_destroy(void)
{
    /* Thumbnail canvases borrow service-owned pixel buffers. Delete them before
     * asking the service to release the current list. */
    clear_cells();
    ui_svc_picture_set_cbs(NULL, NULL, NULL);   /* late async results become no-ops */
    ui_svc_picture_close();

    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
    }
    s_screen = NULL;
    s_view_cont = NULL;
    s_picture = NULL;
    s_overlay = NULL;
    s_empty_hint = NULL;
    s_topbar = NULL;
    s_action_dock = NULL;
    s_back_btn = NULL;
    s_all_btn = NULL;
    s_delete_btn = NULL;
    s_ai_recognize_btn = NULL;
    s_image_to_image_btn = NULL;
    s_pending_action_btn = NULL;
    s_controls_visible = true;
    s_grid_cont = NULL;
    s_grid_title = NULL;
    s_choose_btn = NULL;
    s_cancel_btn = NULL;
    s_grid_scroll = NULL;
    s_bottom_bar = NULL;
    s_select_lbl = NULL;
    /* clear_cells() above already deleted the cells and their cached pointers;
     * release the slot array together with its count. */
    tal_free(s_items);
    s_items = NULL;
    s_item_count = 0;
    s_select_mode = false;
    s_select_count = 0;
    s_ai_action_pending = false;
}

const ui_page_entry_t ui_page_photo_entry = {
    .id = UI_PAGE_PHOTO,
    .name = "photo",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
