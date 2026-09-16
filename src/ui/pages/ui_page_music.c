#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
#include "ui_comp_bar.h"
#include "ui_svc_music.h"
#include "ui_page_ids.h"
#include <stdint.h>

LV_IMG_DECLARE(icon_back_24_24);

#define MUSIC_STATUSBAR_H  32   /* global statusbar band (ui_comp_statusbar) */
#define MUSIC_TITLEBAR_H   48
#define MUSIC_DISC_SIZE    180
#define MUSIC_DISC_VINYL   0x1F2933   /* must read against UI_COLOR_BG 0x1A1A1A */
#define MUSIC_DISC_RIM     0x3A4654   /* edge ring lifting the disc off the bg */
#define MUSIC_DISC_GROOVE  0x2E3947
#define MUSIC_DISC_INNER   0xE84A3F
#define MUSIC_ARTIST_COLOR 0xB8BDDE
#define MUSIC_DISC_SPIN_MS 8000   /* one revolution */
#define MUSIC_DISC_STEP_DEG     5    /* stripe angle quantization, see stripe_spin_set */

/* turntable wrapper: disc + tonearm overlay (design units, see ui_adapt).
 * Children are clipped to the wrapper, so the arm must fit inside it. */
#define MUSIC_TT_W           240
#define MUSIC_TT_H           190
#define MUSIC_DISC_OFS_X     30   /* disc top-left inside the wrapper */
#define MUSIC_DISC_OFS_Y     10
#define MUSIC_LABEL_SIZE     72
#define MUSIC_STRIPE_HALF    28   /* label stripe half-length */
#define MUSIC_STRIPE_W       5
#define MUSIC_STRIPE_COLOR   0xF2E8D8
#define MUSIC_ARM_COLOR      0xAEB6C2
#define MUSIC_ARM_BASE_COLOR 0x7A828E
#define MUSIC_ARM_W          5
#define MUSIC_ARM_BASE_SIZE  16
#define MUSIC_ARM_PIVOT_X    204  /* tonearm pivot inside the wrapper */
#define MUSIC_ARM_PIVOT_Y    22
#define MUSIC_ARM_UP_DEG     55   /* swing between on-disc (0) and rest pose */
#define MUSIC_ARM_SWING_MS   400

static lv_obj_t *s_screen    = NULL;
static lv_obj_t *s_title     = NULL;
static lv_obj_t *s_song      = NULL;
static lv_obj_t *s_artist    = NULL;
static lv_obj_t *s_disc      = NULL;   /* vinyl disc (static, never rotated) */
static lv_obj_t *s_stripe    = NULL;   /* label stripe (animated: conveys disc spin) */
static lv_obj_t *s_arm       = NULL;   /* tonearm polyline (swings on play/pause) */
static lv_obj_t *s_progress  = NULL;
static lv_obj_t *s_play_lbl  = NULL;
static lv_obj_t *s_mode_lbl  = NULL;
static lv_anim_t s_disc_anim;
static lv_anim_t s_arm_anim;
static lv_point_t s_stripe_pts[2];     /* lv_line keeps the pointer: must persist */
static lv_point_t s_arm_pts[3];
static bool      s_anim_running = false;
static bool      s_cb_attached  = false;   /* service callback registered (cleared by on_leave) */
static int       s_shown_pct    = INT16_MIN;   /* last rendered progress (-1 = unknown/dimmed) */
static int16_t   s_stripe_deg   = INT16_MIN;   /* last rendered stripe angle (deg) */
static int16_t   s_arm_deg      = INT16_MIN;   /* last rendered arm angle (deg) */
static int16_t   s_arm_pos      = 0;           /* current arm angle (0.1deg units) */
static int16_t   s_arm_target   = INT16_MIN;   /* active swing destination (0.1deg) */

static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_leave(void);
static void on_destroy(void);
static void back_cb(lv_event_t *e);
static void gesture_cb(lv_event_t *e);
static void prev_cb(lv_event_t *e);
static void play_cb(lv_event_t *e);
static void next_cb(lv_event_t *e);
static void list_cb(lv_event_t *e);
static void mode_cb(lv_event_t *e);
static void on_music_status(const ui_music_status_t *st);
static void refresh_text(const ui_music_status_t *st);
static void refresh_play_icon(const ui_music_status_t *st);
static void refresh_mode_label(void);
static void refresh_progress(void);
static void stripe_spin_set(void *obj, int32_t v);
static void disc_spin_update(AI_PLAYER_STATE_T state);
static void arm_swing_set(void *obj, int32_t v);
static void arm_update(AI_PLAYER_STATE_T state, bool animate);

/* ---- turntable: vinyl disc + spinning label stripe + tonearm ----
 * The disc widget itself never rotates: LVGL 8 renders transform_angle
 * through an intermediate layer (disc_w x disc_h x 4 bytes allocated +
 * software-rotated EVERY frame); on this target the allocation fails
 * (widget skipped — disc invisible while spinning) and the per-frame cost
 * starves audio playback. Spin is conveyed by re-pointing the label stripe
 * (small invalidation area), play/pause by swinging the tonearm. */
static void build_turntable(lv_obj_t *parent)
{
    lv_obj_t *tt = lv_obj_create(parent);
    lv_obj_remove_style_all(tt);
    lv_obj_set_size(tt, ui_adapt(MUSIC_TT_W), ui_adapt(MUSIC_TT_H));
    lv_obj_clear_flag(tt, LV_OBJ_FLAG_SCROLLABLE);

    s_disc = lv_obj_create(tt);
    lv_obj_remove_style_all(s_disc);
    lv_obj_set_size(s_disc, ui_adapt(MUSIC_DISC_SIZE), ui_adapt(MUSIC_DISC_SIZE));
    lv_obj_set_pos(s_disc, ui_adapt(MUSIC_DISC_OFS_X), ui_adapt(MUSIC_DISC_OFS_Y));
    lv_obj_set_style_radius(s_disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_disc, lv_color_hex(MUSIC_DISC_VINYL), 0);
    lv_obj_set_style_bg_opa(s_disc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_disc, ui_adapt(2), 0);
    lv_obj_set_style_border_color(s_disc, lv_color_hex(MUSIC_DISC_RIM), 0);
    lv_obj_clear_flag(s_disc, LV_OBJ_FLAG_SCROLLABLE);

    /* groove rings: border-only circles, static (drawn once per invalidation) */
    static const lv_coord_t groove_d[3] = {164, 140, 116};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *g = lv_obj_create(s_disc);
        lv_obj_remove_style_all(g);
        lv_obj_set_size(g, ui_adapt(groove_d[i]), ui_adapt(groove_d[i]));
        lv_obj_center(g);
        lv_obj_set_style_radius(g, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(g, ui_adapt(2), 0);
        lv_obj_set_style_border_color(g, lv_color_hex(MUSIC_DISC_GROOVE), 0);
        lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE);
    }

    lv_obj_t *inner = lv_obj_create(s_disc);
    lv_obj_remove_style_all(inner);
    lv_obj_set_size(inner, ui_adapt(MUSIC_LABEL_SIZE), ui_adapt(MUSIC_LABEL_SIZE));
    lv_obj_center(inner);
    lv_obj_set_style_radius(inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(inner, lv_color_hex(MUSIC_DISC_INNER), 0);
    lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, 0);
    lv_obj_clear_flag(inner, LV_OBJ_FLAG_SCROLLABLE);

    /* label stripe: a line through the label center; rotating its endpoints
     * is what makes the "record" visibly spin */
    s_stripe = lv_line_create(inner);
    lv_obj_set_style_line_width(s_stripe, ui_adapt(MUSIC_STRIPE_W), 0);
    lv_obj_set_style_line_color(s_stripe, lv_color_hex(MUSIC_STRIPE_COLOR), 0);
    lv_obj_set_style_line_rounded(s_stripe, true, 0);
    stripe_spin_set(s_stripe, 0);   /* park at 12 o'clock */

    /* spindle: child of the disc, created after inner so it covers the
     * stripe's middle section */
    lv_obj_t *center = lv_obj_create(s_disc);
    lv_obj_remove_style_all(center);
    lv_obj_set_size(center, ui_adapt(14), ui_adapt(14));
    lv_obj_center(center);
    lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(center, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(center, LV_OPA_COVER, 0);

    /* tonearm: polyline over the disc; pose applied by arm_update() */
    s_arm = lv_line_create(tt);
    lv_obj_set_style_line_width(s_arm, ui_adapt(MUSIC_ARM_W), 0);
    lv_obj_set_style_line_color(s_arm, lv_color_hex(MUSIC_ARM_COLOR), 0);
    lv_obj_set_style_line_rounded(s_arm, true, 0);

    /* pivot base cap, drawn above the arm joint */
    lv_obj_t *base = lv_obj_create(tt);
    lv_obj_remove_style_all(base);
    lv_obj_set_size(base, ui_adapt(MUSIC_ARM_BASE_SIZE), ui_adapt(MUSIC_ARM_BASE_SIZE));
    lv_obj_set_pos(base,
                   ui_adapt(MUSIC_ARM_PIVOT_X) - ui_adapt(MUSIC_ARM_BASE_SIZE) / 2,
                   ui_adapt(MUSIC_ARM_PIVOT_Y) - ui_adapt(MUSIC_ARM_BASE_SIZE) / 2);
    lv_obj_set_style_radius(base, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(base, lv_color_hex(MUSIC_ARM_BASE_COLOR), 0);
    lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
}

static lv_obj_t *make_symbol_btn(lv_obj_t *row, const char *symbol,
                                 lv_event_cb_t cb, lv_obj_t **lbl_out)
{
    lv_obj_t *btn = lv_btn_create(row);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, ui_adapt(56), ui_adapt(56));
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    /* Press feedback: translucent circular fill, same as the lock-screen
     * circle buttons (shared pressed style; rest state stays transparent). */
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_style(btn, &ui_style_btn_circle_pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);
    if (lbl_out) {
        *lbl_out = lbl;
    }
    return btn;
}

static void on_create(void *parent)
{
    (void)parent;
    s_anim_running = false;
    s_shown_pct    = INT16_MIN;   /* fresh widgets: force first progress render */
    s_stripe_deg   = INT16_MIN;   /* fresh widgets: force first stripe/arm render */
    s_arm_deg      = INT16_MIN;
    s_arm_pos      = MUSIC_ARM_UP_DEG * 10;
    s_arm_target   = INT16_MIN;

    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_screen, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_screen, ui_adapt(10), 0);
    /* Clear the global statusbar band so the title bar renders below it. */
    lv_obj_set_style_pad_top(s_screen, ui_adapt(MUSIC_STATUSBAR_H), 0);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);   /* swipe-right = back */

    /* title bar */
    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(MUSIC_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);
    s_title = lv_label_create(titlebar);
    lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_MUSIC_TITLE));
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    /* song / artist (LONG_DOT: no marquee anywhere on the music pages) */
    s_song = lv_label_create(s_screen);
    lv_label_set_long_mode(s_song, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_song, LV_PCT(80));
    lv_obj_set_style_text_align(s_song, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_song, UI_COLOR_TEXT, 0);

    s_artist = lv_label_create(s_screen);
    lv_label_set_long_mode(s_artist, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_artist, LV_PCT(80));
    lv_obj_set_style_text_align(s_artist, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_artist, lv_color_hex(MUSIC_ARTIST_COLOR), 0);

    build_turntable(s_screen);

    /* progress bar (display-only) */
    s_progress = ui_comp_bar_create(s_screen, 0, 100);
    lv_obj_set_width(s_progress, LV_PCT(80));
    ui_comp_bar_set_value(s_progress, 0);

    /* control row */
    lv_obj_t *row = lv_obj_create(s_screen);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), ui_adapt(64));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    make_symbol_btn(row, LV_SYMBOL_PREV, prev_cb, NULL);
    make_symbol_btn(row, LV_SYMBOL_PLAY, play_cb, &s_play_lbl);
    make_symbol_btn(row, LV_SYMBOL_NEXT, next_cb, NULL);
    make_symbol_btn(row, LV_SYMBOL_LIST, list_cb, NULL);

    /* mode toggle (text label cycles through 4 modes) */
    lv_obj_t *mode_btn = lv_btn_create(s_screen);
    lv_obj_remove_style_all(mode_btn);
    lv_obj_set_size(mode_btn, LV_SIZE_CONTENT, ui_adapt(32));
    lv_obj_set_style_bg_opa(mode_btn, LV_OPA_TRANSP, 0);
    /* Press feedback: translucent pill fill around the mode text. */
    lv_obj_set_style_pad_hor(mode_btn, ui_adapt(UI_SPACE_SM), 0);
    lv_obj_set_style_radius(mode_btn, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_add_style(mode_btn, &ui_style_btn_circle_pressed, LV_STATE_PRESSED);
    lv_obj_add_event_cb(mode_btn, mode_cb, LV_EVENT_CLICKED, NULL);
    s_mode_lbl = lv_label_create(mode_btn);
    lv_obj_set_style_text_color(s_mode_lbl, lv_color_hex(MUSIC_ARTIST_COLOR), 0);
    lv_obj_center(s_mode_lbl);

    /* Resolve percent widths before the initial render: LONG_DOT bakes the
     * "..." truncation into the text using the label's CURRENT width, so
     * setting text while the flex layout is still pending (pct width ~0)
     * would blank the song/artist labels. */
    lv_obj_update_layout(s_screen);

    /* initial render from current status */
    ui_music_status_t st;
    ui_svc_music_get_status(&st);
    refresh_text(&st);
    refresh_play_icon(&st);
    refresh_mode_label();
    refresh_progress();
    disc_spin_update(st.state);
    arm_update(st.state, false);   /* first render: place the arm, no swing */

    ui_svc_music_set_cb(on_music_status);
    s_cb_attached = true;

    /* Opening the page starts playback (resume / first item / cloud browse).
     * Only here in on_create: re-entering from a covering page (music list,
     * pulldown) must not force playback after a deliberate user pause. */
    ui_svc_music_autoplay();
}

static void on_enter(uint32_t dirty)
{
    /* Returning from a covering page (music list / pulldown overlay): on_leave
     * cleared the single service callback slot, so re-register and sync the
     * state changes missed while covered (song text, play icon, disc spin). */
    if (!s_cb_attached) {
        ui_svc_music_set_cb(on_music_status);
        s_cb_attached = true;
        ui_music_status_t st;
        ui_svc_music_get_status(&st);
        on_music_status(&st);
    }
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        if (s_title) {
            lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_MUSIC_TITLE));
        }
        refresh_mode_label();
        ui_music_status_t st;
        ui_svc_music_get_status(&st);
        refresh_text(&st);
    }
    /* per-second SYSTEM tick drives the display-only progress bar */
    if (dirty & (1u << UI_STATE_GROUP_SYSTEM)) {
        refresh_progress();
    }
}

static void on_leave(void)
{
    ui_svc_music_set_cb(NULL);
    s_cb_attached = false;
    /* A covering page (music list / pulldown) leaves this screen alive under
     * it: the spin anim would keep invalidating ~33fps beneath an opaque page,
     * waking the render pipeline against audio for nothing. on_enter's
     * !s_cb_attached resync restarts it via disc_spin_update. */
    if (s_anim_running) {
        lv_anim_del(s_stripe, stripe_spin_set);
        s_anim_running = false;
    }
    /* freeze a mid-swing arm where it is; forgetting the target makes the
     * on_enter resync re-issue the swing from the frozen position */
    if (s_arm) {
        lv_anim_del(s_arm, arm_swing_set);
        s_arm_target = INT16_MIN;
    }
}

static void on_destroy(void)
{
    ui_svc_music_set_cb(NULL);
    s_cb_attached = false;
    if (s_anim_running) {
        lv_anim_del(s_stripe, stripe_spin_set);
        s_anim_running = false;
    }
    if (s_arm) {
        lv_anim_del(s_arm, arm_swing_set);
    }
    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
        s_screen   = NULL;
        s_title    = NULL;
        s_song     = NULL;
        s_artist   = NULL;
        s_disc     = NULL;
        s_stripe   = NULL;
        s_arm      = NULL;
        s_progress = NULL;
        s_play_lbl = NULL;
        s_mode_lbl = NULL;
    }
}

/* ---- refresh helpers ---- */
static void refresh_text(const ui_music_status_t *st)
{
    if (s_song) {
        lv_label_set_text(s_song,
            (st->song_name[0]) ? st->song_name : ui_i18n_text(UI_TEXT_MUSIC_NOT_PLAYING));
    }
    if (s_artist) {
        lv_label_set_text(s_artist, (st->artist[0]) ? st->artist : "-");
    }
}

static void refresh_play_icon(const ui_music_status_t *st)
{
    if (s_play_lbl) {
        lv_label_set_text(s_play_lbl,
            st->state == AI_PLAYER_PLAYING ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }
}

static void refresh_mode_label(void)
{
    if (!s_mode_lbl) {
        return;
    }
    static const ui_i18n_key_t keys[UI_MUSIC_MODE_MAX] = {
        UI_TEXT_MUSIC_MODE_SEQUENCE,
        UI_TEXT_MUSIC_MODE_LIST_LOOP,
        UI_TEXT_MUSIC_MODE_SINGLE_LOOP,
        UI_TEXT_MUSIC_MODE_SHUFFLE,
    };
    ui_music_mode_t m = ui_svc_music_mode_get();
    if (m >= UI_MUSIC_MODE_MAX) {
        m = UI_MUSIC_MODE_SEQUENCE;
    }
    lv_label_set_text(s_mode_lbl, ui_i18n_text(keys[m]));
}

static void refresh_progress(void)
{
    if (!s_progress) {
        return;
    }
    /* Runs on the per-second SYSTEM tick; lv_obj_set_style_opa invalidates the
     * whole bar even for an unchanged value, so skip when nothing moved and
     * only touch opa on the unknown<->known transition. */
    int pct = ui_svc_music_progress_percent();
    if (pct < 0) {
        pct = -1;
    }
    if (pct == s_shown_pct) {
        return;
    }
    if (pct < 0) {
        ui_comp_bar_set_value(s_progress, 0);
        lv_obj_set_style_opa(s_progress, LV_OPA_40, 0);   /* unknown: dim */
    } else {
        if (s_shown_pct < 0) {
            lv_obj_set_style_opa(s_progress, LV_OPA_COVER, 0);
        }
        ui_comp_bar_set_value(s_progress, pct);
    }
    s_shown_pct = pct;
}

/* ---- disc spin (label stripe) ---- */
/* See build_turntable: the stripe endpoints orbit the label center instead of
 * rotating any widget via style transform_angle (intermediate-layer alloc +
 * per-frame software rotation are not affordable on this target). */
static void stripe_spin_set(void *obj, int32_t v)
{
    lv_obj_t *stripe = (lv_obj_t *)obj;
    int16_t deg = (int16_t)((v / 10) % 360);
    /* Quantize to 5° steps: the anim timer still fires every ~30ms, but the
     * line is only re-pointed (and its area invalidated) when the quantized
     * angle moves — once per ~111ms (~9fps) instead of ~33fps, with the spin
     * still reading as continuous. */
    deg = (int16_t)((deg / MUSIC_DISC_STEP_DEG) * MUSIC_DISC_STEP_DEG);
    if (deg == s_stripe_deg) {
        return;
    }
    s_stripe_deg = deg;
    lv_coord_t half = ui_adapt(MUSIC_LABEL_SIZE) / 2;
    lv_coord_t r    = ui_adapt(MUSIC_STRIPE_HALF);
    lv_coord_t dx = (lv_coord_t)(((int32_t)r * lv_trigo_sin(deg)) >> LV_TRIGO_SHIFT);
    lv_coord_t dy = (lv_coord_t)(-(((int32_t)r * lv_trigo_cos(deg)) >> LV_TRIGO_SHIFT));
    s_stripe_pts[0].x = half + dx;
    s_stripe_pts[0].y = half + dy;
    s_stripe_pts[1].x = half - dx;
    s_stripe_pts[1].y = half - dy;
    lv_line_set_points(stripe, s_stripe_pts, 2);
}

static void disc_spin_update(AI_PLAYER_STATE_T state)
{
    if (!s_stripe) {
        return;
    }
    if (state == AI_PLAYER_PLAYING && !s_anim_running) {
        lv_anim_init(&s_disc_anim);
        lv_anim_set_var(&s_disc_anim, s_stripe);
        lv_anim_set_exec_cb(&s_disc_anim, stripe_spin_set);
        lv_anim_set_values(&s_disc_anim, 0, 3600);     /* 0.1deg units: full turn */
        lv_anim_set_time(&s_disc_anim, MUSIC_DISC_SPIN_MS);
        lv_anim_set_repeat_count(&s_disc_anim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&s_disc_anim);
        s_anim_running = true;
    } else if (state != AI_PLAYER_PLAYING && s_anim_running) {
        lv_anim_del(s_stripe, stripe_spin_set);
        s_anim_running = false;
    }
}

/* ---- tonearm swing ---- */
/* The arm is a rigid polyline (pivot → elbow → headshell) rotated around the
 * pivot: 0° rests the headshell on the disc (playing), MUSIC_ARM_UP_DEG
 * swings it off the platter edge (paused/stopped). Same no-transform rule as
 * the stripe: only the line points are recomputed. */
static void arm_swing_set(void *obj, int32_t v)
{
    lv_obj_t *arm = (lv_obj_t *)obj;
    s_arm_pos = (int16_t)v;            /* remembered as the next swing's start */
    int16_t deg = (int16_t)(v / 10);
    if (deg == s_arm_deg) {
        return;
    }
    s_arm_deg = deg;
    /* down-pose (0°) segment vectors relative to the pivot, design units */
    static const lv_point_t seg[3] = { {0, 0}, {-18, 22}, {-34, 36} };
    int32_t sin_a = lv_trigo_sin(deg);
    int32_t cos_a = lv_trigo_cos(deg);
    lv_coord_t px = ui_adapt(MUSIC_ARM_PIVOT_X);
    lv_coord_t py = ui_adapt(MUSIC_ARM_PIVOT_Y);
    for (int i = 0; i < 3; i++) {
        int32_t vx = ui_adapt(seg[i].x);
        int32_t vy = ui_adapt(seg[i].y);
        s_arm_pts[i].x = px + (lv_coord_t)((vx * cos_a + vy * sin_a) >> LV_TRIGO_SHIFT);
        s_arm_pts[i].y = py + (lv_coord_t)((-vx * sin_a + vy * cos_a) >> LV_TRIGO_SHIFT);
    }
    lv_line_set_points(arm, s_arm_pts, 3);
}

static void arm_update(AI_PLAYER_STATE_T state, bool animate)
{
    if (!s_arm) {
        return;
    }
    int16_t target = (state == AI_PLAYER_PLAYING) ? 0 : (int16_t)(MUSIC_ARM_UP_DEG * 10);
    if (target == s_arm_target) {
        return;
    }
    s_arm_target = target;
    lv_anim_del(s_arm, arm_swing_set);
    if (!animate) {
        arm_swing_set(s_arm, target);
        return;
    }
    lv_anim_init(&s_arm_anim);
    lv_anim_set_var(&s_arm_anim, s_arm);
    lv_anim_set_exec_cb(&s_arm_anim, arm_swing_set);
    lv_anim_set_values(&s_arm_anim, s_arm_pos, target);
    lv_anim_set_time(&s_arm_anim, MUSIC_ARM_SWING_MS);
    lv_anim_set_path_cb(&s_arm_anim, lv_anim_path_ease_in_out);
    lv_anim_start(&s_arm_anim);
}

/* ---- service callback (UI thread) ---- */
static void on_music_status(const ui_music_status_t *st)
{
    refresh_text(st);
    refresh_play_icon(st);
    refresh_mode_label();
    disc_spin_update(st->state);
    arm_update(st->state, true);
}

/* ---- input callbacks ---- */
static void back_cb(lv_event_t *e) { (void)e; ui_route_pop(); }
static void prev_cb(lv_event_t *e) { (void)e; ui_svc_music_prev(); }

/* Swipe right anywhere on the page = go back (same as the back button). */
static void gesture_cb(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());   /* swallow rest of touch so the release doesn't click the page below */
        ui_route_pop();
    }
}
static void next_cb(lv_event_t *e) { (void)e; ui_svc_music_next(); }
static void play_cb(lv_event_t *e) { (void)e; ui_svc_music_play_pause(); }
static void list_cb(lv_event_t *e) { (void)e; ui_route_push(UI_PAGE_MUSIC_LIST); }

static void mode_cb(lv_event_t *e)
{
    (void)e;
    ui_music_mode_t m = ui_svc_music_mode_get();
    m = (ui_music_mode_t)((m + 1) % UI_MUSIC_MODE_MAX);
    ui_svc_music_mode_set(m);
    refresh_mode_label();
}

const ui_page_entry_t ui_page_music_entry = {
    .id = UI_PAGE_MUSIC,
    .name = "music",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
