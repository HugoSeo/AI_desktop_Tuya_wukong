/**
 * @file tuya_device_cfg.h
 * @brief Device configuration for Tuya AI toy (wukong): defaults and feature toggles.
 * @version 0.1
 * @date 2023-06-26
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

#ifndef __TUYA_DEVICE_CFG_H__
#define __TUYA_DEVICE_CFG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_app_config.h"
#include "tuya_device_board.h"

/** Default AI toy config initializer (GPIO and trigger mode from board). */
#define TY_AI_TOY_CFG_DEFAULT { \
    .audio_trigger_pin = TUYA_AI_TOY_AUDIO_TRIGGER_PIN, \
    .spk_en_pin = TUYA_AI_TOY_SPK_EN_PIN, \
    .led_pin = TUYA_AI_TOY_LED_PIN, \
    .trigger_mode = TUYA_AI_CHAT_DEFAULT_MODE, \
    .net_pin = TUYA_AI_TOY_NET_PIN, \
}

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_DEVICE_CFG_H__ */
