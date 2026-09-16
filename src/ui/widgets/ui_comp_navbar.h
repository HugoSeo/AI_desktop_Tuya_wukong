#ifndef __UI_COMP_NAVBAR_H__
#define __UI_COMP_NAVBAR_H__

#include "lvgl.h"
#include "ui_route.h"

typedef struct {
    const void  *icon;
    const char  *label;
    ui_page_id_t target;
} ui_comp_navbar_item_t;

lv_obj_t *ui_comp_navbar_create(lv_obj_t *parent, const ui_comp_navbar_item_t *items, uint8_t count);
void      ui_comp_navbar_set_active(lv_obj_t *nav, uint8_t index);

#endif /* __UI_COMP_NAVBAR_H__ */
