/**
 * @file tuya_ai_display.c
 * @brief AI display message dispatch — UI-agnostic implementation
 *
 * This module provides the display message bus between business code and
 * whatever UI implementation is active. It forwards messages (business → UI)
 * via a registered msg handler.
 *
 * It does NOT contain any LVGL/GUI code or rendering logic.
 */

#include "tuya_ai_display.h"

/***********************************************************
 *********************** private data ***********************
 ***********************************************************/
static ty_ai_display_msg_cb_t s_msg_handler = NULL;
static BOOL_T s_paused = FALSE;

/***********************************************************
 ********************** public functions ********************
 ***********************************************************/

VOID tuya_ai_display_init(VOID)
{
    s_paused = FALSE;
}

VOID tuya_ai_display_start(BOOL_T is_mf_test)
{
    (void)is_mf_test;
}

VOID tuya_ai_display_pause(VOID)
{
    s_paused = TRUE;
}

VOID tuya_ai_display_resume(VOID)
{
    s_paused = FALSE;
}

VOID tuya_ai_display_flush(VOID *data)
{
    (void)data;
}

OPERATE_RET tuya_ai_display_msg(UINT8_T *msg, INT_T len, TY_DISPLAY_TYPE_E display_tp)
{
    if (s_paused) {
        return OPRT_OK;
    }
    if (s_msg_handler) {
        return s_msg_handler(msg, len, display_tp);
    }
    return OPRT_OK;
}

OPERATE_RET tuya_ai_display_msg_handler_register(ty_ai_display_msg_cb_t cb)
{
    s_msg_handler = cb;
    return OPRT_OK;
}
