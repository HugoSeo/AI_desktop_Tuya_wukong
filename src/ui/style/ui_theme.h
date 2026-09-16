#ifndef __UI_THEME_H__
#define __UI_THEME_H__

#include "lvgl.h"
#include "ui_adaptive.h"
#include "tuya_app_config.h"   /* UI_WUKONG_PAGES */

#define UI_COLOR_PRIMARY        lv_color_hex(0x4A90D9)
#define UI_COLOR_PRIMARY_DARK   lv_color_hex(0x2E6BAD)
#define UI_COLOR_BG             lv_color_hex(0x1A1A1A)
#define UI_COLOR_BG_CARD        lv_color_hex(0x2A2A2A)
#define UI_COLOR_TEXT           lv_color_hex(0xFFFFFF)
#define UI_COLOR_TEXT_SEC       lv_color_hex(0xAAAAAA)
#define UI_COLOR_SUCCESS        lv_color_hex(0x4CAF50)
#define UI_COLOR_WARNING        lv_color_hex(0xFFC107)
#define UI_COLOR_ERROR          lv_color_hex(0xF44336)
#define UI_COLOR_LINK           lv_color_hex(0x5B9BD5)
#define UI_COLOR_STATUSBAR_BG      lv_color_hex(0xB8BDDE)
#define UI_COLOR_STATUSBAR_BG_OPA  15
#define UI_COLOR_STATUSBAR_ICON    lv_color_hex(0xFFFFFF)
#define UI_COLOR_STATUSBAR_ACCENT  lv_color_hex(0xFFF37B)
#define UI_COLOR_BATTERY_OK        lv_color_hex(0x4CD964)
#define UI_COLOR_BATTERY_WARN      lv_color_hex(0xFFC107)
#define UI_COLOR_BATTERY_LOW       lv_color_hex(0xF44336)
#define UI_COLOR_SLIDER_TRACK      lv_color_hex(0xB8BDDE)
#define UI_COLOR_SLIDER_FILL       lv_color_hex(0xFFF37B)

#if defined(UI_WUKONG_PAGES) && UI_WUKONG_PAGES
LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular18);
#define UI_FONT_DEFAULT (&AlibabaPuHuiTi3_Regular18)
#else
/* 板级 UI 构建不编 assets/font/ 大字库,退回 LVGL 内建字体(与 lv_conf.h 的 LV_FONT_DEFAULT 一致) */
#define UI_FONT_DEFAULT (&lv_font_montserrat_14)
#endif

#define UI_RADIUS_SM    4
#define UI_RADIUS_MD    8
#define UI_RADIUS_LG    16
#define UI_RADIUS_FULL  LV_RADIUS_CIRCLE

/* Default diameter for circular icon buttons (passed through ui_adapt()) */
#define UI_BTN_CIRCLE_SIZE  56

#define UI_SPACE_XS     4
#define UI_SPACE_SM     8
#define UI_SPACE_MD     12
#define UI_SPACE_LG     16
#define UI_SPACE_XL     24

extern lv_style_t ui_style_screen;
extern lv_style_t ui_style_card;
extern lv_style_t ui_style_btn;
extern lv_style_t ui_style_btn_pressed;
extern lv_style_t ui_style_btn_sec;
extern lv_style_t ui_style_btn_sec_pressed;
extern lv_style_t ui_style_btn_ghost_pressed;
extern lv_style_t ui_style_btn_circle;          /* transparent fill + ring border */
extern lv_style_t ui_style_btn_circle_pressed;  /* pressed: translucent fill */
extern lv_style_t ui_style_text;
extern lv_style_t ui_style_text_title;
extern lv_style_t ui_style_text_caption;
extern lv_style_t ui_style_bar;
extern lv_style_t ui_style_slider;

typedef enum {
    UI_THEME_DARK = 0,
    UI_THEME_LIGHT,
} ui_theme_mode_t;

void ui_theme_init(void);
void ui_theme_set_mode(ui_theme_mode_t mode);

#endif /* __UI_THEME_H__ */
