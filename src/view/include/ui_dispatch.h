#ifndef __UI_DISPATCH_H__
#define __UI_DISPATCH_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "tuya_ai_display.h"

VOID_T ui_dispatch_msg(TY_DISPLAY_MSG_T *msg);
OPERATE_RET ui_dispatch_action(TY_DISPLAY_ACTION_E action, UINT8_T *msg, INT_T len);

#ifdef __cplusplus
}
#endif

#endif /* __UI_DISPATCH_H__ */
