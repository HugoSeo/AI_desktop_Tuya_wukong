/**
 * @file tuya_device_board.h
 * @brief QEMU_M33_BOARD board definition.
 *
 * QEMU (mps2-an521) simulation board. Modeled on T5AI_BOARD:
 * same rgb_ili9488 LCD driver and lcd cfg values (320x480 RGB565), but the
 * touch panel is a virtual mouse-backed device (tp_qemu_mouse_device) instead
 * of the real gt1151 I2C touch controller.
 *
 * @copyright Copyright (c) 2026 Tuya Inc. All Rights Reserved.
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

#ifndef __TUYA_DEVICE_BOARD_H__
#define __TUYA_DEVICE_BOARD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_board_config.h"

OPERATE_RET tuya_device_board_init();

#ifdef __cplusplus
}
#endif
#endif
