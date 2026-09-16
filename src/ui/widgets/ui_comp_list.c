#include "ui_comp_list.h"
#include "ui_theme.h"
#include "ui_layout.h"
#include "ui_comp_label.h"
#include "ui_adaptive.h"

void ui_comp_list_clear(lv_obj_t *list) {
    lv_obj_clean(list);
}

static void build_items(lv_obj_t *list, const ui_comp_list_item_t *items, uint16_t count) {
    for (uint16_t i = 0; i < count; i++) {
        lv_obj_t *row = ui_layout_row(list);
        ui_layout_align(row, UI_ALIGN_START, UI_ALIGN_CENTER);
        ui_layout_gap(row, UI_SPACE_SM);
        lv_obj_set_style_pad_ver(row, ui_adapt(UI_SPACE_SM), 0);

        if (items[i].icon) {
            lv_obj_t *img = lv_img_create(row);
            lv_img_set_src(img, items[i].icon);
        }

        lv_obj_t *text_col = ui_layout_col(row);
        ui_layout_grow(text_col, 1);

        if (items[i].text)
            ui_comp_label_create(text_col, items[i].text);
        if (items[i].secondary)
            ui_comp_label_create_variant(text_col, items[i].secondary, UI_COMP_LABEL_CAPTION);

        if (items[i].on_click) {
            lv_obj_add_event_cb(row, items[i].on_click, LV_EVENT_CLICKED, NULL);
            lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        }
    }
}

lv_obj_t *ui_comp_list_create(lv_obj_t *parent, const ui_comp_list_item_t *items, uint16_t count) {
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    if (items && count > 0) build_items(list, items, count);
    return list;
}

void ui_comp_list_set_items(lv_obj_t *list, const ui_comp_list_item_t *items, uint16_t count) {
    ui_comp_list_clear(list);
    build_items(list, items, count);
}
