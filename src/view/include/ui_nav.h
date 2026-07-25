/**
 * @file ui_nav.h
 * @brief Screen navigation stack and screen-id enumeration
 * @version 1.0
 * @date 2026-05-13
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_NAV_H__
#define __UI_NAV_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef enum {
    UI_SCR_NONE = 0,
    UI_SCR_HOME,
    UI_SCR_CHAT,
    UI_SCR_CAMERA,
    UI_SCR_ALBUM,
    UI_SCR_ALBUM_GRID,
    UI_SCR_DEVICE_MODE,
    UI_SCR_RECORD,
    UI_SCR_RECORD_LIST,
    UI_SCR_MUSIC,
    UI_SCR_MUSIC_LIST,
    UI_SCR_CALL,
    UI_SCR_DETECTION,
    UI_SCR_SETTINGS,
    UI_SCR_MAX,
} UI_SCR_ID_E;

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */

/**
 * @brief Initialize the navigation stack
 * @return none
 */
VOID_T ui_nav_init(VOID_T);

/**
 * @brief Navigate to a screen, pushing current onto the stack
 * @param[in] id target screen ID
 * @return none
 */
VOID_T ui_nav_to(UI_SCR_ID_E id);

/**
 * @brief Go back to the previous screen in the stack
 * @return none
 */
VOID_T ui_nav_back(VOID_T);

/**
 * @brief Navigate back until the target screen becomes current
 * @param[in] id target screen ID
 * @return none
 */
VOID_T ui_nav_back_to(UI_SCR_ID_E id);

/**
 * @brief Replace current screen without pushing (for screen refresh)
 * @param[in] id target screen ID
 * @return none
 */
VOID_T ui_nav_replace(UI_SCR_ID_E id);

/**
 * @brief Clear navigation stack and show the target screen
 * @param[in] id target screen ID
 * @return none
 */
VOID_T ui_nav_reset_to(UI_SCR_ID_E id);

/**
 * @brief Get the current screen ID
 * @return current screen ID, UI_SCR_NONE if stack is empty
 */
UI_SCR_ID_E ui_nav_current(VOID_T);

/**
 * @brief Get the previous screen ID
 * @return previous screen ID, UI_SCR_NONE if no previous
 */
UI_SCR_ID_E ui_nav_previous(VOID_T);

#ifdef __cplusplus
}
#endif

#endif /* __UI_NAV_H__ */
