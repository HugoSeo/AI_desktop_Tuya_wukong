/**
 * @file tuya_device_board.c
 * @brief L511_Y7PM_BOARD board initialization.
 *
 * 显示部分：(284x240, rotation=1)，走 SPI0 默认 pinmux 组，
 * 复用 tal_display + tef_player，不启用 LVGL。
 *
 * @version 0.2
 * @date 2026-08-17
 *
 * @copyright Copyright (c) tuya.inc 2023-2026
 */
#include "tuya_device_board.h"

#if defined(ENABLE_TUYA_DISPLAY) && (ENABLE_TUYA_DISPLAY == 1)
#include "tuya_cloud_types.h"
#include "tuya_board_config.h"
#include "tuya_display_hw.h"
#include "wukong_demo_lcd_anim.h"
#include "tal_log.h"
#endif

/* ============================================================
 *
 * SPI0 走默认 pinmux 组，不需要 tkl_io_pinmux_config()。
 * 只需要给屏留 RST / RS(DC) / BL / (可选) PWR 四个 GPIO。
 * 下面的默认值仅是模板：**请按你板子的实际接线改**，改完这几个宏即可。
 * ============================================================ */
#ifndef L511_LCD_SPI_PORT
#define L511_LCD_SPI_PORT           TUYA_SPI_NUM_0
#endif

/* Reset：低有效 */
#ifndef L511_LCD_RST_PIN
#define L511_LCD_RST_PIN            TUYA_GPIO_NUM_51
#endif
#ifndef L511_LCD_RST_ACTIVE
#define L511_LCD_RST_ACTIVE         TUYA_GPIO_LEVEL_LOW
#endif

/* RS / DC：cmd 阶段 = 低（active_level 定义为 cmd 时电平） */
#ifndef L511_LCD_RS_PIN
#define L511_LCD_RS_PIN             TUYA_GPIO_NUM_32
#endif
#ifndef L511_LCD_RS_ACTIVE
#define L511_LCD_RS_ACTIVE          TUYA_GPIO_LEVEL_LOW
#endif

#if defined(ENABLE_TUYA_DISPLAY) && (ENABLE_TUYA_DISPLAY == 1)

static const ty_display_cfg s_lcd_gpio_cfg = {
    .spi_cfg = {
        .port = L511_LCD_SPI_PORT,
        .reset = {
            .pin = L511_LCD_RST_PIN,
            .active_level = L511_LCD_RST_ACTIVE,
        },
        .bl = {
            .pin = TUYA_GPIO_NUM_MAX,
        },
        .power_ctrl = {
            .pin = TUYA_GPIO_NUM_MAX,
        },
        .rs = {
            .pin = L511_LCD_RS_PIN,
            .active_level = L511_LCD_RS_ACTIVE,
        },
        .soft_cs = {
            .pin = TUYA_GPIO_NUM_MAX,
        },
    },
};

static const tuya_display_hw_cfg_t s_display_cfg = {
    .lcd = {
        .lcd_name = TUYA_LCD_IC_NAME,        
        .width = TUYA_LCD_WIDTH,             
        .height = TUYA_LCD_HEIGHT,           
        .rotation = TUYA_LCD_ROTATION_VAL,       
        .lcd_cfg = &s_lcd_gpio_cfg,
    },
    .tp = { .tp_device = NULL, .tp_cfg = NULL }
};

const tuya_display_hw_cfg_t *tuya_board_get_display_cfg(void)
{
    return &s_display_cfg;
}
#endif /* ENABLE_TUYA_DISPLAY */

OPERATE_RET tuya_device_board_init(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;

#if defined(ENABLE_TUYA_DISPLAY) && (ENABLE_TUYA_DISPLAY == 1)
    /* 1) 面板 HW init —— 之前这里直接丢返回码，屏没起来也不报警。 */
    const tuya_display_hw_cfg_t *disp_cfg = tuya_board_get_display_cfg();
    PR_NOTICE("[BOARD] display init: ic=\"%s\" w=%u h=%u rot=%u",
              disp_cfg->lcd.lcd_name,
              (unsigned)disp_cfg->lcd.width,
              (unsigned)disp_cfg->lcd.height,
              (unsigned)disp_cfg->lcd.rotation);
    rt = tuya_display_hw_init(disp_cfg);
    if (rt != OPRT_OK) {
        PR_ERR("[BOARD] tuya_display_hw_init FAILED rt=%d (LCD IC \"%s\" not "
               "registered? tdd_lcd driver missing / wrong IC name / SPI init "
               "fail). Panel will not light up.",
               rt, disp_cfg->lcd.lcd_name);
        return rt;
    }

    /* 2) 背光点亮 */
    tuya_display_hw_backlight_open();

    /* 3) 起 LCD 动图 demo 任务：内部 tef_player_init + tef_player_play */
    wukong_demo_lcd_anim_start();
#endif

    return rt;
}
