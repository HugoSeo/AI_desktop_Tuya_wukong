/**
 * @file ui_comp_link.c
 * @brief Clickable hyperlink label widget (see ui_comp_link.h).
 *
 * Layer: widgets/ — depends only on LVGL + style. Per-instance state (the copied
 * click argument + callback) lives on the label user_data, allocated from LVGL's
 * heap so the widget stays allocator-agnostic, and is released on the LVGL delete
 * event. The click handler copies cb/arg to locals before invoking, so the
 * callback may safely delete the link (or its parent) from within.
 */

#include "ui_comp_link.h"
#include "ui_theme.h"

typedef struct {
    ui_link_cb_t cb;
    void        *arg;   /* owned copy (LVGL heap), or NULL */
} link_ctx_t;

static void __link_delete_cb(lv_event_t *e)
{
    link_ctx_t *ctx = (link_ctx_t *)lv_event_get_user_data(e);
    if (ctx == NULL) {
        return;
    }
    if (ctx->arg) {
        lv_mem_free(ctx->arg);
    }
    lv_mem_free(ctx);
}

static void __link_click_cb(lv_event_t *e)
{
    link_ctx_t *ctx = (link_ctx_t *)lv_event_get_user_data(e);
    if (ctx == NULL || ctx->cb == NULL) {
        return;
    }
    ui_link_cb_t cb  = ctx->cb;
    void        *arg = ctx->arg;
    cb(arg);
}

lv_obj_t *ui_comp_link_create(lv_obj_t *parent, const char *text, ui_link_cb_t cb,
                              const void *arg, uint32_t arg_len)
{
    if (parent == NULL || text == NULL || cb == NULL) {
        return NULL;
    }

    link_ctx_t *ctx = lv_mem_alloc(sizeof(link_ctx_t));
    if (ctx == NULL) {
        return NULL;
    }
    ctx->cb  = cb;
    ctx->arg = NULL;

    if (arg && arg_len > 0) {
        ctx->arg = lv_mem_alloc(arg_len);
        if (ctx->arg == NULL) {
            lv_mem_free(ctx);
            return NULL;
        }
        lv_memcpy(ctx->arg, arg, arg_len);
    }

    lv_obj_t *label = lv_label_create(parent);
    if (label == NULL) {
        if (ctx->arg) {
            lv_mem_free(ctx->arg);
        }
        lv_mem_free(ctx);
        return NULL;
    }

    lv_label_set_text(label, text);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(label, UI_COLOR_LINK, 0);
    lv_obj_set_style_text_decor(label, LV_TEXT_DECOR_UNDERLINE, 0);

    lv_obj_set_user_data(label, ctx);
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(label, __link_click_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(label, __link_delete_cb, LV_EVENT_DELETE, ctx);

    return label;
}
