#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_comp_btn.h"
#include "ui_comp_popup.h"
#include "ui_feature.h"
#include "ui_state.h"
#include "ui_page_ids.h"

/* ---------------------------------------------------------------------------
 * App Center — a launcher page reached by swiping left from Home.
 *
 * Reuses the pulldown's card-grid look (icon-over-text tiles) and its top
 * treatment: the global statusbar + a gap, no title bar. A swipe-right returns
 * to Home. Tapping a card pushes its target page, so back from the target lands
 * back here.
 * -------------------------------------------------------------------------*/

/* ---------------------------------------------------------------------------
 * Style constants
 * -------------------------------------------------------------------------*/
#define APP_CENTER_STATUSBAR_H    32   /* global statusbar height (ui_comp_statusbar) */
#define APP_CENTER_CARD_COLS      3
#define APP_CENTER_CARD_H         75
#define APP_CENTER_CARD_BG        0xB8BDDE   /* match the pulldown card tint */
#define APP_CENTER_CARD_BG_OPA    28

/* ---------------------------------------------------------------------------
 * Icon declarations
 * -------------------------------------------------------------------------*/
LV_IMG_DECLARE(icon_photo_app);
LV_IMG_DECLARE(icon_camera_app);
LV_IMG_DECLARE(icon_record_app);
LV_IMG_DECLARE(icon_music_app);
LV_IMG_DECLARE(icon_clock_app);
LV_IMG_DECLARE(icon_calendar_app);
LV_IMG_DECLARE(icon_detection_app);
LV_IMG_DECLARE(icon_record_list);
LV_IMG_DECLARE(icon_diag_app);
LV_IMG_DECLARE(icon_settings_app);

/* ---------------------------------------------------------------------------
 * Card definitions
 * -------------------------------------------------------------------------*/
typedef struct {
    const void    *icon;
    ui_page_id_t   target;       /* destination page pushed on tap */
    ui_feature_id_t feature;     /* 所属功能簇；UI_FEATURE_ID_NONE = 核心，永远可用 */
} app_center_card_t;

/* Non-const so its entries can be handed to lv_obj_add_event_cb as user_data
 * without discarding a const qualifier (matches the pulldown's card table). */
static app_center_card_t s_card_defs[] = {
    { &icon_photo_app,     UI_PAGE_PHOTO,     UI_FEATURE_ID_CAMERA },
    { &icon_camera_app,    UI_PAGE_VIDEO,     UI_FEATURE_ID_VIDEO },
    { &icon_record_app,    UI_PAGE_RECORDING, UI_FEATURE_ID_RECORDING },
    { &icon_music_app,     UI_PAGE_MUSIC,     UI_FEATURE_ID_MUSIC },
    { &icon_clock_app,     UI_PAGE_CLOCK,     UI_FEATURE_ID_TIME },
    { &icon_calendar_app,  UI_PAGE_SCHEDULE,  UI_FEATURE_ID_TIME },
    { &icon_detection_app, UI_PAGE_DETECTION, UI_FEATURE_ID_DETECTION },
    { &icon_record_list,   UI_PAGE_FILES,     UI_FEATURE_ID_FILES },
    { &icon_diag_app,      UI_PAGE_DIAG,      UI_FEATURE_ID_NONE },
    { &icon_settings_app,  UI_PAGE_SETTINGS,  UI_FEATURE_ID_NONE },
};
#define CARD_COUNT (sizeof(s_card_defs) / sizeof(s_card_defs[0]))

/* Index-aligned with s_card_defs[]: card i renders the label from s_label_keys[i]. */
static const ui_i18n_key_t s_label_keys[] = {
    UI_TEXT_APP_PICTURE,
    UI_TEXT_APP_VIDEO,
    UI_TEXT_APP_RECORD,
    UI_TEXT_APP_MUSIC,
    UI_TEXT_CLOCK_TITLE,
    UI_TEXT_SCHEDULE_TITLE,
    UI_TEXT_APP_DETECTION,
    UI_TEXT_APP_FILES,
    UI_TEXT_APP_DIAG,
    UI_TEXT_APP_SETTINGS,
};

/* ---------------------------------------------------------------------------
 * Internal state
 * -------------------------------------------------------------------------*/
static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_grid   = NULL;
static lv_obj_t *s_cards[CARD_COUNT];             /* cached so on_enter can refresh labels */

/* ---------------------------------------------------------------------------
 * Forward declarations
 * -------------------------------------------------------------------------*/
static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_leave(void);
static void on_destroy(void);
static void gesture_cb(lv_event_t *e);
static void card_click_cb(lv_event_t *e);
static lv_obj_t *create_card(lv_obj_t *parent, const app_center_card_t *cfg,
                             lv_coord_t w, lv_coord_t h, const char *label);

/* ---------------------------------------------------------------------------
 * Page lifecycle
 * -------------------------------------------------------------------------*/

/* App card = icon-over-text tile. Reuse the shared icon+text button (TOP layout)
 * for structure + press feedback; the translucent fill / radius are page-specific
 * so they stay here. The widget cb carries no user_data, so attach our own to pass
 * the card def. */
static lv_obj_t *create_card(lv_obj_t *parent, const app_center_card_t *cfg,
                             lv_coord_t w, lv_coord_t h, const char *label)
{
    lv_obj_t *card = ui_comp_btn_create_with_icon(parent, cfg->icon, label,
                                                  UI_COMP_BTN_TEXT, UI_COMP_BTN_ICON_TOP, NULL);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_CENTER_CARD_BG), 0);
    lv_obj_set_style_bg_opa(card, APP_CENTER_CARD_BG_OPA, 0);
    lv_obj_set_style_radius(card, ui_adapt(UI_RADIUS_LG), 0);
    lv_obj_set_style_text_color(card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(card, card_click_cb, LV_EVENT_CLICKED, (void *)cfg);
    return card;
}

static void on_create(void *parent)
{
    (void)parent;
    uint16_t sw = ui_adapt_screen_w();

    /* Full-screen root, column layout: card grid below the statusbar */
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
    /* statusbar band (32) + a gap below the floating statusbar pill — the card
     * grid is the first content, so without this gap it would sit flush against
     * the pill (same treatment as the pulldown panel). */
    lv_obj_set_style_pad_top(s_screen, ui_adapt(APP_CENTER_STATUSBAR_H) + ui_adapt(UI_SPACE_MD), 0);

    /* Card grid — fills the remaining height, centered both axes */
    s_grid = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_grid);
    lv_obj_set_width(s_grid, LV_PCT(100));
    lv_obj_set_flex_grow(s_grid, 1);
    lv_obj_set_layout(s_grid, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(s_grid, LV_FLEX_FLOW_ROW_WRAP);
    /* Pack from the top so the first card row sits exactly pad_top below the
     * statusbar (same gap as the pulldown), instead of being centered — which
     * pushed the rows down and away from the statusbar. */
    lv_obj_set_flex_align(s_grid,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_hor(s_grid, ui_adapt(UI_SPACE_LG), 0);
    lv_obj_set_style_pad_row(s_grid, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_set_style_pad_column(s_grid, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_clear_flag(s_grid, LV_OBJ_FLAG_SCROLLABLE);

    lv_coord_t total_w = sw - ui_adapt(UI_SPACE_LG) * 2;
    lv_coord_t card_w = (total_w - ui_adapt(UI_SPACE_MD) * (APP_CENTER_CARD_COLS - 1)) / APP_CENTER_CARD_COLS;
    lv_coord_t card_h = ui_adapt(APP_CENTER_CARD_H);

    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        s_cards[i] = create_card(s_grid, &s_card_defs[i], card_w, card_h,
                                 ui_i18n_text(s_label_keys[i]));
    }

    lv_obj_update_layout(s_screen);   /* settle flex before any later label writes */
}

static void on_enter(uint32_t dirty)
{
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        /* Language changed: re-resolve every card label fresh (RULES: never cache
         * ui_i18n_text() pointers across a language switch). */
        for (uint8_t i = 0; i < CARD_COUNT; i++) {
            if (s_cards[i]) {
                ui_comp_btn_set_text(s_cards[i], ui_i18n_text(s_label_keys[i]));
            }
        }
    }
}

static void on_leave(void)
{
    /* No settings to persist on this page. */
}

static void on_destroy(void)
{
    if (s_screen) {
        /* on_destroy runs inside the event-callback chain (back/swipe -> ui_route_pop);
         * hide immediately then defer deletion to avoid freeing the object while
         * LVGL is still processing events on it. */
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
    }
    s_screen = NULL;
    s_grid   = NULL;
    for (uint8_t i = 0; i < CARD_COUNT; i++) {
        s_cards[i] = NULL;
    }
}

/* ---------------------------------------------------------------------------
 * Callbacks
 * -------------------------------------------------------------------------*/

/* Swipe-right = back to Home (RULES §3.1) — the sole back affordance (this page
 * has no title bar / back button). */
static void gesture_cb(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());   /* guard: don't click the page below */
        ui_route_reset(UI_PAGE_HOME);
    }
}

static void card_click_cb(lv_event_t *e)
{
    const app_center_card_t *cfg = lv_event_get_user_data(e);
    if (!cfg || cfg->target == UI_PAGE_NONE) {
        return;
    }
    /* Feature compiled out: toast and stay put, don't navigate. */
    if (!ui_feature_available(cfg->feature)) {
        ui_comp_popup_toast(ui_i18n_text(UI_TEXT_FEATURE_UNAVAILABLE), 2000);
        return;
    }
    /* Push (not reset): back from the target returns to the App Center. */
    ui_route_push(cfg->target);
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/
const ui_page_entry_t ui_page_app_center_entry = {
    .id = UI_PAGE_APP_CENTER,
    .name = "app_center",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
