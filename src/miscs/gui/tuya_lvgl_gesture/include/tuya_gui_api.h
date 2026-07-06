#ifndef __TUYA_GUI_API_H__
#define __TUYA_GUI_API_H__

#include "tuya_gui_compatible.h"
#include "_cJSON.h"
#include "lvgl.h"
#include "tuya_gui_error_report.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
*  Function: tuya_gui_gesture_init
*  Desc:     gui手势模块初始化
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_gui_gesture_init();

/***********************************************************
*  Function: tuya_gui_set_base_dir
*  Desc:     设置页面展示方向
*  Input:    lv_base_dir_t 参考定义
*  Return:   VOID
***********************************************************/
VOID tuya_gui_set_base_dir(lv_base_dir_t dir);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __GUI_OS_API_H__

