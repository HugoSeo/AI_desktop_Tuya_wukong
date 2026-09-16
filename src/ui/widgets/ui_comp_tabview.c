#include "ui_comp_tabview.h"

#include "ui_adaptive.h"
#include "ui_theme.h"

lv_obj_t *ui_comp_tabview_create(lv_obj_t *parent,
                                 const ui_comp_tabview_props_t *props)
{
    if (!parent || !props) {
        return NULL;
    }

    lv_obj_t *tabview = lv_tabview_create(parent, props->tab_pos,
                                          props->tab_size);
    if (!tabview) {
        return NULL;
    }

    lv_obj_set_style_bg_color(tabview, UI_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tabview, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_set_style_bg_color(tab_btns, UI_COLOR_BG_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tab_btns, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(tab_btns, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab_btns, ui_adapt(6), LV_PART_MAIN);
    lv_obj_set_style_pad_column(tab_btns, ui_adapt(6), LV_PART_MAIN);

    /* Inactive tab: transparent rounded pill with secondary text. */
    lv_obj_set_style_radius(tab_btns, ui_adapt(8), LV_PART_ITEMS);
    lv_obj_set_style_border_width(tab_btns, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(tab_btns, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(tab_btns, UI_COLOR_PRIMARY, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(tab_btns, LV_OPA_TRANSP, LV_PART_ITEMS);
    lv_obj_set_style_text_color(tab_btns, UI_COLOR_TEXT_SEC, LV_PART_ITEMS);

    /* Pressed and selected states match the clock segmented control. */
    lv_obj_set_style_bg_color(tab_btns, UI_COLOR_PRIMARY,
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(tab_btns, LV_OPA_10,
                            LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(tab_btns, UI_COLOR_PRIMARY,
                                LV_PART_ITEMS | LV_STATE_PRESSED);

    lv_obj_set_style_bg_color(tab_btns, UI_COLOR_PRIMARY,
                              LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(tab_btns, LV_OPA_20,
                            LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(tab_btns, UI_COLOR_PRIMARY,
                                LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(tab_btns, 0,
                                  LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(tab_btns, LV_OPA_20,
                            LV_PART_ITEMS | LV_STATE_CHECKED |
                            LV_STATE_PRESSED);

    lv_obj_t *content = lv_tabview_get_content(tabview);
    lv_obj_set_style_bg_color(content, UI_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, LV_PART_MAIN);

    return tabview;
}
