/**
 * @file ui_home.h
 * @brief Home clock screen API
 * @version 2.0
 * @date 2026-06-01
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_HOME_H__
#define __UI_HOME_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"

/**
 * @brief Build and show the home clock screen
 * @return none
 * @note The HH:MM time, weekday, and date auto-refresh from the system clock
 *       once tal_time has completed time/zone sync.
 */
VOID_T ui_home_show(VOID_T);

/**
 * @brief Update the home-screen WiFi icon to reflect network connectivity
 * @param[in] connected TRUE if network is connected, FALSE otherwise
 * @return none
 * @note State is cached internally and re-applied each time the home
 *       screen is rebuilt; safe to call before the screen is created.
 */
VOID_T ui_home_set_net_state(BOOL_T connected);

#ifdef __cplusplus
}
#endif

#endif /* __UI_HOME_H__ */
