/**
 * @file ui_camera.c
 * @brief Camera screen UI for T5AI_BOARD (320x480)
 * @version 1.0
 * @date 2025-04-02
 * @copyright Copyright (c) Tuya Inc.
 */
#include <string.h>
#include "ui_common.h"
#include "tuya_device_camera.h"
#include "tal_image_yuv422_to_rgb.h"
#include "tal_image_scale.h"
#include "tal_memory.h"

/* ---------------------------------------------------------------------------
 * Font / icon declarations
 * --------------------------------------------------------------------------- */
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular18_Static);
LV_IMG_DECLARE(icon_back_24_24);

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define CAM_BG_COLOR            0x000000
#define CAM_TOP_BAR_Y           12
#define CAM_SHUTTER_RING_SIZE   72
#define CAM_SHUTTER_BTN_SIZE    60
#define CAM_SHUTTER_Y_OFFSET    (-45)
#define CAM_THUMB_SIZE          60

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    lv_obj_t *cam_scr;
    lv_obj_t *canvas;
    lv_obj_t *overlay;
    lv_obj_t *back_btn;
    lv_obj_t *top_bar;
    lv_obj_t *shutter_btn;
    lv_obj_t *thumbnail;
    lv_obj_t *thumb_canvas;
    BOOL_T    overlay_visible;
    uint8_t  *canvas_buf;
    uint8_t  *thumb_canvas_buf;
} CAM_UI_T;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC CAM_UI_T s_cam_ui = {0};
STATIC uint8_t s_cam_thumb_canvas_dummy[4 * 4 * 2];

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __cam_toggle_overlay(VOID_T);
STATIC VOID_T __cam_screen_click_cb(lv_event_t *e);
STATIC VOID_T __cam_back_cb(lv_event_t *e);
STATIC VOID_T __cam_shutter_cb(lv_event_t *e);
STATIC VOID_T __cam_thumbnail_cb(lv_event_t *e);

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Show or hide overlay UI elements (top bar, shutter, thumbnail, hint)
 * @return none
 */
STATIC VOID_T __cam_toggle_overlay(VOID_T)
{
    if (s_cam_ui.overlay == NULL) {
        return;
    }

    if (s_cam_ui.overlay_visible) {
        lv_obj_add_flag(s_cam_ui.overlay, LV_OBJ_FLAG_HIDDEN);
        s_cam_ui.overlay_visible = FALSE;
    } else {
        lv_obj_clear_flag(s_cam_ui.overlay, LV_OBJ_FLAG_HIDDEN);
        s_cam_ui.overlay_visible = TRUE;
    }
}

/**
 * @brief Screen tap callback: toggle overlay visibility
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __cam_screen_click_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) != s_cam_ui.cam_scr) {
        return;
    }
    __cam_toggle_overlay();
}

/**
 * @brief Back button callback
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __cam_back_cb(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_CLOSE_CAMERA);
}

/**
 * @brief Shutter button callback: trigger capture
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __cam_shutter_cb(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
    TAL_PR_DEBUG("camera: shutter");
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_TAKE_PHOTO);
}

/**
 * @brief Thumbnail click callback: post open-album action
 * @param[in] e LVGL event
 * @return none
 */
STATIC VOID_T __cam_thumbnail_cb(lv_event_t *e)
{
    lv_event_stop_bubbling(e);
    tuya_ai_display_action_post(NULL, 0, TY_DISP_ACT_OPEN_ALBUM);
}

/**
 * @brief Build and show the camera screen
 * @return none
 */
VOID_T setup_scr_camera(VOID_T)
{
    s_cam_ui.overlay_visible = TRUE;

    /* ---- Full-screen base (camera preview goes behind overlay) ---- */
    s_cam_ui.cam_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_cam_ui.cam_scr, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(s_cam_ui.cam_scr, lv_color_hex(CAM_BG_COLOR), 0);
    lv_obj_set_style_pad_all(s_cam_ui.cam_scr, 0, 0);
    lv_obj_set_scrollbar_mode(s_cam_ui.cam_scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_cam_ui.cam_scr, __cam_screen_click_cb,
                        LV_EVENT_CLICKED, NULL);

    /* ---- Canvas for camera preview ---- */
    s_cam_ui.canvas_buf = Malloc(LV_HOR_RES * LV_VER_RES *2);
    if(s_cam_ui.canvas_buf) {
        memset(s_cam_ui.canvas_buf, 0x00, LV_HOR_RES * LV_VER_RES *2);
        s_cam_ui.canvas = lv_canvas_create(s_cam_ui.cam_scr);
        lv_obj_set_pos(s_cam_ui.canvas, 0, 0);
        lv_obj_set_size(s_cam_ui.canvas, LV_HOR_RES, LV_VER_RES);
        lv_obj_set_style_border_width(s_cam_ui.canvas, 0, 0);
        lv_canvas_set_buffer(s_cam_ui.canvas, s_cam_ui.canvas_buf, LV_HOR_RES, LV_VER_RES, LV_IMG_CF_TRUE_COLOR);
        lv_canvas_fill_bg(s_cam_ui.canvas, lv_color_black(), LV_OPA_COVER);
    }

    /* ---- Overlay container (all UI on top of preview) ---- */
    s_cam_ui.overlay = lv_obj_create(s_cam_ui.cam_scr);
    lv_obj_remove_style_all(s_cam_ui.overlay);
    lv_obj_set_size(s_cam_ui.overlay, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_opa(s_cam_ui.overlay, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(s_cam_ui.overlay, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_cam_ui.overlay, LV_OBJ_FLAG_CLICKABLE);

    /* ---- Top bar ---- */

    /* Back button (gray circle) */
    s_cam_ui.back_btn = lv_btn_create(s_cam_ui.overlay);
    lv_obj_remove_style_all(s_cam_ui.back_btn);
    lv_obj_set_size(s_cam_ui.back_btn, 36, 36);
    lv_obj_set_pos(s_cam_ui.back_btn, 12, CAM_TOP_BAR_Y);
    lv_obj_set_style_radius(s_cam_ui.back_btn, 18, 0);
    lv_obj_set_style_bg_opa(s_cam_ui.back_btn, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(s_cam_ui.back_btn, lv_color_hex(0x808080), 0);
    lv_obj_add_event_cb(s_cam_ui.back_btn, __cam_back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_icon = lv_img_create(s_cam_ui.back_btn);
    lv_img_set_src(back_icon, &icon_back_24_24);
    lv_obj_center(back_icon);

    /* ---- Bottom-center shutter button (circle with ring) ---- */
    lv_obj_t *shutter_ring = lv_obj_create(s_cam_ui.overlay);
    lv_obj_remove_style_all(shutter_ring);
    lv_obj_set_size(shutter_ring, CAM_SHUTTER_RING_SIZE, CAM_SHUTTER_RING_SIZE);
    lv_obj_align(shutter_ring, LV_ALIGN_BOTTOM_MID, 0, CAM_SHUTTER_Y_OFFSET);
    lv_obj_set_style_radius(shutter_ring, CAM_SHUTTER_RING_SIZE / 2, 0);
    lv_obj_set_style_bg_opa(shutter_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(shutter_ring, 2, 0);
    lv_obj_set_style_border_color(shutter_ring, lv_color_white(), 0);
    lv_obj_set_style_border_opa(shutter_ring, LV_OPA_COVER, 0);

    s_cam_ui.shutter_btn = lv_btn_create(shutter_ring);
    lv_obj_remove_style_all(s_cam_ui.shutter_btn);
    lv_obj_align(s_cam_ui.shutter_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(s_cam_ui.shutter_btn, CAM_SHUTTER_BTN_SIZE, CAM_SHUTTER_BTN_SIZE);
    lv_obj_set_style_radius(s_cam_ui.shutter_btn, CAM_SHUTTER_BTN_SIZE / 2, 0);
    lv_obj_set_style_bg_color(s_cam_ui.shutter_btn, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_cam_ui.shutter_btn, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_cam_ui.shutter_btn, __cam_shutter_cb,
                        LV_EVENT_CLICKED, NULL);

    /* ---- Thumbnail placeholder (bottom-right, above shutter - hidden initially) ---- */
    s_cam_ui.thumbnail = lv_obj_create(s_cam_ui.overlay);
    lv_obj_remove_style_all(s_cam_ui.thumbnail);
    lv_obj_set_size(s_cam_ui.thumbnail, CAM_THUMB_SIZE, CAM_THUMB_SIZE);
    lv_obj_align(s_cam_ui.thumbnail, LV_ALIGN_BOTTOM_RIGHT, -22, -15);
    lv_obj_set_style_radius(s_cam_ui.thumbnail, 8, 0);
    lv_obj_set_style_bg_opa(s_cam_ui.thumbnail, LV_OPA_50, 0);
    lv_obj_set_style_bg_color(s_cam_ui.thumbnail, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_border_width(s_cam_ui.thumbnail, 2, 0);
    lv_obj_set_style_border_color(s_cam_ui.thumbnail, lv_color_white(), 0);
    lv_obj_set_style_border_opa(s_cam_ui.thumbnail, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_cam_ui.thumbnail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_cam_ui.thumbnail, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_cam_ui.thumbnail, __cam_thumbnail_cb,
                        LV_EVENT_CLICKED, NULL);

    /* Thumbnail canvas (created without buffer, set on first JPEG) */
    s_cam_ui.thumb_canvas = lv_canvas_create(s_cam_ui.thumbnail);
    lv_obj_set_pos(s_cam_ui.thumb_canvas, 0, 0);
    lv_obj_set_size(s_cam_ui.thumb_canvas, CAM_THUMB_SIZE, CAM_THUMB_SIZE);
    lv_obj_set_style_border_width(s_cam_ui.thumb_canvas, 0, 0);

    ui_control_register_gesture(s_cam_ui.cam_scr);
    lv_obj_update_layout(s_cam_ui.cam_scr);

}

/**
 * @brief Set the camera preview image source (for live preview frames)
 * @param[in] img_src pointer to lv_img_dsc_t
 * @return none
 */
VOID_T ui_camera_set_preview_yuv_format(uint16_t width, uint16_t height, uint8_t *data, uint32_t len)
{
    if (s_cam_ui.canvas == NULL || data == NULL) {
        return;
    }

    /* Step 1: Convert YUV422 to RGB565 at camera native resolution */
    uint32_t rgb565_size = width * height * 2;
    uint8_t *rgb565_buf = Malloc(rgb565_size);
    if (rgb565_buf == NULL) {
        TAL_PR_ERR("malloc rgb565 buf failed");
        return;
    }

    TAL_IMAGE_YUV422_TO_RGB_T conv_cfg = {0};
    conv_cfg.in_buf     = data;
    conv_cfg.in_width   = width;
    conv_cfg.in_height  = height;
    conv_cfg.out_buf    = rgb565_buf;
    conv_cfg.out_width  = width;
    conv_cfg.out_height = height;

    OPERATE_RET ret = tal_image_convert_yuv422_to_rgb565(&conv_cfg);
    if (ret != OPRT_OK) {
        TAL_PR_ERR("yuv422 to rgb565 failed: %d", ret);
        tal_free(rgb565_buf);
        return;
    }

    lv_canvas_set_buffer(s_cam_ui.canvas, rgb565_buf, width, height, LV_IMG_CF_TRUE_COLOR);

    if(s_cam_ui.canvas_buf) {
        Free(s_cam_ui.canvas_buf);
    }

    s_cam_ui.canvas_buf = rgb565_buf;

    lv_area_t inv_area;
    lv_area_set(&inv_area, 0, 0, width - 1, height - 1);
    lv_obj_invalidate_area(s_cam_ui.canvas, &inv_area);

    // lv_obj_invalidate(s_cam_ui.canvas);
}

/**
 * @brief Set the thumbnail image after a photo is taken
 * @param[in] data pointer to JPEG data
 * @param[in] len  JPEG data length in bytes
 * @return none
 */
VOID_T ui_camera_set_thumbnail_jpeg(uint8_t *data, uint32_t len)
{
    if (s_cam_ui.thumbnail == NULL || s_cam_ui.thumb_canvas == NULL ||
        data == NULL || len == 0) {
        return;
    }

    TAL_IMAGE_JPEG_SCALE_IN_T in = {0};
    in.method     = TAL_IMAGE_SCALE_MTH_BILINEAR;
    in.mode       = TAL_IMAGE_SCALE_MODE_SIZE;
    in.data       = data;
    in.size       = len;
    in.out_width  = CAM_THUMB_SIZE;
    in.out_height = CAM_THUMB_SIZE;

    TAL_IMAGE_SCALE_OUT_T out = {0};
    OPERATE_RET ret = tal_image_jpeg_scale_rgb565(&in, &out);
    if (ret != OPRT_OK) {
        TAL_PR_ERR("jpeg scale to rgb565 failed: %d", ret);
        return;
    }

    /* Free old buffer and replace with the new one directly */
    if (s_cam_ui.thumb_canvas_buf) {
        tal_image_scale_buf_free(&(TAL_IMAGE_SCALE_OUT_T){.buf = s_cam_ui.thumb_canvas_buf});
    }
    s_cam_ui.thumb_canvas_buf = out.buf;

    lv_canvas_set_buffer(s_cam_ui.thumb_canvas, s_cam_ui.thumb_canvas_buf, out.width, out.height, LV_IMG_CF_TRUE_COLOR);
    lv_obj_invalidate(s_cam_ui.thumb_canvas);
    lv_obj_clear_flag(s_cam_ui.thumbnail, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Hide thumbnail and release its buffer (e.g. album became empty)
 * @return none
 */
VOID_T ui_camera_clear_thumbnail(VOID_T)
{
    if (s_cam_ui.thumb_canvas == NULL || s_cam_ui.thumbnail == NULL) {
        return;
    }

    if (s_cam_ui.thumb_canvas_buf != NULL) {
        tal_image_scale_buf_free(&(TAL_IMAGE_SCALE_OUT_T){.buf = s_cam_ui.thumb_canvas_buf});
        s_cam_ui.thumb_canvas_buf = NULL;
    }

    memset(s_cam_thumb_canvas_dummy, 0, sizeof(s_cam_thumb_canvas_dummy));
    lv_canvas_set_buffer(s_cam_ui.thumb_canvas, s_cam_thumb_canvas_dummy, 4, 4, LV_IMG_CF_TRUE_COLOR);
    lv_obj_add_flag(s_cam_ui.thumbnail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(s_cam_ui.thumb_canvas);
    lv_obj_invalidate(s_cam_ui.thumbnail);
    if (s_cam_ui.cam_scr != NULL) {
        lv_obj_invalidate(s_cam_ui.cam_scr);
    }
}

/**
 * @brief Show the camera screen (creates if needed)
 * @return none
 */
VOID_T ui_camera_show(VOID_T)
{
    if (s_cam_ui.cam_scr == NULL) {
        setup_scr_camera();
    }
    if (lv_scr_act() != s_cam_ui.cam_scr) {
        lv_scr_load(s_cam_ui.cam_scr);
    }
    if (s_cam_ui.thumbnail != NULL) {
        lv_obj_invalidate(s_cam_ui.thumbnail);
    }
    if (s_cam_ui.thumb_canvas != NULL) {
        lv_obj_invalidate(s_cam_ui.thumb_canvas);
    }
}

/**
 * @brief Hide camera screen and switch to target screen
 * @param[in] target_scr screen to switch to (NULL to stay)
 * @return none
 */
VOID_T ui_camera_hide(lv_obj_t *target_scr)
{
    if (target_scr && s_cam_ui.cam_scr && lv_scr_act() == s_cam_ui.cam_scr) {
        lv_scr_load(target_scr);
    }
}

/**
 * @brief Get the camera screen object
 * @return camera screen pointer, NULL if not created
 */
lv_obj_t *ui_camera_get_scr(VOID_T)
{
    return s_cam_ui.cam_scr;
}
