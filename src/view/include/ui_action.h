/**
 * @file ui_action.h
 * @brief UI action handler bootstrap (subscribes to dispatch actions)
 * @version 1.0
 * @date 2026-05-13
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_ACTION_H__
#define __UI_ACTION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"

/**
 * @brief Initialize the UI action handler
 * @return none
 */
VOID_T app_ui_action_init(VOID_T);

#ifdef __cplusplus
}
#endif

#endif /* __UI_ACTION_H__ */
