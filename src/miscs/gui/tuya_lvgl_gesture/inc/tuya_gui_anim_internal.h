#ifndef __GUI_ANINMATION_INTERNAL_H__
#define __GUI_ANINMATION_INTERNAL_H__

#include "tuya_gui_api.h"
#include "tuya_gui_base.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GUI_PAGE_ANIM_OFFSET                      20
#define GUI_PAGE_LOAD_NEW_ANIM_DEF_TIME           150
#define GUI_PAGE_LOAD_OLD_ANIM_DEF_TIME           250

VOID tuya_gui_nav_anim_start(lv_obj_t *new_obj, TUYA_NAV_PAGE_S *p_last_nav, TY_PAGE_ANIM_TP_E anim_type, UINT_T anim_time);
OPERATE_RET tuya_gui_nav_anim_clear();

#ifdef __cplusplus
} // extern "C"
#endif

#endif

