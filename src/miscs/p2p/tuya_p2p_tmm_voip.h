/**
 * @file tuya_p2p_tmm_voip.h
 * @brief TMM VoIP adapter for tuyaos-ai (device-device & app-device calls).
 *
 * 本文件从 ai_video_voice_toy 移植并按 tuyaos-ai 约定改造(见 docs/adr/0002,0003)。
 * 与源工程的区别:AI 与通话「共存」——通话时切到 P2P 模式打断 AI,挂断切回 CHAT
 * 由目标模式 on_init/on_deinit 恢复 KWS/VAD/agent(不再逐项 suspend/resume)。
 *
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __TUYA_TOY_TMM_VOIP_H__
#define __TUYA_TOY_TMM_VOIP_H__

#include "tuya_cloud_types.h"

/* tuyaos-ai 是 AI 玩具,默认「共存」而非 VoIP-only(ADR 0003)。 */
#ifndef ENABLE_TUYA_TOY_VOIP_ONLY
#define ENABLE_TUYA_TOY_VOIP_ONLY 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TUYA_TOY_TMM_VOIP_STATUS_IDLE,
    TUYA_TOY_TMM_VOIP_STATUS_CALLING,
    TUYA_TOY_TMM_VOIP_STATUS_INCOMING,
    TUYA_TOY_TMM_VOIP_STATUS_INCALL,
} TUYA_TOY_TMM_VOIP_STATUS_E;

#define EVENT_TOY_VOIP_UI               "toy.voip.ui"
#define TUYA_TOY_VOIP_PEER_NAME_LEN     64
#define TUYA_TOY_VOIP_PEER_ID_LEN       32

typedef struct {
    TUYA_TOY_TMM_VOIP_STATUS_E status;
    CHAR_T peer_name[TUYA_TOY_VOIP_PEER_NAME_LEN];
    CHAR_T peer_id[TUYA_TOY_VOIP_PEER_ID_LEN];
} TUYA_TOY_VOIP_UI_EVT_T;

/** @brief 网络/MQTT ready 后启动 TMM VoIP 栈 */
OPERATE_RET tuya_toy_tmm_voip_start(VOID);

/** @brief 初始化 TMM VoIP(manager + stream + 音频回调) */
OPERATE_RET tuya_toy_tmm_voip_init(VOID);

/** @brief 向目标发起音频通话 */
OPERATE_RET tuya_toy_tmm_voip_call(CHAR_T *target_id);

/** @brief 接听来电 */
OPERATE_RET tuya_toy_tmm_voip_answer(VOID);

/** @brief 挂断/拒接当前通话 */
OPERATE_RET tuya_toy_tmm_voip_hangup(VOID);

/** @brief 获取当前通话状态 */
TUYA_TOY_TMM_VOIP_STATUS_E tuya_toy_tmm_voip_get_status(VOID);

/** @brief 把采集到的 16K 单声道 PCM 喂入上行(由 P2P 模式 on_audio_input 调用) */
OPERATE_RET tuya_toy_tmm_voip_audio_feed(VOID *data, INT_T len);

/** @brief 是否处于活动通话中(麦克风应路由到此) */
BOOL_T tuya_toy_tmm_voip_is_incall(VOID);

/** @brief 是否 VoIP-only 构建 */
BOOL_T tuya_toy_tmm_voip_is_voip_only(VOID);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_TOY_TMM_VOIP_H__ */
