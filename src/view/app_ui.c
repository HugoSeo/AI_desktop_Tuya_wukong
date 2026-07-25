/**
 * @file app_ui.c
 * @brief Board-level UI entry hooks (init + msg routing) for T5AI_BOARD
 *
 * Implements the `app_ui_init` / `app_ui_msg_handler` symbols that
 * `tuya_ai_display.c` resolves at link time. Real message dispatching
 * lives in `ui_dispatch.c`.
 *
 * @version 1.0
 * @date 2025-04-02
 * @copyright Copyright (c) Tuya Inc.
 */
#include "tuya_cloud_types.h"
#include "tuya_ai_display.h"
#include "ui_common.h"
#include "ui_dispatch.h"
#include "ui_record_runtime.h"
#include "tuya_app_config.h"

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

#if defined(ENABLE_AI_MODE_RECORD) && (ENABLE_AI_MODE_RECORD == 1)
    /* Spawn the transcribe-result poll thread (ADR-0003): fire-and-forget,
     * runs until power-off. Wrapped here per project convention — see
     * wukong_ai_mode.c:452 for the same pattern. */
    ui_record_runtime_poll_start();
#endif
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
