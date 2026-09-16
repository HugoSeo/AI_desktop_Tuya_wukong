#ifndef __UI_COMP_BTN_H__
#define __UI_COMP_BTN_H__

#include "lvgl.h"
#include <stdbool.h>

typedef enum {
    UI_COMP_BTN_PRIMARY = 0,
    UI_COMP_BTN_SECONDARY,
    UI_COMP_BTN_TEXT,
    UI_COMP_BTN_ICON,
    UI_COMP_BTN_CIRCLE,
} ui_comp_btn_style_t;

/* Icon placement for icon + text buttons */
typedef enum {
    UI_COMP_BTN_ICON_LEFT = 0,
    UI_COMP_BTN_ICON_RIGHT,
    UI_COMP_BTN_ICON_TOP,
} ui_comp_btn_icon_pos_t;

typedef void (*ui_comp_btn_cb_t)(lv_event_t *e);

lv_obj_t *ui_comp_btn_create(lv_obj_t *parent, const char *text, ui_comp_btn_cb_t cb);
lv_obj_t *ui_comp_btn_create_styled(lv_obj_t *parent, const char *text,
                                    ui_comp_btn_style_t style, ui_comp_btn_cb_t cb);
lv_obj_t *ui_comp_btn_icon_create(lv_obj_t *parent, const void *icon_src, ui_comp_btn_cb_t cb);

/* Circular frosted icon button (iPhone lock-screen style). Pass text = NULL for
 * an icon-only circle, or a string to render a label below the circle. */
lv_obj_t *ui_comp_btn_circle_create(lv_obj_t *parent, const void *icon_src,
                                    const char *text, ui_comp_btn_cb_t cb);

/* Circular action button with the text centered INSIDE the circle (not below).
 * `accent` tints the ring border + text; the fill is a translucent accent.
 * Used for timer/pomodoro transport controls. */
lv_obj_t *ui_comp_btn_circle_text_create(lv_obj_t *parent, const char *text,
                                         lv_color_t accent, ui_comp_btn_cb_t cb);

/* Button holding both an icon and text. `style` selects the fill
 * (PRIMARY / SECONDARY / TEXT); `pos` selects icon placement. */
lv_obj_t *ui_comp_btn_create_with_icon(lv_obj_t *parent, const void *icon_src, const char *text,
                                       ui_comp_btn_style_t style, ui_comp_btn_icon_pos_t pos,
                                       ui_comp_btn_cb_t cb);

void ui_comp_btn_set_text(lv_obj_t *btn, const char *text);
void ui_comp_btn_set_enabled(lv_obj_t *btn, bool enabled);

#endif /* __UI_COMP_BTN_H__ */
