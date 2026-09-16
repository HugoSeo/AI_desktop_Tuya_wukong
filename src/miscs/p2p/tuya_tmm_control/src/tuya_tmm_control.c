/**
 * @file tuya_tmm_control.c
 * @brief TMM RTC signaling (SS190-compatible MQTT format for device-to-device call)
 * @copyright Copyright (c) Tuya Inc.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "uni_log.h"
#include "ty_cJSON.h"
#include "tal_mutex.h"
#include "tal_semaphore.h"
#include "tal_system.h"
#include "tal_thread.h"
#include "tal_time_service.h"
#include "tuya_iot_internal_api.h"
#include "gw_intf.h"
#include "tuya_tmm_control.h"
#include "mqc_app.h"

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define TMM_CONTROL_MQ_PROTOCOL_NUM             308
#define TMM_CONTROL_DEV_ID_LEN                  64
#define TMM_CONTROL_SESSION_ID_LEN              64
#define TMM_CONTROL_HEART_BEAT_TIMEOUT          (30 * 1000)
#define TMM_CONTROL_DEVICE_META_SAVE            "tuya.device.meta.save"
#define TMM_CONTROL_TASK_STACK_SIZE             (8192)

#define TMM_CONTROL_DEFAULT_BIZTYPE             "screen_ipc"

#define TMM_CONTROL_COM_FORMAT_STR \
    "{\"type\": \"rtc\", \"data\":{\"event\":\"%s\", \"curId\":\"%s\", \"sessionId\":\"%s\"}}"
#define TMM_CONTROL_COM1_FORMAT_STR \
    "{\"type\": \"rtc\", \"data\":{\"event\":\"%s\", \"curId\":\"%s\", \"targetId\":\"%s\", \"sessionId\":\"%s\"}}"
#define TMM_CONTROL_CALL_FORMAT_STR \
    "{\"type\": \"rtc\", \"data\":{\"event\":\"call\", \"curId\":\"%s\", \"targetId\": \"%s\", \"sessionId\":\"%s\", \"timeout\":%lld, \"extra\":{\"channelType\":%d, \"category\":\"%s\"}}}"
#define TMM_CONTROL_CALL_APP_FORMAT_STR \
    "{\"type\": \"rtc\", \"data\":{\"event\":\"call\", \"callType\":3,\"curId\":\"%s\", \"targetId\": \"%s\", \"sessionId\":\"%s\", \"timeout\":%lld, \"extra\":{\"channelType\":%d, \"category\":\"%s\", \"bizType\":\"%s\"}}}"

#define tuya_hal_system_sleep(ms)               tal_system_sleep(ms)
#define tuya_hal_semaphore_create_init          tal_semaphore_create_init
#define tuya_hal_semaphore_wait                 tal_semaphore_wait
#define tuya_hal_semaphore_post                 tal_semaphore_post
#define tuya_hal_semaphore_release              tal_semaphore_release

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef enum {
    TUYA_TMM_CONTROL_STATUS_IDLE = 0,
    TUYA_TMM_CONTROL_STATUS_CALLING,
    TUYA_TMM_CONTROL_STATUS_INCOMING,
    TUYA_TMM_CONTROL_STATUS_ACCEPTED,
} TUYA_TMM_CONTROL_STATUS_E;

typedef struct {
    OPERATE_RET op_ret;
    SEM_HANDLE  sem_handle;
} TMM_SYNC_REPORT;

typedef struct {
    BOOL_T                          inited;
    BOOL_T                          task_run;
    BOOL_T                          report_rtc_cap;
    THREAD_HANDLE                   thrd_handle;
    MUTEX_HANDLE                    lock;
    TUYA_TMM_CONTROL_STATUS_E       status;
    TUYA_TMM_CONTROL_EVT_CB         event_cb;
    VOID                           *priv_data;
    INT_T                           call_timeout_int;
    INT64_T                         call_timeout;
    INT64_T                         heartbeat_tick;
    CHAR_T                          targetid[TMM_CONTROL_DEV_ID_LEN];
    CHAR_T                          sesionid[TMM_CONTROL_SESSION_ID_LEN];
    TUYA_TMM_CONTROL_STREAM_TYPE_E  stream_type;
} TUYA_TMM_CONTROL_HANDLE_S;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC TUYA_TMM_CONTROL_HANDLE_S g_tmm_control_handle = {0};

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID __tuya_tmm_control_task(VOID *pv);
STATIC VOID __tmm_control_generate_session_id(VOID);
STATIC VOID __tmm_control_result_cb(IN CONST OPERATE_RET op_ret, IN CONST VOID *prv_data);
STATIC OPERATE_RET __tmm_control_mqc_proto_cb(IN ty_cJSON *root_json);
STATIC OPERATE_RET __tmm_control_send_mqtt_msg(IN CHAR_T *p_data);
STATIC VOID __tmm_control_fill_target_info(ty_cJSON *json, TUYA_TMM_CONTROL_INFO_S *info);
STATIC BOOL_T __tmm_control_is_app_target(CONST CHAR_T *target_device);

/**
 * @brief Check whether call target is mobile app (key_*)
 * @param[in] target_device call target id
 * @return TRUE if app target
 */
STATIC BOOL_T __tmm_control_is_app_target(CONST CHAR_T *target_device)
{
    if (target_device == NULL) {
        return FALSE;
    }

    return (strncmp(target_device, "key_", 4) == 0) ? TRUE : FALSE;
}

/**
 * @brief Fill target id/local key/type/name from RTC JSON payload
 * @param[in] json RTC data object
 * @param[out] info control event info
 * @return none
 */
STATIC VOID __tmm_control_fill_target_info(ty_cJSON *json, TUYA_TMM_CONTROL_INFO_S *info)
{
    ty_cJSON *localkey = NULL;
    ty_cJSON *targetId = NULL;
    ty_cJSON *targetType = NULL;
    ty_cJSON *targetName = NULL;

    if (json == NULL || info == NULL) {
        return;
    }

    localkey = ty_cJSON_GetObjectItem(json, "targetLocalKey");
    targetId = ty_cJSON_GetObjectItem(json, "targetId");
    targetType = ty_cJSON_GetObjectItem(json, "targetType");
    targetName = ty_cJSON_GetObjectItem(json, "targetName");

    if (localkey != NULL && localkey->valuestring != NULL && localkey->valuestring[0] != '\0') {
        snprintf(info->target_localkey, sizeof(info->target_localkey), "%s", localkey->valuestring);
    } else {
        memset(info->target_localkey, 0, sizeof(info->target_localkey));
    }

    if (targetId != NULL && targetId->valuestring != NULL && targetId->valuestring[0] != '\0') {
        snprintf(info->target_id, sizeof(info->target_id), "%s", targetId->valuestring);
    } else {
        memset(info->target_id, 0, sizeof(info->target_id));
    }

    info->target_type = TUYA_TMM_CONTROL_TARGET_TYPE_APP;
    if (targetType != NULL && targetType->valuestring != NULL) {
        if (strcmp(targetType->valuestring, "dev") == 0) {
            info->target_type = TUYA_TMM_CONTROL_TARGET_TYPE_DEV;
        }
    }

    if (targetName != NULL && targetName->valuestring != NULL) {
        snprintf(info->target_name, sizeof(info->target_name), "%s", targetName->valuestring);
    } else {
        memset(info->target_name, 0, sizeof(info->target_name));
    }
}

/**
 * @brief Initialize TMM control module
 * @param[in] cb event callback
 * @param[in] priv_data user private data
 * @param[in] call_timeout_s call timeout in seconds
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_tmm_control_init(TUYA_TMM_CONTROL_EVT_CB cb, VOID *priv_data, INT_T call_timeout_s)
{
    THREAD_CFG_T thrd_param = {0};

    if (g_tmm_control_handle.inited) {
        return OPRT_OK;
    }

    g_tmm_control_handle.event_cb = cb;
    g_tmm_control_handle.priv_data = priv_data;
    g_tmm_control_handle.call_timeout_int = 30;
    if (call_timeout_s > 0) {
        g_tmm_control_handle.call_timeout_int = call_timeout_s;
    }

    if (tal_mutex_create_init(&g_tmm_control_handle.lock) != OPRT_OK) {
        PR_ERR("tal_mutex_create_init failed");
        return OPRT_COM_ERROR;
    }

    iot_mqc_app_register_cb(TMM_CONTROL_MQ_PROTOCOL_NUM, __tmm_control_mqc_proto_cb);

    thrd_param.stackDepth = TMM_CONTROL_TASK_STACK_SIZE;
    thrd_param.priority = THREAD_PRIO_1;
    thrd_param.thrdname = "tuya_tmm_control";
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    thrd_param.psram_mode = 1;
#endif

    if (tal_thread_create_and_start(&g_tmm_control_handle.thrd_handle, NULL, NULL,
                                    __tuya_tmm_control_task, &g_tmm_control_handle, &thrd_param) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }

    g_tmm_control_handle.inited = TRUE;
    return OPRT_OK;
}

/**
 * @brief Deinitialize TMM control module
 * @return OPRT_NOT_SUPPORTED
 */
OPERATE_RET tuya_tmm_control_deinit(VOID)
{
    return OPRT_NOT_SUPPORTED;
}

/**
 * @brief Place outgoing RTC call (SS190 MQTT format)
 * @param[in] target_device peer device id
 * @param[in] category device category, e.g. dgnzk
 * @param[in] biz_type business type, NULL means default screen_ipc
 * @param[in] stream_type audio/video/av stream type
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_tmm_control_call(CHAR_T *target_device, CHAR_T *category, CHAR_T *biz_type,
                                  TUYA_TMM_CONTROL_STREAM_TYPE_E stream_type)
{
    CHAR_T data[320] = {0};

    if (target_device == NULL || category == NULL) {
        PR_ERR("the target device is NULL");
        return OPRT_INVALID_PARM;
    }

    if (stream_type < TUYA_TMM_CONTROL_STREAM_TYPE_AUDIO ||
        stream_type > TUYA_TMM_CONTROL_STREAM_TYPE_AV) {
        PR_ERR("the stream_type not support");
        return OPRT_INVALID_PARM;
    }

    tal_mutex_lock(g_tmm_control_handle.lock);

    __tmm_control_generate_session_id();
    g_tmm_control_handle.stream_type = stream_type;
    g_tmm_control_handle.call_timeout = (INT64_T)tal_time_get_posix_ms() +
                                          (INT64_T)(g_tmm_control_handle.call_timeout_int * 1000);
    snprintf(g_tmm_control_handle.targetid, sizeof(g_tmm_control_handle.targetid), "%s", target_device);

    if (__tmm_control_is_app_target(target_device)) {
        snprintf(data, sizeof(data), TMM_CONTROL_CALL_APP_FORMAT_STR, get_gw_cntl()->gw_if.id,
                 target_device, g_tmm_control_handle.sesionid, g_tmm_control_handle.call_timeout,
                 stream_type, category, biz_type ? biz_type : TMM_CONTROL_DEFAULT_BIZTYPE);
    } else {
        snprintf(data, sizeof(data), TMM_CONTROL_CALL_FORMAT_STR, get_gw_cntl()->gw_if.id,
                 target_device, g_tmm_control_handle.sesionid, g_tmm_control_handle.call_timeout,
                 stream_type, category);
    }

    if (__tmm_control_send_mqtt_msg(data) != OPRT_OK) {
        PR_ERR("__tmm_control_send_mqtt_msg error");
        tal_mutex_unlock(g_tmm_control_handle.lock);
        return OPRT_COM_ERROR;
    }

    g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_CALLING;
    tal_mutex_unlock(g_tmm_control_handle.lock);
    return OPRT_OK;
}

/**
 * @brief Answer incoming RTC call
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_tmm_control_answer(VOID)
{
    CHAR_T data[256] = {0};

    if (g_tmm_control_handle.status != TUYA_TMM_CONTROL_STATUS_INCOMING) {
        return OPRT_COM_ERROR;
    }

    tal_mutex_lock(g_tmm_control_handle.lock);

    snprintf(data, sizeof(data), TMM_CONTROL_COM_FORMAT_STR,
             "answer", get_gw_cntl()->gw_if.id, g_tmm_control_handle.sesionid);

    if (__tmm_control_send_mqtt_msg(data) != OPRT_OK) {
        PR_ERR("__tmm_control_send_mqtt_msg error");
        g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_IDLE;
        tal_mutex_unlock(g_tmm_control_handle.lock);
        return OPRT_COM_ERROR;
    }

    g_tmm_control_handle.heartbeat_tick = tal_time_get_posix_ms();
    g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_ACCEPTED;
    tal_mutex_unlock(g_tmm_control_handle.lock);
    return OPRT_OK;
}

/**
 * @brief Report unanswered for incoming call timeout
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET tuya_tmm_control_unanswer(VOID)
{
    CHAR_T data[256] = {0};
    OPERATE_RET ret = OPRT_OK;

    tal_mutex_lock(g_tmm_control_handle.lock);

    snprintf(data, sizeof(data), TMM_CONTROL_COM1_FORMAT_STR,
             "not_answered", get_gw_cntl()->gw_if.id,
             g_tmm_control_handle.targetid, g_tmm_control_handle.sesionid);

    ret = __tmm_control_send_mqtt_msg(data);
    if (ret != OPRT_OK) {
        PR_ERR("__tmm_control_send_mqtt_msg error");
    }

    g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_IDLE;
    tal_mutex_unlock(g_tmm_control_handle.lock);
    return ret;
}

/**
 * @brief Reject incoming RTC call
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_tmm_control_reject(VOID)
{
    CHAR_T data[256] = {0};
    OPERATE_RET ret = OPRT_OK;

    tal_mutex_lock(g_tmm_control_handle.lock);

    snprintf(data, sizeof(data), TMM_CONTROL_COM_FORMAT_STR,
             "reject", get_gw_cntl()->gw_if.id, g_tmm_control_handle.sesionid);

    ret = __tmm_control_send_mqtt_msg(data);
    if (ret != OPRT_OK) {
        PR_ERR("__tmm_control_send_mqtt_msg error");
    }

    g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_IDLE;
    tal_mutex_unlock(g_tmm_control_handle.lock);
    return ret;
}

/**
 * @brief Cancel outgoing RTC call
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_tmm_control_cancel(VOID)
{
    CHAR_T data[256] = {0};
    OPERATE_RET ret = OPRT_OK;

    tal_mutex_lock(g_tmm_control_handle.lock);

    snprintf(data, sizeof(data), TMM_CONTROL_COM_FORMAT_STR,
             "cancel", get_gw_cntl()->gw_if.id, g_tmm_control_handle.sesionid);

    ret = __tmm_control_send_mqtt_msg(data);
    if (ret != OPRT_OK) {
        PR_ERR("__tmm_control_send_mqtt_msg error");
    }

    g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_IDLE;
    tal_mutex_unlock(g_tmm_control_handle.lock);
    return ret;
}

/**
 * @brief Hang up active RTC call
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_tmm_control_hangup(VOID)
{
    CHAR_T data[256] = {0};
    OPERATE_RET ret = OPRT_OK;

    tal_mutex_lock(g_tmm_control_handle.lock);

    snprintf(data, sizeof(data), TMM_CONTROL_COM_FORMAT_STR,
             "hang_up", get_gw_cntl()->gw_if.id, g_tmm_control_handle.sesionid);

    ret = __tmm_control_send_mqtt_msg(data);
    if (ret != OPRT_OK) {
        PR_ERR("__tmm_control_send_mqtt_msg error");
    }

    g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_IDLE;
    tal_mutex_unlock(g_tmm_control_handle.lock);
    return ret;
}

/**
 * @brief Stop RTC call abnormally
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_tmm_control_stop(VOID)
{
    CHAR_T data[256] = {0};
    OPERATE_RET ret = OPRT_OK;

    tal_mutex_lock(g_tmm_control_handle.lock);

    snprintf(data, sizeof(data), TMM_CONTROL_COM_FORMAT_STR,
             "stop", get_gw_cntl()->gw_if.id, g_tmm_control_handle.sesionid);

    ret = __tmm_control_send_mqtt_msg(data);
    if (ret != OPRT_OK) {
        PR_ERR("__tmm_control_send_mqtt_msg error");
    }

    g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_IDLE;
    tal_mutex_unlock(g_tmm_control_handle.lock);
    return ret;
}

/**
 * @brief Report busy for incoming RTC call
 * @return OPRT_OK on success
 */
OPERATE_RET tuya_tmm_control_busy(VOID)
{
    CHAR_T data[256] = {0};
    OPERATE_RET ret = OPRT_OK;

    tal_mutex_lock(g_tmm_control_handle.lock);

    snprintf(data, sizeof(data), TMM_CONTROL_COM_FORMAT_STR,
             "busy", get_gw_cntl()->gw_if.id, g_tmm_control_handle.sesionid);

    ret = __tmm_control_send_mqtt_msg(data);
    if (ret != OPRT_OK) {
        PR_ERR("__tmm_control_send_mqtt_msg error");
    }

    g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_IDLE;
    tal_mutex_unlock(g_tmm_control_handle.lock);
    return ret;
}

/**
 * @brief Background task for RTC capability report and call timeout
 * @param[in] pv unused
 * @return none
 */
STATIC VOID __tuya_tmm_control_task(VOID *pv)
{
    INT64_T cur_time = 0;
    TUYA_TMM_CONTROL_INFO_S info = {0};

    (VOID)pv;
    g_tmm_control_handle.task_run = TRUE;

    while (g_tmm_control_handle.task_run) {
        if ((g_tmm_control_handle.report_rtc_cap != TRUE) && get_mqc_conn_stat()) {
            CHAR_T post_data[64] = {0};
            snprintf(post_data, sizeof(post_data), "{\"metas\":{\"rtcCapability\": 7}}");
            if (iot_httpc_common_post_simple(TMM_CONTROL_DEVICE_META_SAVE, "1.0",
                                             post_data, NULL, NULL) == OPRT_OK) {
                g_tmm_control_handle.report_rtc_cap = TRUE;
            }
        }

        cur_time = tal_time_get_posix_ms();
        switch (g_tmm_control_handle.status) {
        case TUYA_TMM_CONTROL_STATUS_IDLE:
            break;

        case TUYA_TMM_CONTROL_STATUS_CALLING:
        case TUYA_TMM_CONTROL_STATUS_INCOMING:
            if (cur_time - g_tmm_control_handle.call_timeout >= 0) {
                if (g_tmm_control_handle.status == TUYA_TMM_CONTROL_STATUS_INCOMING) {
                    tuya_tmm_control_unanswer();
                } else {
                    tal_mutex_lock(g_tmm_control_handle.lock);
                    g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_IDLE;
                    tal_mutex_unlock(g_tmm_control_handle.lock);
                }

                info.event = TUYA_TMM_CONTROL_EVT_UNANSWERED;
                info.stream_type = g_tmm_control_handle.stream_type;
                memset(info.target_id, 0, sizeof(info.target_id));
                if (g_tmm_control_handle.event_cb != NULL) {
                    g_tmm_control_handle.event_cb(&info, g_tmm_control_handle.priv_data);
                }
            }
            break;

        case TUYA_TMM_CONTROL_STATUS_ACCEPTED:
            if (cur_time - g_tmm_control_handle.heartbeat_tick > TMM_CONTROL_HEART_BEAT_TIMEOUT) {
                tal_mutex_lock(g_tmm_control_handle.lock);
                g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_IDLE;
                tal_mutex_unlock(g_tmm_control_handle.lock);

                info.event = TUYA_TMM_CONTROL_EVT_ERROR;
                info.stream_type = g_tmm_control_handle.stream_type;
                snprintf(info.target_id, sizeof(info.target_id), "%s", g_tmm_control_handle.targetid);
                if (g_tmm_control_handle.event_cb != NULL) {
                    g_tmm_control_handle.event_cb(&info, g_tmm_control_handle.priv_data);
                }
            }
            break;

        default:
            break;
        }

        tuya_hal_system_sleep(2000);
    }
}

/**
 * @brief Reply heartbeat to peer
 * @return none
 */
STATIC VOID __heartbeat_reply(VOID)
{
    CHAR_T data[256] = {0};

    snprintf(data, sizeof(data), TMM_CONTROL_COM_FORMAT_STR,
             "heartbeat", get_gw_cntl()->gw_if.id, g_tmm_control_handle.sesionid);

    if (__tmm_control_send_mqtt_msg(data) != OPRT_OK) {
        PR_ERR("__tmm_control_send_mqtt_msg error");
    }
}

/**
 * @brief Reply ring to caller
 * @return none
 */
STATIC VOID __ring_reply(VOID)
{
    CHAR_T data[256] = {0};

    snprintf(data, sizeof(data), TMM_CONTROL_COM_FORMAT_STR,
             "ring", get_gw_cntl()->gw_if.id, g_tmm_control_handle.sesionid);

    if (__tmm_control_send_mqtt_msg(data) != OPRT_OK) {
        PR_ERR("__tmm_control_send_mqtt_msg error");
    }
}

/**
 * @brief MQTT protocol 308 callback for RTC signaling
 * @param[in] root_json parsed MQTT payload
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __tmm_control_mqc_proto_cb(IN ty_cJSON *root_json)
{
    STATIC INT64_T last_time = 0;
    ty_cJSON *data = NULL;
    ty_cJSON *json = NULL;
    ty_cJSON *event = NULL;
    ty_cJSON *targetId = NULL;
    ty_cJSON *sessionId = NULL;
    ty_cJSON *timeout = NULL;
    ty_cJSON *extra = NULL;
    ty_cJSON *type = NULL;
    TUYA_TMM_CONTROL_INFO_S info = {0};

    if ((data = ty_cJSON_GetObjectItem(root_json, "data")) == NULL) {
        PR_ERR("data not in rootJson");
        return OPRT_CJSON_PARSE_ERR;
    }

    type = ty_cJSON_GetObjectItem(data, "type");
    if (type == NULL || type->valuestring == NULL || strcmp(type->valuestring, "rtc") != 0) {
        PR_ERR("the json type not support");
        return OPRT_CJSON_PARSE_ERR;
    }

    if ((json = ty_cJSON_GetObjectItem(data, "data")) == NULL) {
        PR_ERR("data not in rootJson");
        return OPRT_CJSON_PARSE_ERR;
    }

    event = ty_cJSON_GetObjectItem(json, "event");
    sessionId = ty_cJSON_GetObjectItem(json, "sessionId");
    targetId = ty_cJSON_GetObjectItem(json, "targetId");

    /* event/sessionId 来自 MQTT 网络不可信输入,必须同时校验节点存在且 valuestring 非空,
     * 否则 strcmp(NULL, ...) 空指针崩溃(参考 __tmm_control_fill_target_info 已有写法) */
    if (event == NULL || event->valuestring == NULL ||
        sessionId == NULL || sessionId->valuestring == NULL) {
        PR_ERR("event or sessionId invalid");
        return OPRT_CJSON_PARSE_ERR;
    }

    __tmm_control_fill_target_info(json, &info);

    if (strcmp(event->valuestring, "call") == 0) {
        timeout = ty_cJSON_GetObjectItem(json, "timeout");
        extra = ty_cJSON_GetObjectItem(json, "extra");
        if (timeout == NULL || extra == NULL ||
            targetId == NULL || targetId->valuestring == NULL) {
            PR_ERR("call param is error");
            return OPRT_CJSON_PARSE_ERR;
        }

        ty_cJSON *channelType = ty_cJSON_GetObjectItem(extra, "channelType");
        if (channelType == NULL) {
            PR_ERR("get channel type error");
            return OPRT_CJSON_PARSE_ERR;
        }

        tal_mutex_lock(g_tmm_control_handle.lock);
        g_tmm_control_handle.stream_type = (TUYA_TMM_CONTROL_STREAM_TYPE_E)channelType->valueint;
        if (last_time == (INT64_T)timeout->valuedouble) {
            tal_mutex_unlock(g_tmm_control_handle.lock);
            PR_ERR("same time call error");
            return OPRT_COM_ERROR;
        }
        last_time = (INT64_T)timeout->valuedouble;
        g_tmm_control_handle.call_timeout = (INT64_T)timeout->valuedouble;
        snprintf(g_tmm_control_handle.sesionid, sizeof(g_tmm_control_handle.sesionid),
                 "%s", sessionId->valuestring);
        g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_INCOMING;
        snprintf(g_tmm_control_handle.targetid, sizeof(g_tmm_control_handle.targetid),
                 "%s", targetId->valuestring);
        tal_mutex_unlock(g_tmm_control_handle.lock);

        info.event = TUYA_TMM_CONTROL_EVT_INCOMING;
        info.stream_type = g_tmm_control_handle.stream_type;
        if (g_tmm_control_handle.event_cb != NULL) {
            g_tmm_control_handle.event_cb(&info, g_tmm_control_handle.priv_data);
        }
        __ring_reply();
    } else if (strcmp(event->valuestring, "not_answered") == 0) {
        if (g_tmm_control_handle.status != TUYA_TMM_CONTROL_STATUS_IDLE) {
            tal_mutex_lock(g_tmm_control_handle.lock);
            g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_IDLE;
            tal_mutex_unlock(g_tmm_control_handle.lock);

            info.event = TUYA_TMM_CONTROL_EVT_UNANSWERED;
            info.stream_type = g_tmm_control_handle.stream_type;
            if (g_tmm_control_handle.event_cb != NULL) {
                g_tmm_control_handle.event_cb(&info, g_tmm_control_handle.priv_data);
            }
        }
    } else if (strcmp(event->valuestring, "answer") == 0) {
        if (g_tmm_control_handle.status == TUYA_TMM_CONTROL_STATUS_CALLING) {
            tal_mutex_lock(g_tmm_control_handle.lock);
            g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_ACCEPTED;
            g_tmm_control_handle.heartbeat_tick = tal_time_get_posix_ms();
            tal_mutex_unlock(g_tmm_control_handle.lock);

            info.event = TUYA_TMM_CONTROL_EVT_ACCEPTED;
            info.stream_type = g_tmm_control_handle.stream_type;
            if (g_tmm_control_handle.event_cb != NULL) {
                g_tmm_control_handle.event_cb(&info, g_tmm_control_handle.priv_data);
            }
        }
    } else if (strcmp(event->valuestring, "already_answered") == 0) {
        if (g_tmm_control_handle.status != TUYA_TMM_CONTROL_STATUS_IDLE) {
            tal_mutex_lock(g_tmm_control_handle.lock);
            g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_IDLE;
            tal_mutex_unlock(g_tmm_control_handle.lock);

            info.event = TUYA_TMM_CONTROL_EVT_ACCEPTED_BY_OTHERS;
            info.stream_type = g_tmm_control_handle.stream_type;
            if (g_tmm_control_handle.event_cb != NULL) {
                g_tmm_control_handle.event_cb(&info, g_tmm_control_handle.priv_data);
            }
        }
    } else if (strcmp(event->valuestring, "reject") == 0) {
        if (g_tmm_control_handle.status != TUYA_TMM_CONTROL_STATUS_IDLE) {
            tal_mutex_lock(g_tmm_control_handle.lock);
            g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_IDLE;
            tal_mutex_unlock(g_tmm_control_handle.lock);

            info.event = TUYA_TMM_CONTROL_EVT_REJECT;
            info.stream_type = g_tmm_control_handle.stream_type;
            if (g_tmm_control_handle.event_cb != NULL) {
                g_tmm_control_handle.event_cb(&info, g_tmm_control_handle.priv_data);
            }
        }
    } else if (strcmp(event->valuestring, "hang_up") == 0) {
        if (g_tmm_control_handle.status != TUYA_TMM_CONTROL_STATUS_IDLE) {
            tal_mutex_lock(g_tmm_control_handle.lock);
            g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_IDLE;
            tal_mutex_unlock(g_tmm_control_handle.lock);

            info.event = TUYA_TMM_CONTROL_EVT_HANGUP;
            info.stream_type = g_tmm_control_handle.stream_type;
            if (g_tmm_control_handle.event_cb != NULL) {
                g_tmm_control_handle.event_cb(&info, g_tmm_control_handle.priv_data);
            }
        }
    } else if (strcmp(event->valuestring, "busy") == 0) {
        if (g_tmm_control_handle.status != TUYA_TMM_CONTROL_STATUS_IDLE) {
            tal_mutex_lock(g_tmm_control_handle.lock);
            g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_IDLE;
            tal_mutex_unlock(g_tmm_control_handle.lock);

            info.event = TUYA_TMM_CONTROL_EVT_BUSY;
            info.stream_type = g_tmm_control_handle.stream_type;
            if (g_tmm_control_handle.event_cb != NULL) {
                g_tmm_control_handle.event_cb(&info, g_tmm_control_handle.priv_data);
            }
        }
    } else if (strcmp(event->valuestring, "cancel") == 0) {
        if (g_tmm_control_handle.status != TUYA_TMM_CONTROL_STATUS_IDLE) {
            tal_mutex_lock(g_tmm_control_handle.lock);
            g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_IDLE;
            tal_mutex_unlock(g_tmm_control_handle.lock);

            info.event = TUYA_TMM_CONTROL_EVT_CANCEL;
            info.stream_type = g_tmm_control_handle.stream_type;
            if (g_tmm_control_handle.event_cb != NULL) {
                g_tmm_control_handle.event_cb(&info, g_tmm_control_handle.priv_data);
            }
        }
    } else if (strcmp(event->valuestring, "heartbeat") == 0) {
        if (g_tmm_control_handle.status == TUYA_TMM_CONTROL_STATUS_ACCEPTED) {
            __heartbeat_reply();
            tal_mutex_lock(g_tmm_control_handle.lock);
            g_tmm_control_handle.heartbeat_tick = tal_time_get_posix_ms();
            tal_mutex_unlock(g_tmm_control_handle.lock);
        }
    } else if (strcmp(event->valuestring, "stop") == 0) {
        if (g_tmm_control_handle.status != TUYA_TMM_CONTROL_STATUS_IDLE) {
            tal_mutex_lock(g_tmm_control_handle.lock);
            g_tmm_control_handle.status = TUYA_TMM_CONTROL_STATUS_IDLE;
            tal_mutex_unlock(g_tmm_control_handle.lock);

            info.event = TUYA_TMM_CONTROL_EVT_STOP;
            ty_cJSON *reason = ty_cJSON_GetObjectItem(json, "reason");
            if (reason != NULL && reason->valuestring != NULL &&
                strcmp(reason->valuestring, "busy") == 0) {
                info.event = TUYA_TMM_CONTROL_EVT_BUSY;
            }
            info.stream_type = g_tmm_control_handle.stream_type;
            if (g_tmm_control_handle.event_cb != NULL) {
                g_tmm_control_handle.event_cb(&info, g_tmm_control_handle.priv_data);
            }
        }
    } else if (strcmp(event->valuestring, "ring") == 0) {
        if (g_tmm_control_handle.status != TUYA_TMM_CONTROL_STATUS_IDLE) {
            info.event = TUYA_TMM_CONTROL_EVT_RINGING;
            info.stream_type = g_tmm_control_handle.stream_type;
            if (g_tmm_control_handle.event_cb != NULL) {
                g_tmm_control_handle.event_cb(&info, g_tmm_control_handle.priv_data);
            }
        }
    }

    return OPRT_OK;
}

/**
 * @brief Send RTC MQTT message asynchronously
 * @param[in] p_data JSON payload
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __tmm_control_send_mqtt_msg(IN CHAR_T *p_data)
{
    PR_DEBUG("send data:%s", p_data);
    if (iot_mqc_send_custom_msg(TMM_CONTROL_MQ_PROTOCOL_NUM, p_data, 0, 0, NULL, NULL) != OPRT_OK) {
        PR_ERR("iot_mqc_send_custom_msg error");
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

/**
 * @brief MQTT send result callback for sync send
 * @param[in] op_ret operation result
 * @param[in] prv_data user data
 * @return none
 */
STATIC VOID __tmm_control_result_cb(IN CONST OPERATE_RET op_ret, IN CONST VOID *prv_data)
{
    TMM_SYNC_REPORT *p_report = (TMM_SYNC_REPORT *)prv_data;

    if (p_report == NULL) {
        return;
    }

    p_report->op_ret = op_ret;
    tuya_hal_semaphore_post(p_report->sem_handle);
}

/**
 * @brief Generate session id for outgoing call
 * @return none
 */
STATIC VOID __tmm_control_generate_session_id(VOID)
{
    BYTE_T i = 0;
    CHAR_T rand[6] = {0};
    STATIC CHAR_T *str = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHLJKLMNOPQRSTUVWXYZ";

    for (i = 0; i < SIZEOF(rand); i++) {
        rand[i] = str[tal_system_get_random(61)];
    }

    snprintf(g_tmm_control_handle.sesionid, TMM_CONTROL_SESSION_ID_LEN, "%s%c%c%c%c%c%c",
             get_gw_cntl()->gw_if.id, rand[0], rand[1], rand[2], rand[3], rand[4], rand[5]);
}
