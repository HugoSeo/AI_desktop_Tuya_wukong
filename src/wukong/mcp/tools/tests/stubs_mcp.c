/*
 * Test-specific stubs for MCP tool unit test.
 * cJSON stubs are in stubs_cjson.c (shared).
 * Header declarations are in tests/include/.
 */
#include "wukong_ai_mcp.h"
#include "wukong_alarm.h"
#include "skill_clock.h"
#include "tal_time_service.h"
#include "wukong_tm.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- tool registry ---- */
typedef struct {
    const char *name;
    MCP_TOOL_HANDLER_CB handler;
} TOOL_ENTRY_T;

static TOOL_ENTRY_T g_tools[16];
static int g_tool_count = 0;

/* ---- observable test state ---- */
int g_skill_clock_schedule_called = 0;
int g_skill_clock_schedule_query_called = 0;
int g_time_manage_add_called = 0;
int g_time_manage_remove_called = 0;
int g_time_manage_update_called = 0;
int g_time_manage_query_called = 0;
int g_alarm_ack_called = 0;
int g_alarm_add_called = 0;
int g_alarm_update_called = 0;
int g_alarm_remove_called = 0;
int g_countdown_create_called = 0;
int g_countdown_pause_called = 0;
int g_countdown_resume_called = 0;
int g_countdown_delete_called = 0;
TIME_T g_last_reminder_find_time = 0;
TIME_T g_legacy_reminder_find_time = 0;
TIME_T g_last_removed_reminder_time = 0;
char g_last_time_manage_message[128] = {0};
char g_last_alarm_message[128] = {0};
char g_last_alarm_id[64] = {0};
char g_last_reminder_id[64] = {0};
unsigned int g_last_update_alarm_hour = 0;
unsigned int g_last_update_alarm_minute = 0;
static WUKONG_TM_ALARM_CFG_T g_stub_alarm_cfg = {0};
static int g_stub_alarm_cfg_valid = 0;
static WUKONG_TM_REMINDER_CFG_T g_stub_reminder_cfg = {0};
static int g_stub_reminder_cfg_valid = 0;
char g_last_tool_text[256] = {0};
char g_last_reminder_local_time[64] = {0};

#define TEST_TIMEZONE_OFFSET_SEC (8 * 3600)

/* ---- MCP server stubs ---- */
OPERATE_RET mcp_server_tool_register(CONST CHAR_T *name,
                                     CONST CHAR_T *description,
                                     MCP_TOOL_HANDLER_CB handler,
                                     VOID *user_data, ...)
{
    (void)description;
    (void)user_data;
    if (g_tool_count < (int)(sizeof(g_tools) / sizeof(g_tools[0]))) {
        g_tools[g_tool_count].name = name;
        g_tools[g_tool_count].handler = handler;
        g_tool_count++;
    }
    return OPRT_OK;
}

ty_cJSON *mcp_content_make_text(CONST CHAR_T *text)
{
    return ty_cJSON_CreateString(text);
}

/* ---- time stubs ---- */
TIME_T tal_time_mktime(POSIX_TM_S *tm_info)
{
    if (tm_info == NULL) {
        return 0;
    }
    return (TIME_T)timegm(tm_info);
}

OPERATE_RET tal_time_get_local_time_custom(TIME_T ts, POSIX_TM_S *tm_info)
{
    time_t raw = (time_t)(ts + TEST_TIMEZONE_OFFSET_SEC);
    return (gmtime_r(&raw, tm_info) == NULL) ? OPRT_INVALID_PARM : OPRT_OK;
}

TIME_T tal_time_get_posix(VOID)
{
    return (TIME_T)1000;
}

/* ---- tool dispatch helpers ---- */
static MCP_TOOL_HANDLER_CB find_handler(const char *name)
{
    int i = 0;
    for (i = 0; i < g_tool_count; i++) {
        if (strcmp(g_tools[i].name, name) == 0) {
            return g_tools[i].handler;
        }
    }
    return NULL;
}

OPERATE_RET call_registered_tool(const char *name, const ty_cJSON *args)
{
    MCP_TOOL_HANDLER_CB handler = find_handler(name);
    ty_cJSON *out = NULL;
    BOOL_T is_error = FALSE;
    OPERATE_RET rt = OPRT_NOT_FOUND;

    if (handler == NULL) {
        return OPRT_NOT_FOUND;
    }
    rt = handler(name, args, &out, &is_error, NULL);
    (void)is_error;
    if (out != NULL && out->child != NULL &&
        out->child->valuestring != NULL) {
        strncpy(g_last_tool_text, out->child->valuestring,
                sizeof(g_last_tool_text) - 1);
    }
    ty_cJSON_Delete(out);
    return rt;
}

/* ---- legacy alarm stubs ---- */
OPERATE_RET wukong_alarm_add(CONST WUKONG_ALARM_CFG_T *alarm_cfg,
                             CHAR_T *alarm_id, UINT_T alarm_id_len)
{
    (void)alarm_cfg;
    if (alarm_id != NULL && alarm_id_len > 0) {
        alarm_id[0] = '\0';
    }
    return OPRT_OK;
}

OPERATE_RET wukong_alarm_update(CONST CHAR_T *alarm_id,
                                CONST WUKONG_ALARM_CFG_T *alarm_cfg)
{
    if (alarm_id != NULL) {
        strncpy(g_last_alarm_id, alarm_id, sizeof(g_last_alarm_id) - 1);
    }
    (void)alarm_cfg;
    return OPRT_OK;
}

OPERATE_RET wukong_alarm_remove(CONST CHAR_T *alarm_id)
{
    g_alarm_remove_called++;
    if (alarm_id != NULL) {
        strncpy(g_last_alarm_id, alarm_id, sizeof(g_last_alarm_id) - 1);
    }
    return OPRT_OK;
}

OPERATE_RET wukong_alarm_list(CHAR_T **alarm_list_json)
{
    if (alarm_list_json != NULL) {
        *alarm_list_json = strdup("{\"alarms\":[]}");
    }
    return OPRT_OK;
}

OPERATE_RET wukong_alarm_find_by_time(CONST WUKONG_ALARM_CFG_T *alarm_cfg,
                                      CHAR_T *alarm_id,
                                      UINT_T alarm_id_len)
{
    (void)alarm_cfg;
    if (alarm_id != NULL && alarm_id_len > 0) {
        alarm_id[0] = '\0';
    }
    return OPRT_NOT_FOUND;
}

/* ---- TM alarm stubs ---- */
OPERATE_RET wukong_tm_alarm_add(CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg,
                                CONST CHAR_T *alarm_id)
{
    g_alarm_add_called++;
    if (alarm_cfg != NULL) {
        memcpy(&g_stub_alarm_cfg, alarm_cfg, sizeof(g_stub_alarm_cfg));
        g_stub_alarm_cfg_valid = 1;
        strncpy(g_last_alarm_message, alarm_cfg->message,
                sizeof(g_last_alarm_message) - 1);
    }
    if (alarm_id != NULL) {
        strncpy(g_last_alarm_id, alarm_id, sizeof(g_last_alarm_id) - 1);
    }
    return OPRT_OK;
}

OPERATE_RET wukong_tm_alarm_ack(CONST CHAR_T *alarm_id)
{
    g_alarm_ack_called++;
    if (alarm_id != NULL) {
        strncpy(g_last_alarm_id, alarm_id, sizeof(g_last_alarm_id) - 1);
    }
    return OPRT_OK;
}

OPERATE_RET wukong_tm_alarm_get(CONST CHAR_T *alarm_id,
                                WUKONG_TM_ALARM_CFG_T *alarm_cfg)
{
    (void)alarm_id;
    if (!g_stub_alarm_cfg_valid || alarm_cfg == NULL) {
        return OPRT_NOT_FOUND;
    }
    memcpy(alarm_cfg, &g_stub_alarm_cfg, sizeof(*alarm_cfg));
    return OPRT_OK;
}

OPERATE_RET wukong_tm_alarm_update(CONST CHAR_T *alarm_id,
                                   CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg)
{
    g_alarm_update_called++;
    if (alarm_cfg != NULL) {
        memcpy(&g_stub_alarm_cfg, alarm_cfg, sizeof(g_stub_alarm_cfg));
        g_stub_alarm_cfg_valid = 1;
        g_last_update_alarm_hour = (unsigned int)alarm_cfg->hour;
        g_last_update_alarm_minute = (unsigned int)alarm_cfg->minute;
        strncpy(g_last_alarm_message, alarm_cfg->message,
                sizeof(g_last_alarm_message) - 1);
    }
    return wukong_alarm_update(alarm_id,
                               (CONST WUKONG_ALARM_CFG_T *)alarm_cfg);
}

OPERATE_RET wukong_tm_alarm_remove(CONST CHAR_T *alarm_id)
{
    return wukong_alarm_remove(alarm_id);
}

OPERATE_RET wukong_tm_alarm_remove_by_time(
    CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg, UINT_T *removed_count)
{
    (void)alarm_cfg;
    if (removed_count != NULL) {
        *removed_count = 1;
    }
    return OPRT_OK;
}

OPERATE_RET wukong_tm_alarm_list(CHAR_T **alarm_list_json)
{
    return wukong_alarm_list(alarm_list_json);
}

OPERATE_RET wukong_tm_alarm_find_by_time(
    CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg,
    CHAR_T *alarm_id, UINT_T alarm_id_len)
{
    return wukong_alarm_find_by_time(
        (CONST WUKONG_ALARM_CFG_T *)alarm_cfg, alarm_id, alarm_id_len);
}

OPERATE_RET wukong_tm_alarm_ack_active(VOID)
{
    g_alarm_ack_called++;
    return OPRT_OK;
}

/* ---- legacy clock stubs ---- */
TIME_T wukong_clock_time_mktime(CHAR_T *iso_8601_time_str)
{
    if (iso_8601_time_str == NULL) {
        return 0;
    }
    if (strstr(iso_8601_time_str, "20:30:00") != NULL) {
        return 1772829000;
    }
    if (strstr(iso_8601_time_str, "21:30:00") != NULL) {
        return 1772832600;
    }
    return 1772820000;
}

OPERATE_RET wukong_clock_set_countdown_timer(
    TY_AI_CLOCK_TIMER_OPR_TYPE_E opr,
    INT_T hours, INT_T minutes, INT_T seconds)
{
    (void)opr; (void)hours; (void)minutes; (void)seconds;
    return OPRT_OK;
}

OPERATE_RET wukong_clock_set_stopwatch_timer(
    TY_AI_CLOCK_TIMER_OPR_TYPE_E opr)
{
    (void)opr;
    return OPRT_OK;
}

OPERATE_RET wukong_clock_set_pomodoro_timer(
    TY_AI_CLOCK_TIMER_OPR_TYPE_E opr,
    TY_AI_CLOCK_POMODORO_TIMER_CFG_T *pomodoro)
{
    (void)opr; (void)pomodoro;
    return OPRT_OK;
}

OPERATE_RET wukong_clock_set_schedule(
    TY_AI_CLOCK_SCHED_OPR_TYPE_E opr,
    TY_AI_CLOCK_SCHED_CFG_T *sched)
{
    (void)opr; (void)sched;
    g_skill_clock_schedule_called++;
    return OPRT_OK;
}

CHAR_T *wukong_clock_set_schedule_query(
    TY_AI_CLOCK_SCHED_QUERY_METHOD_E query_method,
    TY_AI_CLOCK_SCHED_QUERY_CFG_T *sched_query)
{
    (void)query_method; (void)sched_query;
    g_skill_clock_schedule_query_called++;
    return strdup("[]");
}

/* ---- TM reminder stubs ---- */
OPERATE_RET wukong_tm_reminder_add(
    CONST WUKONG_TM_REMINDER_CFG_T *reminder_cfg,
    CONST CHAR_T *reminder_id)
{
    g_time_manage_add_called++;
    if (reminder_cfg != NULL) {
        memcpy(&g_stub_reminder_cfg, reminder_cfg,
               sizeof(g_stub_reminder_cfg));
        g_stub_reminder_cfg_valid = 1;
        POSIX_TM_S tm_info = {0};
        strncpy(g_last_time_manage_message, reminder_cfg->message,
                sizeof(g_last_time_manage_message) - 1);
        if (tal_time_get_local_time_custom(reminder_cfg->start_time,
                                           &tm_info) == OPRT_OK) {
            snprintf(g_last_reminder_local_time,
                     sizeof(g_last_reminder_local_time),
                     "%04d-%02d-%02dT%02d:%02d:%02d",
                     tm_info.tm_year + 1900, tm_info.tm_mon + 1,
                     tm_info.tm_mday, tm_info.tm_hour,
                     tm_info.tm_min, tm_info.tm_sec);
        }
    }
    (VOID)reminder_id;
    return OPRT_OK;
}

OPERATE_RET wukong_tm_reminder_get(CONST CHAR_T *reminder_id,
                                   WUKONG_TM_REMINDER_CFG_T *reminder_cfg)
{
    (void)reminder_id;
    if (!g_stub_reminder_cfg_valid || reminder_cfg == NULL) {
        return OPRT_NOT_FOUND;
    }
    memcpy(reminder_cfg, &g_stub_reminder_cfg, sizeof(*reminder_cfg));
    return OPRT_OK;
}

OPERATE_RET wukong_tm_reminder_update(
    CONST CHAR_T *reminder_id,
    CONST WUKONG_TM_REMINDER_CFG_T *reminder_cfg)
{
    (void)reminder_id;
    g_time_manage_update_called++;
    if (reminder_cfg != NULL) {
        memcpy(&g_stub_reminder_cfg, reminder_cfg,
               sizeof(g_stub_reminder_cfg));
        g_stub_reminder_cfg_valid = 1;
        POSIX_TM_S tm_info = {0};
        strncpy(g_last_time_manage_message, reminder_cfg->message,
                sizeof(g_last_time_manage_message) - 1);
        if (tal_time_get_local_time_custom(reminder_cfg->start_time,
                                           &tm_info) == OPRT_OK) {
            snprintf(g_last_reminder_local_time,
                     sizeof(g_last_reminder_local_time),
                     "%04d-%02d-%02dT%02d:%02d:%02d",
                     tm_info.tm_year + 1900, tm_info.tm_mon + 1,
                     tm_info.tm_mday, tm_info.tm_hour,
                     tm_info.tm_min, tm_info.tm_sec);
        }
    }
    if (reminder_id != NULL) {
        strncpy(g_last_reminder_id, reminder_id,
                sizeof(g_last_reminder_id) - 1);
    }
    return OPRT_OK;
}

OPERATE_RET wukong_tm_reminder_remove(CONST CHAR_T *reminder_id)
{
    g_time_manage_remove_called++;
    if (reminder_id != NULL) {
        strncpy(g_last_reminder_id, reminder_id,
                sizeof(g_last_reminder_id) - 1);
    }
    return OPRT_OK;
}

OPERATE_RET wukong_tm_reminder_remove_by_time(TIME_T start_time,
                                              UINT_T *removed_count)
{
    g_time_manage_remove_called++;
    g_last_removed_reminder_time = start_time;
    if (removed_count != NULL) {
        *removed_count = 1;
    }
    return OPRT_OK;
}

OPERATE_RET wukong_tm_reminder_find_by_time(TIME_T start_time,
    CHAR_T *reminder_id, UINT_T reminder_id_len)
{
    g_last_reminder_find_time = start_time;
    if (reminder_id == NULL || reminder_id_len == 0) {
        return OPRT_INVALID_PARM;
    }
    if (g_legacy_reminder_find_time != 0 &&
        start_time != g_legacy_reminder_find_time) {
        return OPRT_NOT_FOUND;
    }
    strncpy(reminder_id, "reminder-1", reminder_id_len - 1);
    reminder_id[reminder_id_len - 1] = '\0';
    return OPRT_OK;
}

CHAR_T *wukong_tm_reminder_query_text(TIME_T start_time,
    TIME_T end_time, CONST CHAR_T *keyword)
{
    (void)start_time; (void)end_time; (void)keyword;
    g_time_manage_query_called++;
    return strdup("{\"reminders\":[{\"message\":"
                  "\"8点30提醒您下班啦！\"}]}");
}

/* ---- countdown stubs ---- */
OPERATE_RET wukong_tm_countdown_create(INT_T hours, INT_T minutes,
                                       INT_T seconds)
{
    (void)hours; (void)minutes; (void)seconds;
    g_countdown_create_called++;
    return OPRT_OK;
}

OPERATE_RET wukong_tm_countdown_query(
    WUKONG_TM_COUNTDOWN_SNAPSHOT_T *snapshot)
{
    if (snapshot == NULL) {
        return OPRT_INVALID_PARM;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->active = TRUE;
    snapshot->state = WUKONG_TM_COUNTDOWN_STATE_RUNNING;
    snapshot->remaining_sec = 90;
    snapshot->duration_sec = 90;
    snapshot->elapsed_sec = 0;
    return OPRT_OK;
}

OPERATE_RET wukong_tm_countdown_pause(VOID)
{
    g_countdown_pause_called++;
    return OPRT_OK;
}

OPERATE_RET wukong_tm_countdown_resume(VOID)
{
    g_countdown_resume_called++;
    return OPRT_OK;
}

OPERATE_RET wukong_tm_countdown_delete(VOID)
{
    g_countdown_delete_called++;
    return OPRT_OK;
}

/* ---- stopwatch stubs ---- */
OPERATE_RET wukong_tm_stopwatch_start(VOID) { return OPRT_OK; }

OPERATE_RET wukong_tm_stopwatch_query(
    WUKONG_TM_STOPWATCH_STATE_T *state)
{
    if (state == NULL) {
        return OPRT_INVALID_PARM;
    }
    state->active = TRUE;
    state->paused = FALSE;
    state->elapsed_sec = 0;
    return OPRT_OK;
}

OPERATE_RET wukong_tm_stopwatch_pause(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_stopwatch_resume(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_stopwatch_stop(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_stopwatch_reset(VOID) { return OPRT_OK; }

/* ---- pomodoro stubs ---- */
OPERATE_RET wukong_tm_pomodoro_start(
    CONST WUKONG_TM_POMODORO_CFG_T *pomodoro_cfg)
{
    (void)pomodoro_cfg;
    return OPRT_OK;
}

OPERATE_RET wukong_tm_pomodoro_pause(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_pomodoro_resume(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_pomodoro_stop(VOID) { return OPRT_OK; }

OPERATE_RET wukong_tm_pomodoro_query(
    WUKONG_TM_POMODORO_STATE_T *state)
{
    if (state == NULL) {
        return OPRT_INVALID_PARM;
    }
    memset(state, 0, sizeof(*state));
    state->active = TRUE;
    state->paused = FALSE;
    state->session_id = 1;
    state->phase = WUKONG_TM_POMODORO_PHASE_WORK;
    state->current_cycle = 1;
    state->completed_work_count = 0;
    state->phase_start_ts = 100;
    state->phase_end_ts = 1600;
    state->remaining_sec = 1500;
    state->cfg.work_duration = 25;
    state->cfg.short_break_duration = 5;
    state->cfg.long_break_duration = 15;
    state->cfg.work_sessions_before_long_break = 4;
    return OPRT_OK;
}
