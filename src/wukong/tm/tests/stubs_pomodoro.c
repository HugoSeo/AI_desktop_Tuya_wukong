/*
 * Test-specific stubs for the pomodoro unit test.
 * cJSON stubs live in stubs_cjson.c; headers in tests/include/.
 */
#include "wukong_tm.h"
#include "wukong_ai_agent.h"
#include "tal_time_service.h"
#include "wukong_cron.h"

#include <stdio.h>
#include <string.h>

/* ---- tracking globals exposed to test_pomodoro.c ---- */

INT_T  g_last_event_type = -1;
TIME_T g_fake_now = 1000;
CHAR_T g_last_registered_method[64] = {0};
CHAR_T g_last_cron_job_json[512] = {0};
CHAR_T g_last_removed_job_id[64] = {0};
INT_T  g_cron_add_count = 0;
INT_T  g_cron_remove_count = 0;

WUKONG_CRON_RPC_HANDLER g_phase_end_handler = NULL;

/* ---- platform stubs ---- */

TIME_T tal_time_get_posix(VOID) { return g_fake_now; }

OPERATE_RET tal_time_get_local_time_custom(TIME_T ts, POSIX_TM_S *tm_info)
{
    time_t raw = (time_t)ts;
    return gmtime_r(&raw, tm_info) == NULL ? OPRT_INVALID_PARM : OPRT_OK;
}

VOID wukong_ai_event_notify(WUKONG_AI_EVENT_TYPE_E type, VOID *data)
{
    (void)data;
    g_last_event_type = (INT_T)type;
}

OPERATE_RET wukong_ai_agent_send_text(CONST CHAR_T *text)
{
    (VOID)text;
    return OPRT_OK;
}

/* ---- wukong_cron stubs ---- */

OPERATE_RET wukong_cron_method_register(CONST CHAR_T *method,
                                        WUKONG_CRON_RPC_HANDLER handler)
{
    if (method == NULL) { return OPRT_INVALID_PARM; }
    strncpy(g_last_registered_method, method,
            sizeof(g_last_registered_method) - 1);
    g_phase_end_handler = handler;
    return OPRT_OK;
}

OPERATE_RET wukong_cron_method_unregister(CONST CHAR_T *method)
{
    (VOID)method;
    g_phase_end_handler = NULL;
    return OPRT_OK;
}

OPERATE_RET wukong_cron_job_add(CONST CHAR_T *job_json, CHAR_T *job_id,
                                UINT_T job_id_len)
{
    if (job_json == NULL || job_id == NULL || job_id_len == 0) {
        return OPRT_INVALID_PARM;
    }
    strncpy(g_last_cron_job_json, job_json,
            sizeof(g_last_cron_job_json) - 1);
    snprintf(job_id, job_id_len, "pomo-cron-%d", g_cron_add_count++);
    return OPRT_OK;
}

OPERATE_RET wukong_cron_job_remove(CONST CHAR_T *job_id)
{
    if (job_id == NULL) { return OPRT_INVALID_PARM; }
    strncpy(g_last_removed_job_id, job_id,
            sizeof(g_last_removed_job_id) - 1);
    g_cron_remove_count++;
    return OPRT_OK;
}

/* ---- sibling-module stubs (not under test) ---- */

OPERATE_RET wukong_tm_alarm_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_alarm_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_reminder_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_reminder_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_countdown_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_countdown_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_stopwatch_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_stopwatch_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_reminder_action_notify(CONST CHAR_T *message)
{
    (VOID)message;
    return OPRT_OK;
}
