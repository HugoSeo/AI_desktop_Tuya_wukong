#ifndef __UI_COMP_BAR_H__
#define __UI_COMP_BAR_H__

#include "lvgl.h"
#include <stdint.h>

lv_obj_t *ui_comp_bar_create(lv_obj_t *parent, int32_t min, int32_t max);
void      ui_comp_bar_set_value(lv_obj_t *bar, int32_t value);
void      ui_comp_bar_set_value_anim(lv_obj_t *bar, int32_t value, uint32_t duration_ms);

#endif /* __UI_COMP_BAR_H__ */
