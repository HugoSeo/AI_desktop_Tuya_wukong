#include "ui_adaptive.h"

static uint16_t s_screen_w;
static uint16_t s_screen_h;
static uint16_t s_scale_num;

void ui_adaptive_init(uint16_t screen_w, uint16_t screen_h) {
    s_screen_w = screen_w;
    s_screen_h = screen_h;
    s_scale_num = (uint16_t)(((uint32_t)screen_w * 256u) / UI_ADAPT_BASE_W);
}

int16_t ui_adapt(int16_t base_value) {
    if (base_value == 0) return 0;
    return (int16_t)(((int32_t)base_value * s_scale_num + 128) / 256);
}

uint16_t ui_adapt_screen_w(void) { return s_screen_w; }
uint16_t ui_adapt_screen_h(void) { return s_screen_h; }
bool ui_adapt_is_small(void) { return s_screen_w <= 240; }
