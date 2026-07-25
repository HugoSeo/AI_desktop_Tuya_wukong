/**
 * @file wukong_kws.h
 * @brief Wukong AI Keyword Spotting (KWS) module interface
 *
 * This module provides keyword wake-up detection with two access modes:
 * - On-board mic: via create/detect/reset/deinit callbacks to TUTUClear or SNDX engines
 * - UART external: via tdl_comm_audio for wake events from external CODEC chip
 *
 * @version 1.0
 * @copyright Copyright (c) Tuya Inc.
 */

#ifndef __WUKONG_KWS_H__
#define __WUKONG_KWS_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Wake word event name; subscribe to receive EVENT_WUKONG_KWS_WAKEUP */
#define EVENT_WUKONG_KWS_WAKEUP "event.kws.wakeup"

/**
 * @brief Wake word index enumeration
 *
 * Wake word ID mapping agreed with cloud/engine for identification
 */
typedef enum {
    WUKONG_KWS_NIHAOTUYA = 1,           /**< Ni Hao Tuya */
    WUKONG_KWS_XIAOZHITONGXUE = 2,      /**< Xiao Zhi Tong Xue */
    WUKONG_KWS_HEYTUYA = 3,             /**< hey-tuya */
    WUKONG_KWS_SMARTLIFE = 4,           /**< smartlife */
    WUKONG_KWS_ZHINENGGUANJIA = 5,      /**< Smart butler */
    WUKONG_KWS_UDF1 = 6,               /**< User-defined wake word 1 */
    WUKONG_KWS_UDF2 = 7,               /**< User-defined wake word 2 */
    WUKONG_KWS_UDF3 = 8,               /**< User-defined wake word 3 */
} WUKONG_KWS_INDEX_E;

/**
 * @brief KWS engine context
 *
 * Filled by engine (TUTUClear/SNDX) at create, used by detect/reset/deinit
 */
typedef struct {
    VOID *handle;       /**< Engine instance handle */
    VOID *priv_data;    /**< Engine private data (e.g. externally allocated memory) */
} WUKONG_KWS_CTX_T;

/**
 * @brief KWS engine config and callbacks
 *
 * Injected via wukong_kws_init(); defines create/detect/reset/deinit callbacks
 */
typedef struct {
    INT_T (*create)(WUKONG_KWS_CTX_T *ctx);   /**< Create engine instance, init ctx */
    INT_T (*detect)(WUKONG_KWS_CTX_T *ctx, UINT8_T *data, UINT32_T datalen); /**< Detect one frame; returns OPRT_OK on wake */
    INT_T (*reset)(WUKONG_KWS_CTX_T *ctx);    /**< Reset engine state */
    INT_T (*deinit)(WUKONG_KWS_CTX_T *ctx);  /**< Destroy engine, release resources */
    UINT8_T is_detect_vad;                     /**< 1=throttle detect by VAD; 0=continuous detect */
    UINT32_T detect_sample_bytes;              /**< 0=read all available; >0=read in multiples of this size (bytes per detect unit) */
} WUKONG_KWS_CFG_T;

/**
 * @brief Initialize KWS with default engine (TUTUClear)
 *
 * @return OPRT_OK on success; see tuya_error_code.h for others
 */
INT_T wukong_kws_default_init(VOID);

/**
 * @brief Initialize KWS with custom engine config
 *
 * @param cfg Engine callbacks and config; must not be NULL
 * @return OPRT_OK on success; see tuya_error_code.h for others
 */
INT_T wukong_kws_init(WUKONG_KWS_CFG_T *cfg);

/**
 * @brief Uninit KWS, stop detect thread and release resources
 *
 * @return OPRT_OK
 */
INT_T wukong_kws_uninit(VOID);

/**
 * @brief Feed one frame PCM data with optional VAD flag
 *
 * Audio format: 16kHz, 16bit, mono; frame length by caller (typically 20ms)
 *
 * @param data Raw PCM data
 * @param datalen Byte length
 * @param vadflag 1=voice present, 0=silence; with is_detect_vad controls when to detect
 * @return OPRT_OK on success; OPRT_RESOURCE_NOT_READY if not inited or disabled
 */
INT_T wukong_kws_feed_with_vad(UINT8_T *data, UINT16_T datalen, UINT8_T vadflag);

/**
 * @brief Enable KWS detection
 *
 * @return OPRT_OK
 */
INT_T wukong_kws_enable(VOID);

/**
 * @brief Disable KWS detection
 *
 * @return OPRT_OK
 */
INT_T wukong_kws_disable(VOID);

/**
 * @brief Set whether to throttle detection by VAD
 *
 * @param is_detect_vad 1=detect only when VAD active; 0=continuous detect
 * @return OPRT_OK
 */
INT_T wukong_kws_set_vad_detect(UINT8_T is_detect_vad);

/**
 * @brief Report wake event (internal or external call)
 *
 * Publishes EVENT_WUKONG_KWS_WAKEUP with payload WUKONG_KWS_INDEX_E
 *
 * @param wakeup_kws_index Detected wake word index
 */
VOID wukong_kws_event(WUKONG_KWS_INDEX_E wakeup_kws_index);

#ifdef __cplusplus
}
#endif

#endif /* __WUKONG_KWS_H__ */
