#ifndef _TY_SDK_CALL_H
#define _TY_SDK_CALL_H

#include "tuya_tmm_control.h"
#include "tal_workq_service.h"
#include "tuya_ipc_media_adapter.h"


#ifdef __cplusplus
extern "C" {
#endif

/* 通话业务生命周期事件:载荷为 TUYA_TMM_CONTROL_EVT_E(RINGING/INCOMING/
 * ACCEPTED/HANGUP/...)。由 tuya_sdk_call.c 在收到 SDK 回调时发布,UI 服务订阅。
 * 注意:P2P 媒体流事件请用 TUYA_P2P_MEDIA_STREAM(见 tuya_p2p_app.h)。 */
#define TUYA_IPC_CALL "tuya.ipc.call"

/**双向视频呼叫功能初始化*/
OPERATE_RET TUYA_IPC_call_init();

/**主动呼叫app*/
OPERATE_RET TUYA_IPC_call_app();

/**挂断与app的通话*/
OPERATE_RET TUYA_IPC_hangup();

/**接听来电*/
OPERATE_RET TUYA_IPC_answer();

/**拒接来电（回复忙）*/
OPERATE_RET TUYA_IPC_reject();

/**
 * 来电自动接通开关（仅内存态，不落 KV）。
 * 持久化由 UI 侧 (ui_svc_dev_ctrl) 负责，启动时通过 set 同步进来。
 */
VOID    TUYA_IPC_call_auto_answer_set(BOOL_T on);
BOOL_T  TUYA_IPC_call_auto_answer_get(VOID);

#ifdef __cplusplus
}
#endif

#endif