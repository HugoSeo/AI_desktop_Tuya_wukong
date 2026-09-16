#ifndef __UI_COMP_ICON_H__
#define __UI_COMP_ICON_H__

#include "lvgl.h"

lv_obj_t *ui_comp_icon_create(lv_obj_t *parent, const void *src);
void      ui_comp_icon_set_src(lv_obj_t *icon, const void *src);

#endif /* __UI_COMP_ICON_H__ */
