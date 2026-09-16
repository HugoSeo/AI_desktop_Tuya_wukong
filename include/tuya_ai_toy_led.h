/**
 * @file tuya_ai_toy_led.h
 * @brief LED control for Tuya AI toy (wukong): init, on/off, and flash.
 */

#ifndef __TUYA_AI_TOY_LED_H__
#define __TUYA_AI_TOY_LED_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Turn the AI toy LED on.
 */
VOID tuya_ai_toy_led_on(VOID);

/**
 * @brief Turn the AI toy LED off.
 */
VOID tuya_ai_toy_led_off(VOID);

/**
 * @brief Flash the LED for a given duration.
 * @param[in] time Flash duration in milliseconds (or platform-specific unit; see implementation).
 */
VOID tuya_ai_toy_led_flash(INT_T time);

/**
 * @brief Initialize the LED GPIO and set it off.
 * @param[in] led_pin GPIO number for the LED (TUYA_GPIO_NUM_E).
 * @return OPRT_OK on success. See tuya_error_code.h for errors.
 */
OPERATE_RET tuya_ai_toy_led_init(TUYA_GPIO_NUM_E led_pin);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_AI_TOY_LED_H__ */