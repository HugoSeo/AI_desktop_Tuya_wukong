/**
 * @file wukong_demo_lcd_anim.h
 * @brief L511_Y7PM 的 SPI LCD 动图 demo — 不跑 LVGL，直接用 tef_player 解压推屏。
 *
 * @copyright Copyright (c) tuya.inc 2026
 */
#ifndef __WUKONG_DEMO_LCD_ANIM_H__
#define __WUKONG_DEMO_LCD_ANIM_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 起 LCD 动图 demo。异步，很快返回；实际播放在内部 helper task 中。
 *        必须在 tuya_display_hw_init() 之后调用。多次调用只生效第一次。
 */
OPERATE_RET wukong_demo_lcd_anim_start(void);

#ifdef __cplusplus
}
#endif

#endif /* __WUKONG_DEMO_LCD_ANIM_H__ */
