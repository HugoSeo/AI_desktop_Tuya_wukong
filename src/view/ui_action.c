#include "ui_dispatch.h"

/**
 * @brief Handle UI actions posted from control center or other modules.
 * @param[in] msg optional message data
 * @param[in] len message data length
 * @param[in] disp_action action type
 * @return OPRT_OK on success
 */
OPERATE_RET app_ui_action_cb(UINT8_T *msg, INT_T len, TY_DISPLAY_ACTION_E disp_action)
{
    return ui_dispatch_action(disp_action, msg, len);
}

/**
 * @brief Register action callback
 * @return none
 */
VOID_T app_ui_action_init(VOID_T)
{
    tuya_ai_display_action_register(app_ui_action_cb);
}
