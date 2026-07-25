/**
 * @file tuya_ai_toy_led.c
 * @brief LED control implementation for Tuya AI toy (wukong).
 *
 * This module wraps the Tuya LED driver (tuya_led) to provide a single-LED interface
 * for the AI toy: initialise by GPIO, then turn on, off, or flash. The LED is used
 * for network status (e.g. flashing when unprovisioned, on when cloud connected) and
 * other indicators. All operations are no-ops if the LED has not been initialised
 * or if led_pin was TUYA_GPIO_NUM_MAX at init.
 *
 * @see tuya_ai_toy_led.h
 */

#include "tuya_ai_toy_led.h"
#include "tuya_led.h"
#include "tkl_gpio.h"
#include "tal_log.h"

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
/** LED handle created by tuya_create_led_handle; NULL if not initialised or GPIO invalid. */
STATIC LED_HANDLE s_ai_toy_led = NULL;

/* ---------------------------------------------------------------------------
 * Public API (see tuya_ai_toy_led.h)
 * --------------------------------------------------------------------------- */

/**
 * @brief Turn the AI toy LED on (steady on).
 *
 * Sets the LED to high level with 200 ms cycle; no effect if LED was not initialised.
 */
VOID tuya_ai_toy_led_on(VOID)
{
    if (s_ai_toy_led) {
        tuya_set_led_light_type(s_ai_toy_led, OL_HIGH, 200, 0xFFFF);
    }
}

/**
 * @brief Turn the AI toy LED off (steady off).
 *
 * Sets the LED to low level with 200 ms cycle; no effect if LED was not initialised.
 */
VOID tuya_ai_toy_led_off(VOID)
{
    if (s_ai_toy_led) {
        tuya_set_led_light_type(s_ai_toy_led, OL_LOW, 200, 0xFFFF);
    }
}

/**
 * @brief Flash the LED for a given duration.
 *
 * @param[in] time Flash period/duration in milliseconds. Passed to tuya_set_led_light_type.
 *                 No effect if LED was not initialised.
 */
VOID tuya_ai_toy_led_flash(INT_T time)
{
    if (s_ai_toy_led) {
        tuya_set_led_light_type(s_ai_toy_led, OL_FLASH_LOW, time, 0xFFFF);
    }
}

/**
 * @brief Initialise the AI toy LED on the given GPIO.
 *
 * Creates the LED handle via tuya_create_led_handle with default level OL_HIGH.
 * If led_pin is TUYA_GPIO_NUM_MAX, no handle is created and the other APIs will do nothing.
 *
 * @param[in] led_pin GPIO number for the LED (TUYA_GPIO_NUM_E). Use TUYA_GPIO_NUM_MAX to disable.
 * @return OPRT_OK on success or when led_pin is TUYA_GPIO_NUM_MAX. See tuya_error_code.h for errors.
 */
OPERATE_RET tuya_ai_toy_led_init(TUYA_GPIO_NUM_E led_pin)
{
    OPERATE_RET rt = OPRT_OK;

    TAL_PR_DEBUG("ai toy -> init led %d", led_pin);
    if (led_pin != TUYA_GPIO_NUM_MAX) {
        TUYA_CALL_ERR_RETURN(tuya_create_led_handle(led_pin, OL_HIGH, &s_ai_toy_led));
    }

    return rt;
}
