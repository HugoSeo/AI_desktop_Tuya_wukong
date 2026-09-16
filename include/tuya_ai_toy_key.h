/**
 * @file tuya_ai_toy_key.h
 * @brief Key (button) handling for Tuya AI toy: netconfig reset init and key GPIO init with short/long press.
 */

#ifndef __TUYA_AI_TOY_KEY_H__
#define __TUYA_AI_TOY_KEY_H__

#include "tuya_cloud_types.h"
#include "tuya_key.h"
#include "tkl_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize reset-netconfig key (e.g. long press to enter provisioning).
 * @return OPRT_OK on success. See tuya_error_code.h for errors.
 */
OPERATE_RET tuya_reset_netconfig_init(VOID);

/**
 * @brief Initialize the AI toy key on the given GPIO with short/long press timing and callback.
 * @param[in] pin          GPIO number for the key (TUYA_GPIO_NUM_E).
 * @param[in] low_detect   TRUE if key is active low (pressed = low level).
 * @param[in] seqk_time_ms Short-press threshold in milliseconds.
 * @param[in] logk_time_ms Long-press threshold in milliseconds.
 * @param[in] cb           Callback on key event (port, PUSH_KEY_TYPE_E, count). See KEY_CALLBACK in tuya_key.h.
 * @return OPRT_OK on success. See tuya_error_code.h for errors.
 */
OPERATE_RET tuya_ai_toy_key_init(TUYA_GPIO_NUM_E pin, BOOL_T low_detect, UINT32_T seqk_time_ms, UINT32_T logk_time_ms, KEY_CALLBACK cb);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_AI_TOY_KEY_H__ */

