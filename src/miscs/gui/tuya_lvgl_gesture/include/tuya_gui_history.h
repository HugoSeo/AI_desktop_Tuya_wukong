#ifndef __TUYA_GUI_HISTORY_H__
#define __TUYA_GUI_HISTORY_H__

#include "tuya_gui_api.h"
#include "tuya_gui_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
*  Function: tuya_gui_history_start
*  Desc:     模块start
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_gui_history_start();

/***********************************************************
*  Function: tuya_gui_history_push
*  Desc:     page id压入history表中
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_gui_history_push(TY_PAGE_TP_E type, CHAR_T *page_id);

/***********************************************************
*  Function: tuya_gui_history_get_current
*  Desc:     获取当前的page id
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_gui_history_get_current(CHAR_T *id);

/***********************************************************
*  Function: tuya_gui_history_get_current_type
*  Desc:     获取当前的page类型
*  Return:   page类型
***********************************************************/
TY_PAGE_TP_E tuya_gui_history_get_current_type();

/***********************************************************
*  tuya_gui_history_goto_special_last: goto到历史page的上一个(自定义类型)
*  tuya_gui_history_goto_last: goto到历史page的上一个(过滤掉TY_PAGE_TP_OTHER)
*  tuya_gui_history_goto_last_main: goto到历史page的上一个TY_PAGE_TP_MAIN类型的page
***********************************************************/
OPERATE_RET tuya_gui_history_goto_special_last(TY_PAGE_TP_E type);
#define tuya_gui_history_goto_last() tuya_gui_history_goto_special_last(TY_PAGE_TP_MAIN | TY_PAGE_TP_SUB)
#define tuya_gui_history_goto_last_main() tuya_gui_history_goto_special_last(TY_PAGE_TP_MAIN)

/***********************************************************
*  Function: tuya_gui_history_goto_special_last_anim
*  Desc: goto到历史page的上一个，带过渡动画效果
*  Input:    anim_time: 0代表使用默认值
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_gui_history_goto_special_last_anim(TY_PAGE_TP_E type, TY_PAGE_ANIM_TP_E anim_type, UINT_T anim_time);
#define tuya_gui_history_goto_last_main_anim(anim_type, anim_time) \
    tuya_gui_history_goto_special_last_anim(TY_PAGE_TP_MAIN, anim_type, anim_time)
#define tuya_gui_history_goto_last_anim(anim_type, anim_time) \
    tuya_gui_history_goto_special_last_anim(TY_PAGE_TP_MAIN | TY_PAGE_TP_SUB, anim_type, anim_time)

/***********************************************************
*  Function: tuya_gui_history_get_special_last
*  Desc:     获取历史page的上一个的page id
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_gui_history_get_special_last(TY_PAGE_TP_E type, CHAR_T *id);
#define tuya_gui_history_get_last(id) tuya_gui_history_get_special_last(TY_PAGE_TP_ALL, id)

#ifdef __cplusplus
} // extern "C"
#endif

#endif

