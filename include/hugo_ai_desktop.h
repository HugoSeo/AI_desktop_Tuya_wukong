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

int hugo_ai_desktop_init(void);
int ret_gateway_state(void);

// UINT16_T flag_rgb_bit;
// UINT8_T flag_moto_direction;
// UINT16_T flag_moto_time;
// BYTE_T flag_gateway_state;

//flag_rgb_data
#define RGB_ON      0x01
#define RGB_OFF     0x02
#define RGB_WHITE   0x03
#define RGB_RED     0x04
#define RGB_GREEN   0x05
#define RGB_BLUE    0x06
#define RGB_YELLOW  0x07

//flag_moto_direction
#define MOTO_RIGHT  0x01
#define MOTO_LEFT   0x02


#endif