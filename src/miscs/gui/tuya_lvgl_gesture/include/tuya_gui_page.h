#ifndef __GUI_PAGE_H__
#define __GUI_PAGE_H__

#include "tuya_gui_api.h"
#include "tuya_gui_base.h"
#include "tuya_gui_navigate.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
*  Function: gui_base_page_init
*  Desc:     页面进行基础属性的配置
*  Input:    p_page: 页面obj
*  Return:   OPRT_OK: success  Other: fail
***********************************************************/
OPERATE_RET gui_base_page_init(lv_obj_t *p_page);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __GUI_EVENT_H__

