/**
 * @file hugo_ai_desktop.h
 * @brief Tuya AI Toy module - public API and types.
 *
 * Declares configuration, state types and control interfaces for the AI toy
 * (wukong) application: init, DP processing, trigger mode and timers.
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

#ifndef __HUGO_AI_DESKTOP_H__
#define __HUGO_AI_DESKTOP_H__

#include "tuya_cloud_types.h"
#if defined(ENABLE_WIFI_SERVICE) && (ENABLE_WIFI_SERVICE == 1)
#include "tuya_cloud_wifi_defs.h"
#endif
#include "tal_sw_timer.h"
#include "tal_workqueue.h"
#include "smart_frame.h"
#include "wukong_ai_mode.h"
// #include "tuya_cloud_com_defs.h"

VOID_T hugo_ai_lightboard_init(VOID_T);
VOID_T hugo_ai_lightboard_process(VOID_T);


//===================== SC7A20寄存器定义 =====================
// #define SC7A20_WHO_AM_I     0x0F    // ID寄存器 固定0x11
// #define SC7A20_CTRL1        0x20
// #define SC7A20_CTRL2        0x21
// #define SC7A20_CTRL4        0x23
// #define SC7A20_STATUS       0x27
// #define SC7A20_OUT_X_L      0x28


// I2C 7bit地址 SA0接GND=0x19
#define LIGHTBOARD_DEV_ADDR     0x11

#endif