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

VOID_T hugo_ai_seg_process(VOID_T);
VOID_T seg_init(VOID_T);

// UINT16_T flag_rgb_bit;
// UINT8_T flag_moto_direction;
// UINT16_T flag_moto_time;
// BYTE_T flag_gateway_state;
// UINT8_T flag_rgb_data;

// UINT8_T moto_flag;

// #define DEMO_TEST

//flag_rgb_data
#define RGB_OFF     0x00
#define RGB_ON      0x01
#define RGB_RED     0x02
#define RGB_GREEN   0x03
#define RGB_BLUE    0x04
#define RGB_YELLOW  0x05
#define RGB_WAIT  	0x06
#define RGB_FAIL	0x07
#define RGB_SUCEESS	0x08
#define RGB_RESERVE	0x09

//flag_moto_direction
#define MOTO_LEFT   0x01
#define MOTO_RIGHT  0x02
#define MOTO_BACK  	0x03

#define MOTO_IDLE   		0x00
#define MOTO_CHECK 			0x01
#define MOTO_RUN_UP  		0x02
#define MOTO_RUN_DOWN   	0x03
#define MOTO_STOP   		0x04
#define MOTO_HOLD   		0x05
#define MOTO_WAIT   		0x06
#define MOTO_RUN_UP_END		0x07
#define MOTO_RUN_DOWN_END  	0x08

#define STATE_MOTO_IDLE		0x00
#define STATE_MOTO_ON		0x01
#define STATE_MOTO_OFF		0x02
#define STATE_MOTO_QUIET	0x03
#define STATE_MOTO_NOD		0x04
#define STATE_MOTO_TEST1	0x05
#define STATE_MOTO_TEST2	0x06

#define POWER_STATUS_ON		0x01
#define POWER_STATUS_OFF	0x02

#define PWM_FREQUENCY               20000
#define TASK_PWM_PRIORITY           THREAD_PRIO_2
#define PWM_ID_UP                   TUYA_PWM_NUM_0
#define PWM_ID_DOWN                 TUYA_PWM_NUM_5

typedef enum {
	STA_NONE         = 0,
	POWER_ON,
	LED_CONNECT,
	LED_DISCONNECT,
	MONITOR_MODE,
	SOFTAP_MODE,
	TIMER_POLL,
} DEV_STATE;


extern UINT8_T flag_turn_off_on_state;

#endif