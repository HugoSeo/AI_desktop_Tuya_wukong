#ifndef __GUI_ANINMATION_H__
#define __GUI_ANINMATION_H__

#include "tuya_gui_api.h"
#include "tuya_gui_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
*  Function: gui_anim_load_new_page_anim_time
*  Desc:     设置页面动画的新页面进入的过渡时间
***********************************************************/
VOID gui_anim_load_new_page_anim_time(UINT_T time);

/***********************************************************
*  Function: gui_anim_load_old_page_anim_time
*  Desc:     设置页面动画的上一个页面退出的过渡时间
***********************************************************/
VOID gui_anim_load_old_page_anim_time(UINT_T time);

/***********************************************************
*  Function: gui_anim_load_anim_offset
*  Desc:     设置页面动画的x方向起始偏移量
***********************************************************/
VOID gui_anim_load_anim_offset(UINT_T offset);

#ifdef __cplusplus
} // extern "C"
#endif

#endif

