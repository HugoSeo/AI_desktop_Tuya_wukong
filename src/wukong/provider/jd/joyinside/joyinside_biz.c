/**
 * @file joyinside_biz.c
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
#include <stdio.h>
#include "joyinside_biz.h"
#include "uni_random.h"
#include "http_manager.h"
#include "tuya_devos_utils.h"
#include "joyinside_client.h"
#include "base_event.h"
#include "joyinside_auth.h"
#include "uni_log.h"
#include "uni_base64.h"

#define JD_ROLES_MAX_NUM 3
#define JD_AGENT_ROLES_QUERY_URL "https://joyinside.jd.com/agent/roles/query"
#define JD_TEXT_CHAT_URL "https://joyinside.jd.com/soulmate/chat/v1"

typedef struct {
    CHAR_T *name;
    CHAR_T *code;
} JD_ROLES_INFO_S;

typedef struct {
    JD_ROLES_INFO_S r[JD_ROLES_MAX_NUM];
    JD_CHAT_CFG_S cfg;
    UINT_T index;
} JD_BIZ_S;

STATIC JD_BIZ_S g_jd_biz;

OPERATE_RET joyinside_uuid_v4(CHAR_T *uuid_str)
{
    TUYA_CHECK_NULL_RETURN(uuid_str, OPRT_INVALID_PARM);
    UCHAR_T uuid[16] = {0};
    uni_random_bytes(uuid, 16);
    uuid[6] = (uuid[6] & 0x0F) | 0x40;
    uuid[8] = (uuid[8] & 0x3F) | 0x80;
    snprintf(uuid_str, JD_UUID_V4_LEN + 1, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
             uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
    return OPRT_OK;
}

OPERATE_RET joyinside_http_post(CHAR_T *url, CHAR_T *data, UINT_T len, HTTP_HEAD_ADD_CB head, ty_cJSON **result)
{
    OPERATE_RET rt = OPRT_OK;
    SESSION_ID http_session = NULL;
    S_HTTP_MANAGER *http_manager = get_http_manager_instance();
    TUYA_CHECK_NULL_RETURN(http_manager, OPRT_INVALID_PARM);

    http_session = http_manager->create_http_session(url, TRUE);
    TUYA_CHECK_NULL_RETURN(http_session, OPRT_COM_ERROR);

    http_req_t req = {
        .type = HTTP_POST,
        .resource = url,
        .version = HTTP_VER_1_1,
        .content = data,
        .content_len = len,
        .add_head_cb = head, // if not, will has 401 error
    };

    rt = http_manager->send_http_request(http_session, &req, HDR_ADD_CONTENT_TYPE_JSON); // joyinside need application/json, otherwise 415 error
    if (OPRT_OK != rt) {
        PR_ERR("http send request failed, rt:%d", rt);
        return OPRT_MID_HTTP_SD_REQ_ERROR;
    }

    http_resp_t *resp = NULL;
    rt = http_manager->receive_http_response(http_session, &resp);
    if ((OPRT_OK != rt) || (!resp) || (resp->status_code != 200 && resp->status_code != 201)) {
        PR_ERR("put fail %d,code %d", rt, resp ? resp->status_code : 0xff);
        PR_ERR("http max header size:%d,security level:%d", HTTP_MAX_REQ_RESP_HDR_SIZE, TUYA_SECURITY_LEVEL);
        http_manager->destory_http_session(http_session);
        return OPRT_MID_HTTP_GET_RESP_ERROR;
    }

    if (0 == resp->content_length && !(resp->chunked)) {
        PR_ERR("http head err length %d, chunked %d", resp->content_length, resp->chunked);
        http_manager->destory_http_session(http_session);
        return OPRT_INVALID_PARM;
    }

    PR_DEBUG("content len %d", resp->content_length);

    UCHAR_T *out = NULL;
    rt = http_manager->receive_http_data(http_session, resp, &out);
    if ((OPRT_OK != rt) || (!out)) {
        PR_ERR("http receive data failed, rt:%d", rt);
        http_manager->destory_http_session(http_session);
        return OPRT_MID_HTTP_RD_ERROR;
    }

    http_manager->destory_http_session(http_session);
    *result = ty_cJSON_Parse((CHAR_T *)out);
    Free(out);
    if (*result == NULL) {
        PR_ERR("json parse failed");
        return OPRT_CJSON_PARSE_ERR;
    }
    return rt;
}

VOID joyinside_set_manual_call(BOOL_T manual_call)
{
    g_jd_biz.cfg.needManualCall = manual_call;
    joyinside_client_close();
}

BOOL_T joyinside_get_manual_call(VOID)
{
    return g_jd_biz.cfg.needManualCall;
}

OPERATE_RET joyinside_set_audio_cfg(JD_AUDIO_CFG_S *cfg)
{
    OPERATE_RET rt = OPRT_OK;
    // 1. root
    ty_cJSON *root = ty_cJSON_CreateObject();
    TUYA_CHECK_NULL_RETURN(root, OPRT_MALLOC_FAILED);
    CHAR_T mid[JD_UUID_V4_LEN + 1] = {0};
    joyinside_uuid_v4(mid);
    ty_cJSON_AddStringToObject(root, "mid", mid);
    ty_cJSON_AddStringToObject(root, "contentType", "EVENT");
    ty_cJSON_AddStringToObject(root, "uid", get_gw_dev_id());

    // 2. content object (child of root)
    ty_cJSON *content = ty_cJSON_CreateObject();
    if (content == NULL) {
        ty_cJSON_Delete(root);
        PR_ERR("cjson create content obj failed");
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddItemToObject(root, "content", content);
    ty_cJSON_AddStringToObject(content, "eventType", "CLIENT_VOICE_CHAT_UPDATE");

    // 3. create eventData object (child of content)
    ty_cJSON *eventData = ty_cJSON_CreateObject();
    if (eventData == NULL) {
        ty_cJSON_Delete(root);
        PR_ERR("cjson create eventData obj failed");
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddItemToObject(content, "eventData", eventData);

    // 4. create audio object (child of eventData)
    ty_cJSON *audio = ty_cJSON_CreateObject();
    if (audio == NULL) {
        ty_cJSON_Delete(root);
        PR_ERR("cjson create audio obj failed");
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddItemToObject(eventData, "audio", audio);
    ty_cJSON_AddBoolToObject(audio, "binary", cfg->binary);

    // 5. create output oject (child of audio)
    ty_cJSON *output = ty_cJSON_CreateObject();
    if (output == NULL) {
        ty_cJSON_Delete(root);
        PR_ERR("cjson create output obj failed");
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddItemToObject(audio, "output", output);
    if (cfg->output.codec == JD_AUDIO_CODEC_PCM) {
        ty_cJSON_AddStringToObject(output, "codec", "pcm");
    } else if (cfg->output.codec == JD_AUDIO_CODEC_OPUS) {
        ty_cJSON_AddStringToObject(output, "codec", "opus");
        ty_cJSON_AddBoolToObject(output, "enableOpusCbr", cfg->output.enableOpusCbr);
    } else {
        ty_cJSON_AddStringToObject(output, "codec", "mp3");
    }
    ty_cJSON_AddNumberToObject(output, "sampleRate", cfg->output.sampleRate);
    if (cfg->output.frameSizeMs) {
        ty_cJSON_AddNumberToObject(output, "frameSizeMs", cfg->output.frameSizeMs);
    }

    // 6. create input object (child of audio)
    ty_cJSON *input = ty_cJSON_CreateObject();
    if (input == NULL) {
        ty_cJSON_Delete(root);
        PR_ERR("cjson create input obj failed");
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddItemToObject(audio, "input", input);
    if (cfg->input.codec == JD_AUDIO_CODEC_OPUS) {
        ty_cJSON_AddStringToObject(input, "codec", "opus");
        ty_cJSON_AddNumberToObject(input, "frameSize", cfg->input.frameSize);
    } else {
        ty_cJSON_AddStringToObject(input, "codec", "pcm");
    }
    ty_cJSON_AddNumberToObject(input, "sampleRate", cfg->input.sampleRate);

    // 7. create timbre object (child of audio)
    if (cfg->timbre.voiceSpeed || cfg->timbre.voiceVolume) {
        ty_cJSON *timbre = ty_cJSON_CreateObject();
        if (timbre == NULL) {
            ty_cJSON_Delete(root);
            PR_ERR("cjson create timbre obj failed");
            return OPRT_MALLOC_FAILED;
        }
        ty_cJSON_AddItemToObject(audio, "timbre", timbre);
        if (cfg->timbre.voiceSpeed) {
            ty_cJSON_AddNumberToObject(timbre, "voiceSpeed", cfg->timbre.voiceSpeed);
        }
        if (cfg->timbre.voiceVolume) {
            ty_cJSON_AddNumberToObject(timbre, "voiceVolume", cfg->timbre.voiceVolume);
        }
    }

    CHAR_T *root_str = ty_cJSON_PrintUnformatted(root);
    UINT_T root_str_len = strlen(root_str);
    ty_cJSON_Delete(root);
    TUYA_CHECK_NULL_RETURN(root_str, OPRT_MALLOC_FAILED);
    JD_PR_D("audio cfg json: %s", root_str);
    rt = joyinside_transporter_write(root_str, root_str_len);
    Free(root_str);
    return rt;
}

STATIC OPERATE_RET __jd_make_roles_query_data(CHAR_T **data, UINT_T *len)
{
    JD_AK_KEY_S *ak_key = joyinside_get_ak_key();
    JD_DEV_INFO_S *dev_info = joyinside_get_dev_info();

    CHAR_T request_id[JD_UUID_V4_LEN + 1] = {0};
    joyinside_uuid_v4(request_id);

    ty_cJSON *root = ty_cJSON_CreateObject();
    TUYA_CHECK_NULL_RETURN(root, OPRT_MALLOC_FAILED);
    ty_cJSON_AddStringToObject(root, "requestId", request_id);
    ty_cJSON_AddStringToObject(root, "appId", ak_key->appId);
    ty_cJSON_AddStringToObject(root, "botId", dev_info->botId);
    *data = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    TUYA_CHECK_NULL_RETURN(*data, OPRT_MALLOC_FAILED);
    *len = strlen(*data);
    JD_PR_D("roles query:%s", *data);
    return OPRT_OK;
}

STATIC VOID __jd_save_roles(CHAR_T *name, CHAR_T *code)
{
    if (!name || !code) {
        PR_ERR("invalid role name or code");
        return;
    }
    UINT_T idx = 0;
    for (idx = 0; idx < JD_ROLES_MAX_NUM; idx++) {
        if (g_jd_biz.r[idx].name == NULL) {
            g_jd_biz.r[idx].name = mm_strdup(name);
            g_jd_biz.r[idx].code = mm_strdup(code);
            if (!g_jd_biz.r[idx].name || !g_jd_biz.r[idx].code) {
                if (g_jd_biz.r[idx].name) {
                    Free(g_jd_biz.r[idx].name);
                    g_jd_biz.r[idx].name = NULL;
                }
                if (g_jd_biz.r[idx].code) {
                    Free(g_jd_biz.r[idx].code);
                    g_jd_biz.r[idx].code = NULL;
                }
                PR_ERR("strdup failed for role");
                return;
            }
            break;
        }
    }
    if (idx == JD_ROLES_MAX_NUM) {
        PR_ERR("roles array is full, cannot save more roles");
    }
}

STATIC VOID __jd_roles_free(VOID)
{
    UINT_T idx = 0;
    for (idx = 0; idx < JD_ROLES_MAX_NUM; idx++) {
        if (g_jd_biz.r[idx].name != NULL) {
            Free(g_jd_biz.r[idx].name);
            g_jd_biz.r[idx].name = NULL;
        }
        if (g_jd_biz.r[idx].code != NULL) {
            Free(g_jd_biz.r[idx].code);
            g_jd_biz.r[idx].code = NULL;
        }
    }
}

STATIC OPERATE_RET __jd_parse_roles(ty_cJSON *root)
{
    UINT_T role_num, idx = 0;
    ty_cJSON *data = ty_cJSON_GetObjectItem(root, "data");
    if (!data) {
        PR_ERR("get data fail, missing fields");
        return OPRT_CJSON_GET_ERR;
    }
    __jd_roles_free();
    role_num = ty_cJSON_GetArraySize(data);
    for (idx = 0; idx < role_num; idx++) {
        ty_cJSON *role_item = ty_cJSON_GetArrayItem(data, idx);
        if (!role_item) {
            continue;
        }
        ty_cJSON *name = ty_cJSON_GetObjectItem(role_item, "name");
        ty_cJSON *code = ty_cJSON_GetObjectItem(role_item, "code");
        if (name && name->valuestring && code && code->valuestring) {
            __jd_save_roles(name->valuestring, code->valuestring);
        }
    }
    return OPRT_OK;
}

OPERATE_RET joyinside_query_roles(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    JD_DEV_INFO_S *dev_info = joyinside_get_dev_info();

    if (dev_info->botId == NULL) {
        PR_ERR("please register device first");
        return OPRT_COM_ERROR;
    }

    CHAR_T *post_data = NULL;
    UINT_T post_data_len = 0;
    ty_cJSON *result = NULL;
    rt = __jd_make_roles_query_data(&post_data, &post_data_len);
    if (OPRT_OK != rt) {
        PR_ERR("make roles query data err, rt:%d", rt);
        return rt;
    }
    rt = joyinside_http_post(JD_AGENT_ROLES_QUERY_URL, post_data, post_data_len, joyinside_add_auth_header, &result);
    Free(post_data);
    if (rt != OPRT_OK) {
        PR_ERR("http post err, rt:%d", rt);
        return rt;
    }
    rt = __jd_parse_roles(result);
    ty_cJSON_Delete(result);
    return rt;
}

OPERATE_RET joyinside_send_event(JD_EVENT_TYPE_E event)
{
    OPERATE_RET rt = OPRT_OK;

#if 0
    if (JD_EVENT_FINISH == event) {
        if (!g_jd_biz.cfg.needManualCall) {
            return rt;
        }
    }
#endif

    JD_PR_D("jd send event %d", event);
    ty_cJSON *root = ty_cJSON_CreateObject();
    TUYA_CHECK_NULL_RETURN(root, OPRT_MALLOC_FAILED);
    CHAR_T mid[JD_UUID_V4_LEN + 1] = {0};
    joyinside_uuid_v4(mid);

    ty_cJSON_AddStringToObject(root, "mid", mid);
    ty_cJSON_AddStringToObject(root, "contentType", "EVENT");
    ty_cJSON_AddStringToObject(root, "uid", get_gw_dev_id());

    ty_cJSON *content = ty_cJSON_CreateObject();
    if (content == NULL) {
        ty_cJSON_Delete(root);
        PR_ERR("cjson create content obj failed");
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddItemToObject(root, "content", content);

    if (JD_EVENT_INTERRUPT == event) {
        ty_cJSON_AddStringToObject(content, "eventType", "CLIENT_INTERRUPT");
    } else if (JD_EVENT_FINISH == event) {
        ty_cJSON_AddStringToObject(content, "eventType", "CLIENT_AUDIO_FINISH");
    }

    CHAR_T *root_str = ty_cJSON_PrintUnformatted(root);
    UINT_T root_str_len = strlen(root_str);
    ty_cJSON_Delete(root);
    TUYA_CHECK_NULL_RETURN(root_str, OPRT_MALLOC_FAILED);
    rt = joyinside_transporter_write(root_str, root_str_len);
    Free(root_str);
    return rt;
}

OPERATE_RET joyinside_send_text(CHAR_T *data, UINT_T len)
{
    OPERATE_RET rt = OPRT_OK;
    if (!joyinside_client_is_ready()) {
        PR_ERR("jd client not ready");
        return OPRT_COM_ERROR;
    }
    joyinside_start_ping();
    ty_cJSON *root = ty_cJSON_CreateObject();
    TUYA_CHECK_NULL_RETURN(root, OPRT_MALLOC_FAILED);
    CHAR_T mid[JD_UUID_V4_LEN + 1] = {0};
    joyinside_uuid_v4(mid);
    JD_PR_D("jd send text mid: %s", mid);
    ty_cJSON_AddStringToObject(root, "mid", mid);
    ty_cJSON_AddStringToObject(root, "contentType", "TEXT");
    ty_cJSON_AddStringToObject(root, "uid", get_gw_dev_id());

    ty_cJSON *content = ty_cJSON_CreateObject();
    if (content == NULL) {
        ty_cJSON_Delete(root);
        PR_ERR("cjson create content obj failed");
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddItemToObject(root, "content", content);

    ty_cJSON_AddStringToObject(content, "input", data);

    CHAR_T *root_str = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    TUYA_CHECK_NULL_RETURN(root_str, OPRT_MALLOC_FAILED);
    UINT_T root_str_len = strlen(root_str);
    JD_PR_D("jd send text len: %u", root_str_len);
    rt = joyinside_transporter_write(root_str, root_str_len);
    Free(root_str);
    return rt;
}

OPERATE_RET joyinside_send_audio(CHAR_T *data, UINT_T len)
{
    OPERATE_RET rt = OPRT_OK;
    if (!joyinside_client_is_ready()) {
        PR_ERR("jd client not ready");
        return OPRT_COM_ERROR;
    }
    joyinside_start_ping();
    ty_cJSON *root = ty_cJSON_CreateObject();
    TUYA_CHECK_NULL_RETURN(root, OPRT_MALLOC_FAILED);
    CHAR_T mid[JD_UUID_V4_LEN + 1] = {0};
    joyinside_uuid_v4(mid);
    JD_PR_D("jd send audio mid: %s", mid);
    ty_cJSON_AddStringToObject(root, "mid", mid);
    ty_cJSON_AddStringToObject(root, "contentType", "AUDIO");
    ty_cJSON_AddStringToObject(root, "uid", get_gw_dev_id());

    ty_cJSON *content = ty_cJSON_CreateObject();
    if (content == NULL) {
        ty_cJSON_Delete(root);
        PR_ERR("cjson create content obj failed");
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddItemToObject(root, "content", content);

    CHAR_T *p_base64 = Malloc(len / 3 * 4 + 5);
    if (NULL == p_base64) {
        ty_cJSON_Delete(root);
        PR_ERR("malloc base64 buf failed");
        return OPRT_MALLOC_FAILED;
    }
    tuya_base64_encode((BYTE_T *)data, p_base64, len);
    ty_cJSON_AddStringToObject(content, "audioBase64", p_base64);
    ty_cJSON_AddNumberToObject(content, "index", g_jd_biz.index);
    g_jd_biz.index++;
    if (g_jd_biz.index == 0) { // prevent overflow
        g_jd_biz.index = 1;
    }
    CHAR_T *root_str = ty_cJSON_PrintUnformatted(root);
    Free(p_base64);
    ty_cJSON_Delete(root);
    TUYA_CHECK_NULL_RETURN(root_str, OPRT_MALLOC_FAILED);
    UINT_T root_str_len = strlen(root_str);
    JD_PR_D("jd send audio len: %u", root_str_len);
    rt = joyinside_transporter_write(root_str, root_str_len);
    Free(root_str);
    return rt;
}

OPERATE_RET joyinside_text_to_speech(CHAR_T *data, UINT_T len)
{
    OPERATE_RET rt = OPRT_OK;
    if (!joyinside_client_is_ready()) {
        PR_ERR("jd client not ready");
        return OPRT_COM_ERROR;
    }
    if (len > 1000) {
        PR_ERR("text to speech data too long, len:%d", len);
        return OPRT_INVALID_PARM;
    }

    ty_cJSON *root = ty_cJSON_CreateObject();
    TUYA_CHECK_NULL_RETURN(root, OPRT_MALLOC_FAILED);
    CHAR_T mid[JD_UUID_V4_LEN + 1] = {0};
    joyinside_uuid_v4(mid);

    ty_cJSON_AddStringToObject(root, "mid", mid);
    ty_cJSON_AddStringToObject(root, "contentType", "EVENT");
    ty_cJSON_AddStringToObject(root, "uid", get_gw_dev_id());

    ty_cJSON *content = ty_cJSON_CreateObject();
    if (content == NULL) {
        ty_cJSON_Delete(root);
        PR_ERR("cjson create content obj failed");
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddItemToObject(root, "content", content);

    ty_cJSON_AddStringToObject(content, "eventType", "CLIENT_INPUT_TEXT_TO_SPEECH");

    ty_cJSON *eventData = ty_cJSON_CreateObject();
    if (eventData == NULL) {
        ty_cJSON_Delete(root);
        PR_ERR("cjson create eventData obj failed");
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddItemToObject(content, "eventData", eventData);
    ty_cJSON_AddStringToObject(eventData, "text", data);

    CHAR_T *root_str = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    TUYA_CHECK_NULL_RETURN(root_str, OPRT_MALLOC_FAILED);
    UINT_T root_str_len = strlen(root_str);
    JD_PR_D("text to speech json: %s", root_str);
    rt = joyinside_transporter_write(root_str, root_str_len);
    Free(root_str);
    return rt;
}

STATIC OPERATE_RET __jd_make_text_chat_data(CHAR_T *data, CHAR_T **post_data, UINT_T *post_data_len)
{
    JD_DEV_INFO_S *dev_info = joyinside_get_dev_info();

    ty_cJSON *root = ty_cJSON_CreateObject();
    TUYA_CHECK_NULL_RETURN(root, OPRT_MALLOC_FAILED);
    CHAR_T request_id[JD_UUID_V4_LEN + 1] = {0};
    joyinside_uuid_v4(request_id);
    ty_cJSON_AddStringToObject(root, "requestId", request_id);
    ty_cJSON *messages = ty_cJSON_CreateArray();
    if (messages == NULL) {
        ty_cJSON_Delete(root);
        PR_ERR("cjson create messages array failed");
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON *message_item = ty_cJSON_CreateObject();
    if (message_item == NULL) {
        ty_cJSON_Delete(root);
        PR_ERR("cjson create message item failed");
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddItemToArray(messages, message_item);
    ty_cJSON_AddStringToObject(message_item, "role", "user");
    ty_cJSON_AddStringToObject(message_item, "content", data);

    ty_cJSON_AddItemToObject(root, "messages", messages);

    ty_cJSON_AddStringToObject(root, "botId", dev_info->botId);
    ty_cJSON_AddStringToObject(root, "sessionId", get_gw_dev_id());

    *post_data = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    TUYA_CHECK_NULL_RETURN(*post_data, OPRT_MALLOC_FAILED);
    *post_data_len = strlen(*post_data);

    JD_PR_D("text chat:%s", *post_data);
    return OPRT_OK;
}

// text in && text out
OPERATE_RET joyinside_text_chat(CHAR_T *data, UINT_T len)
{
    OPERATE_RET rt = OPRT_OK;
    if (!joyinside_client_is_ready()) {
        PR_ERR("jd client not ready");
        return OPRT_COM_ERROR;
    }

    CHAR_T *post_data = NULL;
    UINT_T post_data_len = 0;
    rt = __jd_make_text_chat_data(data, &post_data, &post_data_len);
    if (OPRT_OK != rt) {
        PR_ERR("make text chat data err, rt:%d", rt);
        return rt;
    }

    // send http post
    // create one task(can be in joyinside_client) and recv sse(Server-Sent Events) response
    return rt;
}

STATIC INT_T __jd_client_run_cb(VOID *data)
{
    OPERATE_RET rt = OPRT_OK;
    rt = joyinside_set_audio_cfg(&g_jd_biz.cfg.audio);
    return rt;
}

STATIC INT_T __jd_client_close_cb(VOID *data)
{
    g_jd_biz.index = 1;
    return OPRT_OK;
}

OPERATE_RET joyinside_send_content_evt(CHAR_T *content_type, CHAR_T *event_type, CHAR_T *event_data)
{
    OPERATE_RET rt = OPRT_OK;
    ty_cJSON *event_data_obj = NULL;
    if (!content_type || !event_type) {
        PR_ERR("content_type or event_type is null");
        return OPRT_INVALID_PARM;
    }
    if (event_data) {
        event_data_obj = ty_cJSON_Parse(event_data);
        TUYA_CHECK_NULL_RETURN(event_data_obj, OPRT_CJSON_PARSE_ERR);
    } else {
        event_data_obj = ty_cJSON_CreateObject();
        TUYA_CHECK_NULL_RETURN(event_data_obj, OPRT_MALLOC_FAILED);
    }

    JD_PR_D("content_type: %s, event_type: %s", content_type, event_type);
    ty_cJSON *root = ty_cJSON_CreateObject();
    if (root == NULL) {
        ty_cJSON_Delete(event_data_obj);
        return OPRT_MALLOC_FAILED;
    }
    CHAR_T mid[JD_UUID_V4_LEN + 1] = {0}, ts[32] = {0};
    joyinside_uuid_v4(mid);

    snprintf(ts, SIZEOF(ts), "%llu", tal_time_get_posix_ms());
    ty_cJSON_AddStringToObject(root, "mid", mid);
    ty_cJSON_AddStringToObject(root, "uid", get_gw_dev_id());
    ty_cJSON_AddStringToObject(root, "contentType", content_type);
    ty_cJSON_AddStringToObject(root, "t", ts);

    ty_cJSON *content = ty_cJSON_CreateObject();
    if (content == NULL) {
        ty_cJSON_Delete(root);
        ty_cJSON_Delete(event_data_obj);
        PR_ERR("cjson create content obj failed");
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddItemToObject(root, "content", content);
    ty_cJSON_AddStringToObject(content, "eventType", event_type);
    ty_cJSON_AddItemToObject(content, "eventData", event_data_obj);
    CHAR_T *root_str = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    TUYA_CHECK_NULL_RETURN(root_str, OPRT_MALLOC_FAILED);
    UINT_T root_str_len = strlen(root_str);
    JD_PR_D("content event: %s", root_str);
    rt = joyinside_transporter_write(root_str, root_str_len);
    Free(root_str);
    return rt;
}

VOID joyinside_biz_deinit(VOID)
{
    __jd_roles_free();
    ty_unsubscribe_event(EVENT_AI_CLIENT_RUN, "jd_biz", __jd_client_run_cb);
    ty_unsubscribe_event(EVENT_AI_CLIENT_CLOSE, "jd_biz", __jd_client_close_cb);
}

OPERATE_RET joyinside_biz_init(JD_CHAT_CFG_S *chat_cfg)
{
    memcpy(&g_jd_biz.cfg, chat_cfg, SIZEOF(JD_CHAT_CFG_S));
    g_jd_biz.index = 1;
    ty_subscribe_event(EVENT_AI_CLIENT_RUN, "jd_biz", __jd_client_run_cb, SUBSCRIBE_TYPE_NORMAL);
    ty_subscribe_event(EVENT_AI_CLIENT_CLOSE, "jd_biz", __jd_client_close_cb, SUBSCRIBE_TYPE_NORMAL);
    return OPRT_OK;
}