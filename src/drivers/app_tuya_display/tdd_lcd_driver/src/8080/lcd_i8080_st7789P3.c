#include "tuya_app_config.h"

#if defined(TUYA_MULTI_TYPES_LCD) && defined(LCD_MODULE_FRD240B28009_A)
#include "tal_display_service.h"
#include "tkl_8080.h"

static void _init(void)
{
    UINT32_T data;
    tkl_system_sleep(120);

    tkl_8080_cmd_send_with_param(0x11, NULL, 0);//exit sleep mode
    tkl_system_sleep(120);

    UINT32_T buf_b2[5] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    tkl_8080_cmd_send_with_param(0xB2, buf_b2, 5);

    data = 0x00;
    tkl_8080_cmd_send_with_param(0x35, &data, 1);

    data = 0x00;
    tkl_8080_cmd_send_with_param(0x36, &data, 1);

    data = 0x05;    //select rgb565
    //data = 0x06;    //select rgb666
    tkl_8080_cmd_send_with_param(0x3A, &data, 1);

    data = 0x56;
    tkl_8080_cmd_send_with_param(0xB7, &data, 1);

    data = 0x0C;
    tkl_8080_cmd_send_with_param(0xBB, &data, 1);

    data = 0x2C;
    tkl_8080_cmd_send_with_param(0xC0, &data, 1);

    data = 0x01;
    tkl_8080_cmd_send_with_param(0xC2, &data, 1);

    data = 0x0F;
    tkl_8080_cmd_send_with_param(0xC3, &data, 1);

    data = 0x0F;
    tkl_8080_cmd_send_with_param(0xC6, &data, 1);

    data = 0xA7;
    tkl_8080_cmd_send_with_param(0xD0, &data, 1);

    UINT32_T buf_d0[2] = {0xA4, 0xA1};
    tkl_8080_cmd_send_with_param(0xD0, buf_d0, 2);

    data = 0xA1;
    tkl_8080_cmd_send_with_param(0xD6, &data, 1);

    UINT32_T buf_e0[14] = {0xF0, 0x01, 0x08, 0x04, 0x05, 0x14, 0x33, 0x44, 0x49, 0x36, 0x11, 0x14, 0x2E, 0x36};
    tkl_8080_cmd_send_with_param(0xE0, buf_e0, 14);

    UINT32_T buf_e1[14] = {0xF0, 0x0C, 0x10, 0x0E, 0x0C, 0x08, 0x32, 0x43, 0x49, 0x28, 0x12, 0x12, 0x2C, 0x33};
    tkl_8080_cmd_send_with_param(0xE1, buf_e1, 14);

    tkl_8080_cmd_send_with_param(0x21, NULL, 0);
    tkl_8080_cmd_send_with_param(0x29, NULL, 0);
    tkl_8080_cmd_send_with_param(0x2C, NULL, 0);
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
    //240 * 320
    UINT32_T clumn[4] = {0, 0, 0, 239};
    UINT32_T row[4] = {0, 0, 1, 63};//319 = 256 + 63
    tkl_8080_cmd_send_with_param(0x2A, clumn, 4);
    tkl_8080_cmd_send_with_param(0x2B, row, 4);
}

const ty_display_device_s lcd_i8080_st7789P3_device = {
    .type = DISPLAY_8080,
    .name = "8080_st7789P3",
    .mcu8080 = {
        .cfg = {
            .clk = 12000000,
            .data_bits = 8,
            .width = 240,
            .height = 320,
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
