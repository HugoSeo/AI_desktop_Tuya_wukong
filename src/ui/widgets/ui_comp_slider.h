#ifndef __UI_COMP_SLIDER_H__
#define __UI_COMP_SLIDER_H__

#include "lvgl.h"
#include <stdint.h>

/**
 * @brief Create a styled slider with an optional icon.
 *
 * The slider is a horizontal LVGL slider with a hidden knob, rounded track,
 * and an optional icon positioned on the left.  Caller can override colours
 * with lv_obj_set_style_* after creation.
 *
 * @param parent   Parent LVGL object.
 * @param icon_src LVGL image source for the icon (NULL = no icon).
 * @param value    Initial slider value (0–100).
 * @return         The slider object (lv_slider).
 */
lv_obj_t *ui_comp_slider_create(lv_obj_t *parent, const void *icon_src, uint8_t value);

/**
 * @brief Update slider value without animation.
 */
void ui_comp_slider_set_value(lv_obj_t *slider, uint8_t value);

#endif /* __UI_COMP_SLIDER_H__ */
