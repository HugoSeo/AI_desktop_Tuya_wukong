/**
 * @file tuya_device_board.c
 * @author www.tuya.com
 * @brief tuya_device_board module is used to
 * @version 0.1
 * @date 2022-10-28
 *
 * @copyright Copyright (c) tuya.inc 2022
 *
 */
#include "tuya_device_board.h"

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
#include "tuya_cloud_types.h"
#include "tuya_board_config.h"   /* TUYA_LCD_IC_NAME / WIDTH / HEIGHT / ROTATION */
#include "tuya_display_hw.h"
#include "tuya_ai_display.h"
#include "ui_app.h"
#endif

/***********************************************************
************************macro define************************
***********************************************************/

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
/* Display HW config for T5AI_BOARD_EVB — translated from the old
 * gui/display/tuya_ai_display.c `T5AI_BOARD_EVB` block (spi_st7789, 240x240).
 * SPI0 stays on the default pin group (G0: SCK 14 / MOSI 16 / MISO 17), so no
 * tkl_io_pinmux_config is needed — the display only uses GPIO 5/6/7/17 and the
 * one overlap (MISO 17) is driven as the rs line, matching the old behaviour.
 * No touch panel. */
static const ty_display_cfg s_lcd_gpio_cfg = {
    .spi_cfg = {
        .port = TUYA_SPI_NUM_0,
        .reset = {
            .pin = TUYA_GPIO_NUM_6,
            .active_level = TUYA_GPIO_LEVEL_LOW,
        },
        .bl = {
            .pin = TUYA_GPIO_NUM_5,
            .active_level = TUYA_GPIO_LEVEL_HIGH,
        },
        .power_ctrl = {
            .pin = TUYA_GPIO_NUM_7,
            .active_level = TUYA_GPIO_LEVEL_HIGH,
        },
        .rs = {
            .pin = TUYA_GPIO_NUM_17,
            .active_level = TUYA_GPIO_LEVEL_LOW,
        },
        .soft_cs = {
            .pin = TUYA_GPIO_NUM_MAX,
            .active_level = TUYA_GPIO_LEVEL_LOW,
        },
    },
};

static const tuya_display_hw_cfg_t s_display_cfg = {
    .lcd = {
        .lcd_name = TUYA_LCD_IC_NAME,    /* "spi_st7789" */
        .width = TUYA_LCD_WIDTH,         /* 240 */
        .height = TUYA_LCD_HEIGHT,       /* 240 */
        .rotation = TUYA_LCD_ROTATION,   /* 0 */
        .lcd_cfg = &s_lcd_gpio_cfg,
    },
    .tp = {                              /* EVB has no touch panel */
        .tp_device = NULL,
        .tp_cfg = NULL,
    },
};

const tuya_display_hw_cfg_t *tuya_board_get_display_cfg(void)
{
    return &s_display_cfg;
}

extern void app_ui_init(void);
extern void app_ui_msg_handler(TY_DISPLAY_MSG_T *msg);
#endif

/**
 * @brief evb board initialization
 *
 * @param[in] none
 *
 * @return OPRT_OK on success. Others on error, please refer to "tuya_error_code.h".
 */
OPERATE_RET tuya_device_board_init(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
    /* Display HW init收口至 board；注册 EVB 自有 UI（xiaozhi_app）。 */
    tuya_display_hw_init(tuya_board_get_display_cfg());
    ui_app_register_board_ui(app_ui_init, app_ui_msg_handler);
#endif

    return rt;
}
