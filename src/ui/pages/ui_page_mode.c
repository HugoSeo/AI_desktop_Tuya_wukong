#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_svc_dev_ctrl.h"
#include "ui_page_ids.h"

LV_IMG_DECLARE(icon_back_24_24);

/* ---------------------------------------------------------------------------
 * Constants
 * -------------------------------------------------------------------------*/
#define MODE_STATUSBAR_H  32
#define MODE_TITLEBAR_H   48
#define MODE_ROW_H        56

/* Mode list table. Index must stay in sync with AI_DEVICE_MODE_E so the row
 * index equals the mode value. Hidden modes (P2P, record) are kept in the table
 * to preserve that mapping but are not shown in the list. */
typedef struct {
    ui_i18n_key_t key;
    bool          visible;
} mode_item_t;

static const mode_item_t s_modes[] = {
    { UI_TEXT_MODE_CHAT,      true  },  /* 0 chat */
    { UI_TEXT_MODE_TRANSLATE, true  },  /* 1 translate */
    { UI_TEXT_MODE_P2P,       false },  /* 2 p2p */
    { UI_TEXT_MODE_RECORD,    false },  /* 3 record */
    { UI_TEXT_MODE_PICTURE,   true  },  /* 4 picture */
    { UI_TEXT_MODE_DETECTION, true  },  /* 5 detection */
};
#define MODE_COUNT (sizeof(s_modes) / sizeof(s_modes[0]))

/* ---------------------------------------------------------------------------
 * Internal state
 * -------------------------------------------------------------------------*/
static lv_obj_t *s_screen          = NULL;
static lv_obj_t *s_title           = NULL;
static lv_obj_t *s_rows[MODE_COUNT];
static lv_obj_t *s_row_labels[MODE_COUNT];

/* ---------------------------------------------------------------------------
 * Forward declarations
 * -------------------------------------------------------------------------*/
static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_leave(void);
static void on_destroy(void);
static void back_click_cb(lv_event_t *e);
static void gesture_cb(lv_event_t *e);
static void mode_row_cb(lv_event_t *e);
static void refresh_selection(void);

/* ---------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------*/

static void row_set_selected(uint8_t idx, bool selected)
{
    lv_obj_t *row   = s_rows[idx];
    lv_obj_t *label = s_row_labels[idx];
    if (!row || !label) {
        return;
    }
    if (selected) {
        lv_obj_set_style_bg_color(row, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_text_color(label, UI_COLOR_TEXT, 0);
    } else {
        lv_obj_set_style_bg_color(row, UI_COLOR_BG_CARD, 0);
        lv_obj_set_style_text_color(label, UI_COLOR_TEXT_SEC, 0);
    }
}

static lv_obj_t *create_row(lv_obj_t *parent, uint8_t idx)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, ui_adapt(MODE_ROW_H));
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_80, LV_STATE_PRESSED);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, mode_row_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)idx);
    s_rows[idx] = row;

    lv_obj_t *label = lv_label_create(row);
    lv_label_set_text(label, ui_i18n_text(s_modes[idx].key));
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    s_row_labels[idx] = label;

    return row;
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
    lv_obj_set_flex_align(s_screen,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(s_screen, ui_adapt(MODE_STATUSBAR_H), 0);

    /* Title bar */
    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(MODE_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_click_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);

    s_title = lv_label_create(titlebar);
    lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_SETTINGS_MODE));
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    /* Content: vertical list of mode rows */
    lv_obj_t *content = lv_obj_create(s_screen);
    lv_obj_remove_style_all(content);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(content, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_row(content, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t i = 0; i < MODE_COUNT; i++) {
        if (!s_modes[i].visible) {
            continue;
        }
        create_row(content, i);
    }

    refresh_selection();
}

static void on_enter(uint32_t dirty)
{
    if (dirty & (1u << UI_STATE_GROUP_CHAT)) {
        refresh_selection();
    }
}

static void on_leave(void)
{
    ui_svc_dev_ctrl_mode_save();
}

static void on_destroy(void)
{
    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
        s_screen = NULL;
        s_title  = NULL;
        for (uint8_t i = 0; i < MODE_COUNT; i++) {
            s_rows[i]       = NULL;
            s_row_labels[i] = NULL;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Refresh
 * -------------------------------------------------------------------------*/
static void refresh_selection(void)
{
    uint8_t cur = ui_svc_dev_ctrl_mode_get();
    for (uint8_t i = 0; i < MODE_COUNT; i++) {
        row_set_selected(i, i == cur);
    }
}

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

static void mode_row_cb(lv_event_t *e)
{
    uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
    if (idx == ui_svc_dev_ctrl_mode_get()) {
        return;
    }
    ui_svc_dev_ctrl_mode_set(idx);
    /* Clear the whole route stack and land on the chat page. */
    ui_route_reset(UI_PAGE_CHAT);
    // ui_route_pop();
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/
const ui_page_entry_t ui_page_mode_entry = {
    .id = UI_PAGE_MODE,
    .name = "mode",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
