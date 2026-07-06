#ifndef __APP_IPC_COMMAND_H_
#define __APP_IPC_COMMAND_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "lv_vendor_event.h"
#include "tkl_lvgl.h"
#include "tuya_app_gui_core_config.h"

#ifdef TUYA_APP_USE_TAL_IPC
#include "tal_ipc.h"
#define GUI_IPC_EVENT_NAME "app_gui"
#endif

typedef enum{
    AIC_DIR_CP02CP1 = 0,                //核间通讯发送方向:cp0->cp1
    AIC_DIR_CP12CP0 = 1,                //核间通讯发送方向:cp1->cp0
    AIC_DIR_UNKNOWN = 0xFF
}APP_IPC_DIR_TYPE_E;

typedef enum{
/*cpu0 指令 开始*/
    AIC_LVGL_SEND_MSG_EVENT,        //通知CPU1同步对象状态
    AIC_LCD_BACKLIGHT_ON,          //通知CPU1点亮LCD背屏
    AIC_LCD_BACKLIGHT_OFF,         //通知CPU1关闭LCD背屏
    AIC_LCD_BACKLIGHT_SET,         //通知CPU1设置LCD背屏亮度
/*cpu0 指令 结束*/

/*cpu1 指令 开始*/
    AIC_LV_MSG_EVT_CHANGE,    //lvgl widgets changed!
/*cpu1 指令 结束*/
}APP_IPC_CMD_E;

typedef struct{
    uint32_t cmd;
    uint32_t param1;
    uint32_t param2;
    uint32_t param3;
    uint32_t param4;
    int32_t    ret;
}app_message_t;

#ifndef TUYA_APP_MULTI_CORE_IPC
typedef VOID(*app_no_ipc_callback)(VOID *buf, UINT_T len, VOID *args);
VOID app_no_ipc_cp0_recv_callback_register(app_no_ipc_callback cb);
VOID app_no_ipc_cp1_recv_callback_register(app_no_ipc_callback cb);
#endif

//#if (CONFIG_CPU_INDEX == 0)
#include "tuya_list.h"

typedef struct
{
    LIST_HEAD node;
    LV_DISP_EVENT_S evt;
} GUI_DISP_EVENT_LIST_S;

OPERATE_RET ty_disp_lcd_backlight_open(VOID_T);
OPERATE_RET ty_disp_lcd_backlight_close(VOID_T);
OPERATE_RET ty_disp_lcd_backlight_set(UCHAR_T light_percent);
OPERATE_RET tuya_disp_gui_event_init(LIST_HEAD *evt_list_head);
OPERATE_RET tuya_disp_gui_event_send(LV_DISP_EVENT_S *evt);
#ifdef TUYA_APP_USE_TAL_IPC
OPERATE_RET tuya_app_init_ipc(TKL_LVGL_EVENT_CB ipc_hdl_cb);
#endif
//#endif

//#if (CONFIG_CPU_INDEX == 1)
void aic_cpu1_callback(void *data, unsigned int size, void *param);
int aic_lvgl_msg_event_change(void *lv_msg);
//#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
