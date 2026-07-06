/**
 * @file tuya_app_gui.h
 */

#ifndef TUYA_APP_GUI_H
#define TUYA_APP_GUI_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/

#include "lvgl.h"
#include "app_ipc_command.h"
#include "lv_vendor_event.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef bool (*TuyaGuiObjEventHdlFunc)(lv_event_t * e);
typedef struct
{
    LV_DISP_GUI_OBJ_TYPE_E obj_type;
    TuyaGuiObjEventHdlFunc GuiObjEvtHdl;
} gui_event_hdl_s;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**********************
 *      MACROS
 **********************/

void tuya_gui_main(void);

void tuya_app_gui_screen_saver_exit_request(void);
void tuya_app_gui_screen_saver_entry_request(void);

#ifdef LVGL_AS_APP_COMPONENT
void tuya_gui_lvgl_start(void);
void tuya_gui_lvgl_stop(void);
#endif

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*TUYA_APP_GUI_H*/
