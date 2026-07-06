#include "tuya_app_config.h"

#ifdef TUYA_MULTI_TYPES_LCD

#include "tal_display_service.h"

static const DISPLAY_INIT_SEQ_T  nv3041_init_seq[] =
{
	{.type = TY_INIT_RST,   .reset = {{20}, {200}, {120}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xFF, .len = 1,  .v = {0xa5}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xe7, .len = 1,  .v = {0x10}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x35, .len = 1,  .v = {0x00}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x3a, .len = 1,  .v = {0x01}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x40, .len = 1,  .v = {0x01}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x41, .len = 1,  .v = {0x01}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x55, .len = 1,  .v = {0x01}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x44, .len = 1,  .v = {0x15}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x45, .len = 1,  .v = {0x15}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x7d, .len = 1,  .v = {0x03}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xc1, .len = 1,  .v = {0xbb}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xc2, .len = 1,  .v = {0x14}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xc3, .len = 1,  .v = {0x13}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xc6, .len = 1,  .v = {0x3e}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xc7, .len = 1,  .v = {0x25}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xc8, .len = 1,  .v = {0x11}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x7a, .len = 1,  .v = {0x7c}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x6f, .len = 1,  .v = {0x56}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x78, .len = 1,  .v = {0x2a}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x73, .len = 1,  .v = {0x08}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x74, .len = 1,  .v = {0x12}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xc9, .len = 1,  .v = {0x00}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x67, .len = 1,  .v = {0x11}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x51, .len = 1,  .v = {0x4b}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x52, .len = 1,  .v = {0x7c}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x53, .len = 1,  .v = {0x45}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x54, .len = 1,  .v = {0x77}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x46, .len = 1,  .v = {0x0a}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x47, .len = 1,  .v = {0x2a}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x48, .len = 1,  .v = {0x0a}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x49, .len = 1,  .v = {0x1a}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x56, .len = 1,  .v = {0x43}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x57, .len = 1,  .v = {0x42}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x58, .len = 1,  .v = {0x3c}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x59, .len = 1,  .v = {0x64}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x5A, .len = 1,  .v = {0x41}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x5B, .len = 1,  .v = {0x3c}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x5c, .len = 1,  .v = {0x02}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x5d, .len = 1,  .v = {0x3c}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x5e, .len = 1,  .v = {0x1f}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x60, .len = 1,  .v = {0x80}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x61, .len = 1,  .v = {0x3f}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x62, .len = 1,  .v = {0x21}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x63, .len = 1,  .v = {0x07}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x64, .len = 1,  .v = {0x0e}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x65, .len = 1,  .v = {0x01}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xca, .len = 1,  .v = {0x20}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xcb, .len = 1,  .v = {0x52}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xcc, .len = 1,  .v = {0x10}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xcd, .len = 1,  .v = {0x42}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xd0, .len = 1,  .v = {0x20}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xd1, .len = 1,  .v = {0x52}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xd2, .len = 1,  .v = {0x10}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xd3, .len = 1,  .v = {0x42}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xd4, .len = 1,  .v = {0x0a}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xd5, .len = 1,  .v = {0x32}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x6e, .len = 1,  .v = {0x14}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xe5, .len = 1,  .v = {0x06}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xe6, .len = 1,  .v = {0x00}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xf8, .len = 1,  .v = {0x06}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xf9, .len = 1,  .v = {0x00}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x80, .len = 1,  .v = {0x08}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xa0, .len = 1,  .v = {0x08}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x81, .len = 1,  .v = {0x0a}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xa1, .len = 1,  .v = {0x0a}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x82, .len = 1,  .v = {0x09}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xa2, .len = 1,  .v = {0x09}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x86, .len = 1,  .v = {0x38}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xa6, .len = 1,  .v = {0x2a}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x87, .len = 1,  .v = {0x4a}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xa7, .len = 1,  .v = {0x40}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x83, .len = 1,  .v = {0x39}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xa3, .len = 1,  .v = {0x39}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x84, .len = 1,  .v = {0x37}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xa4, .len = 1,  .v = {0x37}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x85, .len = 1,  .v = {0x28}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xa5, .len = 1,  .v = {0x28}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x88, .len = 1,  .v = {0x0b}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xa8, .len = 1,  .v = {0x04}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x89, .len = 1,  .v = {0x13}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xa9, .len = 1,  .v = {0x09}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x8a, .len = 1,  .v = {0x1b}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xaa, .len = 1,  .v = {0x11}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x8b, .len = 1,  .v = {0x11}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xab, .len = 1,  .v = {0x0d}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x8c, .len = 1,  .v = {0x14}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xac, .len = 1,  .v = {0x13}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x8d, .len = 1,  .v = {0x15}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xad, .len = 1,  .v = {0x0e}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x8e, .len = 1,  .v = {0x10}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xae, .len = 1,  .v = {0x0f}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x8f, .len = 1,  .v = {0x18}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xaf, .len = 1,  .v = {0x0e}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x90, .len = 1,  .v = {0x07}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xb0, .len = 1,  .v = {0x05}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x91, .len = 1,  .v = {0x11}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xb1, .len = 1,  .v = {0x0e}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x92, .len = 1,  .v = {0x19}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xb2, .len = 1,  .v = {0x14}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0xff, .len = 1,  .v = {0x00}}},
	{.type = TY_INIT_REG,   .reg = {.r = 0x11, .len = 0,  .v = {0x00}}},
    {.type = TY_INIT_DELAY, .delay_time = 120},//delay 120ms
    {.type = TY_INIT_REG,   .reg = {.r = 0x29, .len = 0,  .v = {0x00}}},
    {.type = TY_INIT_DELAY, .delay_time = 20},//delay 20ms
    {.type = TY_INIT_CONF_END}    //END
};


const ty_display_device_s  lcd_qspi_nv3041_device = {
    .type = DISPLAY_QSPI,
    .name = "qspi_nv3041",
    .qspi = {
        .width = 480,
        .height = 272,
        .pixel_fmt = TY_PIXEL_FMT_RGB565,
        .reg_write_cmd = 0x02,
        .refresh_method = QSPI_REFRESH_BY_FRAME,
		.has_pixel_memory = 1,
        .pixel_pre_cmd = {//0x32, 0x00, 0x2c, 0x00
            .cmd = 0x32,
            .addr[0] = 0x00,  //0x00  0x2c 0x00
            .addr[1] = 0x2c,
            .addr[2] = 0x00,
            .addr_size = 3,
            .cmd_lines = TUYA_QSPI_1WIRE,
            .addr_lines = TUYA_QSPI_1WIRE,
        },

        .qspi_cfg = {
            .role = TUYA_QSPI_ROLE_MASTER,
            .mode = TUYA_QSPI_MODE0,
            .type = TUYA_QSPI_TYPE_LCD,
            .freq_hz = 48000000,
            .use_dma = 1,
			.dma_data_lines = TUYA_QSPI_4WIRE,
        },
        .init_seq = nv3041_init_seq,
        .display_cfg = NULL
    }
};


// static const display_init_seq_t  nvco5300_init_seq[] =
// {
// 	{.type = TY_INIT_RST,   .reset = {20, 200, 120}},
// 	{.type = TY_INIT_REG,   .reg = {.r = 0xfe, .len = 1,  .v = {0x20}}},
// 	{.type = TY_INIT_REG,   .reg = {.r = 0x19, .len = 1,  .v = {0x10}}},
// 	{.type = TY_INIT_REG,   .reg = {.r = 0x1c, .len = 1,  .v = {0xa0}}},
// 	{.type = TY_INIT_REG,   .reg = {.r = 0xfe, .len = 1,  .v = {0x00}}},
// 	{.type = TY_INIT_REG,   .reg = {.r = 0xc4, .len = 1,  .v = {0x80}}},
// 	{.type = TY_INIT_REG,   .reg = {.r = 0x3a, .len = 1,  .v = {0x55}}},
// 	{.type = TY_INIT_REG,   .reg = {.r = 0x35, .len = 1,  .v = {0x00}}},
// 	{.type = TY_INIT_REG,   .reg = {.r = 0x53, .len = 1,  .v = {0x20}}},
// 	{.type = TY_INIT_REG,   .reg = {.r = 0x51, .len = 1,  .v = {0xFF}}},
// 	{.type = TY_INIT_REG,   .reg = {.r = 0x63, .len = 1,  .v = {0xFF}}},
// 	{.type = TY_INIT_REG,   .reg = {.r = 0x2a, .len = 4,  .v = {0x00,0x06,0x01,0xd7}}},
// 	{.type = TY_INIT_REG,   .reg = {.r = 0x2b, .len = 4,  .v = {0x00,0x00,0x01,0xd1}}},
//     {.type = TY_INIT_DELAY, .delay_time = 600},
// 	{.type = TY_INIT_REG,   .reg = {.r = 0x11, .len = 0,  .v = {0x00}}},
//     {.type = TY_INIT_DELAY, .delay_time = 600},
// 	{.type = TY_INIT_REG,   .reg = {.r = 0x29, .len = 0,  .v = {0x00}}},
//     {.type = TY_INIT_CONF_END}    //END
// };


// const ty_display_device_s  qspi_co5300_device = {
//     .type = DISPLAY_QSPI,
//     .name = "co5300",
//     .qspi = {
//         .width = 466,
//         .height = 466,
//         .pixel_fmt = TY_PIXEL_FMT_RGB565,
//         .reg_write_cmd = 0x02,
//         .refresh_method = QSPI_REFRESH_BY_FRAME,
// 		.has_pixel_memory = 1,
//         .pixel_pre_cmd = {//0x32, 0x00, 0x2c, 0x00
//             .cmd = 0x32,
//             .addr[0] = 0x00,  //0x01  0x2c 0x02
//             .addr[1] = 0x2c,
//             .addr[2] = 0x00,
//             .addr_size = 3,
//             .cmd_lines = TUYA_QSPI_1WIRE,
//             .addr_lines = TUYA_QSPI_1WIRE,
//         },
//         .qspi_cfg = {
//             .role = TUYA_QSPI_ROLE_MASTER,
//             .mode = TUYA_QSPI_MODE0,
//             .type = TUYA_QSPI_TYPE_LCD,
//             .freq_hz = 30000000,
//             .use_dma = 1
//         },
//         .init_seq = nvco5300_init_seq,
//         .display_cfg = NULL
//     }
// };
#endif

