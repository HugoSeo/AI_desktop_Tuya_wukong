#include "ui_comp_slider.h"
#include "ui_theme.h"
#include "ui_adaptive.h"

lv_obj_t *ui_comp_slider_create(lv_obj_t *parent, const void *icon_src, uint8_t value)
{
    lv_obj_t *sli = lv_slider_create(parent);
    lv_obj_set_size(sli, LV_PCT(100), ui_adapt(45));
    lv_slider_set_range(sli, 0, 100);
    lv_slider_set_value(sli, value, LV_ANIM_OFF);

    /* Track (MAIN) — use shared slider style */
    lv_obj_add_style(sli, &ui_style_slider, LV_PART_MAIN);

    /* Indicator (filled portion) */
    lv_obj_set_style_bg_color(sli, UI_COLOR_SLIDER_FILL, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sli, ui_adapt(UI_RADIUS_FULL), LV_PART_INDICATOR);

    /* Hide the knob */
    lv_obj_set_style_bg_opa(sli, LV_OPA_TRANSP, LV_PART_KNOB);

    lv_obj_clear_flag(sli, LV_OBJ_FLAG_GESTURE_BUBBLE);

    /* Optional icon on the left */
    if (icon_src) {
        lv_obj_t *img = lv_img_create(sli);
        lv_img_set_src(img, icon_src);
        lv_obj_set_pos(img, ui_adapt(12), (ui_adapt(45) - ui_adapt(24)) / 2);
        lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    }

    return sli;
}

void ui_comp_slider_set_value(lv_obj_t *slider, uint8_t value)
{
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
}
