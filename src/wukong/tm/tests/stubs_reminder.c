/*
 * Test-specific stubs for the reminder unit test.
 * cJSON stubs live in stubs_cjson.c; headers in tests/include/.
 */
#include "wukong_tm.h"
#include "wukong_cron.h"
#include "wukong_audio_player.h"
#include "tuya_ai_agent.h"
#include "wukong_ai_agent.h"
#include "base_event.h"
#include "tuya_ws_db.h"
#include "tal_time_service.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- tracking globals exposed to test_reminder.c ---- */

CHAR_T g_last_cron_job_json[512] = {0};
CHAR_T g_last_registered_method[64] = {0};
CHAR_T g_last_removed_job_id[64] = {0};
/* Must hold full SYS_TTS_NOTIFY prompt + triple-quoted reminder (UTF-8). */
CHAR_T g_last_ai_send_text[1024] = {0};

/* ---- platform stubs ---- */

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
    time_t raw = (time_t)posix_time;
    return gmtime_r(&raw, local_tm) == NULL ? OPRT_INVALID_PARM : OPRT_OK;
}

/* ---- wukong_cron stubs ---- */

OPERATE_RET wukong_cron_method_register(CONST CHAR_T *method,
                                        WUKONG_CRON_RPC_HANDLER handler)
{
    (VOID)handler;
    if (method == NULL) {
        return OPRT_INVALID_PARM;
    }
    strncpy(g_last_registered_method, method,
            sizeof(g_last_registered_method) - 1);
    return OPRT_OK;
}

OPERATE_RET wukong_cron_method_unregister(CONST CHAR_T *method)
{
    (VOID)method;
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
    strncpy(job_id, "cron-reminder-1", job_id_len - 1);
    job_id[job_id_len - 1] = '\0';
    return OPRT_OK;
}

OPERATE_RET wukong_cron_job_remove(CONST CHAR_T *job_id)
{
    if (job_id == NULL) {
        return OPRT_INVALID_PARM;
    }
    strncpy(g_last_removed_job_id, job_id,
            sizeof(g_last_removed_job_id) - 1);
    return OPRT_OK;
}

/* ---- AI / audio / event stubs ---- */

VOID tuya_ai_input_start_s(CHAR_T *scode, BOOL_T force)
{
    (VOID)scode; (VOID)force;
}

VOID tuya_ai_input_stop(void) { }

CHAR_T *tuya_ai_agent_get_scode(void *unused)
{
    (void)unused;
    return "default";
}

OPERATE_RET wukong_ai_agent_send_text(CONST CHAR_T *content)
{
    if (content == NULL) {
        return OPRT_INVALID_PARM;
    }
    strncpy(g_last_ai_send_text, content,
            sizeof(g_last_ai_send_text) - 1);
    return OPRT_OK;
}

OPERATE_RET wukong_audio_player_alert(TY_AI_TOY_ALERT_TYPE_E type,
                                      BOOL_T send_eof)
{
    (VOID)type; (VOID)send_eof;
    return OPRT_OK;
}

VOID wukong_ai_event_notify(WUKONG_AI_EVENT_TYPE_E type, VOID *data)
{
    (VOID)type; (VOID)data;
}

/* ---- sub-module init stubs (not under test) ---- */

OPERATE_RET wukong_tm_countdown_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_countdown_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_stopwatch_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_stopwatch_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_pomodoro_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_pomodoro_deinit(VOID) { return OPRT_OK; }

/* ---- event / time-sync stubs ---- */

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

OPERATE_RET tal_time_check_time_sync(VOID) { return OPRT_OK; }
OPERATE_RET tal_time_check_time_zone_sync(VOID) { return OPRT_OK; }
OPERATE_RET wukong_cron_time_ready_notify(VOID) { return OPRT_OK; }

/* ---- KV persistence stubs ---- */

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
