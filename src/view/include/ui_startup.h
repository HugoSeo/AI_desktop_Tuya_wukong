/**
 * @file ui_startup.h
 * @brief Startup welcome screen API
 * @version 1.0
 * @date 2026-05-13
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_STARTUP_H__
#define __UI_STARTUP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"

/**
 * @brief Build and show the startup welcome screen
 * @return none
 * @note One-shot splash; auto-navigates to UI_SCR_HOME after 1s.
 */
VOID_T setup_scr_startup(VOID_T);

#ifdef __cplusplus
}
#endif

#endif /* __UI_STARTUP_H__ */
