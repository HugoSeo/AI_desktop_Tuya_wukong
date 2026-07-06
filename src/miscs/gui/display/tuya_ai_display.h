/**
 * @file tuya_ai_display.h
 * @author www.tuya.com
 * @version 0.1
 * @date 2025-02-07
 *
 * @copyright Copyright (c) tuya.inc 2025
 *
 */

 #ifndef __TUYA_AI_DISPLAY_H__
 #define __TUYA_AI_DISPLAY_H__
 
 #include "tuya_cloud_types.h"
 #include "tuya_cloud_com_defs.h"
 #include "tuya_device_cfg.h"

 #ifdef __cplusplus
 extern "C" {
 #endif
 
 /***********************************************************
 *********************** macro define ***********************
 ***********************************************************/

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
} TY_DISPLAY_TYPE_E;

typedef struct  {
    TY_DISPLAY_TYPE_E   type;
    UINT_T              len;
    UINT8_T             *data;
} TY_DISPLAY_MSG_T;

typedef enum {
    TY_DISP_ACT_OPEN_CAMERA = 0,
    TY_DISP_ACT_CLOSE_CAMERA,
    TY_DISP_ACT_CAMERA_OPEN_AI,
    TY_DISP_ACT_CAMERA_CLOSE_AI,
    TY_DISP_ACT_TAKE_PHOTO,
    TY_DISP_ACT_OPEN_ALBUM,
    TY_DISP_ACT_CLOSE_ALBUM,
    TY_DISP_ACT_ALBUM_VIEW_PREV_PIC,
    TY_DISP_ACT_ALBUM_VIEW_NEXT_PIC,
    TY_DISP_ACT_ALBUM_DELETE_PIC,
    TY_DISP_ACT_OPEN_ALBUM_GRID,
    TY_DISP_ACT_CLOSE_ALBUM_GRID,
    TY_DISP_ACT_ALBUM_BATCH_DELETE,
    TY_DISP_ACT_ALBUM_AI_RECOGNIZE,
    TY_DISP_ACT_OPEN_DEVICE_MODE,
    TY_DISP_ACT_CLOSE_DEVICE_MODE,
    TY_DISP_ACT_MAX,
} TY_DISPLAY_ACTION_E;

typedef OPERATE_RET (*ty_ai_proc_text_notify_cb_t)(UINT8_T *msg, INT_T len, TY_DISPLAY_TYPE_E display_tp);
typedef OPERATE_RET (*ty_ai_disp_action_cb_t)(UINT8_T *msg, INT_T len, TY_DISPLAY_ACTION_E disp_action);

 /***********************************************************
 ******************* function declaration *******************
 ***********************************************************/
 
VOID tuya_ai_display_init(VOID);
VOID tuya_ai_display_start(BOOL_T is_mf_test);
VOID tuya_ai_display_pause(VOID);
VOID tuya_ai_display_resume(VOID);
VOID tuya_ai_display_flush(VOID *data);
OPERATE_RET tuya_ai_display_msg(UINT8_T *msg, INT_T len, TY_DISPLAY_TYPE_E display_tp);

OPERATE_RET tuya_ai_display_action_register(ty_ai_disp_action_cb_t cb);
OPERATE_RET tuya_ai_display_action_post(UINT8_T *msg, INT_T len, TY_DISPLAY_ACTION_E disp_action);
#ifdef __cplusplus
}
#endif

#endif /* __TUYA_AI_DISPLAY_H__ */

