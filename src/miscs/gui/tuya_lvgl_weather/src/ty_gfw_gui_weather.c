/**
 * @file ty_gfw_gui_weather.c
 * @brief
 * @version 0.1
 * @date 2023-06-15
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
#include "gw_intf.h"
#include "uni_log.h"
#include "ty_cJSON.h"
#include "tal_workq_service.h"
#include "tal_time_service.h"
#include "tal_sw_timer.h"
#include "tal_mutex.h"
#include "ty_gfw_gui_weather.h"

#define WEATHER_API "tuya.device.public.data.get"
#define WEATHER_NEW_API "thing.weather.get"
#define HTTP_WEATHER_PARM "{\"codes\":}"

#define WEATHER_TIMER_FIRST_START (3 * 1000)   //ms
#define WEATHER_REQUEST_INTERVAL  (1800 * 1000) //ms
#define WEATHER_COOLDOWN_GAP      (60 * 1000) //ms

typedef struct {
    CHAR_T *request_data;
    TIMER_ID timer_weather;

    SYS_TIME_T cooldown_time;
    CHAR_T *sync_data;

    BYTE_T failed_cnt;
    BOOL_T new_api;
} GFW_WEATHER_CTX_T;

STATIC GFW_WEATHER_CTX_T s_weather_ctx;
STATIC CHAR_T *gui_weather_data = NULL;
STATIC UINT_T gui_weather_len = 0;
STATIC MUTEX_HANDLE weather_mutex = NULL;
STATIC BOOL_T gui_weather_inited = FALSE;

STATIC UINT_T __weather_json_to_lktlv(ty_cJSON *data, BYTE_T* lktlv)
{
    UINT_T offset = 0;
    ty_cJSON* next = data->child;

    while (next) {
        lktlv[offset] = strlen(next->string);
        memcpy(&lktlv[offset + 1], next->string, lktlv[offset]);

        offset += (lktlv[offset] + 1);

        if (ty_cJSON_IsNumber(next)) {
            lktlv[offset++] = 0x00; // number
            lktlv[offset++] = 4;
            lktlv[offset++] = (next->valueint >> 24) & 0xFF;
            lktlv[offset++] = (next->valueint >> 16) & 0xFF;
            lktlv[offset++] = (next->valueint >> 8) & 0xFF;
            lktlv[offset++] = next->valueint & 0xFF;
        } else if (ty_cJSON_IsString(next)) {
            lktlv[offset] = 0x01; // string
            lktlv[offset + 1] = strlen(next->valuestring);
            memcpy(&lktlv[offset + 2], next->valuestring, lktlv[offset + 1]);
            offset += (lktlv[offset + 1] + 2);
        }

        next = next->next;
    }

    return offset;
}

STATIC OPERATE_RET __sync_weather_from_cloud(CHAR_T *codes, CHAR_T **weather_data, UINT_T *data_len)
{
    OPERATE_RET rt = OPRT_OK;
    ty_cJSON* result = NULL;
    ty_cJSON* expiration = NULL;
    ty_cJSON* data = NULL;
    CHAR_T * data_str = NULL;
    BYTE_T* lktlv = NULL;

    if (NULL == codes) {
        return OPRT_INVALID_PARM;
    }

    if ((get_gw_nw_status() != GNS_WAN_VALID) || (get_gw_active() != ACTIVATED)) {
        return OPRT_NETWORK_ERROR;
    }

    rt = iot_httpc_common_post_simple(WEATHER_NEW_API, "1.0", s_weather_ctx.request_data, NULL, &result);
    if (OPRT_OK != rt) {
        goto EXIT;
    }

    expiration = ty_cJSON_GetObjectItem(result, "expiration");
    data = ty_cJSON_GetObjectItem(result, "data");
    if (!data || !expiration) {
        rt = OPRT_INVALID_PARM;
        goto EXIT;
    }

    data_str = ty_cJSON_PrintUnformatted(data);
    if (!data_str) {
        rt = OPRT_MALLOC_FAILED;
        goto EXIT;
    }

    lktlv = Malloc(strlen(data_str) + 128);
    if (!lktlv) {
        rt = OPRT_MALLOC_FAILED;
        goto EXIT;
    }

    lktlv[0] = 0x01; // SUCCEED

    *data_len = __weather_json_to_lktlv(data, lktlv + 1) + 1;
    *weather_data = (CHAR_T *)lktlv;

EXIT:
    if (result) {
        ty_cJSON_Delete(result);
    }

    if (data_str) {
        Free(data_str);
    }

    return rt;
}

STATIC VOID_T __timer_weather_request_cb(TIMER_ID timer_id, VOID_T *arg)
{
    OPERATE_RET rt = OPRT_OK;
    CHAR_T *weather_data = NULL;
    UINT_T len = 0;
    UINT_T timeout = WEATHER_REQUEST_INTERVAL;

    if (get_gw_nw_status() < GNS_LAN_VALID) {
        tal_sw_timer_start(s_weather_ctx.timer_weather, WEATHER_TIMER_FIRST_START, TAL_TIMER_ONCE);
        return ;
    }

    rt = __sync_weather_from_cloud(s_weather_ctx.request_data, &weather_data, &len);
    if (OPRT_OK == rt) {
        PrintLogRaw("raw weather data-%d:\r\n", len);
        for (UINT_T i = 0; i < len; i++) {
            PrintLogRaw("0x%02X ", *(weather_data+i)&0xFF);
        }
        PrintLogRaw("\r\n");
        s_weather_ctx.failed_cnt = 0;
        tal_mutex_lock(weather_mutex);
        if (gui_weather_data) {
            Free(gui_weather_data);
            gui_weather_data = NULL;
        }
        gui_weather_len = 0;
        gui_weather_data = (CHAR_T *)Malloc(len);
        if (gui_weather_data) {
            memcpy((VOID *)gui_weather_data, (VOID *)weather_data, len);
            gui_weather_len = len;
        }
        tal_mutex_unlock(weather_mutex);
        Free(weather_data);
    } else {                //获取天气失败!
        if (s_weather_ctx.failed_cnt++ <= 3) {
            timeout = WEATHER_TIMER_FIRST_START;
        } else if (s_weather_ctx.failed_cnt < 10) {
            timeout = s_weather_ctx.failed_cnt * 10 * 1000;
        }
    }

    tal_sw_timer_start(s_weather_ctx.timer_weather, timeout, TAL_TIMER_ONCE);
}

OPERATE_RET ty_gfw_gui_weather_get(CHAR_T **out_weather, UINT_T *out_len)
{
    OPERATE_RET rt = OPRT_COM_ERROR;

    if (gui_weather_inited) {
        tal_mutex_lock(weather_mutex);
        if (gui_weather_len > 0 && gui_weather_data != NULL) {
            *out_weather = (CHAR_T *)Malloc(gui_weather_len);
            if (*out_weather != NULL) {
                memcpy((VOID *)(*out_weather), (VOID *)gui_weather_data, gui_weather_len);
                *out_len = gui_weather_len;
                rt = OPRT_OK;
            }
        }
        tal_mutex_unlock(weather_mutex);
    }
    return rt;
}

/**
 * @brief 获取天气数据初始化
 * @param[in]  pArr:天气code
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 * @note
 */
OPERATE_RET ty_gfw_gui_weather_init(IN CHAR_T *pArr)
{
    OPERATE_RET rt = OPRT_OK;
    INT_T len = strlen(pArr) + strlen(HTTP_WEATHER_PARM);
    CHAR_T *pCode = NULL;

    if (gui_weather_inited)
        return rt;

    if (tal_mutex_create_init(&weather_mutex) != OPRT_OK) {
        return OPRT_CR_MUTEX_ERR;
    }

    //生成code
    pCode = (CHAR_T*)Malloc(len + 1);
    if (NULL == pCode) {
        PR_ERR("pub_service_open Malloc err");
        return OPRT_MALLOC_FAILED;
    }
    memset(pCode, 0, (len + 1));
    snprintf(pCode, len + 1, "{\"codes\":%s}", pArr);
    s_weather_ctx.request_data = pCode;
    TUYA_CALL_ERR_RETURN(tal_sw_timer_create(__timer_weather_request_cb, &s_weather_ctx, &s_weather_ctx.timer_weather));
    tal_sw_timer_start(s_weather_ctx.timer_weather, WEATHER_TIMER_FIRST_START, TAL_TIMER_ONCE);
    gui_weather_inited = TRUE;
    return OPRT_OK;
}
