#ifndef __UI_APP_H__
#define __UI_APP_H__

#include "tuya_cloud_types.h"
#include "tuya_ai_display.h"

typedef void (*ui_app_async_cb_t)(void *data);

void ui_app_init(void);
void ui_app_async_call(ui_app_async_cb_t cb, void *data);
void ui_app_notify_nfc_card_detected(const char *uuid);

/* Board UI provider 注册：board 在其 tuya_device_board_init() 中调用。
 * 注册后 override 默认 wukong 路径；不注册则 ui_app_init 走默认完整路径。 */
typedef void (*ui_board_init_fn)(void);                  /* 建屏，== board 原 app_ui_init */
typedef void (*ui_board_msg_fn)(TY_DISPLAY_MSG_T *msg);  /* == board 原 app_ui_msg_handler */
void ui_app_register_board_ui(ui_board_init_fn init, ui_board_msg_fn msg_handler);

/* 默认 wukong 完整页面集路径（从 ui_app_init 抽出，T5AI_BOARD/DESKTOP 用，无需注册）*/
void ui_app_wukong_init(void);

#endif /* __UI_APP_H__ */
