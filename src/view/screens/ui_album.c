/**
 * @file ui_album.c
 * @brief Album photo viewer screen for T5AI_BOARD (320x480)
 * @version 1.0
 * @date 2025-04-02
 * @copyright Copyright (c) Tuya Inc.
 */
#include <stdio.h>
#include <string.h>
#include "ui_common.h"
#include "tuya_ai_display.h"
#include "tal_image_jpeg_codec.h"
#include "tal_memory.h"

/* ---------------------------------------------------------------------------
 * Font / icon declarations
 * --------------------------------------------------------------------------- */
LV_IMG_DECLARE(icon_arrow_right);

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define ALBUM_TOP_Y         12
#define ALBUM_BTN_SIZE      36
#define ALBUM_AI_BTN_SIZE   44

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    lv_obj_t *album_scr;
    lv_obj_t *overlay;
    lv_obj_t *back_btn;
    lv_obj_t *delete_btn;
    lv_obj_t *ai_btn;
    lv_obj_t *title_label;
    lv_obj_t *time_label;
    lv_obj_t *photo_canvas;
    uint8_t  *canvas_buf;
    lv_obj_t *all_photos_btn;
    lv_obj_t *all_photos_label;
    lv_obj_t *arrow_icon;
    lv_obj_t *empty_hint;
    BOOL_T    overlay_visible;
} ALBUM_UI_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC ALBUM_UI_T s_album = {0};
/** Dummy RGB565 buffer so lv_canvas has a valid pointer after real buffer is freed */
STATIC uint8_t s_album_canvas_dummy[4 * 4 * 2];

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __album_toggle_overlay(VOID_T);
STATIC VOID_T __album_screen_click_cb(lv_event_t *e);
STATIC VOID_T __album_back_cb(lv_event_t *e);
STATIC VOID_T __album_delete_cb(lv_event_t *e);
STATIC VOID_T __album_ai_cb(lv_event_t *e);
STATIC VOID_T __album_all_photos_cb(lv_event_t *e);
STATIC VOID_T __album_gesture_cb(lv_event_t *e);

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Show or hide overlay UI elements
 * @return none
 */
STATIC VOID_T __album_toggle_overlay(VOID_T)
{
    if (s_album.overlay == NULL) {
        return;
    }

    if (s_album.overlay_visible) {
        lv_obj_add_flag(s_album.overlay, LV_OBJ_FLAG_HIDDEN);
        s_album.overlay_visible = FALSE;
    } else {
        lv_obj_clear_flag(s_album.overlay, LV_OBJ_FLAG_HIDDEN);
        s_album.overlay_visible = TRUE;
    }
}

/**
 * @brief Screen tap callback: toggle overlay visibility
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __album_screen_click_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) != s_album.album_scr) {
        return;
    }
    __album_toggle_overlay();
}

/**
 * @brief Back button callback
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __album_back_cb(lv_event_t *e)
{
    (VOID_T)e;
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_CLOSE_ALBUM);
}

/**
 * @brief Delete button callback
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __album_delete_cb(lv_event_t *e)
{
    (VOID_T)e;
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_ALBUM_DELETE_PIC);
}

/**
 * @brief AI button callback: send current photo to chat for AI recognition
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __album_ai_cb(lv_event_t *e)
{
    (VOID_T)e;
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_ALBUM_AI_RECOGNIZE);
}

/**
 * @brief "所有照片" button callback
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __album_all_photos_cb(lv_event_t *e)
{
    (VOID_T)e;
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_OPEN_ALBUM_GRID);
}

/**
 * @brief Gesture callback: swipe left/right to post next/prev album event
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __album_gesture_cb(lv_event_t *e)
{
    (VOID_T)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

    if (dir == LV_DIR_LEFT) {
        tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_ALBUM_VIEW_NEXT_PIC);
    } else if (dir == LV_DIR_RIGHT) {
        tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_ALBUM_VIEW_PREV_PIC);
    }
}

/**
 * @brief Create album viewer screen (does NOT load/show it)
 * @return none
 */
VOID_T setup_scr_album(VOID_T)
{
    if (s_album.album_scr) {
        return;
    }

    s_album.overlay_visible = TRUE;

    /* ---- Full-screen base ---- */
    s_album.album_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_album.album_scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_album.album_scr, lv_color_hex(UI_BG_COLOR_BLACK), 0);
    lv_obj_set_style_pad_all(s_album.album_scr, 0, 0);
    lv_obj_set_scrollbar_mode(s_album.album_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_album.album_scr, __album_screen_click_cb,
                        LV_EVENT_CLICKED, NULL);

    /* ---- Photo canvas (full screen, for RGB565 display) ---- */
    s_album.photo_canvas = lv_canvas_create(s_album.album_scr);
    lv_obj_set_pos(s_album.photo_canvas, 0, 0);
    lv_obj_set_size(s_album.photo_canvas, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_border_width(s_album.photo_canvas, 0, 0);

    /* ---- Overlay container ---- */
    s_album.overlay = lv_obj_create(s_album.album_scr);
    lv_obj_remove_style_all(s_album.overlay);
    lv_obj_set_size(s_album.overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_opa(s_album.overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(s_album.overlay, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_album.overlay, LV_OBJ_FLAG_CLICKABLE);

    /* ---- Top-left: back button (gray circle) ---- */
    s_album.back_btn = lv_btn_create(s_album.overlay);
    lv_obj_remove_style_all(s_album.back_btn);
    lv_obj_set_size(s_album.back_btn, ALBUM_BTN_SIZE, ALBUM_BTN_SIZE);
    lv_obj_set_pos(s_album.back_btn, 12, ALBUM_TOP_Y);
    lv_obj_set_style_radius(s_album.back_btn, ALBUM_BTN_SIZE / 2, 0);
    lv_obj_set_style_bg_opa(s_album.back_btn, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(s_album.back_btn, lv_color_hex(0x808080), 0);
    lv_obj_add_event_cb(s_album.back_btn, __album_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_icon = lv_img_create(s_album.back_btn);
    lv_img_set_src(back_icon, &icon_back_24_24);
    lv_obj_center(back_icon);

    /* ---- Top-center: title "今天" + time "10:00" ---- */
    s_album.title_label = lv_label_create(s_album.overlay);
    lv_label_set_text(s_album.title_label, "今天");
    lv_obj_set_style_text_font(s_album.title_label, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_album.title_label, lv_color_white(), 0);
    lv_obj_align(s_album.title_label, LV_ALIGN_TOP_MID, 0, ALBUM_TOP_Y);

    s_album.time_label = lv_label_create(s_album.overlay);
    lv_label_set_text(s_album.time_label, "10:00");
    lv_obj_set_style_text_font(s_album.time_label, &AlibabaPuHuiTi3_Regular16, 0);
    lv_obj_set_style_text_color(s_album.time_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align_to(s_album.time_label, s_album.title_label,
                    LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    /* ---- Top-right: AI button (gray circle) ---- */
    s_album.ai_btn = lv_btn_create(s_album.overlay);
    lv_obj_remove_style_all(s_album.ai_btn);
    lv_obj_set_size(s_album.ai_btn, ALBUM_AI_BTN_SIZE, ALBUM_AI_BTN_SIZE);
    lv_obj_set_pos(s_album.ai_btn, LV_HOR_RES - ALBUM_AI_BTN_SIZE - 12, ALBUM_TOP_Y);
    lv_obj_set_style_radius(s_album.ai_btn, ALBUM_AI_BTN_SIZE / 2, 0);
    lv_obj_set_style_bg_opa(s_album.ai_btn, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(s_album.ai_btn, lv_color_hex(0x808080), 0);
    lv_obj_add_event_cb(s_album.ai_btn, __album_ai_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ai_icon = lv_img_create(s_album.ai_btn);
    lv_img_set_src(ai_icon, &icon_ai_icon);
    lv_obj_center(ai_icon);

    /* ---- Bottom-left: delete button (gray circle) ---- */
    s_album.delete_btn = lv_btn_create(s_album.overlay);
    lv_obj_remove_style_all(s_album.delete_btn);
    lv_obj_set_size(s_album.delete_btn, ALBUM_BTN_SIZE, ALBUM_BTN_SIZE);
    lv_obj_align(s_album.delete_btn, LV_ALIGN_BOTTOM_LEFT, 12, -12);
    lv_obj_set_style_radius(s_album.delete_btn, ALBUM_BTN_SIZE / 2, 0);
    lv_obj_set_style_bg_opa(s_album.delete_btn, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(s_album.delete_btn, lv_color_hex(0x808080), 0);
    lv_obj_add_event_cb(s_album.delete_btn, __album_delete_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *del_icon = lv_img_create(s_album.delete_btn);
    lv_img_set_src(del_icon, &icon_delete);
    lv_img_set_zoom(del_icon, 192);
    lv_obj_center(del_icon);

    s_album.empty_hint = lv_label_create(s_album.overlay);
    lv_label_set_text(s_album.empty_hint, "暂无图片");
    lv_obj_set_style_text_font(s_album.empty_hint, &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_album.empty_hint, lv_color_hex(0x999999), 0);
    lv_obj_center(s_album.empty_hint);
    lv_obj_add_flag(s_album.empty_hint, LV_OBJ_FLAG_HIDDEN);

    /* ---- Bottom-right: "所有照片 >" button ---- */
    s_album.all_photos_btn = lv_btn_create(s_album.overlay);
    lv_obj_remove_style_all(s_album.all_photos_btn);
    lv_obj_set_size(s_album.all_photos_btn, LV_SIZE_CONTENT, 48);
    lv_obj_align(s_album.all_photos_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -8);
    lv_obj_set_flex_flow(s_album.all_photos_btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_album.all_photos_btn, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_album.all_photos_btn, 4, 0);
    lv_obj_set_style_pad_left(s_album.all_photos_btn, 12, 0);
    lv_obj_set_style_pad_right(s_album.all_photos_btn, 12, 0);
    lv_obj_set_ext_click_area(s_album.all_photos_btn, 15);
    lv_obj_add_event_cb(s_album.all_photos_btn, __album_all_photos_cb,
                        LV_EVENT_CLICKED, NULL);

    s_album.all_photos_label = lv_label_create(s_album.all_photos_btn);
    lv_label_set_text(s_album.all_photos_label, "所有照片");
    lv_obj_set_style_text_font(s_album.all_photos_label,
                               &AlibabaPuHuiTi3_Regular18_Static, 0);
    lv_obj_set_style_text_color(s_album.all_photos_label, lv_color_white(), 0);

    s_album.arrow_icon = lv_img_create(s_album.all_photos_btn);
    lv_img_set_src(s_album.arrow_icon, &icon_arrow_right);

    ui_control_center_register_gesture(s_album.album_scr);
    lv_obj_add_event_cb(s_album.album_scr, __album_gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_update_layout(s_album.album_scr);
}

/**
 * @brief Show the album viewer (creates if needed)
 * @return none
 */
VOID_T ui_album_show(VOID_T)
{
    if (s_album.album_scr == NULL) {
        setup_scr_album();
    }
    if (lv_scr_act() != s_album.album_scr) {
        lv_scr_load(s_album.album_scr);
    }
}

/**
 * @brief Set the photo to display
 * @param[in] img_src pointer to lv_img_dsc_t
 * @return none
 */
VOID_T ui_album_set_jpeg_photo(uint16_t width, uint16_t height, uint8_t *data, uint32_t len)
{
    if (s_album.photo_canvas == NULL || data == NULL || len == 0) {
        return;
    }

    if (s_album.empty_hint) {
        lv_obj_add_flag(s_album.empty_hint, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(s_album.photo_canvas, LV_OBJ_FLAG_HIDDEN);

    /* Decode JPEG to RGB565 */
    uint32_t rgb565_size = width * height * 2;
    uint8_t *rgb565_buf = Malloc(rgb565_size);
    if (rgb565_buf == NULL) {
        TAL_PR_ERR("album: malloc rgb565 buf failed, size=%u", rgb565_size);
        return;
    }

    TAL_IMAGE_JPEG_OUTPUT_T out = {0};
    out.out_buf      = rgb565_buf;
    out.out_buf_size = rgb565_size;
    out.out_width    = width;
    out.out_height   = height;

    OPERATE_RET ret = tal_image_jpeg_decode_rgb565(data, len, &out);
    if (ret != OPRT_OK) {
        TAL_PR_ERR("album: jpeg decode rgb565 failed: %d", ret);
        Free(rgb565_buf);
        return;
    }

    /* Set canvas buffer with decoded RGB565 data, then resize the object to
     * match the image size and center it. Keeping obj size == buffer size
     * prevents lv_img's default tile-to-fill behavior when the image is
     * smaller than the screen. */
    lv_canvas_set_buffer(s_album.photo_canvas, rgb565_buf, width, height, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(s_album.photo_canvas, width, height);
    lv_obj_align(s_album.photo_canvas, LV_ALIGN_CENTER, 0, 0);

    /* Free previous buffer */
    if (s_album.canvas_buf) {
        Free(s_album.canvas_buf);
    }
    s_album.canvas_buf = rgb565_buf;

    lv_obj_invalidate(s_album.photo_canvas);
}

/**
 * @brief Show or hide empty-album hint and release photo canvas buffer when empty
 * @param[in] empty TRUE to show "暂无图片" and hide photo; FALSE to show photo area
 * @return none
 */
VOID_T ui_album_set_empty_state(BOOL_T empty)
{
    if (empty) {
        if (s_album.canvas_buf != NULL) {
            Free(s_album.canvas_buf);
            s_album.canvas_buf = NULL;
        }
        if (s_album.photo_canvas != NULL) {
            memset(s_album_canvas_dummy, 0, sizeof(s_album_canvas_dummy));
            lv_canvas_set_buffer(s_album.photo_canvas, s_album_canvas_dummy, 4, 4,
                                 LV_IMG_CF_TRUE_COLOR);
            lv_obj_set_size(s_album.photo_canvas, 4, 4);
            lv_obj_add_flag(s_album.photo_canvas, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_album.empty_hint != NULL) {
            lv_obj_clear_flag(s_album.empty_hint, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        if (s_album.empty_hint != NULL) {
            lv_obj_add_flag(s_album.empty_hint, LV_OBJ_FLAG_HIDDEN);
        }
        if (s_album.photo_canvas != NULL) {
            lv_obj_clear_flag(s_album.photo_canvas, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/**
 * @brief Update the title and time labels
 * @param[in] title title string (e.g. "今天", "昨天", "3月20日")
 * @param[in] time time string (e.g. "10:00")
 * @return none
 */
VOID_T ui_album_set_info(CONST CHAR_T *title, CONST CHAR_T *time)
{
    if (s_album.title_label && title) {
        lv_label_set_text(s_album.title_label, title);
    }
    if (s_album.time_label && time) {
        lv_label_set_text(s_album.time_label, time);
    }
}

/**
 * @brief Get the album screen object
 * @return album screen pointer, NULL if not created
 */
lv_obj_t *ui_album_get_scr(VOID_T)
{
    return s_album.album_scr;
}

/**
 * @brief Hide album screen and release canvas buffer to save memory
 * @return none
 */
VOID_T ui_album_hide(VOID_T)
{
    if (s_album.canvas_buf != NULL) {
        Free(s_album.canvas_buf);
        s_album.canvas_buf = NULL;
    }
    if (s_album.photo_canvas != NULL) {
        memset(s_album_canvas_dummy, 0, sizeof(s_album_canvas_dummy));
        lv_canvas_set_buffer(s_album.photo_canvas, s_album_canvas_dummy, 4, 4,
                             LV_IMG_CF_TRUE_COLOR);
        lv_obj_set_size(s_album.photo_canvas, 4, 4);
    }
}
