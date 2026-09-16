/**
 * @file tuya_ai_display.h
 * @brief AI display message interface (UI-agnostic)
 *
 * This header defines the display message types used by business code to
 * communicate with the UI layer. It is independent of any specific UI
 * implementation (old GUI or new framework).
 */

#ifndef __TUYA_AI_DISPLAY_H__
#define __TUYA_AI_DISPLAY_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
 ********************** typedef define **********************
 ***********************************************************/
typedef enum {
    TY_DISPLAY_TP_HUMAN_CHAT = 0,
    TY_DISPLAY_TP_AI_CHAT,
    TY_DISPLAY_TP_STAT_SLEEP,
    TY_DISPLAY_TP_STAT_WAKEUP,
    TY_DISPLAY_TP_STAT_NETCFG,
    TY_DISPLAY_TP_STAT_NET,
    TY_DISPLAY_TP_STAT_POWERON,
    TY_DISPLAY_TP_STAT_ONLINE,
    TY_DISPLAY_TP_CHAT_MODE,
    TY_DISPLAY_TP_CHAT_STAT,
    TY_DISPLAY_TP_MALLOC = 10,
    TY_DISPLAY_TP_EMOJI,
    TY_DISPLAY_TP_VOLUME,
    TY_DISPLAY_TP_ASR_EMOJI,
    TY_DISPLAY_TP_STAT_IDLE,
    TY_DISPLAY_TP_STAT_LISTEN,
    TY_DISPLAY_TP_STAT_SPEAK,
    TY_DISPLAY_TP_STAT_BATTERY,
    TY_DISPLAY_TP_STAT_CHARGING,
    TY_DISPLAY_TP_LANGUAGE = 19,
    TY_DISPLAY_TP_AI_CHAT_START,
    TY_DISPLAY_TP_AI_CHAT_DATA,
    TY_DISPLAY_TP_AI_CHAT_STOP,
    TY_DISPLAY_TP_LVGL_PAUSE,
    TY_DISPLAY_TP_LVGL_DATA,
    TY_DISPLAY_TP_LVGL_RESUME,
    TY_DISPLAY_TP_CLOCK_MCP_COUNTDOWN_TIMER,
    TY_DISPLAY_TP_CLOCK_MCP_STOPWATCH_TIMER,
    TY_DISPLAY_TP_CLOCK_MCP_POMODORO_TIMER,
    TY_DISPLAY_TP_CLOCK_MCP_SCHEDULE,
    TY_DISPLAY_TP_AI_IMAGE,
    TY_DISPLAY_TP_CLEAR_ATTACHMENT,
    TY_DISPLAY_TP_MODE_NOTIFY,
} TY_DISPLAY_TYPE_E;

typedef struct {
    TY_DISPLAY_TYPE_E   type;
    UINT_T              len;
    UINT8_T             *data;
} TY_DISPLAY_MSG_T;

typedef OPERATE_RET (*ty_ai_display_msg_cb_t)(UINT8_T *msg, INT_T len, TY_DISPLAY_TYPE_E display_tp);

/***********************************************************
 ******************* function declaration *******************
 ***********************************************************/

/**
 * @brief Initialize the display dispatch module (queues, threads)
 */
VOID tuya_ai_display_init(VOID);

/**
 * @brief Start display service
 * @param is_mf_test TRUE if in manufacture test mode
 */
VOID tuya_ai_display_start(BOOL_T is_mf_test);

/**
 * @brief Pause display updates
 */
VOID tuya_ai_display_pause(VOID);

/**
 * @brief Resume display updates
 */
VOID tuya_ai_display_resume(VOID);

/**
 * @brief Direct framebuffer flush (for raw image mode)
 */
VOID tuya_ai_display_flush(VOID *data);

/**
 * @brief Send a display message to the UI layer
 *
 * This is the primary API called by business code. The message is dispatched
 * to the registered message handler (UI implementation).
 */
OPERATE_RET tuya_ai_display_msg(UINT8_T *msg, INT_T len, TY_DISPLAY_TYPE_E display_tp);

/**
 * @brief Register the display message handler (called by UI implementation)
 *
 * The UI framework (old or new) registers its handler here. Messages sent
 * via tuya_ai_display_msg() are forwarded to this callback.
 */
OPERATE_RET tuya_ai_display_msg_handler_register(ty_ai_display_msg_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_AI_DISPLAY_H__ */
