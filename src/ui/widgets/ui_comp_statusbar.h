#ifndef __UI_COMP_STATUSBAR_H__
#define __UI_COMP_STATUSBAR_H__

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    UI_STATUSBAR_MODE_FULL,
    UI_STATUSBAR_MODE_MINIMAL,
} ui_statusbar_mode_t;

typedef struct {
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  calendar_count;
    uint8_t  alarm_count;
    uint8_t  wifi_strength;
    uint8_t  battery_level;
    bool     is_charging;
    bool     wifi_connected;
} ui_comp_statusbar_props_t;

void      ui_comp_statusbar_init(void);
void      ui_comp_statusbar_refresh(const ui_comp_statusbar_props_t *props);
void      ui_comp_statusbar_set_visible(bool visible);
void      ui_comp_statusbar_set_mode(ui_statusbar_mode_t mode);
/* Re-order the singleton to the top of lv_layer_top() so it renders above an
 * overlay (e.g. the pulldown panel) created after it. No-op before init. */
void      ui_comp_statusbar_raise(void);

#endif /* __UI_COMP_STATUSBAR_H__ */
