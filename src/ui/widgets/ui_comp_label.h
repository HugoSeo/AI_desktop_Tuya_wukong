#ifndef __UI_COMP_LABEL_H__
#define __UI_COMP_LABEL_H__

#include "lvgl.h"
#include "ui_i18n.h"

typedef enum {
    UI_COMP_LABEL_TITLE = 0,
    UI_COMP_LABEL_BODY,
    UI_COMP_LABEL_CAPTION,
} ui_comp_label_variant_t;

lv_obj_t *ui_comp_label_create(lv_obj_t *parent, const char *text);
lv_obj_t *ui_comp_label_create_i18n(lv_obj_t *parent, ui_i18n_key_t key);
lv_obj_t *ui_comp_label_create_variant(lv_obj_t *parent, const char *text,
                                       ui_comp_label_variant_t variant);
void ui_comp_label_set_text(lv_obj_t *lbl, const char *text);
void ui_comp_label_set_text_fmt(lv_obj_t *lbl, const char *fmt, ...);
void ui_comp_label_set_i18n(lv_obj_t *lbl, ui_i18n_key_t key);

#endif /* __UI_COMP_LABEL_H__ */
