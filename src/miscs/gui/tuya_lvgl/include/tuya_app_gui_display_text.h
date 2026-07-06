#ifndef __DISPLAY_DATA_H__
#define __DISPLAY_DATA_H__

#ifdef __cplusplus
extern "C" {
#endif
#include "lv_vendor_event.h"
#include "tuya_app_gui_fs_path.h"

/*node_name对应json文件里的节点名*/
char * tuya_app_gui_display_text_get(char *node_name);

/*node_name对应json文件里的字库名*/
const void *tuya_app_gui_display_text_font_get(char *node_name);

/*获取当前环境语言类型*/
GUI_LANGUAGE_E tuya_app_gui_display_text_font_get_language(void);

OPERATE_RET tuya_app_display_text_init(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif

