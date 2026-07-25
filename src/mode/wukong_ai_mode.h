/**
 * @file wukong_ai_mode.h
 * @brief AI mode management and dispatch interface.
 *
 * Defines the two-level mode hierarchy:
 *   Level 1 (device mode): chat, translate, p2p, record, picture, detection
 *   Level 2 (chat sub-mode, only for chat): hold, oneshot, wakeup, free
 *
 * @copyright Copyright (c) 2023 Tuya Inc. All Rights Reserved.
 */

#ifndef __AI_CHAT_MODE_H__
#define __AI_CHAT_MODE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "uni_log.h"
#include "tuya_device_cfg.h"
#include "tuya_app_config.h"
#include "tal_memory.h"
#include "tuya_key.h"
#include "tal_mutex.h"
#include "wukong_ai_agent.h"
#include "wukong_audio_input.h"
#include "tuya_ai_toy_led.h"
#include "wukong_audio_player.h"
#include "tuya_ai_agent.h"
#include "tuya_ai_protocol.h"
#include "base_event.h"
#include "wukong_ai_skills.h"
#include "skill_emotion.h"
#include "wukong_audio_aec_vad.h"
#ifdef ENABLE_TUYA_UI
#include "tuya_ai_display.h"
#endif
#if defined(ENABLE_TUYA_CODEC_OPUS) && (ENABLE_TUYA_CODEC_OPUS == 1)
#include "tuya_ai_encoder_opus.h"
#include "tuya_ai_encoder.h"
#endif

#if defined(ENABLE_TUYA_PICTURE) && (ENABLE_TUYA_PICTURE == 1)
#include "wukong_picture.h"
#endif

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    OPERATE_RET (*input_audio)(AI_AUDIO_CODEC_TYPE codec_type, VOID *data, INT_T len);
} AI_RECORD_HANDLE_T;

typedef enum {
    AI_CHAT_SUB_HOLD,            // 长按触发模式
    AI_CHAT_SUB_ONESHOT,         // 单次按键，回合制对话模式
    AI_CHAT_SUB_WAKEUP,          // 关键词唤醒模式
    AI_CHAT_SUB_FREE,            // 关键词唤醒和自由对话模式
    AI_CHAT_SUB_MAX,
} AI_CHAT_SUB_MODE_E;

typedef enum {
    AI_DEVICE_MODE_CHAT,         // 闲聊模式（使用 AI_CHAT_SUB_MODE_E 子模式）
    AI_DEVICE_MODE_TRANSLATE,    // 翻译模式
    AI_DEVICE_MODE_P2P,          // P2P模式
    AI_DEVICE_MODE_RECORD,       // 录音模式
    AI_DEVICE_MODE_PICTURE,      // 生图模式
    AI_DEVICE_MODE_DETECTION,    // 侦测模式
    AI_DEVICE_MODE_MAX,
} AI_DEVICE_MODE_E;

typedef enum {
    AI_CHAT_INIT,
    AI_CHAT_IDLE,
    AI_CHAT_LISTEN,
    AI_CHAT_UPLOAD,
    AI_CHAT_THINK,
    AI_CHAT_SPEAK,
    AI_CHAT_INVALID,
} AI_CHAT_STATE_E;

/** Common per-mode context: wakeup status and chat state (used by hold/oneshot/wakeup/free/p2p/translate) */
typedef struct {
    BOOL_T wakeup_stat;
    AI_CHAT_STATE_E state;
} AI_CHAT_MODE_PARAM_T;

/** Mode handle: abstract callbacks (on_* = "on event") for strategy pattern */
typedef struct {
    OPERATE_RET (*on_init)          (VOID *data, INT_T len);
    OPERATE_RET (*on_deinit)        (VOID *data, INT_T len);
    OPERATE_RET (*on_key)           (VOID *data, INT_T len);
    OPERATE_RET (*on_task)          (VOID *data, INT_T len);
    OPERATE_RET (*on_event)         (VOID *data, INT_T len);
    OPERATE_RET (*on_wakeup)        (VOID *data, INT_T len);
    OPERATE_RET (*on_vad)           (VOID *data, INT_T len);
    OPERATE_RET (*on_client)        (VOID *data, INT_T len);
    OPERATE_RET (*on_notify_idle)   (VOID *data, INT_T len);
    OPERATE_RET (*on_audio_input)   (VOID *data, INT_T len);  /**< optional; null => default upload */
} AI_CHAT_MODE_HANDLE_T;

/** Per-mode entry: enabled flag, mutex, and handler */
typedef struct {
    BOOL_T enabled;
    MUTEX_HANDLE mutex;
    AI_CHAT_MODE_HANDLE_T *handle;
} AI_MODE_ENTRY_T;

/** Mode manager: two-level mode state with active entry pointer */
typedef struct {
    AI_DEVICE_MODE_E    device_mode;
    AI_CHAT_SUB_MODE_E  chat_sub_mode;
    AI_MODE_ENTRY_T     chat_subs[AI_CHAT_SUB_MAX];
    AI_MODE_ENTRY_T     device_modes[AI_DEVICE_MODE_MAX];
    AI_MODE_ENTRY_T    *active;
} AI_MODE_MGR_T;

extern CHAR_T *_chat_sub_str[];
extern CHAR_T *_device_mode_str[];
extern CHAR_T *_state_str[];

#define CHAT_SUB_STATE_CHANGE(_sub, _old, _new) \
do { \
    PR_DEBUG("mode %s state change from %s to %s", _chat_sub_str[_sub], _state_str[_old], _state_str[_new]); \
    _old = _new; \
} while (0)

#define DEVICE_MODE_STATE_CHANGE(_mode, _old, _new) \
do { \
    PR_DEBUG("mode %s state change from %s to %s", _device_mode_str[_mode], _state_str[_old], _state_str[_new]); \
    _old = _new; \
} while (0)

/** Mode operation for unified dispatch (Strategy pattern: one entry, delegate to current mode) */
typedef enum {
    AI_MODE_OP_INIT,
    AI_MODE_OP_DEINIT,
    AI_MODE_OP_KEY,
    AI_MODE_OP_TASK,
    AI_MODE_OP_EVENT,
    AI_MODE_OP_WAKEUP,
    AI_MODE_OP_VAD,
    AI_MODE_OP_CLIENT,
    AI_MODE_OP_NOTIFY_IDLE,
    AI_MODE_OP_AUDIO_INPUT,
    AI_MODE_OP_MAX,
} AI_MODE_OP_E;

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */

/**
 * @brief Dispatch operation to the currently active mode.
 * @param[in] op   Operation type.
 * @param[in] data Payload (may be NULL).
 * @param[in] len  Payload length in bytes.
 * @return OPRT_OK on success, OPRT_NOT_FOUND when no handler, or handler result.
 */
OPERATE_RET wukong_ai_mode_dispatch(AI_MODE_OP_E op, VOID *data, INT_T len);

/**
 * @brief Initialize the AI mode module.
 * @return OPRT_OK on success, or an error code.
 */
OPERATE_RET wukong_ai_mode_init(VOID);

/**
 * @brief Switch to a specific device mode.
 * @param[in] mode Target device mode.
 * @return OPRT_OK on success, or OPRT_NOT_SUPPORTED if the mode is not enabled.
 */
OPERATE_RET wukong_ai_device_mode_switch(AI_DEVICE_MODE_E mode);

/**
 * @brief Switch to a specific chat sub-mode (only when device_mode == CHAT).
 * @param[in] sub Target chat sub-mode.
 * @return OPRT_OK on success.
 */
OPERATE_RET wukong_ai_chat_sub_mode_switch(AI_CHAT_SUB_MODE_E sub);

/**
 * @brief Cycle to next enabled chat sub-mode (for double-press in chat mode).
 * @return New sub-mode on success, or OPRT_NOT_SUPPORTED.
 */
OPERATE_RET wukong_ai_chat_sub_mode_cycle(VOID);

/**
 * @brief Get the mode manager (read-only access for query).
 * @return Pointer to the mode manager.
 */
CONST AI_MODE_MGR_T *wukong_ai_mode_mgr_get(VOID);

/**
 * @brief Set the record handle callbacks.
 * @param[in] handle Pointer to the record handle to copy from.
 * @return OPRT_OK on success, OPRT_INVALID_PARM if handle is NULL.
 */
OPERATE_RET wukong_ai_record_handle_set(CONST AI_RECORD_HANDLE_T *handle);

/**
 * @brief Get the current record handle.
 * @return Pointer to the internal record handle structure.
 */
AI_RECORD_HANDLE_T *wukong_ai_record_handle_get(VOID);

#ifdef __cplusplus
}
#endif

#endif  // __AI_CHAT_MODE_H__
