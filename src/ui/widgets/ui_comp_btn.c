#include "ui_comp_btn.h"
#include "ui_theme.h"
#include "ui_adaptive.h"

/* The default theme grows the button a few px on press. If the button sits
 * flush against its container edge, that growth would be clipped into a flat
 * edge. Let the parent show the overflow so callers don't each have to handle
 * this — the button owns its own press feedback. */
static void enable_press_overflow(lv_obj_t *parent) {
    if (parent) {
        lv_obj_add_flag(parent, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    }
}

/* Apply one of the rectangular fill styles (PRIMARY / SECONDARY / TEXT / ICON).
 * Shared by the plain styled button and the icon+text button. */
static void apply_btn_fill_style(lv_obj_t *btn, ui_comp_btn_style_t style) {
    /* Strip the default button theme first. It carries a drop shadow / padding
     * that makes the centered content look vertically offset; our own styles
     * below fully define the look, so a clean slate keeps content centered. */
    lv_obj_remove_style_all(btn);
    switch (style) {
        case UI_COMP_BTN_PRIMARY:
            lv_obj_add_style(btn, &ui_style_btn, 0);
            lv_obj_add_style(btn, &ui_style_btn_pressed, LV_STATE_PRESSED);
            break;
        case UI_COMP_BTN_SECONDARY:
            lv_obj_add_style(btn, &ui_style_btn_sec, 0);
            lv_obj_add_style(btn, &ui_style_btn_sec_pressed, LV_STATE_PRESSED);
            break;
        case UI_COMP_BTN_TEXT:
            lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(btn, 0, 0);
            lv_obj_set_style_pad_all(btn, ui_adapt(UI_SPACE_SM), 0);
            lv_obj_set_style_text_color(btn, UI_COLOR_PRIMARY, 0);
            lv_obj_add_style(btn, &ui_style_btn_ghost_pressed, LV_STATE_PRESSED);
            break;
        case UI_COMP_BTN_ICON:
        default:
            lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(btn, 0, 0);
            lv_obj_set_style_pad_all(btn, ui_adapt(UI_SPACE_XS), 0);
            lv_obj_add_style(btn, &ui_style_btn_ghost_pressed, LV_STATE_PRESSED);
            break;
    }
}

lv_obj_t *ui_comp_btn_create_styled(lv_obj_t *parent, const char *text,
                                    ui_comp_btn_style_t style, ui_comp_btn_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    enable_press_overflow(parent);
    apply_btn_fill_style(btn, style);
    if (text) {
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, text);
    }
    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    return btn;
}

lv_obj_t *ui_comp_btn_create(lv_obj_t *parent, const char *text, ui_comp_btn_cb_t cb) {
    return ui_comp_btn_create_styled(parent, text, UI_COMP_BTN_PRIMARY, cb);
}

lv_obj_t *ui_comp_btn_icon_create(lv_obj_t *parent, const void *icon_src, ui_comp_btn_cb_t cb) {
    lv_obj_t *btn = ui_comp_btn_create_styled(parent, NULL, UI_COMP_BTN_ICON, cb);
    lv_obj_t *img = lv_img_create(btn);
    lv_img_set_src(img, icon_src);
    return btn;
}

/* Build the circular frosted visual (an obj or btn) and center an icon in it. */
static void build_circle_visual(lv_obj_t *circle, const void *icon_src) {
    lv_coord_t d = ui_adapt(UI_BTN_CIRCLE_SIZE);
    lv_obj_set_size(circle, d, d);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
    if (icon_src) {
        lv_obj_t *img = lv_img_create(circle);
        lv_img_set_src(img, icon_src);
        lv_obj_center(img);
    }
}

lv_obj_t *ui_comp_btn_circle_create(lv_obj_t *parent, const void *icon_src,
                                    const char *text, ui_comp_btn_cb_t cb) {
    enable_press_overflow(parent);

    if (!text) {
        /* Icon-only: the button itself is the ring circle. Strip the default
         * button theme first — its padding/press transform would nudge the
         * centered icon off-center; we supply our own visuals. */
        lv_obj_t *btn = lv_btn_create(parent);
        lv_obj_remove_style_all(btn);
        lv_obj_add_style(btn, &ui_style_btn_circle, 0);
        lv_obj_add_style(btn, &ui_style_btn_circle_pressed, LV_STATE_PRESSED);
        build_circle_visual(btn, icon_src);
        if (cb) {
            lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
        }
        return btn;
    }

    /* With label: transparent column container [circle][label]; the whole unit
     * is clickable and dims on press. */
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);   /* drop default theme shadow/pad (see above) */
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_height(btn, LV_SIZE_CONTENT);
    lv_obj_set_width(btn, LV_SIZE_CONTENT);
    lv_obj_add_style(btn, &ui_style_btn_ghost_pressed, LV_STATE_PRESSED);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(btn, ui_adapt(UI_SPACE_XS), 0);

    lv_obj_t *circle = lv_obj_create(btn);
    lv_obj_remove_style_all(circle);
    lv_obj_add_style(circle, &ui_style_btn_circle, 0);
    build_circle_visual(circle, icon_src);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);

    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    return btn;
}

lv_obj_t *ui_comp_btn_circle_text_create(lv_obj_t *parent, const char *text,
                                         lv_color_t accent, ui_comp_btn_cb_t cb) {
    enable_press_overflow(parent);

    /* The button itself is the circle; the label sits centered inside it.
     * Strip the default button theme first (shadow/pad/press transform would
     * offset the centered label); we fully define the visual below. */
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn);

    lv_coord_t d = ui_adapt(60);
    lv_obj_set_size(btn, d, d);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, accent, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_20, 0);
    lv_obj_set_style_border_color(btn, accent, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, ui_adapt(2), 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    /* Same pressed-dim feedback the other circle buttons use. */
    lv_obj_add_style(btn, &ui_style_btn_circle_pressed, LV_STATE_PRESSED);

    /* Inner label centered INSIDE the circle. Created as the only label child so
     * ui_comp_btn_set_text() (which targets the first label child) retexts it. */
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, accent, 0);
    lv_obj_center(lbl);

    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    return btn;
}

lv_obj_t *ui_comp_btn_create_with_icon(lv_obj_t *parent, const void *icon_src, const char *text,
                                       ui_comp_btn_style_t style, ui_comp_btn_icon_pos_t pos,
                                       ui_comp_btn_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    enable_press_overflow(parent);
    apply_btn_fill_style(btn, style);

    bool vertical = (pos == UI_COMP_BTN_ICON_TOP);
    lv_obj_set_flex_flow(btn, vertical ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_coord_t gap = ui_adapt(UI_SPACE_SM);
    lv_obj_set_style_pad_column(btn, gap, 0);
    lv_obj_set_style_pad_row(btn, gap, 0);

    /* Create children in the order dictated by `pos`; flex lays them out. */
    bool icon_first = (pos != UI_COMP_BTN_ICON_RIGHT);
    lv_obj_t *img = NULL;
    lv_obj_t *lbl = NULL;
    if (icon_first && icon_src) {
        img = lv_img_create(btn);
        lv_img_set_src(img, icon_src);
    }
    if (text) {
        lbl = lv_label_create(btn);
        lv_label_set_text(lbl, text);
    }
    if (!icon_first && icon_src) {
        img = lv_img_create(btn);
        lv_img_set_src(img, icon_src);
    }

    if (cb) {
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    }
    return btn;
}

void ui_comp_btn_set_text(lv_obj_t *btn, const char *text) {
    /* Find the first label child — the label is not always child 0 (e.g. icon+text
     * buttons may put the icon first). */
    uint32_t cnt = lv_obj_get_child_cnt(btn);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(btn, i);
        if (child && lv_obj_check_type(child, &lv_label_class)) {
            lv_label_set_text(child, text);
            return;
        }
    }
}

void ui_comp_btn_set_enabled(lv_obj_t *btn, bool enabled) {
    if (enabled) {
        lv_obj_clear_state(btn, LV_STATE_DISABLED);
        lv_obj_set_style_opa(btn, LV_OPA_COVER, 0);
    } else {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
        lv_obj_set_style_opa(btn, LV_OPA_50, 0);
    }
}
