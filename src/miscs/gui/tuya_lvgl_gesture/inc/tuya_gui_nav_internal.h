#ifndef __TUYA_GUI_NAV_INTERNAL_H__
#define __TUYA_GUI_NAV_INTERNAL_H__

#include "tuya_gui_api.h"
#include "tuya_gui_base.h"
#include "tuya_gui_gesture.h"
#include "tuya_gui_navigate.h"

#define TY_HOR_TABVIEW_ANIM_TIME    100 //ms

typedef PVOID_T TY_PAGE_PRIV_CTX;

//#pragma pack(1)
typedef struct {
    TY_PAGE_INIT_CB         page_init_cb;
    TUYA_GUI_PAGE_S         page;
    TY_PAGE_PRIV_CTX        priv_ctx;

    lv_obj_t                *obj;
    UINT8_T                 hor_idx;

    VOID                    *start_param;
    UINT_T                  param_len;

    SLIST_HEAD              node;

} TUYA_NAV_PAGE_S;

typedef struct {
    TY_PAGE_LAYER_E layer;
    BOOL_T          is_hor;             //horizontal page
    BOOL_T          forbid_cover;       //this page forbid cover
    BOOL_T          is_keep;            //keep start_param，goto last will use this param
    CHAR_T          *page_name;
    BOOL_T          is_swipe_back;      //side slide to back last page;
    BOOL_T          is_gesture_triggered;
    TY_SWIPE_BACK_FUNCTION     back_fn;    //back function

} TUYA_PAGE_PRIV_CTX_S;

typedef struct {
    TUYA_NAV_PAGE_S *hor_tabview;       //horizontal tabview page
    UINT16_T        hor_num;

} TUYA_NAV_HOR_S;

typedef struct {
    SLIST_HEAD      page_list;
    UINT16_T        page_num;
    lv_base_dir_t   dir;
    CHAR_T          top_gesture[GUI_PAGE_ID_MAX];
    TUYA_NAV_HOR_S  hor;

    CHAR_T          curr_page_id[GUI_PAGE_ID_MAX];      //当前start的page id

} TUYA_NAV_MGR_S;
//#pragma pack()

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET tuya_gui_navigate_start();
OPERATE_RET tuya_gui_nav_page_get(CHAR_T *id, TUYA_NAV_PAGE_S *nav);

/***********************************************************
*  Function: tuya_gui_page_reload_anim
*  Desc:     重新加载页面
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET tuya_gui_page_reload_anim(CHAR_T *id, TY_PAGE_ANIM_TP_E anim_type, UINT_T anim_time);

/***********************************************************
*  Function: tuya_page_get_forbid_cover
*  Desc:     获取页面是否forbid cover
*  Return:   BOOL_T
***********************************************************/
BOOL_T tuya_page_get_forbid_cover(CHAR_T *id);

/***********************************************************
*  Function: tuya_page_is_gesture_triggered
*  DDesc:     获取页面的gesture状态
*  Return:   BOOL_T
***********************************************************/
BOOL_T tuya_page_is_gesture_triggered(CHAR_T *id);

/***********************************************************
*  Function: tuya_page_is_swipe_back
*  Desc:     获取页面的swipe状态
*  Return:   BOOL_T
***********************************************************/
BOOL_T tuya_page_is_swipe_back(CHAR_T *id);

/***********************************************************
*  Function: tuya_page_get_swipe_back_fn
*  Desc:     获取边缘滑动返回上个界面的函数
*  Return:   SwipeBackFn 回调函数
***********************************************************/
TY_SWIPE_BACK_FUNCTION tuya_page_get_swipe_back_fn(CHAR_T *id);

/***********************************************************
*  Function: gui_nav_get_current_page_id
*  Desc:     获取当前页面ID
*  Return:   CHAR_T*
***********************************************************/
OPERATE_RET gui_nav_get_current_page_id(CHAR_T *id, int len);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __GUI_NAVIGATE_H__

