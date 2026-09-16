#ifndef __UI_ADAPTIVE_H__
#define __UI_ADAPTIVE_H__

#include <stdint.h>
#include <stdbool.h>

#define UI_ADAPT_BASE_W  320
#define UI_ADAPT_BASE_H  480

void     ui_adaptive_init(uint16_t screen_w, uint16_t screen_h);
int16_t  ui_adapt(int16_t base_value);
uint16_t ui_adapt_screen_w(void);
uint16_t ui_adapt_screen_h(void);
bool     ui_adapt_is_small(void);

#endif /* __UI_ADAPTIVE_H__ */
