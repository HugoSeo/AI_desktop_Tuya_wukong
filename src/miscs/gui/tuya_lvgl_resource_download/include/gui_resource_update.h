#ifndef __GUI_RESOURCE_UPDATE_H_
#define __GUI_RESOURCE_UPDATE_H_

#ifdef __cplusplus
extern "C" {
#endif
#include "lv_vendor_event.h"
#include "tuya_app_gui_fs_path.h"

gui_resource_info_s * tuya_gui_resource_statistics(void);

void tuya_gui_resource_release(gui_resource_info_s *gui_res);

void tuya_gui_resource_show(gui_resource_info_s *gui_res);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif

