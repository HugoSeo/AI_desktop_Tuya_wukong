/*
 * Test-specific stubs for alarm unit test.
 * cJSON stubs are in stubs_cjson.c (shared).
 * Header declarations are in tests/include/.
 */
#include "wukong_tm.h"
#include "wukong_cron.h"
#include "wukong_audio_player.h"
#include "wukong_ai_agent.h"
#include "tal_time_service.h"
#include "base_event.h"
#include "tuya_ws_db.h"
#include "ty_cJSON.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ---- observable test state ---- */
INT_T g_alert_play_count = 0;
CHAR_T g_last_cron_job_json[512] = {0};
CHAR_T g_prev_cron_job_json[512] = {0};
CHAR_T g_last_removed_cron_job_id[64] = {0};
CHAR_T g_last_registered_method[64] = {0};
CHAR_T g_last_unregistered_method[64] = {0};
INT_T g_cron_job_add_count = 0;
INT_T g_cron_job_remove_count = 0;

/* cron method registry: 4 slots */
CHAR_T g_registered_methods[4][64] = {{0}};
WUKONG_CRON_RPC_HANDLER g_registered_handlers[4] = {0};
INT_T g_registered_method_count = 0;

/* event notify tracking */
INT_T g_event_notify_count = 0;
WUKONG_AI_EVENT_TYPE_E g_last_event_type = 0;

/* ---- cron stubs ---- */
OPERATE_RET wukong_cron_method_register(CONST CHAR_T *method,
                                        WUKONG_CRON_RPC_HANDLER handler)
{
    if (method == NULL) {
        return OPRT_INVALID_PARM;
    }
    strncpy(g_last_registered_method, method,
            sizeof(g_last_registered_method) - 1);
    if (g_registered_method_count < 4) {
        strncpy(g_registered_methods[g_registered_method_count], method,
                sizeof(g_registered_methods[g_registered_method_count]) - 1);
        g_registered_handlers[g_registered_method_count] = handler;
        g_registered_method_count++;
    }
    return OPRT_OK;
}

OPERATE_RET wukong_cron_method_unregister(CONST CHAR_T *method)
{
    if (method == NULL) {
        return OPRT_INVALID_PARM;
    }
    strncpy(g_last_unregistered_method, method,
            sizeof(g_last_unregistered_method) - 1);
    return OPRT_OK;
}

OPERATE_RET wukong_cron_job_add(CONST CHAR_T *job_json, CHAR_T *job_id,
                                UINT_T job_id_len)
{
    if (job_json == NULL || job_id == NULL || job_id_len == 0) {
        return OPRT_INVALID_PARM;
    }
    strncpy(g_prev_cron_job_json, g_last_cron_job_json,
            sizeof(g_prev_cron_job_json) - 1);
    strncpy(g_last_cron_job_json, job_json,
            sizeof(g_last_cron_job_json) - 1);
    g_cron_job_add_count++;
    snprintf(job_id, job_id_len, "cron-job-%d", g_cron_job_add_count);
    job_id[job_id_len - 1] = '\0';
    return OPRT_OK;
}

OPERATE_RET wukong_cron_job_remove(CONST CHAR_T *job_id)
{
    if (job_id == NULL) {
        return OPRT_INVALID_PARM;
    }
    g_cron_job_remove_count++;
    strncpy(g_last_removed_cron_job_id, job_id,
            sizeof(g_last_removed_cron_job_id) - 1);
    return OPRT_OK;
}

OPERATE_RET wukong_audio_player_alert(TY_AI_TOY_ALERT_TYPE_E type,
                                      BOOL_T send_eof)
{
    (VOID)type;
    (VOID)send_eof;
    g_alert_play_count++;
    return OPRT_OK;
}

/* other TM sub-module stubs (not under test) */
OPERATE_RET wukong_tm_reminder_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_reminder_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_countdown_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_countdown_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_stopwatch_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_stopwatch_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_pomodoro_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_pomodoro_deinit(VOID) { return OPRT_OK; }

/* ---- event notify stub ---- */
VOID wukong_ai_event_notify(WUKONG_AI_EVENT_TYPE_E type, VOID *data)
{
    (VOID)data;
    g_last_event_type = type;
    g_event_notify_count++;
}

/* ---- subscribe stubs ---- */
OPERATE_RET ty_subscribe_event(INT_T event_id, CONST CHAR_T *name,
                               SUBSCRIBE_CALLBACK cb, INT_T type)
{
    (VOID)event_id; (VOID)name; (VOID)cb; (VOID)type;
    return OPRT_OK;
}

OPERATE_RET ty_unsubscribe_event(INT_T event_id, CONST CHAR_T *name,
                                 SUBSCRIBE_CALLBACK cb)
{
    (VOID)event_id; (VOID)name; (VOID)cb;
    return OPRT_OK;
}

/* ---- time stubs ---- */
OPERATE_RET tal_time_check_time_sync(VOID) { return OPRT_OK; }
OPERATE_RET tal_time_check_time_zone_sync(VOID) { return OPRT_OK; }
OPERATE_RET wukong_cron_time_ready_notify(VOID) { return OPRT_OK; }

TIME_T tal_time_get_posix(void)
{
    return 1772829000;
}

TIME_T tal_time_mktime(POSIX_TM_S *tm_info)
{
    if (tm_info == NULL) {
        return 0;
    }
    return (TIME_T)timegm(tm_info);
}

OPERATE_RET tal_time_get_local_time_custom(TIME_T posix_time,
                                           POSIX_TM_S *local_tm)
{
    time_t raw_time = (time_t)posix_time;
    if (local_tm == NULL) {
        return OPRT_INVALID_PARM;
    }
    return (gmtime_r(&raw_time, local_tm) == NULL)
               ? OPRT_INVALID_PARM : OPRT_OK;
}

/* ---- ws_db stubs ---- */
OPERATE_RET wd_common_write(CONST CHAR_T *key, CONST BYTE_T *value,
                            UINT_T len)
{
    (VOID)key; (VOID)value; (VOID)len;
    return OPRT_OK;
}

OPERATE_RET wd_common_read(CONST CHAR_T *key, BYTE_T **value, UINT_T *len)
{
    (VOID)key; (VOID)value; (VOID)len;
    return OPRT_NOT_FOUND;
}

VOID wd_common_free_data(BYTE_T *data) { (VOID)data; }

/* ---- test helpers ---- */
OPERATE_RET call_registered_cron_method(const CHAR_T *method,
                                        const CHAR_T *alarm_id,
                                        INT_T ring_seq)
{
    ty_cJSON *params = NULL;
    ty_cJSON *result = NULL;
    INT_T index = 0;

    for (index = 0; index < g_registered_method_count; index++) {
        if (strcmp(g_registered_methods[index], method) == 0) {
            params = ty_cJSON_CreateObject();
            if (params == NULL) {
                return OPRT_MALLOC_FAILED;
            }
            ty_cJSON_AddStringToObject(params, "alarm_id", alarm_id);
            ty_cJSON_AddNumberToObject(params, "ring_seq", ring_seq);
            if (g_registered_handlers[index] == NULL) {
                ty_cJSON_Delete(params);
                return OPRT_INVALID_PARM;
            }
            if (g_registered_handlers[index](params, &result) != OPRT_OK) {
                ty_cJSON_Delete(params);
                ty_cJSON_Delete(result);
                return OPRT_COM_ERROR;
            }
            ty_cJSON_Delete(params);
            ty_cJSON_Delete(result);
            return OPRT_OK;
        }
    }

    return OPRT_NOT_FOUND;
}

BOOL_T is_cron_method_registered(const CHAR_T *method)
{
    INT_T index = 0;

    for (index = 0; index < g_registered_method_count; index++) {
        if (strcmp(g_registered_methods[index], method) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}
