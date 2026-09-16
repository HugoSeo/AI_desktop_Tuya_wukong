#ifndef __UI_COMP_CARD_H__
#define __UI_COMP_CARD_H__

#include "lvgl.h"

lv_obj_t *ui_comp_card_create(lv_obj_t *parent);
lv_obj_t *ui_comp_card_create_titled(lv_obj_t *parent, const char *title);
void      ui_comp_card_set_title(lv_obj_t *card, const char *title);

#endif /* __UI_COMP_CARD_H__ */
