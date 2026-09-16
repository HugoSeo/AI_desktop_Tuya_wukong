/**
 * @file tuya_tmm_control.h
 * @brief TMM RTC signaling control API (SS190-compatible MQTT format)
 * @version 0.1
 * @date 2022-08-31
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __TUYA_TMM_CONTROL_H__
#define __TUYA_TMM_CONTROL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef enum {
    TUYA_TMM_CONTROL_EVT_RINGING,            /**< Peer is ringing, not yet answered */
    TUYA_TMM_CONTROL_EVT_INCOMING,           /**< Incoming call */
    TUYA_TMM_CONTROL_EVT_ACCEPTED,           /**< Peer accepted the call */
    TUYA_TMM_CONTROL_EVT_UNANSWERED,         /**< Peer did not answer */
    TUYA_TMM_CONTROL_EVT_REJECT,             /**< Peer rejected the call */
    TUYA_TMM_CONTROL_EVT_HANGUP,             /**< Peer hung up */
    TUYA_TMM_CONTROL_EVT_BUSY,               /**< Peer is busy */
    TUYA_TMM_CONTROL_EVT_CANCEL,             /**< Outgoing call cancelled before connect */
    TUYA_TMM_CONTROL_EVT_STOP,               /**< Call stopped abnormally */
    TUYA_TMM_CONTROL_EVT_ACCEPTED_BY_OTHERS, /**< Call accepted by another endpoint */
    TUYA_TMM_CONTROL_EVT_ERROR,              /**< Internal error */
} TUYA_TMM_CONTROL_EVT_E;

typedef enum {
    TUYA_TMM_CONTROL_STREAM_TYPE_AUDIO,      /**< Audio only */
    TUYA_TMM_CONTROL_STREAM_TYPE_VIDEO,      /**< Video only */
    TUYA_TMM_CONTROL_STREAM_TYPE_AV,         /**< Audio and video */
} TUYA_TMM_CONTROL_STREAM_TYPE_E;

typedef enum {
    TUYA_TMM_CONTROL_TARGET_TYPE_APP,          /**< Target is mobile app */
    TUYA_TMM_CONTROL_TARGET_TYPE_DEV,          /**< Target is device */
} TUYA_TMM_CONTROL_TARGET_TYPE_E;

typedef struct {
    TUYA_TMM_CONTROL_EVT_E          event;           /**< Signaling event */
    TUYA_TMM_CONTROL_STREAM_TYPE_E  stream_type;     /**< Stream type, valid on call event */
    TUYA_TMM_CONTROL_TARGET_TYPE_E  target_type;     /**< Target type: app or device */
    CHAR_T                          target_id[64];   /**< Target device id */
    CHAR_T                          target_localkey[64]; /**< Target local key */
    CHAR_T                          target_name[64]; /**< Target display name */
} TUYA_TMM_CONTROL_INFO_S;

typedef OPERATE_RET (*TUYA_TMM_CONTROL_EVT_CB)(TUYA_TMM_CONTROL_INFO_S *pinfo, VOID *priv_data);

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */
/**
 * @brief Initialize TMM control module
 * @param[in] cb event callback
 * @param[in] priv_data user private data passed back via callback
 * @param[in] call_timeout_s call timeout in seconds, <= 0 uses default 30s
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_tmm_control_init(TUYA_TMM_CONTROL_EVT_CB cb, VOID *priv_data, INT_T call_timeout_s);

/**
 * @brief Deinitialize TMM control module
 * @return OPRT_OK on success, OPRT_NOT_SUPPORTED if not implemented
 */
OPERATE_RET tuya_tmm_control_deinit(VOID);

/**
 * @brief Place outgoing RTC call
 * @param[in] target_device peer device id (dev id or key_* for app)
 * @param[in] category device category, e.g. dgnzk
 * @param[in] biz_type business type, NULL means default screen_ipc
 * @param[in] stream_type audio/video/av stream type
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_tmm_control_call(CHAR_T *target_device, CHAR_T *category, CHAR_T *biz_type,
                                  TUYA_TMM_CONTROL_STREAM_TYPE_E stream_type);

/**
 * @brief Answer incoming RTC call
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_tmm_control_answer(VOID);

/**
 * @brief Reject incoming RTC call
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_tmm_control_reject(VOID);

/**
 * @brief Cancel outgoing call before peer answers
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_tmm_control_cancel(VOID);

/**
 * @brief Hang up active call
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_tmm_control_hangup(VOID);

/**
 * @brief Stop call abnormally
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_tmm_control_stop(VOID);

/**
 * @brief Report device busy to peer
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_tmm_control_busy(VOID);

#ifdef __cplusplus
}
#endif

#endif /* __TUYA_TMM_CONTROL_H__ */
