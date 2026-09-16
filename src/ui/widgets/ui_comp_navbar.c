#include "ui_comp_navbar.h"
#include "ui_theme.h"
#include "ui_layout.h"
#include "ui_comp_label.h"
#include "ui_adaptive.h"

static void nav_item_click(lv_event_t *e) {
    ui_page_id_t *target = lv_event_get_user_data(e);
    if (target) ui_route_replace(*target);
}

lv_obj_t *ui_comp_navbar_create(lv_obj_t *parent, const ui_comp_navbar_item_t *items, uint8_t count) {
    lv_obj_t *bar = ui_layout_row(parent);
    lv_obj_set_size(bar, LV_PCT(100), ui_adapt(48));
    ui_layout_align(bar, UI_ALIGN_SPACE_EVENLY, UI_ALIGN_CENTER);
    lv_obj_set_style_bg_color(bar, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

    for (uint8_t i = 0; i < count; i++) {
        lv_obj_t *item = ui_layout_col(bar);
        ui_layout_align(item, UI_ALIGN_CENTER, UI_ALIGN_CENTER);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);

        if (items[i].icon) {
            lv_obj_t *img = lv_img_create(item);
            lv_img_set_src(img, items[i].icon);
        }
        if (items[i].label) {
            ui_comp_label_create_variant(item, items[i].label, UI_COMP_LABEL_CAPTION);
        }
        lv_obj_add_event_cb(item, nav_item_click, LV_EVENT_CLICKED,
                            (void *)&items[i].target);
    }
    return bar;
}

void ui_comp_navbar_set_active(lv_obj_t *nav, uint8_t index) {
    uint32_t count = lv_obj_get_child_cnt(nav);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *child = lv_obj_get_child(nav, (int32_t)i);
        if (i == index) {
            lv_obj_set_style_text_color(child, UI_COLOR_PRIMARY, 0);
        } else {
            lv_obj_set_style_text_color(child, UI_COLOR_TEXT_SEC, 0);
        }
    }
}
