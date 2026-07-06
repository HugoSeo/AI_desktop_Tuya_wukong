/*
 * Test-specific stubs for countdown unit test.
 * cJSON functions are in stubs_cjson.c; headers are in tests/include/.
 */
#include "wukong_tm.h"
#include "wukong_ai_agent.h"
#include "wukong_cron.h"
#include "base_event.h"
#include "tal_time_service.h"
#include "ty_cJSON.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

INT_T g_last_event_type = -1;
INT_T g_event_count = 0;
TIME_T g_fake_now = 0;
INT_T g_cron_add_count = 0;
INT_T g_cron_remove_count = 0;
INT_T g_force_cron_add_fail = 0;
INT_T g_local_time_convert_count = 0;
CHAR_T g_last_cron_job_json[512] = {0};
CHAR_T g_last_removed_job_id[64] = {0};
CHAR_T g_registered_method[64] = {0};
WUKONG_CRON_RPC_HANDLER g_registered_handler = NULL;

OPERATE_RET wukong_tm_alarm_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_alarm_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_reminder_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_reminder_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_stopwatch_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_stopwatch_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_pomodoro_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_pomodoro_deinit(VOID) { return OPRT_OK; }
OPERATE_RET tal_time_check_time_sync(VOID) { return OPRT_OK; }
OPERATE_RET tal_time_check_time_zone_sync(VOID) { return OPRT_OK; }
OPERATE_RET wukong_cron_time_ready_notify(VOID) { return OPRT_OK; }

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

TIME_T tal_time_get_posix(VOID)
{
    return g_fake_now;
}

OPERATE_RET tal_time_get_local_time_custom(TIME_T ts,
                                           POSIX_TM_S *tm_info)
{
    time_t raw = (time_t)(ts + (8 * 3600));

    if (tm_info == NULL) {
        return OPRT_INVALID_PARM;
    }
    g_local_time_convert_count++;
    return (gmtime_r(&raw, tm_info) == NULL)
               ? OPRT_COM_ERROR : OPRT_OK;
}

VOID wukong_ai_event_notify(WUKONG_AI_EVENT_TYPE_E type,
                             VOID *data)
{
    (void)data;
    g_last_event_type = (INT_T)type;
    g_event_count++;
}

OPERATE_RET wukong_cron_method_register(CONST CHAR_T *method,
                                        WUKONG_CRON_RPC_HANDLER handler)
{
    if (method == NULL || handler == NULL) {
        return OPRT_INVALID_PARM;
    }
    strncpy(g_registered_method, method,
            sizeof(g_registered_method) - 1);
    g_registered_handler = handler;
    return OPRT_OK;
}

OPERATE_RET wukong_cron_method_unregister(CONST CHAR_T *method)
{
    (void)method;
    g_registered_method[0] = '\0';
    g_registered_handler = NULL;
    return OPRT_OK;
}

OPERATE_RET wukong_cron_job_add(CONST CHAR_T *job_json,
                                CHAR_T *job_id, UINT_T job_id_len)
{
    if (g_force_cron_add_fail) {
        return OPRT_COM_ERROR;
    }
    g_cron_add_count++;
    if (job_json != NULL) {
        strncpy(g_last_cron_job_json, job_json,
                sizeof(g_last_cron_job_json) - 1);
    }
    if (job_id != NULL && job_id_len > 0) {
        strncpy(job_id, "countdown-job-1", job_id_len - 1);
        job_id[job_id_len - 1] = '\0';
    }
    return OPRT_OK;
}

OPERATE_RET wukong_cron_job_remove(CONST CHAR_T *job_id)
{
    g_cron_remove_count++;
    if (job_id != NULL) {
        strncpy(g_last_removed_job_id, job_id,
                sizeof(g_last_removed_job_id) - 1);
    }
    return OPRT_OK;
}
