/**
 * @file ui_music.c
 * @brief Music player main screen for T5AI_BOARD (320x480)
 *
 * Reachable from the control-center "音乐" card. Renders a vertical 2-stop red
 * gradient as the screen body, a 50px transparent title bar with a centered
 * "音乐" and a left back button, then below that:
 *  - song name / artist labels
 *  - a concentric-circle "vinyl record" drawn entirely with lv_obj geometry
 *    primitives (no PNG asset; saves ~27.5 KB of flash vs music_disc.png)
 *  - a static tonearm rendered with lv_line + a circular pivot
 *  - a 4-button control row (prev / play-pause / next / list) using LVGL's
 *    built-in symbols at lv_font_montserrat_24 — no per-button PNGs.
 *
 * Playback control delegates to the wukong_playback_ctrl / wukong_audio_player
 * APIs. Live song / state updates are pushed via EVENT_MUSIC_PLAYER (data +
 * state messages) and EVENT_MUSIC_BREAK; the callback runs on the event
 * thread but the view module already drives UI directly from event callbacks
 * (precedent: ui_call.c) so we follow the same convention.
 *
 * Lifecycle follows view's lazy-create + per-visit subscribe pattern. The
 * persistent screen tree lives for the rest of the app session; show/hide
 * only manage state cache + event subscription.
 *
 * @version 1.1
 * @date 2026-05-21
 * @copyright Copyright (c) Tuya Inc.
 */
#include <string.h>
#include <stdint.h>
#include "ui_common.h"
#include "tuya_ai_display.h"
#include "base_event.h"
#include "wukong_playback_ctrl.h"
#include "wukong_audio_player.h"
#include "wukong_ai_skills.h"
#include "svc_ai_player.h"

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define MUSIC_BG_TOP            0x1A0507   /* near-black with hint of red */
#define MUSIC_BG_BOTTOM         0x5A0B0E   /* deep maroon                  */

#define MUSIC_EVT_SUBSCRIBER    "ui_music_main"

/* Song / artist label area inside content_bar */
#define MUSIC_TEXT_PAD_X        16
#define MUSIC_SONG_W            (LV_HOR_RES - 2 * MUSIC_TEXT_PAD_X)
#define MUSIC_SONG_Y            8
#define MUSIC_SONG_H            24
#define MUSIC_ARTIST_Y          44
#define MUSIC_ARTIST_H          22
#define MUSIC_ARTIST_COLOR      0xB8BDDE

/* Disc geometry (content_bar coordinates) */
#define MUSIC_DISC_SIZE         200
#define MUSIC_DISC_X            ((LV_HOR_RES - MUSIC_DISC_SIZE) / 2)   /* 60 */
#define MUSIC_DISC_Y            80
#define MUSIC_DISC_OUTER_BG     0x1F2933
#define MUSIC_DISC_OUTER_BORDER 0x2C3540
#define MUSIC_DISC_INNER_SIZE   80
#define MUSIC_DISC_INNER_BG     0xE84A3F
#define MUSIC_DISC_CENTER_SIZE  16

/* Tonearm geometry (content_bar coordinates) */
#define MUSIC_ARM_PIVOT_X       267
#define MUSIC_ARM_PIVOT_Y       22
#define MUSIC_ARM_PIVOT_SIZE    16
#define MUSIC_ARM_COLOR         0xB8BDDE
#define MUSIC_ARM_WIDTH         4

/* Control row */
#define MUSIC_CTRL_ROW_Y        280
#define MUSIC_CTRL_ROW_H        80
#define MUSIC_CTRL_BTN_SIZE     64

/* Cache sizes (mirror WUKONG_MUSIC_PLAYER_T fields) */
#define MUSIC_NAME_LEN          128

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    /* Screen chrome */
    lv_obj_t *scr;
    lv_obj_t *title_bar;
    lv_obj_t *title_lbl;
    lv_obj_t *back_btn;
    lv_obj_t *content_bar;

    /* Song / artist labels */
    lv_obj_t *song_lbl;
    lv_obj_t *artist_lbl;

    /* Vinyl disc primitives */
    lv_obj_t *disc_outer;
    lv_obj_t *disc_inner;
    lv_obj_t *disc_center;

    /* Tonearm */
    lv_obj_t *tonearm_line;
    lv_obj_t *tonearm_pivot;

    /* Control buttons */
    lv_obj_t *btn_prev;
    lv_obj_t *btn_play;
    lv_obj_t *btn_play_lbl;
    lv_obj_t *btn_next;
    lv_obj_t *btn_list;

    /* State */
    BOOL_T              evt_subscribed;
    AI_PLAYER_STATE_T   player_state;
    CHAR_T              song_name[MUSIC_NAME_LEN];
    CHAR_T              artist[MUSIC_NAME_LEN];
} MUSIC_UI_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC MUSIC_UI_T s_music = {0};

STATIC CONST lv_point_t s_arm_pts[2] = {
    { MUSIC_ARM_PIVOT_X + MUSIC_ARM_PIVOT_SIZE / 2,
      MUSIC_ARM_PIVOT_Y + MUSIC_ARM_PIVOT_SIZE / 2 },
    { 180, 130 },
};

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __music_back_cb(lv_event_t *e);
STATIC VOID_T __music_prev_cb(lv_event_t *e);
STATIC VOID_T __music_next_cb(lv_event_t *e);
STATIC VOID_T __music_play_cb(lv_event_t *e);
STATIC VOID_T __music_list_cb(lv_event_t *e);
STATIC VOID_T __music_apply_gradient(lv_obj_t *scr);
STATIC VOID_T __music_build_title_bar(VOID_T);
STATIC VOID_T __music_build_content(VOID_T);
STATIC VOID_T __music_build_disc(VOID_T);
STATIC VOID_T __music_build_tonearm(VOID_T);
STATIC VOID_T __music_build_controls(VOID_T);
STATIC lv_obj_t *__music_make_symbol_btn(lv_obj_t *parent, CONST CHAR_T *symbol,
                                         lv_event_cb_t cb, lv_obj_t **lbl_out);
STATIC VOID_T __music_refresh_text(VOID_T);
STATIC VOID_T __music_refresh_play_icon(VOID_T);
STATIC VOID_T __music_refresh_all(VOID_T);
STATIC VOID_T __music_load_status(VOID_T);
STATIC INT_T  __music_event_cb(VOID_T *data);
STATIC INT_T  __music_break_cb(VOID_T *data);

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Apply the vertical 2-stop linear gradient to the screen body
 * @param[in] scr screen object to decorate
 * @return none
 */
STATIC VOID_T __music_apply_gradient(lv_obj_t *scr)
{
    if (scr == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(scr, lv_color_hex(MUSIC_BG_TOP), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(MUSIC_BG_BOTTOM), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

#if LV_DITHER_GRADIENT
#if LV_DITHER_ERROR_DIFFUSION
    lv_obj_set_style_bg_dither_mode(scr, LV_DITHER_ERR_DIFF, LV_PART_MAIN);
#else
    lv_obj_set_style_bg_dither_mode(scr, LV_DITHER_ORDERED, LV_PART_MAIN);
#endif
#endif
}

/**
 * @brief Back button callback: route through dispatch so ui_nav drives ui_music_hide
 * @param[in] e LVGL event (unused)
 * @return none
 */
STATIC VOID_T __music_back_cb(lv_event_t *e)
{
    (VOID_T)e;
    PR_DEBUG("music: back");
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_CLOSE_MUSIC);
}

/**
 * @brief Prev button: jump to the previous track in the local playlist
 * @param[in] e LVGL event (unused)
 * @return none
 */
STATIC VOID_T __music_prev_cb(lv_event_t *e)
{
    (VOID_T)e;
    OPERATE_RET rt = wukong_playback_playlist_prev();
    if (rt != OPRT_OK) {
        PR_WARN("music: prev failed rt=%d", rt);
    }
}

/**
 * @brief Next button: jump to the next track (uses non-blocking helper)
 * @param[in] e LVGL event (unused)
 * @return none
 */
STATIC VOID_T __music_next_cb(lv_event_t *e)
{
    (VOID_T)e;
    OPERATE_RET rt = wukong_playback_ctrl_next();
    if (rt != OPRT_OK) {
        PR_WARN("music: next failed rt=%d", rt);
    }
}

/**
 * @brief Play / pause toggle: dispatches to pause or resume by current state
 * @param[in] e LVGL event (unused)
 * @return none
 */
STATIC VOID_T __music_play_cb(lv_event_t *e)
{
    (VOID_T)e;
    OPERATE_RET rt = OPRT_OK;
    if (s_music.player_state == AI_PLAYER_PLAYING) {
        rt = wukong_audio_player_pause();
        PR_DEBUG("music: pause rt=%d", rt);
    } else {
        rt = wukong_audio_player_resume();
        PR_DEBUG("music: resume rt=%d", rt);
    }
}

/**
 * @brief List button: open the music list screen
 * @param[in] e LVGL event (unused)
 * @return none
 */
STATIC VOID_T __music_list_cb(lv_event_t *e)
{
    (VOID_T)e;
    PR_DEBUG("music: open list");
    ui_nav_to(UI_SCR_MUSIC_LIST);
}

/**
 * @brief Build the standard view title bar (back button + centered "音乐")
 * @return none
 */
STATIC VOID_T __music_build_title_bar(VOID_T)
{
    s_music.title_bar = lv_obj_create(s_music.scr);
    lv_obj_remove_style_all(s_music.title_bar);
    lv_obj_set_pos(s_music.title_bar, 0, 0);
    lv_obj_set_size(s_music.title_bar, LV_HOR_RES, UI_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(s_music.title_bar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_music.title_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_music.back_btn = lv_btn_create(s_music.title_bar);
    lv_obj_remove_style_all(s_music.back_btn);
    lv_obj_set_size(s_music.back_btn, UI_TITLE_BTN_W, UI_TITLE_BAR_H);
    lv_obj_set_pos(s_music.back_btn, 0, 0);
    lv_obj_set_style_bg_opa(s_music.back_btn, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_music.back_btn, __music_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_icon = lv_img_create(s_music.back_btn);
    lv_img_set_src(back_icon, &icon_back_24_24);
    lv_obj_set_size(back_icon, UI_TITLE_ICON_SIZE, UI_TITLE_ICON_SIZE);
    lv_obj_center(back_icon);

    s_music.title_lbl = lv_label_create(s_music.title_bar);
    lv_label_set_text(s_music.title_lbl, "音乐");
    lv_obj_set_style_text_font(s_music.title_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_music.title_lbl, lv_color_white(), 0);
    lv_obj_center(s_music.title_lbl);
}

/**
 * @brief Build the concentric-circle vinyl disc (replaces music_disc.png)
 * @return none
 * @note Fully symmetric, no rotation animation: identical visuals at any
 *       angle would only burn CPU/flash for no UX gain.
 */
STATIC VOID_T __music_build_disc(VOID_T)
{
    s_music.disc_outer = lv_obj_create(s_music.content_bar);
    lv_obj_remove_style_all(s_music.disc_outer);
    lv_obj_set_size(s_music.disc_outer, MUSIC_DISC_SIZE, MUSIC_DISC_SIZE);
    lv_obj_set_pos(s_music.disc_outer, MUSIC_DISC_X, MUSIC_DISC_Y);
    lv_obj_set_style_radius(s_music.disc_outer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_music.disc_outer,
                              lv_color_hex(MUSIC_DISC_OUTER_BG), 0);
    lv_obj_set_style_bg_opa(s_music.disc_outer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_music.disc_outer,
                                  lv_color_hex(MUSIC_DISC_OUTER_BORDER), 0);
    lv_obj_set_style_border_width(s_music.disc_outer, 2, 0);
    lv_obj_clear_flag(s_music.disc_outer, LV_OBJ_FLAG_SCROLLABLE);

    s_music.disc_inner = lv_obj_create(s_music.disc_outer);
    lv_obj_remove_style_all(s_music.disc_inner);
    lv_obj_set_size(s_music.disc_inner,
                    MUSIC_DISC_INNER_SIZE, MUSIC_DISC_INNER_SIZE);
    lv_obj_center(s_music.disc_inner);
    lv_obj_set_style_radius(s_music.disc_inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_music.disc_inner,
                              lv_color_hex(MUSIC_DISC_INNER_BG), 0);
    lv_obj_set_style_bg_opa(s_music.disc_inner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_music.disc_inner, 0, 0);
    lv_obj_clear_flag(s_music.disc_inner, LV_OBJ_FLAG_SCROLLABLE);

    s_music.disc_center = lv_obj_create(s_music.disc_inner);
    lv_obj_remove_style_all(s_music.disc_center);
    lv_obj_set_size(s_music.disc_center,
                    MUSIC_DISC_CENTER_SIZE, MUSIC_DISC_CENTER_SIZE);
    lv_obj_center(s_music.disc_center);
    lv_obj_set_style_radius(s_music.disc_center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_music.disc_center, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_music.disc_center, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_music.disc_center, 0, 0);
    lv_obj_clear_flag(s_music.disc_center, LV_OBJ_FLAG_SCROLLABLE);
}

/**
 * @brief Draw the tonearm: a static lv_line angled onto the disc + pivot dot
 * @return none
 */
STATIC VOID_T __music_build_tonearm(VOID_T)
{
    s_music.tonearm_line = lv_line_create(s_music.content_bar);
    lv_line_set_points(s_music.tonearm_line, (lv_point_t *)s_arm_pts, 2);
    lv_obj_set_style_line_color(s_music.tonearm_line,
                                lv_color_hex(MUSIC_ARM_COLOR), 0);
    lv_obj_set_style_line_width(s_music.tonearm_line, MUSIC_ARM_WIDTH, 0);
    lv_obj_set_style_line_rounded(s_music.tonearm_line, true, 0);

    s_music.tonearm_pivot = lv_obj_create(s_music.content_bar);
    lv_obj_remove_style_all(s_music.tonearm_pivot);
    lv_obj_set_size(s_music.tonearm_pivot,
                    MUSIC_ARM_PIVOT_SIZE, MUSIC_ARM_PIVOT_SIZE);
    lv_obj_set_pos(s_music.tonearm_pivot, MUSIC_ARM_PIVOT_X, MUSIC_ARM_PIVOT_Y);
    lv_obj_set_style_radius(s_music.tonearm_pivot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_music.tonearm_pivot,
                              lv_color_hex(MUSIC_ARM_COLOR), 0);
    lv_obj_set_style_bg_opa(s_music.tonearm_pivot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_music.tonearm_pivot, 0, 0);
    lv_obj_clear_flag(s_music.tonearm_pivot, LV_OBJ_FLAG_SCROLLABLE);
}

/**
 * @brief Factory for one transparent control button with a centered LVGL symbol
 * @param[in]  parent flex-row container
 * @param[in]  symbol LV_SYMBOL_* string
 * @param[in]  cb     click handler
 * @param[out] lbl_out optional pointer storage for the inner label (caller may
 *                    want to re-set the text later, e.g. play/pause toggle)
 * @return the created button
 */
STATIC lv_obj_t *__music_make_symbol_btn(lv_obj_t *parent, CONST CHAR_T *symbol,
                                         lv_event_cb_t cb, lv_obj_t **lbl_out)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, MUSIC_CTRL_BTN_SIZE, MUSIC_CTRL_BTN_SIZE);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_remove_style_all(lbl);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);

    if (lbl_out != NULL) {
        *lbl_out = lbl;
    }
    return btn;
}

/**
 * @brief Build the 4 control buttons (prev / play-pause / next / list) flex row
 * @return none
 */
STATIC VOID_T __music_build_controls(VOID_T)
{
    lv_obj_t *row = lv_obj_create(s_music.content_bar);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_HOR_RES, MUSIC_CTRL_ROW_H);
    lv_obj_set_pos(row, 0, MUSIC_CTRL_ROW_Y);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    s_music.btn_prev = __music_make_symbol_btn(row, LV_SYMBOL_PREV,
                                               __music_prev_cb, NULL);
    s_music.btn_play = __music_make_symbol_btn(row, LV_SYMBOL_PLAY,
                                               __music_play_cb,
                                               &s_music.btn_play_lbl);
    s_music.btn_next = __music_make_symbol_btn(row, LV_SYMBOL_NEXT,
                                               __music_next_cb, NULL);
    s_music.btn_list = __music_make_symbol_btn(row, LV_SYMBOL_LIST,
                                               __music_list_cb, NULL);
}

/**
 * @brief Build the transparent content area below the title bar
 * @return none
 */
STATIC VOID_T __music_build_content(VOID_T)
{
    s_music.content_bar = lv_obj_create(s_music.scr);
    lv_obj_remove_style_all(s_music.content_bar);
    lv_obj_set_pos(s_music.content_bar, 0, UI_TITLE_BAR_H);
    lv_obj_set_size(s_music.content_bar, LV_HOR_RES, LV_VER_RES - UI_TITLE_BAR_H);
    lv_obj_set_style_bg_opa(s_music.content_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(s_music.content_bar, 0, 0);
    lv_obj_set_scrollbar_mode(s_music.content_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_music.content_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Song name */
    s_music.song_lbl = lv_label_create(s_music.content_bar);
    lv_obj_remove_style_all(s_music.song_lbl);
    lv_label_set_long_mode(s_music.song_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(s_music.song_lbl, "未在播放");
    lv_obj_set_size(s_music.song_lbl, MUSIC_SONG_W, MUSIC_SONG_H);
    lv_obj_set_pos(s_music.song_lbl, MUSIC_TEXT_PAD_X, MUSIC_SONG_Y);
    lv_obj_set_style_text_font(s_music.song_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_music.song_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_music.song_lbl, LV_TEXT_ALIGN_LEFT, 0);

    /* Artist */
    s_music.artist_lbl = lv_label_create(s_music.content_bar);
    lv_obj_remove_style_all(s_music.artist_lbl);
    lv_label_set_long_mode(s_music.artist_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(s_music.artist_lbl, "-");
    lv_obj_set_size(s_music.artist_lbl, MUSIC_SONG_W, MUSIC_ARTIST_H);
    lv_obj_set_pos(s_music.artist_lbl, MUSIC_TEXT_PAD_X, MUSIC_ARTIST_Y);
    lv_obj_set_style_text_font(s_music.artist_lbl,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_music.artist_lbl,
                                lv_color_hex(MUSIC_ARTIST_COLOR), 0);
    lv_obj_set_style_text_align(s_music.artist_lbl, LV_TEXT_ALIGN_LEFT, 0);

    __music_build_disc();
    __music_build_tonearm();
    __music_build_controls();
}

/**
 * @brief Refresh song / artist labels from the file-scope cache
 * @return none
 */
STATIC VOID_T __music_refresh_text(VOID_T)
{
    if (s_music.song_lbl == NULL || s_music.artist_lbl == NULL) {
        return;
    }
    lv_label_set_text(s_music.song_lbl,
        s_music.song_name[0] != '\0' ? s_music.song_name : "未在播放");
    lv_label_set_text(s_music.artist_lbl,
        s_music.artist[0] != '\0' ? s_music.artist : "-");
}

/**
 * @brief Refresh the play / pause button glyph from cached state
 * @return none
 */
STATIC VOID_T __music_refresh_play_icon(VOID_T)
{
    if (s_music.btn_play_lbl == NULL) {
        return;
    }
    lv_label_set_text(s_music.btn_play_lbl,
        s_music.player_state == AI_PLAYER_PLAYING ? LV_SYMBOL_PAUSE
                                                  : LV_SYMBOL_PLAY);
}

/**
 * @brief Refresh every dynamic UI element from the file-scope cache
 * @return none
 */
STATIC VOID_T __music_refresh_all(VOID_T)
{
    __music_refresh_text();
    __music_refresh_play_icon();
}

/**
 * @brief Pull current player status from the SDK into the file-scope cache
 * @return none
 */
STATIC VOID_T __music_load_status(VOID_T)
{
    WUKONG_MUSIC_PLAYER_T status = {0};
    if (wukong_playback_ctrl_get_status(&status) != OPRT_OK) {
        return;
    }
    s_music.player_state = status.state;
    strncpy(s_music.song_name, status.song_name, sizeof(s_music.song_name) - 1);
    s_music.song_name[sizeof(s_music.song_name) - 1] = '\0';
    strncpy(s_music.artist, status.artist, sizeof(s_music.artist) - 1);
    s_music.artist[sizeof(s_music.artist) - 1] = '\0';
}

/**
 * @brief EVENT_MUSIC_PLAYER handler: copy data into cache, refresh UI
 * @param[in] data WUKONG_MUSIC_PLAYER_T *
 * @return OPRT_OK
 * @note Direct UI calls match view's existing event-callback convention
 *       (see ui_call.c::__call_status_event_cb). The cache lets this code
 *       stay safe even if `data` is reused after the callback returns.
 */
STATIC INT_T __music_event_cb(VOID_T *data)
{
    if (data == NULL) {
        return OPRT_OK;
    }
    WUKONG_MUSIC_PLAYER_T *msg = (WUKONG_MUSIC_PLAYER_T *)data;

    switch (msg->cmd) {
        case MUSIC_PLAYER_STATE:
            s_music.player_state = msg->state;
            break;
        case MUSIC_PLAYER_DATA:
            strncpy(s_music.song_name, msg->song_name,
                    sizeof(s_music.song_name) - 1);
            s_music.song_name[sizeof(s_music.song_name) - 1] = '\0';
            strncpy(s_music.artist, msg->artist, sizeof(s_music.artist) - 1);
            s_music.artist[sizeof(s_music.artist) - 1] = '\0';
            break;
        default:
            break;
    }

    if (s_music.scr != NULL && lv_scr_act() == s_music.scr) {
        __music_refresh_all();
    }
    return OPRT_OK;
}

/**
 * @brief EVENT_MUSIC_BREAK handler: forced pause from external interrupt (TTS, etc.)
 * @param[in] data unused
 * @return OPRT_OK
 */
STATIC INT_T __music_break_cb(VOID_T *data)
{
    (VOID_T)data;
    s_music.player_state = AI_PLAYER_PAUSED;
    if (s_music.scr != NULL && lv_scr_act() == s_music.scr) {
        __music_refresh_play_icon();
    }
    return OPRT_OK;
}

/**
 * @brief Create the music screen lazily (idempotent)
 * @return none
 */
VOID_T setup_scr_music(VOID_T)
{
    if (s_music.scr) {
        return;
    }

    memset(&s_music, 0, sizeof(s_music));

    s_music.scr = lv_obj_create(NULL);
    lv_obj_set_size(s_music.scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_pad_all(s_music.scr, 0, 0);
    lv_obj_set_scrollbar_mode(s_music.scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_music.scr, LV_OBJ_FLAG_SCROLLABLE);

    __music_apply_gradient(s_music.scr);
    __music_build_title_bar();
    __music_build_content();

    ui_control_center_register_gesture(s_music.scr);

    lv_obj_update_layout(s_music.scr);
}

/**
 * @brief Show the music screen (creates lazily, syncs status, subscribes)
 * @return none
 * @note We don't call wukong_audio_player_pause on hide: the user may want to
 *       keep listening after navigating away.
 */
VOID_T ui_music_show(VOID_T)
{
    if (s_music.scr == NULL) {
        setup_scr_music();
    }

    __music_load_status();
    __music_refresh_all();

    if (!s_music.evt_subscribed) {
        OPERATE_RET rt = ty_subscribe_event(EVENT_MUSIC_PLAYER,
                                            MUSIC_EVT_SUBSCRIBER,
                                            __music_event_cb,
                                            SUBSCRIBE_TYPE_NORMAL);
        if (rt == OPRT_OK) {
            (VOID_T)ty_subscribe_event(EVENT_MUSIC_BREAK,
                                       MUSIC_EVT_SUBSCRIBER,
                                       __music_break_cb,
                                       SUBSCRIBE_TYPE_NORMAL);
            s_music.evt_subscribed = TRUE;
        } else {
            PR_ERR("music: subscribe EVENT_MUSIC_PLAYER failed rt=%d", rt);
        }
    }

    if (lv_scr_act() != s_music.scr) {
        lv_scr_load(s_music.scr);
    }
}

/**
 * @brief Hide the music screen, drop per-visit subscriptions
 * @return none
 */
VOID_T ui_music_hide(VOID_T)
{
    if (s_music.evt_subscribed) {
        ty_unsubscribe_event(EVENT_MUSIC_PLAYER, MUSIC_EVT_SUBSCRIBER,
                             __music_event_cb);
        ty_unsubscribe_event(EVENT_MUSIC_BREAK, MUSIC_EVT_SUBSCRIBER,
                             __music_break_cb);
        s_music.evt_subscribed = FALSE;
    }
}

/**
 * @brief Get the music screen object
 * @return music screen pointer, NULL if not created
 */
lv_obj_t *ui_music_get_scr(VOID_T)
{
    return s_music.scr;
}
