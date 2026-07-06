#ifndef __GUI_NAVIGATE_H__
#define __GUI_NAVIGATE_H__

#include "tuya_gui_api.h"
#include "tuya_gui_base.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TY_GUI_EV_REFRESH
#define TY_GUI_EV_REFRESH       "ref"
#endif

/***********************************************************
*  Function: tuya_gui_nav_page_add
*  Desc:     添加页面到导航模块
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
typedef OPERATE_RET (*TY_PAGE_INIT_CB)(TUYA_GUI_PAGE_S *page);
OPERATE_RET tuya_gui_nav_page_add(CHAR_T *id, TY_PAGE_INIT_CB init_cb);
#define TY_GUI_PAGE_ADD tuya_gui_nav_page_add

/***********************************************************
*  Function: tuya_gui_goto_page
*  Desc:     跳转到新页面
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_gui_goto_page(CHAR_T *id, VOID *start_param, UINT_T param_len);

/***********************************************************
*  Function: tuya_gui_goto_page_anim
*  Desc:     跳转到新页面带过渡动画
*  Input:    anim_time: 0代表使用默认值
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_gui_goto_page_anim(CHAR_T *id, VOID *start_param, UINT_T param_len, 
                                    TY_PAGE_ANIM_TP_E anim_type, UINT_T anim_time);
#define tuya_gui_nav_goto_page(id) tuya_gui_goto_page(id, NULL, 0)
#define tuya_gui_nav_goto_page_anim(id, anim, anim_time) tuya_gui_goto_page_anim(id, NULL, 0, anim, anim_time)

/***********************************************************
*  Function: tuya_gui_page_reload
*  Desc:     重新加载页面
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_gui_page_reload(CHAR_T *id);

/***********************************************************
*  Function: gui_nav_set_top_gesture
*  Desc:     设置顶部下拉的手势页面
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET gui_nav_set_top_gesture(CHAR_T *id);

/***********************************************************
*  Function: tuya_page_set_forbid_cover
*  Desc:     设置页面禁止覆盖,该页面将不会被下拉页面覆盖
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_page_set_forbid_cover(CHAR_T *id, BOOL_T forbid_cover);

/***********************************************************
*  Function: tuya_page_set_layer
*  Desc:     设置页面层级
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_page_set_layer(CHAR_T *id, TY_PAGE_LAYER_E layer);

/***********************************************************
*  Function: tuya_page_set_hor
*  Desc:     设置页面为horizontal page，水平页面可以左右滑动
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_page_set_hor(CHAR_T *id, BOOL_T is_hor);

/***********************************************************
*  Function: tuya_page_set_gesture_triggered
*  Desc:     设置页面为gesture triggered 状态
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_page_set_gesture_triggered(CHAR_T *id, BOOL_T is_gesture_triggered);

/***********************************************************
*  Function: tuya_page_set_swipe_back
*  Desc:     设置页面为swipe back, 边缘滑动可以返回上个界面
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_page_set_swipe_back(CHAR_T *id, BOOL_T is_swipe_back);

/***********************************************************
*  Function: tuya_page_set_swipe_back_fn
*  Desc:     设置返回页面函数
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_page_set_swipe_back_fn(CHAR_T *id,  TY_SWIPE_BACK_FUNCTION fn);

/***********************************************************
*  Function: tuya_page_set_param_keep
*  Desc: 设置是否保存参数, keep的页面在进行goto page时将参数保存，被history模块reload时，page start使用已保存的参数
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_page_set_param_keep(CHAR_T *id, BOOL_T is_keep);

/***********************************************************
*  Function: tuya_page_set_type
*  Desc:     设置页面类型
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_page_set_type(CONST CHAR_T *id, TY_PAGE_TP_E type);

/***********************************************************
*  Function: tuya_page_set_create_cb
*  Desc:     设置页面create回调
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_page_set_create_cb(CONST CHAR_T *id, TY_PAGE_CREATE_CB cb);

/***********************************************************
*  Function: tuya_page_set_start_cb
*  Desc:     设置页面start回调
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_page_set_start_cb(CONST CHAR_T *id, TY_PAGE_START_CB cb);

/***********************************************************
*  Function: tuya_page_set_stop_cb
*  Desc:     设置页面stop回调
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_page_set_stop_cb(CONST CHAR_T *id, TY_PAGE_STOP_CB cb);

/***********************************************************
*  Function: tuya_page_set_hor_tab_hidden
*  Desc:     设置tabview的tab隐藏
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_page_set_hor_tab_hidden(BOOL_T hidden);

/***********************************************************
*  Function: tuya_gui_add_sys_tabview
*  Desc:     添加tabview页面
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_gui_add_sys_tabview(CHAR_T *id, TY_PAGE_INIT_CB init_cb);

/***********************************************************
*  Function: gui_nav_get_top_gesture
*  Desc:     获取顶部下拉的手势页面
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET gui_nav_get_top_gesture(CHAR_T *id, UINT_T len);

/***********************************************************
*  Function: tuya_page_set_name
*  Desc:     设置页面name
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_page_set_name(CHAR_T *id, CHAR_T *name);

/***********************************************************
*  Function: tuya_gui_nav_obj_get
*  Desc:     获取page的lvgl obj
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_gui_nav_obj_get(CHAR_T *id, lv_obj_t **obj);

/***********************************************************
*  Function: tuya_page_is_hor
*  Desc:     判断页面是否为水平页面
*  Return:   BOOL_T
***********************************************************/
BOOL_T tuya_page_is_hor(CHAR_T *id);

/***********************************************************
 * Function: gui_nav_set_hor_idx
 * Desc:     设置页面hor_idx
 * Return:   OPRT_OK: success  Other: fail
 * ***********************************************************/
OPERATE_RET gui_nav_set_hor_idx(CHAR_T *id, UINT8_T hor_idx);

/***********************************************************
*  Function: tuya_gui_page_delete
*  Desc:     删除页面
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_gui_page_delete(CHAR_T *id);

#ifdef __cplusplus
} // extern "C"
#endif

#endif

