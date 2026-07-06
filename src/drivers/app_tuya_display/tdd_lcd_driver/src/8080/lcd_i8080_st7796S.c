#include "tuya_app_config.h"

#if defined(TUYA_MULTI_TYPES_LCD) && defined(TUYA_LCD_INTERFACE_8080_ST7796S)
#include "tal_display_service.h"
#include "tkl_8080.h"
static void _init(void)
{
    UINT32_T data;
    tkl_system_sleep(120);

    tkl_8080_cmd_send_with_param(0x01, NULL, 0);
    tkl_8080_cmd_send_with_param(0x28, NULL, 0);
    tkl_system_sleep(120);

    data = 0xc3;
    tkl_8080_cmd_send_with_param(0xf0, &data, 1);
    data = 0x96;
    tkl_8080_cmd_send_with_param(0xf0, &data, 1);

    data = 0x00;
    tkl_8080_cmd_send_with_param(0x35, &data, 1);

    UINT32_T buf_44[2] = {0x00, 0x01};
    tkl_8080_cmd_send_with_param(0x44, buf_44, 2);

    INT32_T buf_b1[2] = {0x60, 0x11};
    tkl_8080_cmd_send_with_param(0xb1, buf_b1, 2);

    data = 0x98;
    tkl_8080_cmd_send_with_param(0x36, &data, 1);

    data = 0x55;
    tkl_8080_cmd_send_with_param(0x3A, &data, 1);

    data = 0x01;
    tkl_8080_cmd_send_with_param(0xb4, &data, 1);

    data = 0xc6;
    tkl_8080_cmd_send_with_param(0xb7, &data, 1);

    UINT32_T buf_e8[8] = {0x40, 0x8a, 0x00, 0x00, 0x29, 0x19, 0xa5, 0x33};
    tkl_8080_cmd_send_with_param(0xe8, buf_e8, 8);

    data = 0xa7;
    tkl_8080_cmd_send_with_param(0xC2, &data, 1);

    data = 0x2b;
    tkl_8080_cmd_send_with_param(0xc5, &data, 1);

    UINT32_T buf_e0[14] = {0xf0, 0x09, 0x13, 0x12, 0x12, 0x2b, 0x3c, 0x44, 0x4b, 0x1b, 0x18, 0x17, 0x1d, 0x21};
    tkl_8080_cmd_send_with_param(0xE0, buf_e0, 14);

    UINT32_T buf_e1[14] = {0xf0, 0x09, 0x13, 0x0c, 0x0d, 0x27, 0x3b, 0x44, 0x4d, 0x0b, 0x17, 0x17, 0x1d, 0x21};
    tkl_8080_cmd_send_with_param(0xE1, buf_e1, 14);

    data = 0x3c;
    tkl_8080_cmd_send_with_param(0xf0, &data, 1);
    data = 0x96;
    tkl_8080_cmd_send_with_param(0xf0, &data, 1);

    tkl_8080_cmd_send_with_param(0x11, NULL, 0);
    tkl_system_sleep(150);
    tkl_8080_cmd_send_with_param(0x29, NULL, 0);
   //tkl_system_sleep(100);
}

static void _transfer_start(void)
{
    tkl_8080_cmd_send(0x2c);
}

static void _transfer_continue(void)
{
    tkl_8080_cmd_send(0x3c);
}

static void _set_display_area(void)
{
    //320 * 480
    // UINT32_T clumn[4] = {0, 0, 1, 63};
    // UINT32_T row[4] = {0, 0, 1, 223};
    // tkl_8080_cmd_send_with_param(0x2A, clumn, 4);
    // tkl_8080_cmd_send_with_param(0x2B, row, 4);
}

const ty_display_device_s lcd_i8080_st7796S_device = {
    .type = DISPLAY_8080,
    .name = "8080_st7796S",
    .mcu8080 = {
        .cfg = {
            .clk = 60000000,
            .data_bits = 8,
            .width = 320,
            .height = 480,
            .pixel_fmt = TY_PIXEL_FMT_RGB565,
        },
        .init =_init,
        .set_display_area = _set_display_area,
        .transfer_start = _transfer_start,
        .transfer_continue = _transfer_continue,

        .display_cfg = NULL
    }
};
#endif
