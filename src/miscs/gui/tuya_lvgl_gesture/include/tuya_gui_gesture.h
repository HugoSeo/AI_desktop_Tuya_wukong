#ifndef __GUI_GESTURE_H__
#define __GUI_GESTURE_H__

#include "tuya_gui_api.h"
#include "lvgl.h"

#define GUI_GESTURE_INTR_MIN    200     //ms
#define TY_GESTURE_EDGE_MAX     30      //pix

typedef enum{
    TY_GUI_GESTURE_TP_TOP = 0,
    TY_GUI_GESTURE_TP_MID = 1,           
    TY_GUI_GESTURE_TP_BOTTOM = 2,           

} TY_GUI_GEST_START_TP_E;

#pragma pack(1)

typedef struct {
    lv_dir_t         last_dir;
    SYS_TICK_T       last_time;         //ms    TODO: rtos
    BOOL_T           round_screen_mode; // 圆形屏幕模式
} TUYA_GEST_MGR_S;
#pragma pack()

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
*  Function: gui_gesture_get_dir
*  Desc:     获取正在进行的手势操作方向
*  Output:   p_dir: 手势方向
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET gui_gesture_get_dir(lv_dir_t *p_dir);

/***********************************************************
*  Function: gui_gesture_Round_screen_mode
*  Desc:     滑动返回功能适配圆形屏幕
*  Input:    en:开启或者关闭
***********************************************************/
VOID_T gui_gesture_Round_screen_mode(BOOL_T en);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __GUI_GESTURE_H__

