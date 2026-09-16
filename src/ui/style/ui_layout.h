#ifndef __UI_LAYOUT_H__
#define __UI_LAYOUT_H__

#include "lvgl.h"
#include "ui_adaptive.h"

typedef enum {
    UI_ALIGN_START = 0,
    UI_ALIGN_CENTER,
    UI_ALIGN_END,
    UI_ALIGN_SPACE_BETWEEN,
    UI_ALIGN_SPACE_AROUND,
    UI_ALIGN_SPACE_EVENLY,
} ui_align_t;

lv_obj_t *ui_layout_row(lv_obj_t *parent);
lv_obj_t *ui_layout_col(lv_obj_t *parent);
lv_obj_t *ui_layout_full_screen(lv_obj_t *parent);

void ui_layout_gap(lv_obj_t *obj, int16_t gap);
void ui_layout_padding(lv_obj_t *obj, int16_t h, int16_t v);
void ui_layout_align(lv_obj_t *obj, ui_align_t main_axis, ui_align_t cross_axis);
void ui_layout_grow(lv_obj_t *child, uint8_t grow);
void ui_layout_wrap(lv_obj_t *obj);

#endif /* __UI_LAYOUT_H__ */
