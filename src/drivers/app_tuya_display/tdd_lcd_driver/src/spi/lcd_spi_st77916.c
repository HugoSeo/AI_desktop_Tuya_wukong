#include "tuya_app_config.h"
#if !defined(ENABLE_TUYA_DISPLAY)
#include "tal_display_service.h"
#include "tuya_port_disp.h"
#include "tal_system.h"

#define ST77916_CMD_SLPIN          0x10
#define ST77916_CMD_SLPOUT         0x11
#define ST77916_CMD_NOROFF         0x12
#define ST77916_CMD_NORON          0x13
#define ST77916_CMD_DISPOFF        0x28
#define ST77916_CMD_DISPON         0x29
#define ST77916_CMD_PTLAR          0x30
#define ST77916_CMD_TEON           0x35
#define ST77916_CMD_IDMOFF         0x38
#define ST77916_CMD_IDMON          0x39
#define ST77916_CMD_RAMCLACT       0x4C

static const DISPLAY_INIT_SEQ_T  st77916_init_seq[] = {
    // 硬件复位
    {.type = TY_INIT_RST, .reset = {{120}, {120}, {120}}},

    {.type = TY_INIT_REG, .reg = {.r = 0xF0, .len = 1, .v = {0x28}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xF2, .len = 1, .v = {0x28}}},

    {.type = TY_INIT_REG, .reg = {.r = 0x73, .len = 1, .v = {0xF0}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x76, .len = 1, .v = {0x0F}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x7C, .len = 1, .v = {0xD1}}},

    {.type = TY_INIT_REG, .reg = {.r = 0x83, .len = 1, .v = {0xE0}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x84, .len = 1, .v = {0x61}}},

    {.type = TY_INIT_REG, .reg = {.r = 0xF2, .len = 1, .v = {0x82}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xF0, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xF0, .len = 1, .v = {0x01}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xF1, .len = 1, .v = {0x01}}},

    {.type = TY_INIT_REG, .reg = {.r = 0xB0, .len = 1, .v = {0x52}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xB1, .len = 1, .v = {0x49}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xB2, .len = 1, .v = {0x24}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xB3, .len = 1, .v = {0x01}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xB4, .len = 1, .v = {0x66}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xB5, .len = 1, .v = {0x44}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xB6, .len = 1, .v = {0xC5}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xB7, .len = 1, .v = {0x40}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xB8, .len = 1, .v = {0x86}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xB9, .len = 1, .v = {0x15}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xBA, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xBB, .len = 1, .v = {0x08}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xBC, .len = 1, .v = {0x08}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xBD, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xBE, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xBF, .len = 1, .v = {0x07}}},

    {.type = TY_INIT_REG, .reg = {.r = 0xC0, .len = 1, .v = {0x80}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xC1, .len = 1, .v = {0x10}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xC2, .len = 1, .v = {0x76}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xC3, .len = 1, .v = {0x80}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xC4, .len = 1, .v = {0x10}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xC5, .len = 1, .v = {0x76}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xC6, .len = 1, .v = {0xA9}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xC7, .len = 1, .v = {0x41}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xC8, .len = 1, .v = {0x01}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xC9, .len = 1, .v = {0xA9}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xCA, .len = 1, .v = {0x41}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xCB, .len = 1, .v = {0x01}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xCC, .len = 1, .v = {0x7F}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xCD, .len = 1, .v = {0x7F}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xCE, .len = 1, .v = {0xFF}}},

    {.type = TY_INIT_REG, .reg = {.r = 0xD0, .len = 1, .v = {0x91}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xD1, .len = 1, .v = {0x68}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xD2, .len = 1, .v = {0x68}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xF5, .len = 2, .v = {0x00, 0xA5}}},

    {.type = TY_INIT_REG, .reg = {.r = 0xF1, .len = 1, .v = {0x10}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xF0, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xF0, .len = 1, .v = {0x02}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xE0, .len = 14, .v = {0xF0, 0x0E, 0x14, 0x0B, 0x0B, 0x16, 0x3A, 0x44, 0x4E, 0x18, 0x14, 0x13, 0x2F, 0x35}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xE1, .len = 14, .v = {0xF0, 0x0D, 0x13, 0x0B, 0x0A, 0x16, 0x39, 0x43, 0x4E, 0x17, 0x13, 0x13, 0x2E, 0x34}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xF0, .len = 1, .v = {0x10}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xF3, .len = 1, .v = {0x10}}},

    {.type = TY_INIT_REG, .reg = {.r = 0xE0, .len = 1, .v = {0x09}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xE1, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xE2, .len = 1, .v = {0x03}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xE3, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xE4, .len = 1, .v = {0xE0}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xE5, .len = 1, .v = {0x06}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xE6, .len = 1, .v = {0x21}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xE7, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xE8, .len = 1, .v = {0x05}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xE9, .len = 1, .v = {0x82}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xEA, .len = 1, .v = {0xDE}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xEB, .len = 1, .v = {0xC0}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xEC, .len = 1, .v = {0x40}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xED, .len = 1, .v = {0x84}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xEE, .len = 1, .v = {0xFF}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xEF, .len = 1, .v = {0x71}}},

    {.type = TY_INIT_REG, .reg = {.r = 0xF8, .len = 1, .v = {0xFF}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xF9, .len = 1, .v = {0x50}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xFA, .len = 1, .v = {0xFF}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xFB, .len = 1, .v = {0xF3}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xFC, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xFD, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xFE, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xFF, .len = 1, .v = {0x00}}},

    {.type = TY_INIT_REG, .reg = {.r = 0x60, .len = 1, .v = {0x42}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x61, .len = 1, .v = {0xDF}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x62, .len = 1, .v = {0x40}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x63, .len = 1, .v = {0x40}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x64, .len = 1, .v = {0x02}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x65, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x66, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x67, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x68, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x69, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x6A, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x6B, .len = 1, .v = {0x00}}},

    {.type = TY_INIT_REG, .reg = {.r = 0x70, .len = 1, .v = {0x42}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x71, .len = 1, .v = {0xDF}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x72, .len = 1, .v = {0x40}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x73, .len = 1, .v = {0x40}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x74, .len = 1, .v = {0x01}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x75, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x76, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x77, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x78, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x79, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x7A, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x7B, .len = 1, .v = {0x00}}},

    {.type = TY_INIT_REG, .reg = {.r = 0x80, .len = 1, .v = {0x48}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x81, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x82, .len = 1, .v = {0x04}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x83, .len = 1, .v = {0x02}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x84, .len = 1, .v = {0xDC}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x85, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x86, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x87, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x88, .len = 1, .v = {0x48}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x89, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x8A, .len = 1, .v = {0x06}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x8B, .len = 1, .v = {0x02}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x8C, .len = 1, .v = {0xDE}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x8D, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x8E, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x8F, .len = 1, .v = {0x00}}},

    {.type = TY_INIT_REG, .reg = {.r = 0x90, .len = 1, .v = {0x48}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x91, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x92, .len = 1, .v = {0x08}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x93, .len = 1, .v = {0x02}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x94, .len = 1, .v = {0xE0}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x95, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x96, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x97, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x98, .len = 1, .v = {0x48}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x99, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x9A, .len = 1, .v = {0x0A}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x9B, .len = 1, .v = {0x02}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x9C, .len = 1, .v = {0xE2}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x9D, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x9E, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x9F, .len = 1, .v = {0x00}}},

    {.type = TY_INIT_REG, .reg = {.r = 0xA0, .len = 1, .v = {0x48}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xA1, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xA2, .len = 1, .v = {0x03}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xA3, .len = 1, .v = {0x02}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xA4, .len = 1, .v = {0xDB}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xA5, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xA6, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xA7, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xA8, .len = 1, .v = {0x48}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xA9, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xAA, .len = 1, .v = {0x05}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xAB, .len = 1, .v = {0x02}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xAC, .len = 1, .v = {0xDD}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xAD, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xAE, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xAF, .len = 1, .v = {0x00}}},

    {.type = TY_INIT_REG, .reg = {.r = 0xB0, .len = 1, .v = {0x48}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xB1, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xB2, .len = 1, .v = {0x07}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xB3, .len = 1, .v = {0x02}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xB4, .len = 1, .v = {0xDF}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xB5, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xB6, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xB7, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xB8, .len = 1, .v = {0x48}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xB9, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xBA, .len = 1, .v = {0x09}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xBB, .len = 1, .v = {0x02}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xBC, .len = 1, .v = {0xE1}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xBD, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xBE, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xBF, .len = 1, .v = {0x00}}},

    {.type = TY_INIT_REG, .reg = {.r = 0xC0, .len = 1, .v = {0x65}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xC1, .len = 1, .v = {0x74}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xC2, .len = 1, .v = {0x47}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xC3, .len = 1, .v = {0x56}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xC4, .len = 1, .v = {0xAA}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xC5, .len = 1, .v = {0x11}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xC6, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xC7, .len = 1, .v = {0x2A}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xC8, .len = 1, .v = {0xA2}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xC9, .len = 1, .v = {0x33}}},

    {.type = TY_INIT_REG, .reg = {.r = 0xD0, .len = 1, .v = {0x65}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xD1, .len = 1, .v = {0x74}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xD2, .len = 1, .v = {0x47}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xD3, .len = 1, .v = {0x56}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xD4, .len = 1, .v = {0xAA}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xD5, .len = 1, .v = {0x11}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xD6, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xD7, .len = 1, .v = {0x2A}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xD8, .len = 1, .v = {0xA2}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xD9, .len = 1, .v = {0x33}}},

    {.type = TY_INIT_REG, .reg = {.r = 0xF3, .len = 1, .v = {0x01}}},
    {.type = TY_INIT_REG, .reg = {.r = 0xF0, .len = 1, .v = {0x00}}},

    {.type = TY_INIT_REG, .reg = {.r = 0x21, .len = 0}},

    {.type = TY_INIT_REG, .reg = {.r = 0x2A, .len = 4, .v = {0x00, 0x00, 0x01, 0x67}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x2B, .len = 4, .v = {0x00, 0x00, 0x01, 0x67}}},

    {.type = TY_INIT_REG, .reg = {.r = 0x35, .len = 0}},

    {.type = TY_INIT_REG, .reg = {.r = 0x36, .len = 1, .v = {0x00}}},
    {.type = TY_INIT_REG, .reg = {.r = 0x3A, .len = 1, .v = {0x55}}},

    {.type = TY_INIT_REG, .reg = {.r = 0x11, .len = 0}},

    {.type = TY_INIT_DELAY, .delay_time = 120},

    {.type = TY_INIT_REG, .reg = {.r = 0x29, .len = 0}},
    {.type = TY_INIT_DELAY, .delay_time = 20},

    // 结束标记
    {.type = TY_INIT_CONF_END}
};

const ty_display_device_s  lcd_spi_st77916_device = {
    .type = DISPLAY_SPI,
    .name = "spi_st77916",
    .spi = {
        .width = 360,
        .height = 360,
        .x_offset = 0,
        .y_offset = 0,
        .pixel_fmt = TY_PIXEL_FMT_RGB565,
        .cfg = {
            .role = TUYA_SPI_ROLE_MASTER,
            .mode = TUYA_SPI_MODE0,
            .type = TUYA_SPI_AUTO_TYPE,
            .databits = TUYA_SPI_DATA_BIT8,
            .bitorder = TUYA_SPI_ORDER_MSB2LSB,
            .freq_hz = 62500000,
            .spi_dma_flags = 1
        },
        .init_seq = st77916_init_seq,
        .display_cfg = NULL
    }
};

/**
 * @brief 使用 RAMCLACT(0x4C) 硬件指令将 ST77916 整个 GRAM 填充为黑色(RGB565 0x0000)。
 * @param[in] disp_device: 显示设备句柄 (ty_display_device_s *)
 * @return OPRT_OK 成功，其他值表示 SPI 传输失败
 * @note 调用前需确保 SPI 总线空闲，避免与 __spi_task 队列中的帧传输产生竞争。
 */
OPERATE_RET lcd_spi_st77916_screen_clear(void *disp_device)
{
    ty_display_device_s *device = (ty_display_device_s *)disp_device;
    OPERATE_RET rt = OPRT_OK;
    UINT8_T color[2] = {0x00, 0x00};

    rt = tal_display_spi_send_cmd(&(device->spi), ST77916_CMD_RAMCLACT);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("lcd spi device memory clear fail !");
        return rt;
    }
    rt = tal_display_spi_send_data(&(device->spi), color, SIZEOF(color));
    if (rt != OPRT_OK) {
        TAL_PR_ERR("lcd spi device memory clear data fail !");
        return rt;
    }

    return OPRT_OK;
}

/**
 * @brief Enter ST77916 low-power clock display mode.
 * @param[in] disp_device
 * @param[in] x start x of the visible update region
 * @param[in] y start y of the visible update region
 * @param[in] width region width
 * @param[in] height region height
 * @param[slp_mode] sleep mode
 * @return OPRT_OK on success, others on error
 * @note ST77916 partial mode is line-based. This function uses y/height to
 *       limit panel scan lines, while x/width is kept for later partial refresh.
 */
OPERATE_RET lcd_spi_st77916_lp_enter(void *disp_device, UINT16_T x, UINT16_T y, UINT16_T width, UINT16_T height, LCD_DISPLAY_SLEEP_MODE_E slp_mode)
{
    ty_display_device_s *device = (ty_display_device_s *)disp_device;
    OPERATE_RET rt = OPRT_OK;
    UINT16_T psl = 0;
    UINT16_T pel = 0;
    UINT8_T area[4] = {0};

    if (slp_mode > LCD_DISPLAY_DEEP_SLEEP) {
        TAL_PR_ERR("lcd spi device sleep mode '%d' unsupport !", slp_mode);
        return OPRT_INVALID_PARM;
    }
    else if (slp_mode == LCD_DISPLAY_DEEP_SLEEP) {
        rt = tal_display_spi_send_cmd(&(device->spi), ST77916_CMD_DISPOFF);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("lcd spi device enter lp set DISPOFF fail !");
            return rt;
        }
        rt = tal_display_spi_send_cmd(&(device->spi), ST77916_CMD_SLPIN);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("lcd spi device enter lp set SLPIN fail !");
            return rt;
        }
        return rt;
    }

    if ((width == 0) || (height == 0)) {
        TAL_PR_ERR("lcd spi device enter lp get invalid parm !");
        return OPRT_INVALID_PARM;
    }

    psl = y;
    pel = y + height - 1;
    area[0] = (UINT8_T)((psl >> 8) & 0xFF);
    area[1] = (UINT8_T)(psl & 0xFF);
    area[2] = (UINT8_T)((pel >> 8) & 0xFF);
    area[3] = (UINT8_T)(pel & 0xFF);

    /* Optional: enable TE before entering low-power partial refresh */
    //rt = display_spi_send_cmd(device, ST77916_CMD_TEON);
    //if (rt != OPRT_OK) {
    //    return rt;
    //}
    /* Set partial scan area: only these lines are driven in low-power mode */
    rt = tal_display_spi_send_cmd(&(device->spi), ST77916_CMD_PTLAR);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("lcd spi device enter lp set PTLAR fail !");
        return rt;
    }
    rt = tal_display_spi_send_data(&(device->spi), area, SIZEOF(area));
    if (rt != OPRT_OK) {
        TAL_PR_ERR("lcd spi device enter lp set partial data fail !");
        return rt;
    }
    /* Enter partial mode */
    rt = tal_display_spi_send_cmd(&(device->spi), ST77916_CMD_NOROFF);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("lcd spi device enter lp set NOROFF fail !");
        return rt;
    }
    /* Enter idle mode: 8-color reduced mode, lower panel power */
    rt = tal_display_spi_send_cmd(&(device->spi), ST77916_CMD_IDMON);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("lcd spi device enter lp set IDMON fail !");
        return rt;
    }

    TAL_PR_NOTICE("lcd spi device enter lp successful !");
    return OPRT_OK;
}

/**
 * @brief Exit ST77916 low-power clock display mode.
 * @param[in] disp_device
 * @param[slp_mode] sleep mode
 * @return OPRT_OK on success, others on error
 * @note This restores normal display mode and exits idle mode.
 */
OPERATE_RET lcd_spi_st77916_lp_exit(void *disp_device, LCD_DISPLAY_SLEEP_MODE_E slp_mode)
{
    ty_display_device_s *device = (ty_display_device_s *)disp_device;
    OPERATE_RET rt = OPRT_OK;

    if (slp_mode > LCD_DISPLAY_DEEP_SLEEP) {
        TAL_PR_ERR("lcd spi device sleep mode '%d' unsupport !", slp_mode);
        return OPRT_INVALID_PARM;
    }
    else if (slp_mode == LCD_DISPLAY_DEEP_SLEEP) {
        rt = tal_display_spi_send_cmd(&(device->spi), ST77916_CMD_SLPOUT);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("lcd spi device exit lp set SLPOUT fail !");
            return rt;
        }
        tal_system_sleep(120);
        rt = tal_display_spi_send_cmd(&(device->spi), ST77916_CMD_DISPON);
        if (rt != OPRT_OK) {
            TAL_PR_ERR("lcd spi device exit lp set DISPON fail !");
            return rt;
        }
        return rt;
    }

    /* Exit idle mode first */
    rt = tal_display_spi_send_cmd(&(device->spi), ST77916_CMD_IDMOFF);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("lcd spi device exit lp set IDMOFF fail !");
        return rt;
    }
    /* Return to normal full display mode */
    rt = tal_display_spi_send_cmd(&(device->spi), ST77916_CMD_NORON);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("lcd spi device exit lp set NORON fail !");
        return rt;
    }

    TAL_PR_NOTICE("lcd spi device exit lp successful !");
    return OPRT_OK;
}
#endif
