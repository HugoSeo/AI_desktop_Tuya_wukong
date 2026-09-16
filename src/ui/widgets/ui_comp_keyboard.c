#include "ui_comp_keyboard.h"

#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_theme.h"

#define KEYBOARD_BUTTON_COUNT  38
#define KEYBOARD_MAP_COUNT     42
#define KEYBOARD_CASE_ID       29
#define KEYBOARD_DELETE_ID     37

typedef struct {
    lv_obj_t  *textarea;
    bool       uppercase;
    const char *map[KEYBOARD_MAP_COUNT];
} keyboard_ctx_t;

static const char *const s_numbers[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0"
};
static const char *const s_row_two_lower[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p"
};
static const char *const s_row_two_upper[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"
};
static const char *const s_row_three_lower[] = {
    "a", "s", "d", "f", "g", "h", "j", "k", "l"
};
static const char *const s_row_three_upper[] = {
    "A", "S", "D", "F", "G", "H", "J", "K", "L"
};
static const char *const s_row_four_lower[] = {
    "z", "x", "c", "v", "b", "n", "m"
};
static const char *const s_row_four_upper[] = {
    "Z", "X", "C", "V", "B", "N", "M"
};

static keyboard_ctx_t *keyboard_ctx_get(const lv_obj_t *keyboard)
{
    if (!keyboard || !lv_obj_check_type(keyboard, &lv_btnmatrix_class)) {
        return NULL;
    }
    return (keyboard_ctx_t *)lv_obj_get_user_data((lv_obj_t *)keyboard);
}

static void textarea_delete_cb(lv_event_t *e)
{
    keyboard_ctx_t *ctx = (keyboard_ctx_t *)lv_event_get_user_data(e);
    if (ctx && ctx->textarea == lv_event_get_target(e)) {
        ctx->textarea = NULL;
    }
}

static void keyboard_delete_cb(lv_event_t *e)
{
    keyboard_ctx_t *ctx = (keyboard_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) {
        return;
    }

    if (ctx->textarea) {
        lv_obj_remove_event_cb_with_user_data(ctx->textarea,
                                              textarea_delete_cb,
                                              ctx);
        ctx->textarea = NULL;
    }
    lv_mem_free(ctx);
}

static void append_keys(const char **map, uint8_t *index,
                        const char *const keys[], uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        map[(*index)++] = keys[i];
    }
}

static void rebuild_map(lv_obj_t *keyboard, keyboard_ctx_t *ctx)
{
    const char *const *row_two = ctx->uppercase ? s_row_two_upper : s_row_two_lower;
    const char *const *row_three = ctx->uppercase ? s_row_three_upper : s_row_three_lower;
    const char *const *row_four = ctx->uppercase ? s_row_four_upper : s_row_four_lower;
    uint8_t index = 0;

    append_keys(ctx->map, &index, s_numbers, 10);
    ctx->map[index++] = "\n";
    append_keys(ctx->map, &index, row_two, 10);
    ctx->map[index++] = "\n";
    append_keys(ctx->map, &index, row_three, 9);
    ctx->map[index++] = "\n";
    ctx->map[index++] = ui_i18n_text(ctx->uppercase ?
                                     UI_TEXT_KEYBOARD_LOWER :
                                     UI_TEXT_KEYBOARD_UPPER);
    append_keys(ctx->map, &index, row_four, 7);
    ctx->map[index++] = ui_i18n_text(UI_TEXT_KEYBOARD_DELETE);
    ctx->map[index] = "";

    lv_btnmatrix_set_map(keyboard, ctx->map);

    lv_btnmatrix_ctrl_t controls[KEYBOARD_BUTTON_COUNT];
    for (uint8_t i = 0; i < KEYBOARD_BUTTON_COUNT; i++) {
        controls[i] = 1 | LV_BTNMATRIX_CTRL_NO_REPEAT |
                      LV_BTNMATRIX_CTRL_CLICK_TRIG;
    }
    controls[KEYBOARD_CASE_ID] = 2 | LV_BTNMATRIX_CTRL_NO_REPEAT |
                                 LV_BTNMATRIX_CTRL_CLICK_TRIG;
    controls[KEYBOARD_DELETE_ID] = 2 | LV_BTNMATRIX_CTRL_NO_REPEAT |
                                   LV_BTNMATRIX_CTRL_CLICK_TRIG;
    if (ctx->uppercase) {
        controls[KEYBOARD_CASE_ID] |= LV_BTNMATRIX_CTRL_CHECKED;
    }
    lv_btnmatrix_set_ctrl_map(keyboard, controls);
}

static void keyboard_value_changed_cb(lv_event_t *e)
{
    lv_obj_t *keyboard = lv_event_get_target(e);
    keyboard_ctx_t *ctx = (keyboard_ctx_t *)lv_event_get_user_data(e);
    if (!ctx) {
        return;
    }

    uint16_t button_id = lv_btnmatrix_get_selected_btn(keyboard);
    if (button_id == LV_BTNMATRIX_BTN_NONE) {
        return;
    }
    if (button_id == KEYBOARD_CASE_ID) {
        ctx->uppercase = !ctx->uppercase;
        rebuild_map(keyboard, ctx);
        return;
    }
    if (!ctx->textarea) {
        return;
    }
    if (button_id == KEYBOARD_DELETE_ID) {
        lv_textarea_del_char(ctx->textarea);
        return;
    }

    const char *text = lv_btnmatrix_get_btn_text(keyboard, button_id);
    if (text && text[0] != '\0') {
        lv_textarea_add_text(ctx->textarea, text);
    }
}

lv_obj_t *ui_comp_keyboard_create(lv_obj_t *parent,
                                  const ui_comp_keyboard_props_t *props)
{
    if (!parent) {
        return NULL;
    }

    keyboard_ctx_t *ctx = lv_mem_alloc(sizeof(*ctx));
    if (!ctx) {
        return NULL;
    }
    lv_memset_00(ctx, sizeof(*ctx));
    ctx->uppercase = props ? props->uppercase : false;

    lv_obj_t *keyboard = lv_btnmatrix_create(parent);
    if (!keyboard) {
        lv_mem_free(ctx);
        return NULL;
    }

    lv_obj_remove_style_all(keyboard);
    lv_obj_set_size(keyboard, LV_PCT(100), ui_adapt(UI_COMP_KEYBOARD_HEIGHT));
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(keyboard, UI_COLOR_BG_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(keyboard, ui_adapt(UI_RADIUS_LG), LV_PART_MAIN);
    lv_obj_set_style_pad_all(keyboard, ui_adapt(6), LV_PART_MAIN);
    lv_obj_set_style_pad_row(keyboard, ui_adapt(UI_SPACE_XS), LV_PART_MAIN);
    lv_obj_set_style_pad_column(keyboard, ui_adapt(3), LV_PART_MAIN);

    lv_obj_set_style_bg_color(keyboard, UI_COLOR_STATUSBAR_BG, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_30, LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, UI_COLOR_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_radius(keyboard, ui_adapt(6), LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, UI_COLOR_PRIMARY,
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_50,
                            LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(keyboard, UI_COLOR_PRIMARY,
                              LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_30,
                            LV_PART_ITEMS | LV_STATE_CHECKED);

    lv_obj_set_user_data(keyboard, ctx);
    lv_obj_add_event_cb(keyboard, keyboard_value_changed_cb,
                        LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_add_event_cb(keyboard, keyboard_delete_cb, LV_EVENT_DELETE, ctx);
    rebuild_map(keyboard, ctx);
    ui_comp_keyboard_set_textarea(keyboard, props ? props->textarea : NULL);
    return keyboard;
}

void ui_comp_keyboard_set_textarea(lv_obj_t *keyboard, lv_obj_t *textarea)
{
    keyboard_ctx_t *ctx = keyboard_ctx_get(keyboard);
    if (!ctx || (textarea && !lv_obj_check_type(textarea, &lv_textarea_class))) {
        return;
    }
    if (ctx->textarea == textarea) {
        return;
    }

    if (ctx->textarea) {
        lv_obj_remove_event_cb_with_user_data(ctx->textarea,
                                              textarea_delete_cb,
                                              ctx);
    }
    ctx->textarea = textarea;
    if (ctx->textarea) {
        lv_obj_add_event_cb(ctx->textarea, textarea_delete_cb,
                            LV_EVENT_DELETE, ctx);
    }
}

void ui_comp_keyboard_set_uppercase(lv_obj_t *keyboard, bool uppercase)
{
    keyboard_ctx_t *ctx = keyboard_ctx_get(keyboard);
    if (!ctx || ctx->uppercase == uppercase) {
        return;
    }
    ctx->uppercase = uppercase;
    rebuild_map(keyboard, ctx);
}

bool ui_comp_keyboard_is_uppercase(const lv_obj_t *keyboard)
{
    keyboard_ctx_t *ctx = keyboard_ctx_get(keyboard);
    return ctx ? ctx->uppercase : false;
}

void ui_comp_keyboard_refresh(lv_obj_t *keyboard)
{
    keyboard_ctx_t *ctx = keyboard_ctx_get(keyboard);
    if (ctx) {
        rebuild_map(keyboard, ctx);
    }
}

void ui_comp_keyboard_destroy(lv_obj_t *keyboard)
{
    if (!keyboard_ctx_get(keyboard)) {
        return;
    }
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_del_async(keyboard);
}
