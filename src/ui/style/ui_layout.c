#include "ui_layout.h"

static lv_flex_align_t to_lv_align(ui_align_t a) {
    switch (a) {
        case UI_ALIGN_START:         return LV_FLEX_ALIGN_START;
        case UI_ALIGN_CENTER:        return LV_FLEX_ALIGN_CENTER;
        case UI_ALIGN_END:           return LV_FLEX_ALIGN_END;
        case UI_ALIGN_SPACE_BETWEEN: return LV_FLEX_ALIGN_SPACE_BETWEEN;
        case UI_ALIGN_SPACE_AROUND:  return LV_FLEX_ALIGN_SPACE_AROUND;
        case UI_ALIGN_SPACE_EVENLY:  return LV_FLEX_ALIGN_SPACE_EVENLY;
        default:                     return LV_FLEX_ALIGN_START;
    }
}

static lv_obj_t *create_flex(lv_obj_t *parent, lv_flex_flow_t flow) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(obj, flow);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    return obj;
}

lv_obj_t *ui_layout_row(lv_obj_t *parent) {
    return create_flex(parent, LV_FLEX_FLOW_ROW);
}

lv_obj_t *ui_layout_col(lv_obj_t *parent) {
    return create_flex(parent, LV_FLEX_FLOW_COLUMN);
}

lv_obj_t *ui_layout_full_screen(lv_obj_t *parent) {
    lv_obj_t *obj = create_flex(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    return obj;
}

void ui_layout_gap(lv_obj_t *obj, int16_t gap) {
    int16_t g = ui_adapt(gap);
    lv_obj_set_style_pad_row(obj, g, 0);
    lv_obj_set_style_pad_column(obj, g, 0);
}

void ui_layout_padding(lv_obj_t *obj, int16_t h, int16_t v) {
    lv_obj_set_style_pad_hor(obj, ui_adapt(h), 0);
    lv_obj_set_style_pad_ver(obj, ui_adapt(v), 0);
}

void ui_layout_align(lv_obj_t *obj, ui_align_t main_axis, ui_align_t cross_axis) {
    lv_obj_set_flex_align(obj, to_lv_align(main_axis),
                          to_lv_align(cross_axis), to_lv_align(cross_axis));
}

void ui_layout_grow(lv_obj_t *child, uint8_t grow) {
    lv_obj_set_flex_grow(child, grow);
}

void ui_layout_wrap(lv_obj_t *obj) {
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW_WRAP);
}
