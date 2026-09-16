/**
 * @file gfw_core_frame.c
 * @brief
 * @version 0.1
 * @date 2023-06-20
 *
 * @copyright Copyright (c) 2023 Tuya Inc. All Rights Reserved.
 *
 * Permission is hereby granted, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), Under the premise of complying
 * with the license of the third-party open source software contained in the software,
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software.
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 */

#include "tal_memory.h"
#include "gfw_core_cmd.h"
#include "gfw_core_frame.h"

BYTE_T gfw_core_frame_calc_crc(BYTE_T *data, USHORT_T len)
{
    BYTE_T crc = 0;
    USHORT_T i = 0;

    for (i = 0; i < len; i++) {
        crc += data[i];
    }

    return crc;
}

OPERATE_RET gfw_core_frame_pack(AUDIO_GFW_CMD_TYPE_E type, BYTE_T cmd, BYTE_T *data, UINT16_T data_len, BYTE_T **frame, UINT16_T *frame_len)
{
    BYTE_T *pack = NULL;
    UINT16_T pack_len = data_len + MIN_FRAME_LEN;

    if (AUDIO_GFW_CMD_TYPE_ZEROCOPY == type) {
        pack = data - (MIN_FRAME_LEN - 1);
    } else {
        pack = (BYTE_T *)Malloc(pack_len);
        if (NULL == pack) {
            return OPRT_MALLOC_FAILED;
        }
    }

    pack[0] = 0x55;
    pack[1] = 0xAA;
    pack[2] = DEFAULT_FRAME_VER;
    pack[3] = cmd;
    pack[4] = (data_len >> 8) & 0xFF;
    pack[5] = data_len & 0xFF;
    if (AUDIO_GFW_CMD_TYPE_ZEROCOPY != type && data && data_len) {
        memcpy(pack + 6, data, data_len);
    }
    pack[pack_len - 1] = gfw_core_frame_calc_crc(pack, pack_len - 1);

    *frame = pack;
    *frame_len = pack_len;

    return OPRT_OK;
}
