#include "ui_i18n.h"
#include <stddef.h>

extern const char *const ui_i18n_strings[UI_LANG_MAX][UI_TEXT_MAX];

static ui_lang_t s_lang;

void ui_i18n_init(ui_lang_t lang) {
    s_lang = lang;
}

void ui_i18n_set_lang(ui_lang_t lang) {
    if (lang < UI_LANG_MAX) {
        s_lang = lang;
    }
}

ui_lang_t ui_i18n_get_lang(void) {
    return s_lang;
}

const char *ui_i18n_text(ui_i18n_key_t key) {
    if (key >= UI_TEXT_MAX) return "";
    const char *text = ui_i18n_strings[s_lang][key];
    if (!text) {
        text = ui_i18n_strings[UI_LANG_EN][key];
    }
    return text ? text : "";
}
