#ifndef __GUI_GESTURE_INTERNAL_H__
#define __GUI_GESTURE_INTERNAL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_gui_gesture.h"

OPERATE_RET gui_gesture_start();
VOID gui_gesture_screen_event(lv_event_t *e);


#ifdef __cplusplus
} // extern "C"
#endif

#endif
