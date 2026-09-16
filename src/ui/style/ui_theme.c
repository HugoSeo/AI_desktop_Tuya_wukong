#include "ui_theme.h"

lv_style_t ui_style_screen;
lv_style_t ui_style_card;
lv_style_t ui_style_btn;
lv_style_t ui_style_btn_pressed;
lv_style_t ui_style_btn_sec;
lv_style_t ui_style_btn_sec_pressed;
lv_style_t ui_style_btn_ghost_pressed;
lv_style_t ui_style_btn_circle;
lv_style_t ui_style_btn_circle_pressed;
lv_style_t ui_style_text;
lv_style_t ui_style_text_title;
lv_style_t ui_style_text_caption;
lv_style_t ui_style_bar;
lv_style_t ui_style_slider;

static void init_dark_theme(void) {
    lv_style_init(&ui_style_screen);
    lv_style_set_bg_color(&ui_style_screen, UI_COLOR_BG);
    lv_style_set_bg_opa(&ui_style_screen, LV_OPA_COVER);
    lv_style_set_text_font(&ui_style_screen, UI_FONT_DEFAULT);

    lv_style_init(&ui_style_card);
    lv_style_set_bg_color(&ui_style_card, UI_COLOR_BG_CARD);
    lv_style_set_bg_opa(&ui_style_card, LV_OPA_COVER);
    lv_style_set_radius(&ui_style_card, ui_adapt(UI_RADIUS_MD));
    lv_style_set_pad_all(&ui_style_card, ui_adapt(UI_SPACE_MD));

    lv_style_init(&ui_style_btn);
    lv_style_set_bg_color(&ui_style_btn, UI_COLOR_PRIMARY);
    lv_style_set_bg_opa(&ui_style_btn, LV_OPA_COVER);
    lv_style_set_radius(&ui_style_btn, ui_adapt(UI_RADIUS_SM));
    lv_style_set_pad_hor(&ui_style_btn, ui_adapt(UI_SPACE_LG));
    lv_style_set_pad_ver(&ui_style_btn, ui_adapt(UI_SPACE_SM));
    lv_style_set_text_color(&ui_style_btn, UI_COLOR_TEXT);

    /* Primary pressed: darken the fill */
    lv_style_init(&ui_style_btn_pressed);
    lv_style_set_bg_color(&ui_style_btn_pressed, UI_COLOR_PRIMARY_DARK);

    lv_style_init(&ui_style_btn_sec);
    lv_style_set_bg_opa(&ui_style_btn_sec, LV_OPA_TRANSP);
    lv_style_set_border_color(&ui_style_btn_sec, UI_COLOR_PRIMARY);
    lv_style_set_border_width(&ui_style_btn_sec, 1);
    lv_style_set_radius(&ui_style_btn_sec, ui_adapt(UI_RADIUS_SM));
    lv_style_set_pad_hor(&ui_style_btn_sec, ui_adapt(UI_SPACE_LG));
    lv_style_set_pad_ver(&ui_style_btn_sec, ui_adapt(UI_SPACE_SM));
    lv_style_set_text_color(&ui_style_btn_sec, UI_COLOR_PRIMARY);

    /* Secondary pressed: subtle primary fill behind the outline */
    lv_style_init(&ui_style_btn_sec_pressed);
    lv_style_set_bg_color(&ui_style_btn_sec_pressed, UI_COLOR_PRIMARY);
    lv_style_set_bg_opa(&ui_style_btn_sec_pressed, LV_OPA_20);

    /* Text / icon pressed: dim the whole button */
    lv_style_init(&ui_style_btn_ghost_pressed);
    lv_style_set_opa(&ui_style_btn_ghost_pressed, LV_OPA_70);

    /* Circular icon button: transparent fill + a ring border at rest.
     * A translucent fill only appears while pressed (see _pressed below). */
    lv_style_init(&ui_style_btn_circle);
    lv_style_set_bg_opa(&ui_style_btn_circle, LV_OPA_TRANSP);
    lv_style_set_border_color(&ui_style_btn_circle, UI_COLOR_STATUSBAR_BG);
    lv_style_set_border_opa(&ui_style_btn_circle, LV_OPA_50);
    lv_style_set_border_width(&ui_style_btn_circle, 2);
    lv_style_set_radius(&ui_style_btn_circle, LV_RADIUS_CIRCLE);
    lv_style_set_pad_all(&ui_style_btn_circle, 0);

    /* Circle pressed: translucent fill appears; reverts to transparent on release */
    lv_style_init(&ui_style_btn_circle_pressed);
    lv_style_set_bg_color(&ui_style_btn_circle_pressed, UI_COLOR_STATUSBAR_BG);
    lv_style_set_bg_opa(&ui_style_btn_circle_pressed, LV_OPA_30);

    lv_style_init(&ui_style_text);
    lv_style_set_text_color(&ui_style_text, UI_COLOR_TEXT);

    lv_style_init(&ui_style_text_title);
    lv_style_set_text_color(&ui_style_text_title, UI_COLOR_TEXT);

    lv_style_init(&ui_style_text_caption);
    lv_style_set_text_color(&ui_style_text_caption, UI_COLOR_TEXT_SEC);

    lv_style_init(&ui_style_bar);
    lv_style_set_bg_color(&ui_style_bar, UI_COLOR_BG_CARD);
    lv_style_set_bg_opa(&ui_style_bar, LV_OPA_COVER);
    lv_style_set_radius(&ui_style_bar, ui_adapt(UI_RADIUS_FULL));

    lv_style_init(&ui_style_slider);
    lv_style_set_bg_color(&ui_style_slider, UI_COLOR_SLIDER_TRACK);
    lv_style_set_bg_opa(&ui_style_slider, 71);  /* 28% */
    lv_style_set_radius(&ui_style_slider, ui_adapt(UI_RADIUS_FULL));
}

void ui_theme_init(void) {
    init_dark_theme();
}

void ui_theme_set_mode(ui_theme_mode_t mode) {
    (void)mode;
}
