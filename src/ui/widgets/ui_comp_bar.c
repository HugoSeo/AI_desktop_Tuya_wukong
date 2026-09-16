#include "ui_comp_bar.h"
#include "ui_theme.h"
#include "ui_adaptive.h"

lv_obj_t *ui_comp_bar_create(lv_obj_t *parent, int32_t min, int32_t max) {
    lv_obj_t *bar = lv_bar_create(parent);
    lv_bar_set_range(bar, min, max);
    lv_obj_set_size(bar, LV_PCT(100), ui_adapt(8));
    lv_obj_add_style(bar, &ui_style_bar, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, UI_COLOR_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, ui_adapt(UI_RADIUS_FULL), LV_PART_INDICATOR);
    return bar;
}

void ui_comp_bar_set_value(lv_obj_t *bar, int32_t value) {
    lv_bar_set_value(bar, value, LV_ANIM_OFF);
}

void ui_comp_bar_set_value_anim(lv_obj_t *bar, int32_t value, uint32_t duration_ms) {
    (void)duration_ms;
    lv_bar_set_value(bar, value, LV_ANIM_ON);
}
