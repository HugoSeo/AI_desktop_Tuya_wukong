#ifndef __UI_COMP_LIST_H__
#define __UI_COMP_LIST_H__

#include "lvgl.h"
#include <stdint.h>

typedef void (*ui_comp_list_click_cb_t)(lv_event_t *e);

typedef struct {
    const char              *text;
    const char              *secondary;
    const void              *icon;
    ui_comp_list_click_cb_t  on_click;
} ui_comp_list_item_t;

lv_obj_t *ui_comp_list_create(lv_obj_t *parent, const ui_comp_list_item_t *items, uint16_t count);
void      ui_comp_list_set_items(lv_obj_t *list, const ui_comp_list_item_t *items, uint16_t count);
void      ui_comp_list_clear(lv_obj_t *list);

#endif /* __UI_COMP_LIST_H__ */
