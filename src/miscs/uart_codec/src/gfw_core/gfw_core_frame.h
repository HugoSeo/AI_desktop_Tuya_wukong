/**
 * @file gfw_core_frame.h
 * @brief
 * @version 0.1
 * @date 2023-06-19
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


#ifndef __GFW_CORE_FRAME_H__
#define __GFW_CORE_FRAME_H__

#include "tuya_cloud_types.h"
#include "gfw_core_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEFAULT_FRAME_VER (0)

typedef struct {
    USHORT_T head;      // 0x55aa
    BYTE_T   ver;       // 0x00
    BYTE_T   cmd;
    USHORT_T len;
    BYTE_T   data[0];
//  BYTE_T   crc;
} TY_GFW_FRAME_T;

/**
 * @brief
 *
 * @param data
 * @param len
 * @return BYTE_T
 */
BYTE_T gfw_core_frame_calc_crc(BYTE_T *data, USHORT_T len);

/**
 * @brief
 *
 * @param cmd
 * @param data
 * @param len
 * @return OPERATE_RET
 */
OPERATE_RET gfw_core_frame_pack(AUDIO_GFW_CMD_TYPE_E type, BYTE_T cmd, BYTE_T *data, UINT16_T data_len, BYTE_T **frame, UINT16_T *frame_len);

#ifdef __cplusplus
}
#endif

#endif // __GFW_CORE_FRAME_H__