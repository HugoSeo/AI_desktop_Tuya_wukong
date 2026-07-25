#include "tuya_app_config.h"

#ifdef TUYA_MULTI_TYPES_LCD
#include "tal_display_service.h"

static const DISPLAY_INIT_SEQ_T  st7789v2_init_seq[] = {
    {.type = TY_INIT_RST,   .reset = {{500}, {500}, {500}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0x11, .len = 0}},
    {.type = TY_INIT_DELAY, .delay_time = 120},
    {.type = TY_INIT_REG,   .reg = {.r = 0x36, .len = 1,  .v = {0xA0}}},    //顺时针转90度
    {.type = TY_INIT_REG,   .reg = {.r = 0x3A, .len = 1,  .v = {0x05}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xB0, .len = 1,  .v = {0x00}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xB2, .len = 5,  .v = {0x05, 0x05, 0x00, 0x33, 0x33}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xB7, .len = 1,  .v = {0x75}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xBB, .len = 1,  .v = {0x22}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC0, .len = 1,  .v = {0x2C}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC2, .len = 1,  .v = {0x01}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC3, .len = 1,  .v = {0x13}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC4, .len = 1,  .v = {0x20}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC6, .len = 1,  .v = {0x05}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xD0, .len = 2,  .v = {0xA4, 0xA1}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xD6, .len = 1,  .v = {0xA1}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE0, .len = 14, .v = {0xD0, 0x05, 0x0A, 0x09, 0x08, 0x05, 0x2E, 0x44, 0x45, 0x0F, 0x17, 0x16, 0x2B, 0x33}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE1, .len = 14, .v = {0xD0, 0x05, 0x0A, 0x09, 0x08, 0x05, 0x2E, 0x43, 0x45, 0x0F, 0x16, 0x16, 0x2B, 0x33}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0x2A, .len = 4,  .v = {0x00, 0x00, 0x00, 0xEF}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0x2B, .len = 4,  .v = {0x00, 0x00, 0x01, 0x3F}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0x21, .len = 0}},
    {.type = TY_INIT_REG,   .reg = {.r = 0x29, .len = 0}},
    {.type = TY_INIT_REG,   .reg = {.r = 0x2C, .len = 0}},
    {.type = TY_INIT_CONF_END}    //END
};

const ty_display_device_s  lcd_spi_st7789v2_device = {
    .type = DISPLAY_SPI,
    .name = "spi_st7789v2",
    .spi = {
        .width = 320,
        .height = 240,
        .pixel_fmt = TY_PIXEL_FMT_RGB565,
        .cfg = {
            .role = TUYA_SPI_ROLE_MASTER,
            .mode = TUYA_SPI_MODE0,
            .type = TUYA_SPI_AUTO_TYPE,
            .databits = TUYA_SPI_DATA_BIT8,
            .bitorder = TUYA_SPI_ORDER_MSB2LSB,
            .freq_hz = 60000000,
            .spi_dma_flags = 1
        },
        .init_seq = st7789v2_init_seq,
        .display_cfg = NULL
    }
};
#endif
