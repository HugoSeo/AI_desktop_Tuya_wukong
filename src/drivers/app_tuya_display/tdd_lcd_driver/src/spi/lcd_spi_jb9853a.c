#include "tuya_app_config.h"
#include "tal_display_service.h"

//! set direction 284 * 240

static const DISPLAY_INIT_SEQ_T  jb9853a_init_seq[] = {
    {.type = TY_INIT_RST,   .reset = {500, 500, 500}},
    {.type = TY_INIT_REG,   .reg = {.r = 0x11, .len = 0}},
    {.type = TY_INIT_DELAY, .delay_time = 120},
    {.type = TY_INIT_REG,   .reg = {.r = 0x2A, .len = 4,  .v = {0x00, 0x00, 0x00, 0xef}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0x2B, .len = 4,  .v = {0x00, 0x28, 0x01, 0x17}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xB2, .len = 5,  .v = {0x0c, 0x0c, 0x00, 0x33, 0x33}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0x20, .len = 0}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xB7, .len = 1,  .v = {0x56}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xBB, .len = 1,  .v = {0x18}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC0, .len = 1,  .v = {0x2c}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC2, .len = 1,  .v = {0x01}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC3, .len = 1,  .v = {0x1f}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC4, .len = 1,  .v = {0x20}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC6, .len = 1,  .v = {0x0f}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xD0, .len = 2,  .v = {0xa6, 0xa1}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE0, .len = 14, .v = {0xd0, 0x0d, 0x14, 0x0b, 0x0b, 0x07, 0x3a, 0x44, 0x50, 0x08, 0x13, 0x13, 0x2d, 0x32}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE1, .len = 14, .v = {0xd0, 0x0d, 0x14, 0x0b, 0x0b, 0x07, 0x3a, 0x44, 0x50, 0x08, 0x13, 0x13, 0x2d, 0x32}}},
#if TUYA_LCD_ROTATION_VAL == 0
    {.type = TY_INIT_REG,   .reg = {.r = 0x36, .len = 1,  .v = {0x0}}},         //默认方向，  240*284
#elif TUYA_LCD_ROTATION_VAL == 1
    {.type = TY_INIT_REG,   .reg = {.r = 0x36, .len = 1,  .v = {0x60}}},        //90°        284*240
#elif TUYA_LCD_ROTATION_VAL == 2
    {.type = TY_INIT_REG,   .reg = {.r = 0x36, .len = 1,  .v = {0xC0}}},        //180°       240*284
#elif TUYA_LCD_ROTATION_VAL == 3
    {.type = TY_INIT_REG,   .reg = {.r = 0x36, .len = 1,  .v = {0xA0}}},        //270°       284*240
#endif
    {.type = TY_INIT_REG,   .reg = {.r = 0x3A, .len = 1,  .v = {0x55}}},            //RGB565
    //{.type = TY_INIT_REG,   .reg = {.r = 0x3A, .len = 1,  .v = {0x66}}},            //RGB666
    {.type = TY_INIT_REG,   .reg = {.r = 0xE7, .len = 1,  .v = {0x00}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0x21, .len = 0}},
    {.type = TY_INIT_REG,   .reg = {.r = 0x29, .len = 0}},
    {.type = TY_INIT_CONF_END}    //END
};

const ty_display_device_s  lcd_spi_jb9853a_device = {
    .type = DISPLAY_SPI,
    .name = "spi_jb9853a",
    .spi = {

#if TUYA_LCD_ROTATION_VAL == 0
        .width = 240,
        .height = 284,
        .x_offset = 0,
        .y_offset = 0,
#elif TUYA_LCD_ROTATION_VAL == 1
        .width = 284,
        .height = 240,
        .x_offset = 0,
        .y_offset = 0,
#elif TUYA_LCD_ROTATION_VAL == 2
        .width = 240,
        .height = 284,
        /* 180°(MADCTL 0xC0): MV=0 -> 320轴走RASET, MY=1 -> 可视区偏到GRAM末端,
         * y方向补 gap = 320-284 = 36 = 0x24 */
        .x_offset = 0,
        .y_offset = 0x24,
#elif TUYA_LCD_ROTATION_VAL == 3
        .width = 284,
        .height = 240,
        /* 270°(MADCTL 0xA0): MV=1 -> 320轴走CASET, MY=1 -> 可视区偏到GRAM末端,
         * x方向补 gap = 320-284 = 36 = 0x24 */
        .x_offset = 0x24,
        .y_offset = 0,
#endif
        .pixel_fmt = TY_PIXEL_FMT_RGB565,
        .cfg = {
            .role = TUYA_SPI_ROLE_MASTER,
            .mode = TUYA_SPI_MODE0,
            .type = TUYA_SPI_AUTO_TYPE,
            .databits = TUYA_SPI_DATA_BIT8,
            .bitorder = TUYA_SPI_ORDER_MSB2LSB,
            .freq_hz = 51000000,
            .spi_dma_flags = 1
        },
        .init_seq = jb9853a_init_seq,
        .display_cfg = NULL
    }
};
