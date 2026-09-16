/**
 * @file joyinside_biz.h
 * @brief joyinside biz module
 * @version 0.1
 * @date 2025-12-15
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

#ifndef __JOYINSIDE_BIZ_H__
#define __JOYINSIDE_BIZ_H__

#include "tuya_cloud_types.h"
#include "ty_cJSON.h"
#include "httpc.h"

#define JD_UUID_V4_LEN 38

typedef enum {
    JD_AUDIO_CODEC_PCM = 0,
    JD_AUDIO_CODEC_OPUS,
    JD_AUDIO_CODEC_MP3,
    JD_AUDIO_CODEC_INVALID
} JD_AUDIO_CODEC_E;

typedef struct {
    JD_AUDIO_CODEC_E codec;// pcm,opus
    UINT_T sampleRate;
    UINT_T frameSize;
} JD_AUDIO_INPUT_CFG_S;

typedef struct {
    JD_AUDIO_CODEC_E codec; // pcm,opus,mp3
    UINT_T sampleRate;
    BOOL_T enableOpusCbr;
    UINT_T frameSizeMs;
} JD_AUDIO_OUTPUT_CFG_S;

typedef struct {
    FLOAT_T voiceSpeed; // 0.8-1.2
    FLOAT_T voiceVolume; // 0.5-10
} JD_AUDIO_TIMBRE_CFG_S;

typedef struct {
    BOOL_T binary;
    JD_AUDIO_INPUT_CFG_S input;
    JD_AUDIO_OUTPUT_CFG_S output;
    JD_AUDIO_TIMBRE_CFG_S timbre;
} JD_AUDIO_CFG_S;

typedef struct {
    JD_AUDIO_CFG_S audio;
    BOOL_T needManualCall;
} JD_CHAT_CFG_S;

typedef enum {
    JD_EVENT_INTERRUPT,
    JD_EVENT_FINISH
} JD_EVENT_TYPE_E;

/**
 * @brief generate a uuid v4 string
 *
 * @param[out] uuid_str uuid v4 string buffer, length must be >= JD_UUID_V4_LEN
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET joyinside_uuid_v4(CHAR_T *uuid_str);

/**
 * @brief joyinside http post
 *
 * @param[in] url http post url
 * @param[in] data http post data
 * @param[in] len http post data length
 * @param[in] head http head add callback
 * @param[out] result http post result json object
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET joyinside_http_post(CHAR_T *url, CHAR_T *data, UINT_T len, HTTP_HEAD_ADD_CB head, ty_cJSON **result);

/**
 * @brief set audio configuration
 *
 * @param[in] cfg audio configuration
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET joyinside_set_audio_cfg(JD_AUDIO_CFG_S *cfg);

/**
 * @brief set manual call flag
 *
 * @param[in] manual_call manual call flag
 *
 * @return none
 */
VOID joyinside_set_manual_call(BOOL_T manual_call);

/**
 * @brief get manual call flag
 *
 * @return manual call flag
 */
BOOL_T joyinside_get_manual_call(VOID);

/**
 * @brief joyinside biz module init
 *
 * @param[in] chat_cfg chat configuration
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET joyinside_biz_init(JD_CHAT_CFG_S *chat_cfg);

/**
 * @brief joyinside biz module deinit
 *
 * @return none
 */
VOID joyinside_biz_deinit(VOID);

/**
 * @brief send text data to joyinside
 *
 * @param[in] data text data
 * @param[in] len text data length
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET joyinside_send_text(CHAR_T *data, UINT_T len);

/**
 * @brief send audio data to joyinside
 *
 * @param[in] data audio data
 * @param[in] len audio data length
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET joyinside_send_audio(CHAR_T *data, UINT_T len);

/**
 * @brief send event to joyinside
 *
 * @param[in] event event type
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET joyinside_send_event(JD_EVENT_TYPE_E event);

/**
 * @brief text to speech
 *
 * @param[in] data text data
 * @param[in] len text data length
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET joyinside_text_to_speech(CHAR_T *data, UINT_T len);

/**
 * @brief Send custom content event to joyinside
 * @param[in] content_type content type string
 * @param[in] event_type event type string
 * @param[in] event_data event data JSON string, NULL to send empty object
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET joyinside_send_content_evt(CHAR_T *content_type, CHAR_T *event_type, CHAR_T *event_data);
#endif // __JOYINSIDE_BIZ_H__