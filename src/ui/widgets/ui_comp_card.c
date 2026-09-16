#include "ui_comp_card.h"
#include "ui_theme.h"
#include "ui_layout.h"
#include "ui_comp_label.h"

lv_obj_t *ui_comp_card_create(lv_obj_t *parent) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_layout(card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_style(card, &ui_style_card, 0);
    return card;
}

lv_obj_t *ui_comp_card_create_titled(lv_obj_t *parent, const char *title) {
    lv_obj_t *card = ui_comp_card_create(parent);
    ui_comp_label_create_variant(card, title, UI_COMP_LABEL_TITLE);
    return card;
}

void ui_comp_card_set_title(lv_obj_t *card, const char *title) {
    lv_obj_t *lbl = lv_obj_get_child(card, 0);
    if (lbl) lv_label_set_text(lbl, title);
}
