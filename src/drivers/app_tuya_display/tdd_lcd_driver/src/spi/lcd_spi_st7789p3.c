#include "tuya_app_config.h"

#ifdef TUYA_MULTI_TYPES_LCD
#include "tal_display_service.h"

//! set direction 320 * 172

static const DISPLAY_INIT_SEQ_T  st7789p3_init_seq[] = {
    {.type = TY_INIT_RST,   .reset = {{500}, {500}, {500}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0x11, .len = 0}},
    {.type = TY_INIT_DELAY, .delay_time = 100},                      // sleep
    {.type = TY_INIT_REG,   .reg = {.r = 0x36, .len = 1,  .v = {0xA0}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0x3A, .len = 1,  .v = {0x05}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xB2, .len = 5,  .v = {0x0C, 0x0C, 0x00, 0x33, 0x33}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xB7, .len = 1,  .v = {0x35}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xBB, .len = 1,  .v = {0x35}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC0, .len = 1,  .v = {0x2C}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC2, .len = 1,  .v = {0x01}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC3, .len = 1,  .v = {0x13}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC4, .len = 1,  .v = {0x20}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC6, .len = 1,  .v = {0x0F}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xD0, .len = 2,  .v = {0xA4, 0xA1}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xD6, .len = 1,  .v = {0xA1}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE0, .len = 14, .v = {0xF0, 0x00, 0x04, 0x04, 0x04, 0x05, 0x29, 0x33, 0x3E, 0x38, 0x12, 0x12, 0x28, 0x30}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE1, .len = 14, .v = {0xF0, 0x07, 0x0A, 0x0D, 0x0B, 0x07, 0x28, 0x33, 0x3E, 0x36, 0x14, 0x14, 0x29, 0x32}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0x11, .len = 0}},      // Exit sleep mode
    {.type = TY_INIT_REG,   .reg = {.r = 0x21, .len = 0}},      // Display inversion on
    {.type = TY_INIT_REG,   .reg = {.r = 0x29, .len = 0}},      // Display ON
    {.type = TY_INIT_CONF_END}    //END
};

const ty_display_device_s  lcd_spi_st7789p3_device = {
    .type = DISPLAY_SPI,
    .name = "spi_st7789p3",
    .spi = {
        .width = 320,
        .height = 172,
        .pixel_fmt = TY_PIXEL_FMT_RGB565,
        .cfg = {
            .role = TUYA_SPI_ROLE_MASTER,
            .mode = TUYA_SPI_MODE0,
            .type = TUYA_SPI_AUTO_TYPE,
            .databits = TUYA_SPI_DATA_BIT8,
            .bitorder = TUYA_SPI_ORDER_MSB2LSB,
            // 性能优化：从30MHz提升到48MHz
            // 理论帧时间：110KB / (48MHz/8) = 18.4ms ≈ 54 FPS
            // 考虑开销后实际：约43-45 FPS
            // 如果48MHz不稳定，可尝试：40MHz, 36MHz
            .freq_hz = 48000000,  // 48 MHz (原30MHz)
            .spi_dma_flags = 1
        },
        .init_seq = st7789p3_init_seq,
        .display_cfg = NULL
    }
};
#endif
