/**
 * @file ui_page_camera.c
 * @brief Camera page — square live viewfinder, capture controls and album entry.
 *
 * The camera layer hands this page a native 320x320 RGB565 preview, so LVGL can
 * draw the canvas directly without an expensive per-refresh image transform.
 * Camera and album work remain behind ui_svc_camera; this page only mutates
 * LVGL objects from callbacks already marshalled to the UI thread.
 */

#include "lvgl.h"
#include "ui_route.h"
#include "ui_state.h"
#include "ui_adaptive.h"
#include "ui_theme.h"
#include "ui_i18n.h"
#include "ui_comp_btn.h"
#include "ui_comp_picture.h"
#include "ui_comp_popup.h"
#include "ui_comp_statusbar.h"
#include "ui_svc_camera.h"
#include "ui_svc_video.h"
#include "ui_feature.h"
#include "ui_page_ids.h"
#include "uni_log.h"

LV_IMG_DECLARE(icon_back_24_24);

/* Base layout: 48px header + 320px square viewfinder + 112px controls. */
#define CAM_HEADER_H          48
#define CAM_PREVIEW_SIZE     320
#define CAM_BOTTOM_H         112
#define CAM_BACK_SIZE         40
#define CAM_SHUTTER_RING      68
#define CAM_SHUTTER_IDLE      54
#define CAM_SHUTTER_PRESSED   48
#define CAM_VIDEO_IDLE        48
#define CAM_VIDEO_PRESSED     42
#define CAM_VIDEO_STOP        26
#define CAM_MODE_SWITCH_W    136
#define CAM_MODE_SWITCH_H     30
#define CAM_MODE_SWITCH_TOP    3
#define CAM_SHUTTER_BOTTOM     5
#define CAM_THUMB_BOX         48
#define CAM_THUMB_MARGIN      20
#define CAM_THUMB_BOTTOM      15
#define CAM_THUMB_RADIUS       8
#define CAM_TIMER_W           82
#define CAM_TIMER_DOT          8
#define CAM_TIMER_INVALID_SEC 0xFFFFFFFFu
#define CAM_RECORD_RED    0xFF3B30

typedef enum {
    CAM_CAPTURE_PHOTO = 0,
    CAM_CAPTURE_VIDEO,
} cam_capture_mode_t;

static lv_obj_t *s_screen;
static lv_obj_t *s_title;
static lv_obj_t *s_preview_box;
static lv_obj_t *s_canvas;
static lv_obj_t *s_loading;
static lv_obj_t *s_loading_lbl;
static lv_obj_t *s_flash;
static lv_obj_t *s_shutter_ring;
static lv_obj_t *s_shutter;
static lv_obj_t *s_thumb_box;
static lv_obj_t *s_thumb;
static lv_obj_t *s_photo_mode_btn;
static lv_obj_t *s_photo_mode_lbl;
static lv_obj_t *s_video_mode_btn;
static lv_obj_t *s_video_mode_lbl;
static lv_obj_t *s_record_timer;
static lv_obj_t *s_record_time_lbl;
static bool      s_preview_on;
static bool      s_capturing;
static bool      s_record_timer_visible;
static uint32_t  s_record_last_sec;
static cam_capture_mode_t s_capture_mode;
static ui_svc_video_state_t s_video_state;

/* --------------------------------------------------------------------------
 * Small visual helpers
 * -------------------------------------------------------------------------- */

static void flash_opa_set(void *obj, int32_t value)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

static void thumb_opa_set(void *obj, int32_t value)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

static void play_capture_flash(void)
{
    if (!s_flash) return;
    lv_anim_del(s_flash, flash_opa_set);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_flash);
    lv_anim_set_exec_cb(&anim, flash_opa_set);
    lv_anim_set_values(&anim, LV_OPA_80, LV_OPA_TRANSP);
    lv_anim_set_time(&anim, 180);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

static void play_thumbnail_reveal(void)
{
    if (!s_thumb) return;
    lv_anim_del(s_thumb, thumb_opa_set);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_thumb);
    lv_anim_set_exec_cb(&anim, thumb_opa_set);
    lv_anim_set_values(&anim, LV_OPA_50, LV_OPA_COVER);
    lv_anim_set_time(&anim, 180);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

static bool video_state_allows_mode_switch(void)
{
    return s_video_state == UI_VIDEO_STATE_IDLE ||
           s_video_state == UI_VIDEO_STATE_ERROR;
}

static void apply_capture_visual(void)
{
    bool recording = s_video_state == UI_VIDEO_STATE_RECORDING;
    bool switching = s_video_state == UI_VIDEO_STATE_STARTING ||
                     s_video_state == UI_VIDEO_STATE_STOPPING;
    bool mode_enabled = !s_capturing && video_state_allows_mode_switch();

    if (s_photo_mode_btn && s_video_mode_btn) {
        bool photo = s_capture_mode == CAM_CAPTURE_PHOTO;

        lv_obj_set_style_bg_color(s_photo_mode_btn, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(s_photo_mode_btn, photo ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(s_video_mode_btn, lv_color_hex(CAM_RECORD_RED), 0);
        lv_obj_set_style_bg_opa(s_video_mode_btn, photo ? LV_OPA_TRANSP : LV_OPA_30, 0);
        lv_obj_set_style_text_color(s_photo_mode_lbl,
                                    photo ? UI_COLOR_TEXT : UI_COLOR_TEXT_SEC, 0);
        lv_obj_set_style_text_color(s_video_mode_lbl,
                                    photo ? UI_COLOR_TEXT_SEC : lv_color_hex(CAM_RECORD_RED), 0);
        ui_comp_btn_set_enabled(s_photo_mode_btn, mode_enabled);
        ui_comp_btn_set_enabled(s_video_mode_btn, mode_enabled);
    }

    if (!s_shutter || !s_shutter_ring) return;

    lv_obj_set_style_border_color(s_shutter_ring,
                                  recording ? lv_color_hex(CAM_RECORD_RED) : lv_color_white(), 0);
    lv_obj_set_style_opa(s_shutter_ring,
                         (s_capturing || switching) ? LV_OPA_60 : LV_OPA_COVER, 0);

    if (s_capture_mode == CAM_CAPTURE_VIDEO) {
        lv_obj_set_style_bg_color(s_shutter, lv_color_hex(CAM_RECORD_RED), 0);
        lv_obj_set_size(s_shutter,
                        ui_adapt(recording ? CAM_VIDEO_STOP : CAM_VIDEO_IDLE),
                        ui_adapt(recording ? CAM_VIDEO_STOP : CAM_VIDEO_IDLE));
        lv_obj_set_style_radius(s_shutter,
                                recording ? ui_adapt(5) : LV_RADIUS_CIRCLE, 0);
    } else {
        lv_obj_set_style_bg_color(s_shutter, lv_color_white(), 0);
        lv_obj_set_size(s_shutter, ui_adapt(CAM_SHUTTER_IDLE),
                        ui_adapt(CAM_SHUTTER_IDLE));
        lv_obj_set_style_radius(s_shutter, LV_RADIUS_CIRCLE, 0);
    }
    lv_obj_center(s_shutter);

    if (s_capturing || switching ||
        (s_capture_mode == CAM_CAPTURE_PHOTO && recording)) {
        lv_obj_add_state(s_shutter, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(s_shutter, LV_STATE_DISABLED);
    }
}

static void update_record_timer(void)
{
    bool visible = s_video_state == UI_VIDEO_STATE_RECORDING ||
                   s_video_state == UI_VIDEO_STATE_STOPPING;
    uint32_t elapsed_sec;

    if (!s_record_timer || !s_record_time_lbl) return;

    if (!visible) {
        if (s_record_timer_visible) {
            lv_obj_add_flag(s_record_timer, LV_OBJ_FLAG_HIDDEN);
            s_record_timer_visible = false;
        }
        s_record_last_sec = CAM_TIMER_INVALID_SEC;
        return;
    }

    if (!s_record_timer_visible) {
        lv_obj_clear_flag(s_record_timer, LV_OBJ_FLAG_HIDDEN);
        s_record_timer_visible = true;
    }

    elapsed_sec = ui_svc_video_get_elapsed_ms() / 1000u;
    if (elapsed_sec == s_record_last_sec) return;

    s_record_last_sec = elapsed_sec;
    lv_label_set_text_fmt(s_record_time_lbl, "%02u:%02u",
                          (unsigned int)(elapsed_sec / 60u),
                          (unsigned int)(elapsed_sec % 60u));
}

static void set_capture_busy(bool busy)
{
    s_capturing = busy;
    apply_capture_visual();
}

/* --------------------------------------------------------------------------
 * Service callbacks — all run on the UI thread
 * -------------------------------------------------------------------------- */

static void on_preview_frame(uint16_t w, uint16_t h, uint8_t *rgb565, uint32_t len)
{
    (void)len;
    if (!s_canvas || !rgb565 || w == 0 || h == 0) {
        if (rgb565) ui_svc_camera_rgb_free(rgb565);
        return;
    }

    ui_comp_picture_set_rgb565(s_canvas, w, h, rgb565, ui_svc_camera_rgb_free);

    /* Remove the startup state as soon as the first usable frame is visible. */
    if (s_loading) {
        lv_obj_add_flag(s_loading, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_loading);
        s_loading = NULL;
        s_loading_lbl = NULL;
    }
}

static void on_thumb_ready(uint16_t w, uint16_t h, uint8_t *rgb565, uint32_t len)
{
    (void)len;
    if (!s_thumb || !s_thumb_box) {
        if (rgb565) ui_svc_camera_rgb_free(rgb565);
        return;
    }

    if (rgb565 && w > 0 && h > 0) {
        ui_comp_picture_set_rgb565(s_thumb, w, h, rgb565, ui_svc_camera_rgb_free);
        lv_obj_clear_flag(s_thumb_box, LV_OBJ_FLAG_HIDDEN);
        play_thumbnail_reveal();
    } else {
        ui_comp_picture_clear(s_thumb);
        lv_obj_add_flag(s_thumb_box, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_capture_done(bool success)
{
    set_capture_busy(false);
    ui_comp_popup_toast(ui_i18n_text(success ? UI_TEXT_CAMERA_SAVED
                                             : UI_TEXT_CAMERA_FAILED),
                        success ? 900 : 1800);
}

static void apply_video_visual(ui_svc_video_state_t state)
{
    s_video_state = state;
    apply_capture_visual();
    update_record_timer();
}

static void on_video_state(ui_svc_video_state_t state, ui_svc_video_error_t error)
{
    ui_svc_video_state_t previous = s_video_state;

    apply_video_visual(state);
    if (state == UI_VIDEO_STATE_STARTING) {
        ui_comp_popup_toast(ui_i18n_text(UI_TEXT_VIDEO_STARTING), 900);
    } else if (state == UI_VIDEO_STATE_IDLE && previous == UI_VIDEO_STATE_STOPPING) {
        ui_comp_popup_toast(ui_i18n_text(UI_TEXT_VIDEO_SAVED), 1200);
    } else if (state == UI_VIDEO_STATE_ERROR) {
        ui_comp_popup_toast(ui_i18n_text(error == UI_VIDEO_ERROR_STORAGE ?
                                         UI_TEXT_VIDEO_STORAGE_UNAVAILABLE :
                                         UI_TEXT_VIDEO_FAILED), 1800);
    } else if (error != UI_VIDEO_ERROR_NONE) {
        ui_comp_popup_toast(ui_i18n_text(UI_TEXT_VIDEO_FAILED), 1800);
    }
}

/* --------------------------------------------------------------------------
 * Interactions
 * -------------------------------------------------------------------------- */

static void shutter_cb(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
    if (!s_shutter || s_capturing) return;

    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        if (s_capture_mode == CAM_CAPTURE_VIDEO &&
            s_video_state == UI_VIDEO_STATE_RECORDING) {
            lv_obj_set_size(s_shutter, ui_adapt(CAM_VIDEO_STOP - 3),
                            ui_adapt(CAM_VIDEO_STOP - 3));
        } else if (s_capture_mode == CAM_CAPTURE_VIDEO) {
            lv_obj_set_size(s_shutter, ui_adapt(CAM_VIDEO_PRESSED),
                            ui_adapt(CAM_VIDEO_PRESSED));
        } else {
            lv_obj_set_size(s_shutter, ui_adapt(CAM_SHUTTER_PRESSED),
                            ui_adapt(CAM_SHUTTER_PRESSED));
        }
        lv_obj_center(s_shutter);
        break;
    case LV_EVENT_PRESS_LOST:
        apply_capture_visual();
        break;
    case LV_EVENT_RELEASED:
        apply_capture_visual();
        if (s_capture_mode == CAM_CAPTURE_VIDEO) {
            ui_svc_video_record_toggle();
            /* The service updates its state synchronously before queueing the
             * worker; reflect STARTING/STOPPING immediately instead of waiting
             * for the UI-thread notification to round-trip. */
            apply_video_visual(ui_svc_video_get_state());
        } else {
            set_capture_busy(true);
            play_capture_flash();
            PR_INFO("camera page: shutter released, request capture");
            ui_svc_camera_capture();
        }
        break;
    default:
        break;
    }
}

static void thumb_cb(lv_event_t *e)
{
    (void)e;
    if (!s_capturing) ui_route_push(UI_PAGE_PHOTO);
}

static void photo_mode_cb(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
    if (s_capturing || !video_state_allows_mode_switch()) return;
    s_capture_mode = CAM_CAPTURE_PHOTO;
    apply_capture_visual();
}

static void video_mode_cb(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
    if (s_capturing || !video_state_allows_mode_switch()) return;
    s_capture_mode = CAM_CAPTURE_VIDEO;
    apply_capture_visual();
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_route_pop();
}

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        ui_route_pop();
    }
}

/* --------------------------------------------------------------------------
 * Build
 * -------------------------------------------------------------------------- */

static void build_header(void)
{
    lv_obj_t *header = lv_obj_create(s_screen);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, LV_PCT(100), ui_adapt(CAM_HEADER_H));
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(header, &icon_back_24_24, back_cb);
    lv_obj_set_size(back, ui_adapt(CAM_BACK_SIZE), ui_adapt(CAM_BACK_SIZE));
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(4), 0);

    s_title = lv_label_create(header);
    lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_APP_CAMERA));
    lv_obj_set_style_text_color(s_title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_title, UI_FONT_DEFAULT, 0);
    lv_obj_center(s_title);

    if (ui_feature_available(UI_FEATURE_ID_VIDEO)) {
        s_record_timer = lv_obj_create(header);
        lv_obj_remove_style_all(s_record_timer);
        lv_obj_set_size(s_record_timer, ui_adapt(CAM_TIMER_W),
                        ui_adapt(CAM_BACK_SIZE));
        lv_obj_align(s_record_timer, LV_ALIGN_RIGHT_MID, -ui_adapt(8), 0);
        lv_obj_set_flex_flow(s_record_timer, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(s_record_timer, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(s_record_timer, ui_adapt(6), 0);
        lv_obj_clear_flag(s_record_timer,
                          LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *dot = lv_obj_create(s_record_timer);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, ui_adapt(CAM_TIMER_DOT), ui_adapt(CAM_TIMER_DOT));
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(CAM_RECORD_RED), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        s_record_time_lbl = lv_label_create(s_record_timer);
        lv_label_set_text(s_record_time_lbl, "00:00");
        lv_obj_set_style_text_color(s_record_time_lbl, UI_COLOR_TEXT, 0);
        lv_obj_add_flag(s_record_timer, LV_OBJ_FLAG_HIDDEN);
    }
}

static void build_grid(void)
{
    lv_coord_t side = ui_adapt(CAM_PREVIEW_SIZE);
    for (int i = 1; i <= 2; ++i) {
        lv_obj_t *v = lv_obj_create(s_preview_box);
        lv_obj_remove_style_all(v);
        lv_obj_set_size(v, 1, side);
        lv_obj_set_pos(v, side * i / 3, 0);
        lv_obj_set_style_bg_color(v, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(v, LV_OPA_20, 0);
        lv_obj_clear_flag(v, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *h = lv_obj_create(s_preview_box);
        lv_obj_remove_style_all(h);
        lv_obj_set_size(h, side, 1);
        lv_obj_set_pos(h, 0, side * i / 3);
        lv_obj_set_style_bg_color(h, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(h, LV_OPA_20, 0);
        lv_obj_clear_flag(h, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
}

static void build_loading(void)
{
    s_loading = lv_obj_create(s_preview_box);
    lv_obj_remove_style_all(s_loading);
    lv_obj_set_size(s_loading, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_loading);
    lv_obj_set_style_bg_color(s_loading, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(s_loading, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(s_loading, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_loading, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_loading, ui_adapt(12), 0);
    lv_obj_clear_flag(s_loading, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *spinner = lv_spinner_create(s_loading, 900, 70);
    lv_obj_set_size(spinner, ui_adapt(34), ui_adapt(34));
    lv_obj_set_style_arc_width(spinner, ui_adapt(3), LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, ui_adapt(3), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_color_white(), LV_PART_INDICATOR);

    s_loading_lbl = lv_label_create(s_loading);
    lv_label_set_text(s_loading_lbl, ui_i18n_text(UI_TEXT_LOADING));
    lv_obj_set_style_text_color(s_loading_lbl, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_text_font(s_loading_lbl, UI_FONT_DEFAULT, 0);
}

static void build_viewfinder(void)
{
    s_preview_box = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_preview_box);
    lv_obj_set_size(s_preview_box, ui_adapt(CAM_PREVIEW_SIZE),
                    ui_adapt(CAM_PREVIEW_SIZE));
    lv_obj_align(s_preview_box, LV_ALIGN_TOP_MID, 0, ui_adapt(CAM_HEADER_H));
    lv_obj_set_style_bg_color(s_preview_box, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(s_preview_box, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(s_preview_box, true, 0);
    lv_obj_clear_flag(s_preview_box, LV_OBJ_FLAG_SCROLLABLE);

    s_canvas = ui_comp_picture_create(s_preview_box);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);

    build_grid();
    build_loading();

    s_flash = lv_obj_create(s_preview_box);
    lv_obj_remove_style_all(s_flash);
    lv_obj_set_size(s_flash, LV_PCT(100), LV_PCT(100));
    lv_obj_center(s_flash);
    lv_obj_set_style_bg_color(s_flash, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_flash, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_flash, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

static void build_shutter(lv_obj_t *controls)
{
    s_shutter_ring = lv_obj_create(controls);
    lv_obj_remove_style_all(s_shutter_ring);
    lv_obj_set_size(s_shutter_ring, ui_adapt(CAM_SHUTTER_RING),
                    ui_adapt(CAM_SHUTTER_RING));
    lv_obj_align(s_shutter_ring, LV_ALIGN_BOTTOM_MID, 0,
                 -ui_adapt(CAM_SHUTTER_BOTTOM));
    lv_obj_set_style_radius(s_shutter_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_shutter_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_shutter_ring, ui_adapt(2), 0);
    lv_obj_set_style_border_color(s_shutter_ring, lv_color_white(), 0);
    lv_obj_set_style_border_opa(s_shutter_ring, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_shutter_ring, LV_OBJ_FLAG_SCROLLABLE);

    s_shutter = lv_btn_create(s_shutter_ring);
    lv_obj_remove_style_all(s_shutter);
    lv_obj_set_size(s_shutter, ui_adapt(CAM_SHUTTER_IDLE),
                    ui_adapt(CAM_SHUTTER_IDLE));
    lv_obj_center(s_shutter);
    lv_obj_set_style_radius(s_shutter, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_shutter, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_shutter, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_shutter, shutter_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_shutter, shutter_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_shutter, shutter_cb, LV_EVENT_PRESS_LOST, NULL);
}

static lv_obj_t *build_mode_button(lv_obj_t *parent, const char *text,
                                   lv_event_cb_t cb, lv_obj_t **label)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_height(btn, LV_PCT(100));
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    *label = lv_label_create(btn);
    lv_label_set_text(*label, text);
    lv_obj_center(*label);
    return btn;
}

static void build_mode_switch(lv_obj_t *controls)
{
    lv_obj_t *mode_switch = lv_obj_create(controls);
    lv_obj_remove_style_all(mode_switch);
    lv_obj_set_size(mode_switch, ui_adapt(CAM_MODE_SWITCH_W),
                    ui_adapt(CAM_MODE_SWITCH_H));
    lv_obj_align(mode_switch, LV_ALIGN_TOP_MID, 0, ui_adapt(CAM_MODE_SWITCH_TOP));
    lv_obj_set_style_radius(mode_switch, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(mode_switch, lv_color_hex(0x242424), 0);
    lv_obj_set_style_bg_opa(mode_switch, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(mode_switch, ui_adapt(2), 0);
    lv_obj_set_style_pad_column(mode_switch, 0, 0);
    lv_obj_set_flex_flow(mode_switch, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(mode_switch, LV_OBJ_FLAG_SCROLLABLE);

    s_photo_mode_btn = build_mode_button(
        mode_switch, ui_i18n_text(UI_TEXT_CAMERA_PHOTO),
        photo_mode_cb, &s_photo_mode_lbl);
    s_video_mode_btn = build_mode_button(
        mode_switch, ui_i18n_text(UI_TEXT_VIDEO_RECORD),
        video_mode_cb, &s_video_mode_lbl);
}

static void build_thumbnail(lv_obj_t *controls)
{
    s_thumb_box = lv_obj_create(controls);
    lv_obj_remove_style_all(s_thumb_box);
    lv_obj_set_size(s_thumb_box, ui_adapt(CAM_THUMB_BOX),
                    ui_adapt(CAM_THUMB_BOX));
    lv_obj_align(s_thumb_box, LV_ALIGN_BOTTOM_LEFT, ui_adapt(CAM_THUMB_MARGIN),
                 -ui_adapt(CAM_THUMB_BOTTOM));
    lv_obj_set_style_radius(s_thumb_box, ui_adapt(CAM_THUMB_RADIUS), 0);
    lv_obj_set_style_clip_corner(s_thumb_box, true, 0);
    lv_obj_set_style_border_width(s_thumb_box, 1, 0);
    lv_obj_set_style_border_color(s_thumb_box, lv_color_white(), 0);
    lv_obj_set_style_border_opa(s_thumb_box, LV_OPA_60, 0);
    lv_obj_set_style_bg_color(s_thumb_box, lv_color_hex(0x303030), 0);
    lv_obj_set_style_bg_opa(s_thumb_box, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_thumb_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_thumb_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_thumb_box, thumb_cb, LV_EVENT_CLICKED, NULL);

    s_thumb = ui_comp_picture_create(s_thumb_box);
    lv_obj_clear_flag(s_thumb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_thumb_box, LV_OBJ_FLAG_HIDDEN);
}

static void build_controls(void)
{
    lv_obj_t *controls = lv_obj_create(s_screen);
    lv_obj_remove_style_all(controls);
    lv_obj_set_size(controls, LV_PCT(100), ui_adapt(CAM_BOTTOM_H));
    lv_obj_align(controls, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(controls, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(controls, LV_OPA_COVER, 0);
    lv_obj_clear_flag(controls, LV_OBJ_FLAG_SCROLLABLE);

    build_shutter(controls);
    build_thumbnail(controls);
    if (ui_feature_available(UI_FEATURE_ID_VIDEO)) {
        build_mode_switch(controls);
    }
    apply_video_visual(ui_svc_video_get_state());
}

/* --------------------------------------------------------------------------
 * Page lifecycle
 * -------------------------------------------------------------------------- */

static void on_create(void *parent)
{
    (void)parent;
    PR_INFO("camera page: on_create");

    s_preview_on = false;
    s_capturing  = false;
    s_record_timer_visible = false;
    s_record_last_sec = CAM_TIMER_INVALID_SEC;
    s_capture_mode = CAM_CAPTURE_PHOTO;
    s_video_state = UI_VIDEO_STATE_IDLE;
    ui_svc_camera_interrupt_chat();

    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(s_screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);

    build_header();
    build_viewfinder();
    build_controls();

    ui_svc_camera_set_preview_cb(on_preview_frame);
    ui_svc_camera_set_thumb_cb(on_thumb_ready);
    ui_svc_camera_set_capture_cb(on_capture_done);
    ui_svc_video_set_cb(on_video_state);
    ui_svc_camera_refresh_thumb();
}

static void on_enter(uint32_t dirty)
{
    if (ui_dirty_is_enter(dirty)) {
        PR_INFO("camera page: on_enter, preview_on=%d", s_preview_on ? 1 : 0);
        ui_comp_statusbar_set_visible(false);
        ui_svc_video_set_cb(on_video_state);
        apply_video_visual(ui_svc_video_get_state());
        if (!s_preview_on) {
            ui_svc_camera_preview_start();
            s_preview_on = true;
        }
    }
    if (dirty & (1u << UI_STATE_GROUP_SYSTEM)) {
        update_record_timer();
        if (ui_svc_video_get_state() == UI_VIDEO_STATE_RECORDING &&
            ui_svc_video_get_elapsed_ms() >= UI_SVC_VIDEO_MAX_DURATION_MS) {
            ui_comp_popup_toast(ui_i18n_text(UI_TEXT_VIDEO_LIMIT_REACHED), 1500);
            ui_svc_video_record_stop();
        }
    }
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        if (s_title) lv_label_set_text(s_title, ui_i18n_text(UI_TEXT_APP_CAMERA));
        if (s_photo_mode_lbl) {
            lv_label_set_text(s_photo_mode_lbl, ui_i18n_text(UI_TEXT_CAMERA_PHOTO));
        }
        if (s_video_mode_lbl) {
            lv_label_set_text(s_video_mode_lbl, ui_i18n_text(UI_TEXT_VIDEO_RECORD));
        }
        apply_video_visual(ui_svc_video_get_state());
    }
}

static void on_leave(void)
{
    PR_INFO("camera page: on_leave, preview_on=%d", s_preview_on ? 1 : 0);
    ui_svc_video_record_stop();
    ui_svc_video_set_cb(NULL);
    if (s_preview_on) {
        ui_svc_camera_preview_stop();
        s_preview_on = false;
    }
    ui_comp_statusbar_set_visible(true);
}

static void on_destroy(void)
{
    PR_INFO("camera page: on_destroy, preview_on=%d", s_preview_on ? 1 : 0);
    if (s_preview_on) ui_svc_camera_preview_stop();
    s_preview_on = false;

    ui_svc_camera_set_preview_cb(NULL);
    ui_svc_camera_set_thumb_cb(NULL);
    ui_svc_camera_set_capture_cb(NULL);
    ui_svc_video_set_cb(NULL);
    ui_svc_video_record_stop();
    ui_comp_statusbar_set_visible(true);

    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
    }
    s_screen = NULL;
    s_title = NULL;
    s_preview_box = NULL;
    s_canvas = NULL;
    s_loading = NULL;
    s_loading_lbl = NULL;
    s_flash = NULL;
    s_shutter_ring = NULL;
    s_shutter = NULL;
    s_thumb_box = NULL;
    s_thumb = NULL;
    s_photo_mode_btn = NULL;
    s_photo_mode_lbl = NULL;
    s_video_mode_btn = NULL;
    s_video_mode_lbl = NULL;
    s_record_timer = NULL;
    s_record_time_lbl = NULL;
    s_capturing = false;
    s_record_timer_visible = false;
    s_record_last_sec = CAM_TIMER_INVALID_SEC;
    s_capture_mode = CAM_CAPTURE_PHOTO;
    s_video_state = UI_VIDEO_STATE_IDLE;
}

const ui_page_entry_t ui_page_camera_entry = {
    .id = UI_PAGE_CAMERA,
    .name = "camera",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
