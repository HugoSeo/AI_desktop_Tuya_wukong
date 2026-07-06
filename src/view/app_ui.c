/**
 * @file app_ui.c
 * @brief UI message dispatcher for T5AI_BOARD
 * @version 1.0
 * @date 2025-04-02
 * @copyright Copyright (c) Tuya Inc.
 */
#include "tuya_cloud_types.h"
#include "tuya_ai_display.h"
#include "ui_common.h"
#include "ui_dispatch.h"

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Desktop UI initialization, called once at startup
 * @return none
 */
void app_ui_init(void)
{
    ui_nav_init();
    app_ui_action_init();
    setup_scr_startup();
}

/**
 * @brief Handle display messages from the platform (desktop UI)
 * @param[in] msg display message
 * @return none
 */
void app_ui_msg_handler(TY_DISPLAY_MSG_T *msg)
{
    ui_dispatch_msg(msg);
}
