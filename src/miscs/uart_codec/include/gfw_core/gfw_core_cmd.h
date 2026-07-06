/**
 * @file gfw_core_cmd.h
 * @brief
 * @version 0.1
 * @date 2023-06-14
 *
 * @copyright Copyright (c) 2023 Tuya Inc. All Rights Reserved.
 *
 * Permission is hereby granted, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), Under the premise of complying
 * with the license of the third-party open source software contained in the software,
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software.
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 */

#ifndef __CORE_CMD_H__
#define __CORE_CMD_H__

#include "tuya_cloud_types.h"
// #include "tuya_device_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GFW_DEF_PROTOCOL_VER (0x3) // default mcu protocol version
#define GFW_CMD_RESPONSE_TIMEOUT (3000) // ms
#define GFW_CMD_WAIT_FOREVER (0xFFFFffff)

#define INVALID_CMD 0xFF
#define INVALID_SUBCMD 0xFF
#define MIN_FRAME_LEN (7)

#if defined(ENABLE_GFW_CMD_STANDALONE_SECTION) && (ENABLE_GFW_CMD_STANDALONE_SECTION == 1)
#define GFW_CMD_SECTION __attribute__((section(".gfw_cmd")))
#else
#define GFW_CMD_SECTION
#endif

typedef enum {
    GFW_CMD_OTA_START = 0x0a,
    GFW_CMD_OTA_TRANS = 0x0b,
    GFW_CMD_VOICE_SERV = 0x92,
} AUDIO_GFW_CMD_E;

typedef enum {
    GFW_0x92_VER_QUERY = 0x01,
    GFW_0x92_MIC_CFG   = 0x02,
    GFW_0x92_SPK_CFG   = 0x03,
    GFW_0x92_WAKE_CFG  = 0x04,
    GFW_0x92_MIC_DATA  = 0x05,
    GFW_0x92_SPK_PLAY  = 0x06,
    GFW_0x92_VOICE_CFG = 0x07,
    GFW_0x92_VOICE_WAKE = 0x08,
    GFW_0x92_VOICE_SLEEP = 0x09,
    GFW_0x92_VOICE_QUERY = 0x0a,
    GFW_0x92_VOICE_TEST = 0x0b,
    GFW_0x92_AUDIO_TEST = 0x0c,
    GFW_0x92_GPIO_TEST = 0x0d,
    GFW_0x92_EVENT_NOTIFY = 0x12,
} GFW_0x92_SUBCMD_E;

typedef enum {
    AUDIO_GFW_EVENT_PLAY_START = 0x00000001,
    AUDIO_GFW_EVENT_PLAY_END = 0x00000003
} AUDIO_GFW_EVENT_E;

/**
 * @brief
 *
 */
typedef struct {
    UCHAR_T ver;
    UCHAR_T cmd; // cmd or subcmd
    USHORT_T len;
    UCHAR_T *data;
} AUDIO_GFW_CMD_DATA_T;

/**
 * @brief
 *
 */
typedef OPERATE_RET(*AUDIO_GFW_CMD_CB)(AUDIO_GFW_CMD_DATA_T *data);

/**
 * @brief
 *
 */
typedef struct {
    UCHAR_T cmd;
    UCHAR_T sub_cmd; // if no sub_cmd, set to INVALID_SUBCMD
    AUDIO_GFW_CMD_CB cb;
} AUDIO_GFW_CMD_CFG_T;

/**
 * @brief
 *
 * @param cmds
 * @param count
 * @return OPERATE_RET
 */
OPERATE_RET gfw_core_cmd_register(CONST AUDIO_GFW_CMD_CFG_T *cmds, UINT_T count);

/**
 * @brief Generally speaking, CMDs are asynchronous and placed in a sending queue,
 * which are sent one by one. If the previous CMD fails to send and needs to be resent,
 * the next CMD will not be sent until the previous CMD has successfully
 * sent (received ACK) or timed out. Then the next CMD will be sent (with the timeout
 * time starting at this point).
 *
 * Synchronous sending APIs will be blocked until the CMD receives an ACK or times out.
 *
 * Instant sending APIs will be sent out directly with the highest priority,
 * without waiting in line in the queue to be sent.
 *
 */

typedef enum {
    AUDIO_GFW_CMD_TYPE_DEFAULT, // Enqueue
    AUDIO_GFW_CMD_TYPE_SYNC,    // Enqueue and wait for complete (shall not execute within a CMD callback)
    AUDIO_GFW_CMD_TYPE_INSTANT, // Urgent delivery, do not enqueue (shall not execute within a CMD callback)
    AUDIO_GFW_CMD_TYPE_ZEROCOPY,// Urgent delivery (0 copy), do not enqueue (shall not execute within a CMD callback)
    AUDIO_GFW_CMD_TYPE_DIRECT,  // Direct delivery (shall not execute within a CMD callback)
} AUDIO_GFW_CMD_TYPE_E;

/**
 * @brief
 *
 */
typedef OPERATE_RET(*AUDIO_GFW_CMD_TIMEOUT_CB)(AUDIO_GFW_CMD_DATA_T *data);

/**
 * @brief Send a CMD with parameters to MCU.
 *
 * @param type The type of the CMD to send.
 * @param cmd The CMD to send.
 * @param data The data to send with the CMD.
 * @param len The length of the data to send.
 * @param timeout_ms The timeout time for sending the CMD every time.
 * @param timeout_cb The callback function for handling timeout.
 * @param resend_cnt The number of times to resend the CMD before timing out.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET gfw_core_cmd_send_with_params(AUDIO_GFW_CMD_TYPE_E type, BYTE_T cmd, BYTE_T *data, UINT16_T len,
                                             UINT_T timeout_ms, AUDIO_GFW_CMD_TIMEOUT_CB timeout_cb, UINT_T resend_cnt);

/**
 * @brief Enqueues a CMD for sending without waiting for ACK and retries on timeout.
 */
#define gfw_core_cmd_send(cmd, data, len) \
            gfw_core_cmd_send_with_params(AUDIO_GFW_CMD_TYPE_DEFAULT, cmd, data, len, 0, NULL, 0)

/**
 * @brief Enqueues a CMD for sending, waits for ACK and retries once on timeout.
 * @note Shall not execute within a CMD callback
 */
#define gfw_core_cmd_send_timeout(cmd, data, len, timeout, timeout_cb) \
            gfw_core_cmd_send_with_params(AUDIO_GFW_CMD_TYPE_DEFAULT, cmd, data, len, timeout, timeout_cb, 1)

/**
 * @brief The API waits for the CMD to be sent completely before exiting, without waiting for ACK and timeout retransmission.
 * @note Shall not execute within a CMD callback
 */
#define gfw_core_cmd_send_sync(cmd, data, len) \
            gfw_core_cmd_send_with_params(AUDIO_GFW_CMD_TYPE_SYNC, cmd, data, len, 0, NULL, 0)

/**
 * @brief The API waits for the CMD to be sent completely before exiting, waits for ACK, and retransmits once if it times out.
 * @note Shall not execute within a CMD callback
 */
#define gfw_core_cmd_send_sync_timeout(cmd, data, len, timeout, timeout_cb) \
            gfw_core_cmd_send_with_params(AUDIO_GFW_CMD_TYPE_SYNC, cmd, data, len, timeout, timeout_cb, 1)

/**
 * @brief The API sends with the highest priority and exits after completion, without waiting for ACK or retransmitting on timeout.
 * It will pause commands waiting in the queue for transmission.
 * @note Shall not execute within a CMD callback
 */
#define gfw_core_cmd_send_instant(cmd, data, len) \
            gfw_core_cmd_send_with_params(AUDIO_GFW_CMD_TYPE_INSTANT, cmd, data, len, 0, NULL, 0)

/**
 * @brief The API sends with the highest priority and exits after completion,
 * waiting for ACK and retransmitting once if timed out. It will pause commands waiting in the queue for transmission.
 * @note Shall not execute within a CMD callback
 */
#define gfw_core_cmd_send_instant_timeout(cmd, data, len, timeout, timeout_cb) \
            gfw_core_cmd_send_with_params(AUDIO_GFW_CMD_TYPE_INSTANT, cmd, data, len, timeout, timeout_cb, 1)


#define gfw_core_cmd_send_direct(cmd, data, len) \
            gfw_core_cmd_send_with_params(AUDIO_GFW_CMD_TYPE_DIRECT, cmd, data, len, 0, NULL, 0)

/**
 * @brief cmd handle
 *
 * @param data The cmd data.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
typedef OPERATE_RET(*AUDIO_GFW_CMD_HANDLE_CB)(AUDIO_GFW_CMD_DATA_T *data);

/**
 * @brief cmd handle reg
 *
 * @param cb cb.
 *
 */
VOID gfw_core_cmd_ext_handle_reg(AUDIO_GFW_CMD_HANDLE_CB cb);

#ifdef __cplusplus
}
#endif

#endif // __CORE_CMD_H__
