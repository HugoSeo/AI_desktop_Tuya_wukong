#include "ui_comp_label.h"
#include "ui_theme.h"
#include <stdarg.h>
#include <stdio.h>

lv_obj_t *ui_comp_label_create_variant(lv_obj_t *parent, const char *text,
                                       ui_comp_label_variant_t variant) {
    lv_obj_t *lbl = lv_label_create(parent);
    switch (variant) {
        case UI_COMP_LABEL_TITLE:
            lv_obj_add_style(lbl, &ui_style_text_title, 0);
            break;
        case UI_COMP_LABEL_BODY:
            lv_obj_add_style(lbl, &ui_style_text, 0);
            break;
        case UI_COMP_LABEL_CAPTION:
            lv_obj_add_style(lbl, &ui_style_text_caption, 0);
            break;
    }
    if (text) lv_label_set_text(lbl, text);
    return lbl;
}

lv_obj_t *ui_comp_label_create(lv_obj_t *parent, const char *text) {
    return ui_comp_label_create_variant(parent, text, UI_COMP_LABEL_BODY);
}

lv_obj_t *ui_comp_label_create_i18n(lv_obj_t *parent, ui_i18n_key_t key) {
    return ui_comp_label_create(parent, ui_i18n_text(key));
}

void ui_comp_label_set_text(lv_obj_t *lbl, const char *text) {
    lv_label_set_text(lbl, text);
}

void ui_comp_label_set_text_fmt(lv_obj_t *lbl, const char *fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    lv_label_set_text(lbl, buf);
}

void ui_comp_label_set_i18n(lv_obj_t *lbl, ui_i18n_key_t key) {
    lv_label_set_text(lbl, ui_i18n_text(key));
}
