#ifndef __UI_COMP_PICTURE_H__
#define __UI_COMP_PICTURE_H__

/**
 * @file ui_comp_picture.h
 * @brief Reusable RGB565 image canvas — renders one decoded picture or thumbnail.
 *
 * This is a PURE renderer: it draws an RGB565 buffer at native size, centred in
 * its parent. It does NOT decode JPEG and does NOT touch any business/platform
 * header — JPEG decode and album access live in services/ui_svc_picture.c.
 *
 * The same widget backs both album use-cases (see CONTEXT.md):
 *   - Album viewer big picture → parent is the screen, buffer is service-owned
 *                               (pass free_fn so the widget releases it on replace).
 *   - Grid thumbnail          → parent is a fixed cell, buffer is borrowed from the
 *                               service's thumb list (pass free_fn = NULL, zero-copy).
 *
 * Buffer ownership is decided per call by @ref ui_comp_picture_set_rgb565's free_fn:
 *   - free_fn == NULL : widget only references @p data; caller keeps ownership.
 *   - free_fn != NULL : widget owns @p data and calls free_fn(data) when the image
 *                       is replaced, cleared, or the widget is deleted.
 * Keeping the free function as a callback lets the widget stay allocator-agnostic
 * (no tal_ or psram dependency in the widget layer).
 */

#include "lvgl.h"
#include <stdint.h>

/** Releases a pixel buffer previously handed to the widget (e.g. tal_psram_free). */
typedef void (*ui_picture_free_fn)(void *buf);

/**
 * @brief Create an empty picture canvas centred in @p parent.
 * @param[in] parent LVGL parent (screen for big picture, cell for thumbnail)
 * @return the canvas object, or NULL on failure
 */
lv_obj_t *ui_comp_picture_create(lv_obj_t *parent);

/**
 * @brief Show an RGB565 image at native size, centred in the parent.
 * @param[in] obj     widget returned by ui_comp_picture_create
 * @param[in] w       image width in pixels
 * @param[in] h       image height in pixels
 * @param[in] data    RGB565 pixel buffer (LV_IMG_CF_TRUE_COLOR)
 * @param[in] free_fn NULL to borrow @p data; non-NULL to transfer ownership
 * @return none
 */
void ui_comp_picture_set_rgb565(lv_obj_t *obj, uint16_t w, uint16_t h,
                                uint8_t *data, ui_picture_free_fn free_fn);

/**
 * @brief Release any owned buffer and show nothing (hidden 4x4 placeholder).
 * @param[in] obj widget returned by ui_comp_picture_create
 * @return none
 */
void ui_comp_picture_clear(lv_obj_t *obj);

#endif /* __UI_COMP_PICTURE_H__ */
