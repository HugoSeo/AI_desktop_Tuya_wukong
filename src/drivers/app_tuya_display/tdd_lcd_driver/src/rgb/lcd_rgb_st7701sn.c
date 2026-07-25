#include "tuya_app_config.h"

#ifdef TUYA_MULTI_TYPES_LCD
#include "tal_display_service.h"

static const DISPLAY_INIT_SEQ_T  st7701sn_init_seq[] = {
    {.type = TY_INIT_RST,   .reset = {{10}, {20}, {120}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xFF, .len = 5,  .v = {0x77, 0x01, 0x00, 0x00, 0x13}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xEF, .len = 1,  .v = {0x08}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xFF, .len = 5,  .v = {0x77, 0x01, 0x00, 0x00, 0x10}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC0, .len = 2,  .v = {0xE9, 0x03}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC1, .len = 2,  .v = {0x11, 0x02}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC3, .len = 1,  .v = {0x02}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC2, .len = 2,  .v = {0x01, 0x06}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xCC, .len = 1,  .v = {0x18}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xB0, .len = 16, .v = {0x00, 0x0D, 0x14, 0x0D, 0x10, 0x05, 0x02, 0x08, 0x08, 0x1E, 0x05, 0x13, 0x11, 0xA3, 0x29, 0x18}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xB1, .len = 16, .v = {0x00, 0x0C, 0x14, 0x0C, 0x10, 0x05, 0x03, 0x08, 0x07, 0x20, 0x05, 0x13, 0x11, 0xA4, 0x29, 0x18}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xFF, .len = 5,  .v = {0x77, 0x01, 0x00, 0x00, 0x11}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xB0, .len = 1,  .v = {0x6C}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xB1, .len = 1,  .v = {0x4F}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xB2, .len = 1,  .v = {0x89}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xB3, .len = 1,  .v = {0x80}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xB5, .len = 1,  .v = {0x4E}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xB7, .len = 1,  .v = {0x85}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xB8, .len = 1,  .v = {0x20}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xB9, .len = 2,  .v = {0x00, 0x13}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC0, .len = 1,  .v = {0x09}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC1, .len = 1,  .v = {0x78}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xC2, .len = 1,  .v = {0x78}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xD0, .len = 1,  .v = {0x88}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xEE, .len = 1,  .v = {0x42}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xEF, .len = 6,  .v = {0x08, 0x08, 0x08, 0x45, 0x3F, 0x54}}},
    {.type = TY_INIT_DELAY, .delay_time = 10},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE0, .len = 3,  .v = {0x00, 0x00, 0x02}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE1, .len = 11, .v = {0x08, 0x00, 0x0A, 0x00, 0x07, 0x00, 0x09, 0x00, 0x00, 0x33, 0x33}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE2, .len = 13, .v = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE3, .len = 4,  .v = {0x00, 0x00, 0x33, 0x33}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE4, .len = 2,  .v = {0x44, 0x44}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE5, .len = 16, .v = {0x0E, 0x60, 0xA0, 0xA0, 0x10, 0x60, 0xA0, 0xA0, 0x0A, 0x60, 0xA0, 0xA0, 0x0C, 0x60, 0xA0, 0xA0}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE6, .len = 4,  .v = {0x00, 0x00, 0x33, 0x33}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE7, .len = 2,  .v = {0x44, 0x44}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE8, .len = 16, .v = {0x0D, 0x60, 0xA0, 0xA0, 0x0F, 0x60, 0xA0, 0xA0, 0x09, 0x60, 0xA0, 0xA0, 0x0B, 0x60, 0xA0, 0xA0}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xEB, .len = 7,  .v = {0x02, 0x01, 0xE4, 0xE4, 0x44, 0x00, 0x40}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xEC, .len = 2,  .v = {0x02, 0x01}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xED, .len = 16, .v = {0xAB, 0x89, 0x76, 0x54, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x10, 0x45, 0x67, 0x98, 0xBA}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xFF, .len = 5,  .v = {0x77, 0x01, 0x00, 0x00, 0x13}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE6, .len = 2,  .v = {0x16, 0x7C}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xFF, .len = 5,  .v = {0x77, 0x01, 0x00, 0x00, 0x00}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xFF, .len = 5,  .v = {0x77, 0x01, 0x00, 0x00, 0x13}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE8, .len = 2,  .v = {0x00, 0x0E}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0x11, .len = 0}},
    {.type = TY_INIT_DELAY, .delay_time = 120},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE8, .len = 2,  .v = {0x00, 0x0C}}},
    {.type = TY_INIT_DELAY, .delay_time = 10},
    {.type = TY_INIT_REG,   .reg = {.r = 0xE8, .len = 2,  .v = { 0x00, 0x00}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0xFF, .len = 5,  .v = { 0x77, 0x01, 0x00, 0x00, 0x00}}},
    {.type = TY_INIT_REG,   .reg = {.r = 0x29, .len = 0}},
    {.type = TY_INIT_DELAY, .delay_time = 20},
    {.type = TY_INIT_CONF_END},    //END
};

const ty_display_device_s lcd_rgb_st7701sn_device = {
    .type = DISPLAY_RGB,
    .name = "rgb_st7701sn",
    .rgb = {
        .cfg = {
            .clk = 30000000,
            .out_data_clk_edge = RGB_DATA_IN_RISING_EDGE,
            .width = 480,
            .height = 854,
            .pixel_fmt = TY_PIXEL_FMT_RGB565,
            .hsync_back_porch = 30,
            .hsync_front_porch = 30,
            .vsync_back_porch = 20,
            .vsync_front_porch = 20,
            .hsync_pulse_width = 10,
            .vsync_pulse_width = 10
        },
        .init_seq = st7701sn_init_seq,
        .display_cfg = NULL
    }
};
#endif
