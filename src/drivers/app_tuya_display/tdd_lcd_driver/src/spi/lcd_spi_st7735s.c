#include "tuya_app_config.h"

#ifdef TUYA_MULTI_TYPES_LCD
#include "tal_display_service.h"

//! ST7735S LCD driver - 128 x 128

static const DISPLAY_INIT_SEQ_T  st7735s_init_seq[] = {
    {.type = TY_INIT_RST,   .reset = {{500}, {500}, {500}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0x01, .len = 0}},                          // Software reset
    {.type = TY_INIT_DELAY, .delay_time = 100},                                     // Delay 100ms
    {.type = TY_INIT_REG,   .reg = {.r = 0x11, .len = 0}},                          // Sleep out
    {.type = TY_INIT_DELAY, .delay_time = 100},                                     // Delay 100ms
    {.type = TY_INIT_REG,   .reg = {.r = 0xB1, .len = 3,  .v = {0x02, 0x35, 0x36}}},    // Frame rate control
    {.type = TY_INIT_REG,   .reg = {.r = 0xB2, .len = 3,  .v = {0x02, 0x35, 0x36}}},    // Frame rate control
    {.type = TY_INIT_REG,   .reg = {.r = 0xB3, .len = 6,  .v = {0x02, 0x35, 0x36, 0x02, 0x35, 0x36}}},  // Frame rate control
    {.type = TY_INIT_REG,   .reg = {.r = 0xB4, .len = 1,  .v = {0x00}}},            // Display inversion
    {.type = TY_INIT_REG,   .reg = {.r = 0xC0, .len = 3,  .v = {0xA2, 0x02, 0x84}}},    // Power control
    {.type = TY_INIT_REG,   .reg = {.r = 0xC1, .len = 1,  .v = {0xC5}}},            // Power control
    {.type = TY_INIT_REG,   .reg = {.r = 0xC2, .len = 2,  .v = {0x0D, 0x00}}},      // Power control
    {.type = TY_INIT_REG,   .reg = {.r = 0xC3, .len = 2,  .v = {0x8A, 0x2A}}},      // Power control
    {.type = TY_INIT_REG,   .reg = {.r = 0xC4, .len = 2,  .v = {0x8D, 0xEE}}},      // Power control
    {.type = TY_INIT_REG,   .reg = {.r = 0xC5, .len = 1,  .v = {0x02}}},            // VCOM control
    {.type = TY_INIT_REG,   .reg = {.r = 0xE0, .len = 16, .v = {0x12, 0x1C, 0x10, 0x18, 0x33, 0x2C, 0x25, 0x28, 0x28, 0x27, 0x2F, 0x3C, 0x00, 0x03, 0x03, 0x10}}},  // Gamma
    {.type = TY_INIT_REG,   .reg = {.r = 0xE1, .len = 16, .v = {0x12, 0x1C, 0x10, 0x18, 0x2D, 0x28, 0x23, 0x28, 0x28, 0x26, 0x2F, 0x3B, 0x00, 0x03, 0x03, 0x10}}},  // Gamma
    {.type = TY_INIT_REG,   .reg = {.r = 0x3A, .len = 1,  .v = {0x05}}},            // Pixel format RGB565
    {.type = TY_INIT_REG,   .reg = {.r = 0x36, .len = 1,  .v = {0xC8}}},            // Memory access control
    {.type = TY_INIT_REG,   .reg = {.r = 0x29, .len = 0}},                          // Display ON
    {.type = TY_INIT_REG,   .reg = {.r = 0x2C, .len = 0}},                          // Memory write
    {.type = TY_INIT_CONF_END}    //END
};

const ty_display_device_s  lcd_spi_st7735s_device = {
    .type = DISPLAY_SPI,
    .name = "spi_st7735s",
    .spi = {
        .width = 128,
        .height = 128,
        .pixel_fmt = TY_PIXEL_FMT_RGB565,
        .cfg = {
            .role = TUYA_SPI_ROLE_MASTER,
            .mode = TUYA_SPI_MODE0,
            .type = TUYA_SPI_AUTO_TYPE,
            .databits = TUYA_SPI_DATA_BIT8,
            .bitorder = TUYA_SPI_ORDER_MSB2LSB,
            .freq_hz = 48000000,
            .spi_dma_flags = 1
        },
        .init_seq = st7735s_init_seq,
        .display_cfg = NULL
    }
};
#endif
