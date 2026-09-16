#ifndef __UI_COMP_KEYBOARD_H__
#define __UI_COMP_KEYBOARD_H__

#include "lvgl.h"
#include <stdbool.h>

/* Logical height before ui_adapt() scaling. */
#define UI_COMP_KEYBOARD_HEIGHT  196

typedef struct {
    lv_obj_t *textarea;
    bool      uppercase;
} ui_comp_keyboard_props_t;

/**
 * @brief Create a compact four-row English keyboard.
 *
 * The keyboard writes directly to the bound LVGL textarea. Its layout is:
 * 1234567890 / qwertyuiop / asdfghjkl / case + zxcvbnm + delete.
 *
 * @param parent Parent LVGL object.
 * @param props  Optional initial target and letter-case properties.
 * @return Native LVGL button-matrix object, or NULL on failure.
 */
lv_obj_t *ui_comp_keyboard_create(lv_obj_t *parent,
                                  const ui_comp_keyboard_props_t *props);

/** Bind or detach the textarea receiving keyboard input. */
void ui_comp_keyboard_set_textarea(lv_obj_t *keyboard, lv_obj_t *textarea);

/** Set/query the current letter case and refresh all displayed letter keys. */
void ui_comp_keyboard_set_uppercase(lv_obj_t *keyboard, bool uppercase);
bool ui_comp_keyboard_is_uppercase(const lv_obj_t *keyboard);

/** Re-resolve localized action labels and rebuild the current key map. */
void ui_comp_keyboard_refresh(lv_obj_t *keyboard);

/** Hide and asynchronously delete the keyboard. Parent deletion is also safe. */
void ui_comp_keyboard_destroy(lv_obj_t *keyboard);

#endif /* __UI_COMP_KEYBOARD_H__ */
