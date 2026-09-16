#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_comp_popup.h"
#include "ui_comp_statusbar.h"
#include "ui_page_ids.h"
#include "tal_memory.h"
#include "uni_log.h"
#include <string.h>

/* ---------------------------------------------------------------------------
 * Screen test — immersive full-screen LCD/touch test page under Diagnostics.
 *
 * Tap anywhere to advance through the test steps:
 *   solid white / black / red / green / blue / 50% gray  (dead pixels, light
 *   leak; the R/G/B screens expose RGB565 byte-order faults at a glance)
 *   -> color bars (8 vertical strips)
 *   -> touch draw (strokes rendered with lv_line; Clear / Exit buttons)
 *
 * Exit: swipe right on any color/bars step; the draw step swallows gestures
 * (they would conflict with drawing), so it exits via its Exit button only.
 * The global statusbar is hidden while the page is foreground (same immersive
 * treatment as WLAN/Camera) and ui_app disables the pulldown gesture zone.
 * -------------------------------------------------------------------------*/

/* ---------------------------------------------------------------------------
 * Test sequence
 * -------------------------------------------------------------------------*/
static const uint32_t s_solid_colors[] = {
    0xFFFFFF,   /* white */
    0x000000,   /* black */
    0xFF0000,   /* red   */
    0x00FF00,   /* green */
    0x0000FF,   /* blue  */
    0x808080,   /* 50% gray */
};
#define SOLID_COUNT     (sizeof(s_solid_colors) / sizeof(s_solid_colors[0]))
#define STEP_BARS       SOLID_COUNT          /* color-bars step index */
#define STEP_DRAW       (SOLID_COUNT + 1)    /* touch-draw step index */
#define STEP_COUNT      (SOLID_COUNT + 2)

/* Color-bars strip colors (classic 8-bar pattern). */
static const uint32_t s_bar_colors[] = {
    0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00,
    0xFF00FF, 0xFF0000, 0x0000FF, 0x000000,
};
#define BAR_COUNT       (sizeof(s_bar_colors) / sizeof(s_bar_colors[0]))

/* Touch-draw stroke storage: static pools, no canvas buffer (a full-screen
 * RGB565 canvas would cost hundreds of KB of RAM). */
#define DRAW_MAX_STROKES    8
#define DRAW_MAX_POINTS     256
#define DRAW_LINE_WIDTH     3

/* ---------------------------------------------------------------------------
 * Internal state
 * -------------------------------------------------------------------------*/
static lv_obj_t *s_screen     = NULL;
static lv_obj_t *s_bars       = NULL;   /* color-bars container (hidden elsewhere) */
static lv_obj_t *s_draw_layer = NULL;   /* draw-step container: strokes + buttons */
static uint8_t   s_step       = 0;

static lv_obj_t  *s_stroke_obj[DRAW_MAX_STROKES];               /* lv_line per stroke */
/* Point pool, allocated in on_create and released in on_destroy. Declared as a
 * pointer-to-array so s_stroke_pts[stroke][point] indexing is unchanged.
 * NULL means the allocation failed — draw_track_point then bails out and the
 * draw step simply doesn't record strokes (the other test patterns still work). */
static lv_point_t (*s_stroke_pts)[DRAW_MAX_POINTS];
static uint16_t   s_stroke_len[DRAW_MAX_STROKES];
static uint8_t    s_stroke_cnt = 0;
static uint8_t    s_stroke_oldest = 0;   /* ring cursor: slot to reclaim when the pool is full */
static bool       s_stroking   = false;  /* finger currently down on the draw step */

/* ---------------------------------------------------------------------------
 * Forward declarations
 * -------------------------------------------------------------------------*/
static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_leave(void);
static void on_destroy(void);
static void screen_event_cb(lv_event_t *e);
static void clear_click_cb(lv_event_t *e);
static void exit_click_cb(lv_event_t *e);
static void apply_step(void);
static void draw_clear(void);

/* ---------------------------------------------------------------------------
 * Step rendering
 * -------------------------------------------------------------------------*/

/* Paint the current step: solid background, or show the bars / draw layer. */
static void apply_step(void)
{
    if (!s_screen) {
        return;
    }
    lv_obj_add_flag(s_bars, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_draw_layer, LV_OBJ_FLAG_HIDDEN);

    if (s_step < SOLID_COUNT) {
        lv_obj_set_style_bg_color(s_screen, lv_color_hex(s_solid_colors[s_step]), 0);
    } else if (s_step == STEP_BARS) {
        lv_obj_clear_flag(s_bars, LV_OBJ_FLAG_HIDDEN);
    } else {            /* STEP_DRAW */
        lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x000000), 0);
        draw_clear();
        lv_obj_clear_flag(s_draw_layer, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Drop all strokes (draw-step reset; also called when re-entering the step). */
static void draw_clear(void)
{
    for (uint8_t i = 0; i < DRAW_MAX_STROKES; i++) {
        if (s_stroke_obj[i]) {
            lv_obj_del(s_stroke_obj[i]);
            s_stroke_obj[i] = NULL;
        }
        s_stroke_len[i] = 0;
    }
    s_stroke_cnt = 0;
    s_stroke_oldest = 0;
    s_stroking   = false;
}

/* Append the current touch point to the active stroke (draw step only). */
static void draw_track_point(void)
{
    if (!s_stroke_pts) {
        return;   /* point pool alloc failed → drawing disabled */
    }
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    uint8_t idx = s_stroke_cnt - 1;
    if (!s_stroking) {
        /* New stroke: reuse the oldest slot when the pool is full. */
        if (s_stroke_cnt < DRAW_MAX_STROKES) {
            idx = s_stroke_cnt++;
        } else {
            idx = s_stroke_oldest;
            lv_obj_del(s_stroke_obj[idx]);
            s_stroke_obj[idx] = NULL;
            s_stroke_len[idx] = 0;
            s_stroke_oldest = (uint8_t)((s_stroke_oldest + 1) % DRAW_MAX_STROKES);
        }
        s_stroke_obj[idx] = lv_line_create(s_draw_layer);
        lv_obj_set_style_line_color(s_stroke_obj[idx], lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_line_width(s_stroke_obj[idx], DRAW_LINE_WIDTH, 0);
        lv_obj_set_style_line_rounded(s_stroke_obj[idx], true, 0);
        s_stroking = true;
    }
    if (s_stroke_len[idx] >= DRAW_MAX_POINTS) {
        return;   /* stroke full: keep what's drawn, ignore further points */
    }
    s_stroke_pts[idx][s_stroke_len[idx]++] = p;
    lv_line_set_points(s_stroke_obj[idx], s_stroke_pts[idx], s_stroke_len[idx]);
}

/* ---------------------------------------------------------------------------
 * Page lifecycle
 * -------------------------------------------------------------------------*/
static void on_create(void *parent)
{
    (void)parent;
    s_step = 0;

    /* 8KB point pool: only the draw step needs it, and this page is entered
     * from the diagnostics menu. Failure is non-fatal — see the declaration. */
    s_stroke_pts = tal_malloc(sizeof(*s_stroke_pts) * DRAW_MAX_STROKES);
    if (s_stroke_pts) {
        memset(s_stroke_pts, 0, sizeof(*s_stroke_pts) * DRAW_MAX_STROKES);
    } else {
        PR_ERR("screen_test: stroke pool alloc %d bytes failed, drawing disabled",
               (int)(sizeof(*s_stroke_pts) * DRAW_MAX_STROKES));
    }

    /* Full-screen root; bg color IS the test pattern for the solid steps. */
    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_CLICKABLE);
    /* One handler for tap-advance, swipe-exit and draw tracking — the mode
     * split lives in screen_event_cb. */
    lv_obj_add_event_cb(s_screen, screen_event_cb, LV_EVENT_ALL, NULL);

    /* Color bars: 8 equal vertical strips, prebuilt and hidden. */
    s_bars = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_bars);
    lv_obj_set_size(s_bars, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(s_bars, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(s_bars, LV_FLEX_FLOW_ROW);
    for (uint8_t i = 0; i < BAR_COUNT; i++) {
        lv_obj_t *bar = lv_obj_create(s_bars);
        lv_obj_remove_style_all(bar);
        lv_obj_set_height(bar, LV_PCT(100));
        lv_obj_set_flex_grow(bar, 1);
        lv_obj_set_style_bg_color(bar, lv_color_hex(s_bar_colors[i]), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    }

    /* Draw layer: holds the strokes plus the Clear / Exit buttons. */
    s_draw_layer = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_draw_layer);
    lv_obj_set_size(s_draw_layer, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(s_draw_layer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *clear_btn = ui_comp_btn_create_styled(s_draw_layer,
        ui_i18n_text(UI_TEXT_SCREEN_TEST_CLEAR), UI_COMP_BTN_TEXT, clear_click_cb);
    lv_obj_align(clear_btn, LV_ALIGN_TOP_LEFT, ui_adapt(UI_SPACE_MD), ui_adapt(UI_SPACE_MD));

    lv_obj_t *exit_btn = ui_comp_btn_create_styled(s_draw_layer,
        ui_i18n_text(UI_TEXT_SCREEN_TEST_EXIT), UI_COMP_BTN_TEXT, exit_click_cb);
    lv_obj_align(exit_btn, LV_ALIGN_TOP_RIGHT, -ui_adapt(UI_SPACE_MD), ui_adapt(UI_SPACE_MD));

    apply_step();

    /* Immersive: hide the global statusbar while this page is foreground. */
    ui_comp_statusbar_set_visible(false);
    ui_comp_popup_toast(ui_i18n_text(UI_TEXT_SCREEN_TEST_HINT), 2500);
}

static void on_enter(uint32_t dirty)
{
    (void)dirty;   /* test patterns have no language/state-dependent content */
}

static void on_leave(void)
{
    ui_comp_statusbar_set_visible(true);
}

static void on_destroy(void)
{
    ui_comp_statusbar_set_visible(true);

    /* lv_line_set_points() stores the caller's array by POINTER — it never
     * copies. s_screen below is deleted asynchronously, so its lv_line children
     * outlive this function by at least one refresh and would read the freed
     * pool. Break the references first, then free (RULES §7.1). */
    for (uint8_t i = 0; i < DRAW_MAX_STROKES; i++) {
        if (s_stroke_obj[i]) {
            lv_line_set_points(s_stroke_obj[i], NULL, 0);
        }
    }

    if (s_screen) {
        /* on_destroy runs inside the event-callback chain (exit btn / swipe ->
         * ui_route_pop); hide immediately then defer deletion to avoid freeing
         * the object while LVGL is still processing events on it. */
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
    }
    s_screen     = NULL;
    s_bars       = NULL;
    s_draw_layer = NULL;
    for (uint8_t i = 0; i < DRAW_MAX_STROKES; i++) {
        s_stroke_obj[i] = NULL;   /* children die with s_screen (RULES §7.1) */
        s_stroke_len[i] = 0;
    }
    /* Safe now: every lv_line was unbound above, so nothing points into the pool. */
    tal_free(s_stroke_pts);
    s_stroke_pts = NULL;
    s_stroke_cnt = 0;
    s_stroke_oldest = 0;
    s_stroking   = false;
}

/* ---------------------------------------------------------------------------
 * Callbacks
 * -------------------------------------------------------------------------*/

/* All screen input lands here; behavior depends on the current step:
 * color/bars steps: CLICKED advances, right-swipe GESTURE exits;
 * draw step: PRESSED/PRESSING track strokes, gestures/taps are swallowed. */
static void screen_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (s_step == STEP_DRAW) {
        if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
            draw_track_point();
        } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            s_stroking = false;
        }
        return;
    }

    if (code == LV_EVENT_CLICKED) {
        s_step = (uint8_t)((s_step + 1) % STEP_COUNT);
        apply_step();
    } else if (code == LV_EVENT_GESTURE &&
               lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());   /* swallow rest of touch so the release doesn't click the page below */
        ui_route_pop();
    }
}

/* Draw step "clear" button — drop all strokes. */
static void clear_click_cb(lv_event_t *e)
{
    (void)e;
    draw_clear();
}

/* Draw step "exit" button — the draw step swallows gestures, so this is its
 * only way out. */
static void exit_click_cb(lv_event_t *e)
{
    (void)e;
    ui_route_pop();
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/
const ui_page_entry_t ui_page_screen_test_entry = {
    .id = UI_PAGE_SCREEN_TEST,
    .name = "screen_test",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
