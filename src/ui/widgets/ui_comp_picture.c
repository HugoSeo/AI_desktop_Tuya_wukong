/**
 * @file ui_comp_picture.c
 * @brief RGB565 image canvas widget (see ui_comp_picture.h).
 *
 * Layer: widgets/ — depends only on LVGL. Per-instance bookkeeping (owned buffer
 * + its free function) is stored in the canvas user_data and released on the
 * LVGL delete event, so callers can simply lv_obj_del() the parent and the
 * widget self-cleans its non-LVGL resources.
 */

#include "ui_comp_picture.h"

/* Dummy buffer so lv_canvas always has a valid pointer after the real (owned)
 * buffer is freed. Read-only, all-zero, shared across cleared instances. */
static uint8_t s_picture_dummy[4 * 4 * 2];

/* Per-instance state kept on the canvas user_data (allocated via LVGL's heap so
 * the widget stays allocator-agnostic — the big pixel buffer is never ours). */
typedef struct {
    uint8_t           *owned;    /* non-NULL when we must free on replace/clear/delete */
    ui_picture_free_fn free_fn;  /* how to free @owned */
} picture_ctx_t;

static void __picture_release_owned(picture_ctx_t *ctx)
{
    if (ctx && ctx->owned && ctx->free_fn) {
        ctx->free_fn(ctx->owned);
    }
    if (ctx) {
        ctx->owned   = NULL;
        ctx->free_fn = NULL;
    }
}

static void __picture_delete_cb(lv_event_t *e)
{
    picture_ctx_t *ctx = (picture_ctx_t *)lv_event_get_user_data(e);
    __picture_release_owned(ctx);
    if (ctx) {
        lv_mem_free(ctx);
    }
}

lv_obj_t *ui_comp_picture_create(lv_obj_t *parent)
{
    if (parent == NULL) {
        return NULL;
    }

    picture_ctx_t *ctx = lv_mem_alloc(sizeof(picture_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }
    ctx->owned   = NULL;
    ctx->free_fn = NULL;

    lv_obj_t *canvas = lv_canvas_create(parent);
    if (canvas == NULL) {
        lv_mem_free(ctx);
        return NULL;
    }

    lv_obj_set_style_border_width(canvas, 0, 0);
    lv_obj_set_user_data(canvas, ctx);
    lv_obj_add_event_cb(canvas, __picture_delete_cb, LV_EVENT_DELETE, ctx);

    /* Start cleared so the canvas pointer is always valid. */
    ui_comp_picture_clear(canvas);
    return canvas;
}

void ui_comp_picture_set_rgb565(lv_obj_t *obj, uint16_t w, uint16_t h,
                                uint8_t *data, ui_picture_free_fn free_fn)
{
    if (obj == NULL || data == NULL || w == 0 || h == 0) {
        return;
    }

    picture_ctx_t *ctx = (picture_ctx_t *)lv_obj_get_user_data(obj);

    /* Release the previous owned buffer unless the caller re-sets the same one. */
    if (ctx && ctx->owned && ctx->owned != data) {
        __picture_release_owned(ctx);
    }

    lv_canvas_set_buffer(obj, data, w, h, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(obj, w, h);
    lv_obj_center(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);

    if (ctx) {
        ctx->owned   = free_fn ? data : NULL;
        ctx->free_fn = free_fn;
    }
}

void ui_comp_picture_clear(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }

    picture_ctx_t *ctx = (picture_ctx_t *)lv_obj_get_user_data(obj);
    __picture_release_owned(ctx);

    lv_canvas_set_buffer(obj, s_picture_dummy, 4, 4, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_size(obj, 4, 4);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
