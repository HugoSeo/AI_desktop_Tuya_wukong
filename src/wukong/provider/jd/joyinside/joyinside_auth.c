/**
 * @file joyinside_auth.c
 * @brief joyinside authentication module
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
#include "tuya_cloud_types.h"
#include "joyinside_auth.h"
#include "joyinside_biz.h"
#include "joyinside_client.h"
#include "tal_security.h"
#include "tal_sw_timer.h"
#include "uni_log.h"
#include "ty_cJSON.h"
#include "httpc.h"
#include "mix_method.h"
#include "base_event.h"
#include "tuya_ws_db.h"
#include "gw_intf.h"

#define JD_SIGN_LEN 16

#define JD_GET_TOKEN_URL "https://joyinside.jd.com/auth/getToken"
#define JD_REFRESH_TOKEN_URL "https://joyinside.jd.com/auth/refreshToken"
#define JD_DEVICE_REGISTER_URL "https://joyinside.jd.com/device/register"
#define JD_BOT_ID_KV_KEY "jd_bot_id"

#define JD_DEFAULT_VENDOR_ID "100106"
#define JD_DEFAULT_APP_ID "10495"
#define JD_DEFAULT_AK "059b406c7b5c46ba8133a5cd"
#define JD_DEFAULT_SK "0dbb7b8c52f54391a9ec29fe2da3f6ce"

typedef struct {
    JD_TOKEN_S t;
    JD_SIGN_S sign;
    TIMER_ID access_token_tid;
    TIMER_ID refresh_token_tid;
} JD_AUTH_S;

STATIC JD_AUTH_S g_jd_auth;

JD_AK_KEY_S *joyinside_get_ak_key(VOID)
{
    return &g_jd_auth.sign.a;
}

JD_DEV_INFO_S *joyinside_get_dev_info(VOID)
{
    return &g_jd_auth.sign.d;
}

JD_TOKEN_S *joyinside_get_token_info(VOID)
{
    return &g_jd_auth.t;
}

STATIC OPERATE_RET __jd_calc_sign(CHAR_T *ak, CHAR_T *sk, CHAR_T *ts, CHAR_T *nonce, UCHAR_T *sign)
{
    OPERATE_RET rt = OPRT_OK;
    if (ak == NULL || sk == NULL || ts == NULL || nonce == NULL || sign == NULL) {
        PR_ERR("ak %p, %p, %p, %p, %p", ak, sk, ts, nonce, sign);
        PR_ERR("invalid input param");
        return OPRT_INVALID_PARM;
    }
    UCHAR_T sign_hex[JD_SIGN_LEN] = {0};
    UINT_T sign_len = 0;

    CHAR_T *sign_buf = Malloc(256);
    TUYA_CHECK_NULL_RETURN(sign_buf, OPRT_MALLOC_FAILED);
    sign_len = snprintf(sign_buf, 256, "accesskeyid=%s&accessnonce=%s&accesstimestamp=%s&accessversion=V2", ak, nonce, ts);

    rt = mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_MD5), (UCHAR_T *)sk, strlen((CHAR_T *)sk), (UCHAR_T *)sign_buf, sign_len, sign_hex);
    Free(sign_buf);
    if (OPRT_OK != rt) {
        PR_ERR("sha mac err, rt:%d", rt);
        return rt;
    }

    hex2str(sign, sign_hex, JD_SIGN_LEN);
    // Convert to lowercase, hex2str produces JD_SIGN_LEN * 2 characters
    for (UINT_T idx = 0; idx < JD_SIGN_LEN * 2; idx++) {
        sign[idx] = tuya_tolower(sign[idx]);
    }
    return rt;
}

STATIC OPERATE_RET __jd_make_token_data(CHAR_T **data, UINT_T *len)
{
    OPERATE_RET rt = OPRT_OK;
    CHAR_T sign[JD_SIGN_LEN * 2 + 1] = {0}, ts[32] = {0}, nonce[JD_UUID_V4_LEN + 1] = {0};
    JD_AK_KEY_S *ak_key = joyinside_get_ak_key();
    JD_DEV_INFO_S *dev_info = joyinside_get_dev_info();
    snprintf(ts, SIZEOF(ts), "%llu", tal_time_get_posix_ms());
    joyinside_uuid_v4(nonce);
    rt = __jd_calc_sign(ak_key->ak, ak_key->sk, ts, nonce, (UCHAR_T *)sign);
    if (OPRT_OK != rt) {
        PR_ERR("get sign err, rt:%d", rt);
        return rt;
    }

    ty_cJSON *root = ty_cJSON_CreateObject();
    TUYA_CHECK_NULL_RETURN(root, OPRT_MALLOC_FAILED);
    ty_cJSON_AddStringToObject(root, "accessVersion", "V2");
    ty_cJSON_AddStringToObject(root, "accessTimestamp", ts);
    ty_cJSON_AddStringToObject(root, "accessNonce", nonce);
    ty_cJSON_AddStringToObject(root, "accessKeyId", ak_key->ak);
    if (dev_info->botId) {
        ty_cJSON_AddStringToObject(root, "botId", dev_info->botId); // bot id and verdor id coexistence, then use bot id
    } else if (ak_key->vendorId) {
        ty_cJSON_AddStringToObject(root, "vendorId", ak_key->vendorId);
    }
    ty_cJSON_AddStringToObject(root, "accessSign", sign);
    *data = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    TUYA_CHECK_NULL_RETURN(*data, OPRT_MALLOC_FAILED);
    *len = strlen(*data);
    JD_PR_D("make token data: %s", *data);
    return rt;
}

STATIC VOID __jd_token_info_free(VOID)
{
    if (g_jd_auth.t.accessToken) {
        Free(g_jd_auth.t.accessToken);
        g_jd_auth.t.accessToken = NULL;
    }
    if (g_jd_auth.t.refreshToken) {
        Free(g_jd_auth.t.refreshToken);
        g_jd_auth.t.refreshToken = NULL;
    }
    g_jd_auth.t.expireIn = 0;
    g_jd_auth.t.refreshExpireIn = 0;
}

STATIC VOID __jd_dev_info_free(VOID)
{
    if (g_jd_auth.sign.d.botId) {
        Free(g_jd_auth.sign.d.botId);
        g_jd_auth.sign.d.botId = NULL;
    }
    if (g_jd_auth.sign.d.sn) {
        Free(g_jd_auth.sign.d.sn);
        g_jd_auth.sign.d.sn = NULL;
    }
    if (g_jd_auth.sign.d.name) {
        Free(g_jd_auth.sign.d.name);
        g_jd_auth.sign.d.name = NULL;
    }
    if (g_jd_auth.sign.d.type) {
        Free(g_jd_auth.sign.d.type);
        g_jd_auth.sign.d.type = NULL;
    }
    if (g_jd_auth.sign.d.deviceModel) {
        Free(g_jd_auth.sign.d.deviceModel);
        g_jd_auth.sign.d.deviceModel = NULL;
    }
    if (g_jd_auth.sign.d.timbreId) {
        Free(g_jd_auth.sign.d.timbreId);
        g_jd_auth.sign.d.timbreId = NULL;
    }
    if (g_jd_auth.sign.d.desc) {
        Free(g_jd_auth.sign.d.desc);
        g_jd_auth.sign.d.desc = NULL;
    }
}

STATIC VOID __jd_ak_key_free(VOID)
{
    if (g_jd_auth.sign.a.vendorId) {
        Free(g_jd_auth.sign.a.vendorId);
        g_jd_auth.sign.a.vendorId = NULL;
    }
    if (g_jd_auth.sign.a.appId) {
        Free(g_jd_auth.sign.a.appId);
        g_jd_auth.sign.a.appId = NULL;
    }
    if (g_jd_auth.sign.a.ak) {
        Free(g_jd_auth.sign.a.ak);
        g_jd_auth.sign.a.ak = NULL;
    }
    if (g_jd_auth.sign.a.sk) {
        Free(g_jd_auth.sign.a.sk);
        g_jd_auth.sign.a.sk = NULL;
    }
}

STATIC VOID __jd_parse_code_msg(UINT_T code)
{
    if (code == JD_TOKEN_INVALID_PARM) {
        PR_ERR("1.required parameter is empty or");
        PR_ERR("2.vendorId and botId are empty at the same time");
    } else if (code == JD_TOKEN_VER_ERR) {
        PR_ERR("accessVersion not V2");
    } else if (code == JD_TOKEN_TS_INVALID) {
        PR_ERR("1.timestamp format is incorrect or");
        PR_ERR("2.timestamp is out of the allowed range (within 15 minutes)");
    } else if (code == JD_TOKEN_AUTH_FAILED) {
        PR_ERR("1.the enterprise corresponding to vendorId does not exist or");
        PR_ERR("2.the device corresponding to botId does not exist");
    } else if (code == JD_TOKEN_SIGN_ERR) {
        PR_ERR("the client signature does not match the server-generated signature");
    } else if (code == JD_TOKEN_SYSTEM_ERR) {
        PR_ERR("an exception occurred in the encryption algorithm when generating the signature");
    }
}

STATIC OPERATE_RET __jd_parse_token(ty_cJSON *root)
{
    ty_cJSON *code = ty_cJSON_GetObjectItem(root, "code");
    if (code) {
        JD_PR_D("code:%d", code->valueint);
        __jd_parse_code_msg(code->valueint);
    }
    ty_cJSON *msg = ty_cJSON_GetObjectItem(root, "msg");
    if (msg) {
        JD_PR_D("msg:%s", msg->valuestring ? msg->valuestring : "null");
    }
    ty_cJSON *accessToken = ty_cJSON_GetObjectItem(root, "accessToken");
    ty_cJSON *expireIn = ty_cJSON_GetObjectItem(root, "expireIn"); // 文档2小时,实际是8小时
    ty_cJSON *refreshToken = ty_cJSON_GetObjectItem(root, "refreshToken");
    ty_cJSON *refreshExpireIn = ty_cJSON_GetObjectItem(root, "refreshExpireIn");
    if (accessToken && expireIn && refreshToken && refreshExpireIn) {
        __jd_token_info_free();
        g_jd_auth.t.accessToken = mm_strdup(accessToken->valuestring);
        g_jd_auth.t.refreshToken = mm_strdup(refreshToken->valuestring);
        if (!g_jd_auth.t.accessToken || !g_jd_auth.t.refreshToken) {
            __jd_token_info_free();
            PR_ERR("strdup failed");
            return OPRT_MALLOC_FAILED;
        }
        g_jd_auth.t.expireIn = expireIn->valueint;
        g_jd_auth.t.refreshExpireIn = refreshExpireIn->valueint;
        JD_PR_D("get token success, accessToken:%s, expireIn:%u, refreshToken:%s, refreshExpireIn:%u",
                 g_jd_auth.t.accessToken, g_jd_auth.t.expireIn,
                 g_jd_auth.t.refreshToken, g_jd_auth.t.refreshExpireIn);
        tal_sw_timer_start(g_jd_auth.access_token_tid, g_jd_auth.t.expireIn * 1000, TAL_TIMER_ONCE);
        tal_sw_timer_start(g_jd_auth.refresh_token_tid, g_jd_auth.t.refreshExpireIn * 1000, TAL_TIMER_ONCE);
        return OPRT_OK;
    } else {
        PR_ERR("get token fail, missing fields");
        return OPRT_CJSON_GET_ERR;
    }
}

STATIC OPERATE_RET __jd_make_reg_dev_data(CHAR_T **post_data, UINT_T *post_data_len)
{
    JD_AK_KEY_S *ak_key = joyinside_get_ak_key();
    JD_DEV_INFO_S *dev_info = joyinside_get_dev_info();

    GW_CNTL_S *gw_cntl = get_gw_cntl();
    ty_cJSON *root = ty_cJSON_CreateObject();
    TUYA_CHECK_NULL_RETURN(root, OPRT_MALLOC_FAILED);
    ty_cJSON_AddStringToObject(root, "vendorId", ak_key->vendorId);
    ty_cJSON_AddStringToObject(root, "appId", ak_key->appId);
    if (dev_info->type) {
        ty_cJSON_AddStringToObject(root, "type", dev_info->type);
    } else {
        ty_cJSON_AddStringToObject(root, "type", JD_DEV_TYPE);
    }
    if (dev_info->name) {
        ty_cJSON_AddStringToObject(root, "name", dev_info->name);
    } else {
        ty_cJSON_AddStringToObject(root, "name", gw_cntl->gw_if.product_key);
    }
    if (dev_info->sn) {
        ty_cJSON_AddStringToObject(root, "deviceId", dev_info->sn);
    } else {
        ty_cJSON_AddStringToObject(root, "deviceId", gw_cntl->gw_base.uuid);
    }
    *post_data = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    TUYA_CHECK_NULL_RETURN(*post_data, OPRT_MALLOC_FAILED);
    *post_data_len = strlen(*post_data);
    return OPRT_OK;
}

STATIC OPERATE_RET __jd_parse_dev_reg(ty_cJSON *root)
{
    OPERATE_RET rt = OPRT_OK;
    ty_cJSON *state = ty_cJSON_GetObjectItem(root, "state");
    ty_cJSON *code = ty_cJSON_GetObjectItem(root, "code");
    ty_cJSON *data = ty_cJSON_GetObjectItem(root, "data");
    // no result field
    if ((!data) || (!state) || (!code)) {
        PR_ERR("missing fields");
        return OPRT_CJSON_GET_ERR;
    }

    if (g_jd_auth.sign.d.botId) {
        Free(g_jd_auth.sign.d.botId);
        g_jd_auth.sign.d.botId = NULL;
    }

    g_jd_auth.sign.d.botId = mm_strdup(data->valuestring);
    if (NULL == g_jd_auth.sign.d.botId) {
        PR_ERR("strdup botId failed");
        return OPRT_MALLOC_FAILED;
    }
    PR_NOTICE("get bot id %s", g_jd_auth.sign.d.botId);
    rt = wd_common_write(JD_BOT_ID_KV_KEY, (BYTE_T *)g_jd_auth.sign.d.botId, strlen(g_jd_auth.sign.d.botId));
    if (OPRT_OK != rt) {
        PR_ERR("write bot id to kv fail, rt:%d", rt);
    }
    return rt;
}

VOID joyinside_add_auth_header(http_session_t session, VOID* param)
{
    JD_TOKEN_S *token_info = joyinside_get_token_info();
    if (NULL == token_info) {
        PR_ERR("token was null");
        return;
    }
    UINT_T header_len = strlen(token_info->accessToken) + 8;
    CHAR_T *auth_header = Malloc(header_len);
    if (NULL == auth_header) {
        PR_ERR("malloc auth header fail");
        return;
    }
    snprintf(auth_header, header_len, "Bearer %s", token_info->accessToken);
    http_add_header(session, NULL, "Authorization", auth_header);
    Free(auth_header);
    PR_DEBUG("add auth header success");
}

OPERATE_RET joyinside_dev_register(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    CHAR_T *post_data = NULL;
    UINT_T post_data_len = 0;
    ty_cJSON *result = NULL;

    JD_DEV_INFO_S *dev_info = joyinside_get_dev_info();
    if (dev_info->botId != NULL) {
        return OPRT_OK;
    }

    JD_TOKEN_S *token_info = joyinside_get_token_info();
    if (token_info->accessToken == NULL) {
        PR_ERR("please get token first");
        return OPRT_COM_ERROR;
    }

    rt = __jd_make_reg_dev_data(&post_data, &post_data_len);
    if (OPRT_OK != rt) {
        PR_ERR("make reg dev data err, rt:%d", rt);
        return rt;
    }
    rt = joyinside_http_post(JD_DEVICE_REGISTER_URL, post_data, post_data_len, joyinside_add_auth_header, &result);
    Free(post_data);
    if (rt != OPRT_OK) {
        PR_ERR("http post err, rt:%d", rt);
        return rt;
    }
    rt = __jd_parse_dev_reg(result);
    ty_cJSON_Delete(result);
    return rt;
}

OPERATE_RET joyinside_auth_token(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    CHAR_T *post_data = NULL;
    UINT_T post_data_len = 0;
    ty_cJSON *result = NULL;

    JD_TOKEN_S *token_info = joyinside_get_token_info();
    if (token_info->accessToken != NULL) {
        PR_ERR("token already obtained");
        return OPRT_OK;
    }

    rt = __jd_make_token_data(&post_data, &post_data_len);
    if (OPRT_OK != rt) {
        PR_ERR("make post data err, rt:%d", rt);
        return rt;
    }

    rt = joyinside_http_post(JD_GET_TOKEN_URL, post_data, post_data_len, NULL, &result);
    Free(post_data);
    if (rt != OPRT_OK) {
        PR_ERR("http post err, rt:%d", rt);
        return rt;
    }

    rt = __jd_parse_token(result);
    ty_cJSON_Delete(result);
    return rt;
}

STATIC VOID __jd_set_ak_key(JD_AK_KEY_S *a)
{
    if (!a || !a->vendorId || !a->appId || !a->ak || !a->sk) {
        PR_ERR("invalid ak key param");
        return;
    }
    __jd_ak_key_free();
    g_jd_auth.sign.a.vendorId = mm_strdup(a->vendorId);
    g_jd_auth.sign.a.appId = mm_strdup(a->appId);
    g_jd_auth.sign.a.ak = mm_strdup(a->ak);
    g_jd_auth.sign.a.sk = mm_strdup(a->sk);
    PR_DEBUG("set ak key success");
}

STATIC VOID __jd_default_ak_key(VOID)
{
    if (!g_jd_auth.sign.a.vendorId && !g_jd_auth.sign.a.appId &&
        !g_jd_auth.sign.a.ak && !g_jd_auth.sign.a.sk) {
        g_jd_auth.sign.a.vendorId = mm_strdup(JD_DEFAULT_VENDOR_ID);
        g_jd_auth.sign.a.appId = mm_strdup(JD_DEFAULT_APP_ID);
        g_jd_auth.sign.a.ak = mm_strdup(JD_DEFAULT_AK);
        g_jd_auth.sign.a.sk = mm_strdup(JD_DEFAULT_SK);
        JD_PR_D("set default ak key success");
    }
}

STATIC VOID __jd_set_dev_info(JD_DEV_INFO_S *d)
{
    __jd_dev_info_free();
    
    if (d->name) {
        g_jd_auth.sign.d.name = mm_strdup(d->name);
        JD_PR_D("device name %s", g_jd_auth.sign.d.name);
    }
    if (d->type) {
        g_jd_auth.sign.d.type = mm_strdup(d->type);
    }
    if (d->botId) {
        g_jd_auth.sign.d.botId =  mm_strdup(d->botId);
    }
    if (d->sn) {
        g_jd_auth.sign.d.sn = mm_strdup(d->sn);
        JD_PR_D("device sn:%s", g_jd_auth.sign.d.sn);
    }
    if (d->deviceModel) {
        g_jd_auth.sign.d.deviceModel = mm_strdup(d->deviceModel);
    }
    if (d->timbreId) {
        g_jd_auth.sign.d.timbreId = mm_strdup(d->timbreId);
    }
    if (d->desc) {
        g_jd_auth.sign.d.desc = mm_strdup(d->desc);
    }
}

VOID joyinside_set_auth_info(JD_SIGN_S *s)
{
    __jd_set_ak_key(&(s->a));
    __jd_set_dev_info(&(s->d));
}

STATIC OPERATE_RET __jd_make_refresh_token_data(CHAR_T **data, UINT_T *len)
{
    JD_AK_KEY_S *ak_key = joyinside_get_ak_key();
    JD_TOKEN_S *token_info = joyinside_get_token_info();
    JD_DEV_INFO_S *dev_info = joyinside_get_dev_info();
    if (token_info->refreshToken == NULL) {
        PR_ERR("refresh token is null");
        return OPRT_INVALID_PARM;
    }

    ty_cJSON *root = ty_cJSON_CreateObject();
    TUYA_CHECK_NULL_RETURN(root, OPRT_MALLOC_FAILED);
    ty_cJSON_AddStringToObject(root, "accessKeyId", ak_key->ak);
    ty_cJSON_AddStringToObject(root, "refreshToken", token_info->refreshToken);
    if (dev_info->botId) {
        ty_cJSON_AddStringToObject(root, "botId", dev_info->botId);
    } else if (ak_key->vendorId) {
        ty_cJSON_AddStringToObject(root, "vendorId", ak_key->vendorId);
    }
    *data = ty_cJSON_PrintUnformatted(root);
    ty_cJSON_Delete(root);
    TUYA_CHECK_NULL_RETURN(*data, OPRT_MALLOC_FAILED);
    *len = strlen(*data);
    return OPRT_OK;
}

STATIC OPERATE_RET __jd_parse_refresh_token(ty_cJSON *root)
{
    ty_cJSON *code = ty_cJSON_GetObjectItem(root, "code");
    if (code) {
        JD_PR_D("code:%d", code->valueint);
    }
    ty_cJSON *msg = ty_cJSON_GetObjectItem(root, "msg");
    if (msg) {
        JD_PR_D("msg:%s", msg->valuestring);
    }
    ty_cJSON *accessToken = ty_cJSON_GetObjectItem(root, "accessToken");
    ty_cJSON *expireIn = ty_cJSON_GetObjectItem(root, "expireIn");
    if (accessToken && expireIn) {
        if (g_jd_auth.t.accessToken) {
            Free(g_jd_auth.t.accessToken);
            g_jd_auth.t.accessToken = NULL;
        }
        g_jd_auth.t.accessToken = mm_strdup(accessToken->valuestring);
        if (!g_jd_auth.t.accessToken) {
            PR_ERR("strdup failed");
            return OPRT_MALLOC_FAILED;
        }
        g_jd_auth.t.expireIn = expireIn->valueint;
        JD_PR_D("refresh token success, accessToken:%s, expireIn:%u",
                 g_jd_auth.t.accessToken, g_jd_auth.t.expireIn);
        tal_sw_timer_start(g_jd_auth.access_token_tid, g_jd_auth.t.expireIn * 1000, TAL_TIMER_ONCE);
        return OPRT_OK;
    } else {
        PR_ERR("refresh token fail, missing fields");
        return OPRT_CJSON_GET_ERR;
    }
}

STATIC OPERATE_RET __jd_refresh_token(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    CHAR_T *post_data = NULL;
    UINT_T post_data_len = 0;
    ty_cJSON *result = NULL;

    rt = __jd_make_refresh_token_data(&post_data, &post_data_len);
    if (OPRT_OK != rt) {
        PR_ERR("make post data err, rt:%d", rt);
        return rt;
    }
    rt = joyinside_http_post(JD_REFRESH_TOKEN_URL, post_data, post_data_len, NULL, &result);
    Free(post_data);
    if (rt != OPRT_OK) {
        PR_ERR("http post err, rt:%d", rt);
        return rt;
    }

    rt = __jd_parse_refresh_token(result);
    ty_cJSON_Delete(result);
    return rt;
}

STATIC VOID __jd_access_token_expired(TIMER_ID timer_id, VOID_T *data)
{
    PR_ERR("access token expire timeout");
    __jd_refresh_token();
}

STATIC VOID __jd_refresh_token_expired(TIMER_ID timer_id, VOID_T *data)
{
    PR_ERR("refresh token expire timeout, need re-auth");
    tal_sw_timer_stop(g_jd_auth.access_token_tid);
    tal_sw_timer_stop(g_jd_auth.refresh_token_tid);
    joyinside_client_close();
}

STATIC INT_T __jd_reset_evt_cb(VOID *data)
{
    PR_DEBUG("jd auth reset event received");
    wd_common_delete(JD_BOT_ID_KV_KEY);
    return OPRT_OK;
}

VOID joyinside_auth_deinit(VOID)
{
    __jd_ak_key_free();
    __jd_dev_info_free();
    __jd_token_info_free();

    ty_unsubscribe_event(EVENT_RESET, "jd_auth", __jd_reset_evt_cb);
    if (g_jd_auth.access_token_tid) {
        tal_sw_timer_delete(g_jd_auth.access_token_tid);
        g_jd_auth.access_token_tid = NULL;
    }
    if (g_jd_auth.refresh_token_tid) {
        tal_sw_timer_delete(g_jd_auth.refresh_token_tid);
        g_jd_auth.refresh_token_tid = NULL;
    }
    memset(&g_jd_auth, 0, SIZEOF(JD_AUTH_S));
}

STATIC OPERATE_RET __jd_restore_bot_id()
{
    OPERATE_RET rt = OPRT_OK;
    BYTE_T *value = NULL;
    UINT_T len = 0;
    rt = wd_common_read(JD_BOT_ID_KV_KEY, &value, &len);
    if (rt == OPRT_OK && value != NULL && len > 0) {
        if (g_jd_auth.sign.d.botId) {
            Free(g_jd_auth.sign.d.botId);
            g_jd_auth.sign.d.botId = NULL;
        }
        g_jd_auth.sign.d.botId = Malloc(len + 1);
        if (NULL == g_jd_auth.sign.d.botId) {
            PR_ERR("malloc bot id failed");
            Free(value);
            return OPRT_MALLOC_FAILED;
        }
        memset(g_jd_auth.sign.d.botId, 0, len + 1);
        memcpy(g_jd_auth.sign.d.botId, value, len);
        PR_NOTICE("restore bot id %s", g_jd_auth.sign.d.botId);
        Free(value);
    } else {
        PR_DEBUG("no bot id in kv");
    }
    return rt;
}

OPERATE_RET joyinside_auth_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    __jd_default_ak_key();
    TUYA_CALL_ERR_RETURN(__jd_restore_bot_id());
    ty_subscribe_event(EVENT_RESET, "jd_auth", __jd_reset_evt_cb, SUBSCRIBE_TYPE_NORMAL);
    tal_sw_timer_create(__jd_access_token_expired, NULL, &g_jd_auth.access_token_tid);
    tal_sw_timer_create(__jd_refresh_token_expired, NULL, &g_jd_auth.refresh_token_tid);
    return rt;
}