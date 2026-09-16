#ifndef __UI_COMP_POPUP_H__
#define __UI_COMP_POPUP_H__

#include "lvgl.h"
#include <stdbool.h>

typedef enum {
    UI_COMP_POPUP_INFO = 0,
    UI_COMP_POPUP_CONFIRM,
    UI_COMP_POPUP_LOADING,
    UI_COMP_POPUP_TOAST,
    UI_COMP_POPUP_INFO_DELETE,
} ui_comp_popup_type_t;

typedef void (*ui_comp_popup_cb_t)(bool confirmed);

void ui_comp_popup_show(ui_comp_popup_type_t type, const char *title,
                        const char *msg, ui_comp_popup_cb_t cb);
void ui_comp_popup_toast(const char *msg, uint32_t duration_ms);
void ui_comp_popup_dismiss(void);

#endif /* __UI_COMP_POPUP_H__ */
