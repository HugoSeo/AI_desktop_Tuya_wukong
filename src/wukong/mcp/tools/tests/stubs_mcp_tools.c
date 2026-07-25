/**
 * @file stubs_mcp_tools.c
 * @brief Stubs for MCP tool integration tests (schema capture + TM state).
 * Migrated from test_wukong_mcp_tm_tools.sh embedded stubs.c; cJSON is in stubs_cjson.c.
 */
#include "wukong_ai_mcp.h"
#include "wukong_alarm.h"
#include "skill_clock.h"
#include "wukong_tm.h"
#include "tal_time_service.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ty_cJSON.h"

typedef struct {
    const char *name;
    const char *description;
    MCP_TOOL_HANDLER_CB handler;
    char prop_names[32][64];
    char prop_descriptions[32][256];
    BOOL_T prop_required[32];
    int prop_count;
} REGISTERED_TOOL_T;

static REGISTERED_TOOL_T g_registered_tools[16];
static int g_registered_tool_count = 0;
static BOOL_T g_countdown_active = FALSE;
static BOOL_T g_countdown_paused = FALSE;
static TIME_T g_countdown_remaining_sec = 0;
static TIME_T g_countdown_duration_sec = 0;
static BOOL_T g_stopwatch_active = FALSE;
static BOOL_T g_stopwatch_paused = FALSE;
static TIME_T g_stopwatch_seg_start = 0;
static TIME_T g_stopwatch_accum = 0;
static BOOL_T g_pomodoro_active = FALSE;
static TIME_T g_fake_now = 0;
static BOOL_T g_reminder_add_called = FALSE;
static BOOL_T g_reminder_update_called = FALSE;
static CHAR_T g_stub_reminder_id[WUKONG_TM_REMINDER_ID_LEN + 1] = {0};
static WUKONG_TM_REMINDER_CFG_T g_stub_reminder_cfg = {0};
static WUKONG_TM_REMINDER_CFG_T g_last_reminder_add_cfg = {0};
static CHAR_T g_last_reminder_update_id[WUKONG_TM_REMINDER_ID_LEN + 1] = {0};
static WUKONG_TM_REMINDER_CFG_T g_last_reminder_update_cfg = {0};

#define TM_TOOLS_TEST_TZ_OFFSET_SEC (8 * 3600)

OPERATE_RET mcp_server_tool_register(CONST CHAR_T *name,
                                     CONST CHAR_T *description,
                                     MCP_TOOL_HANDLER_CB handler,
                                     VOID *user_data, ...)
{
    (void)user_data;
    va_list ap;
    const MCP_SCHEMA_PROP_T *prop = NULL;

    if (g_registered_tool_count < (int)(sizeof(g_registered_tools) / sizeof(g_registered_tools[0]))) {
        g_registered_tools[g_registered_tool_count].name = name;
        g_registered_tools[g_registered_tool_count].description = description;
        g_registered_tools[g_registered_tool_count].handler = handler;
        g_registered_tools[g_registered_tool_count].prop_count = 0;

        va_start(ap, user_data);
        while ((prop = va_arg(ap, const MCP_SCHEMA_PROP_T *)) != NULL) {
            if (g_registered_tools[g_registered_tool_count].prop_count <
                (int)(sizeof(g_registered_tools[g_registered_tool_count].prop_names) /
                      sizeof(g_registered_tools[g_registered_tool_count].prop_names[0]))) {
                int idx = g_registered_tools[g_registered_tool_count].prop_count++;
                if (prop->name != NULL) {
                    strncpy(g_registered_tools[g_registered_tool_count].prop_names[idx],
                            prop->name,
                            sizeof(g_registered_tools[g_registered_tool_count].prop_names[idx]) - 1);
                }
                if (prop->description != NULL) {
                    strncpy(g_registered_tools[g_registered_tool_count].prop_descriptions[idx],
                            prop->description,
                            sizeof(g_registered_tools[g_registered_tool_count].prop_descriptions[idx]) - 1);
                }
                g_registered_tools[g_registered_tool_count].prop_required[idx] = prop->required;
            }
        }
        va_end(ap);

        g_registered_tool_count++;
    }
    return OPRT_OK;
}

ty_cJSON *mcp_content_make_text(CONST CHAR_T *text)
{
    return ty_cJSON_CreateString(text);
}

OPERATE_RET wukong_alarm_update(CONST CHAR_T *alarm_id, CONST WUKONG_ALARM_CFG_T *alarm_cfg)
{
    (void)alarm_id;
    (void)alarm_cfg;
    return OPRT_OK;
}

OPERATE_RET wukong_alarm_remove(CONST CHAR_T *alarm_id)
{
    (void)alarm_id;
    return OPRT_OK;
}

OPERATE_RET wukong_alarm_list(CHAR_T **alarm_list_json)
{
    if (alarm_list_json != NULL) {
        *alarm_list_json = strdup("{\"alarms\":[]}");
    }
    return OPRT_OK;
}

OPERATE_RET wukong_alarm_find_by_time(CONST WUKONG_ALARM_CFG_T *alarm_cfg, CHAR_T *alarm_id, UINT_T alarm_id_len)
{
    (void)alarm_cfg;
    if (alarm_id != NULL && alarm_id_len > 0) {
        alarm_id[0] = '\0';
    }
    return OPRT_OK;
}

OPERATE_RET wukong_alarm_add(CONST WUKONG_ALARM_CFG_T *alarm_cfg, CHAR_T *alarm_id, UINT_T alarm_id_len)
{
    (void)alarm_cfg;
    if (alarm_id != NULL && alarm_id_len > 0) {
        alarm_id[0] = '\0';
    }
    return OPRT_OK;
}

OPERATE_RET wukong_tm_alarm_add(CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg, CONST CHAR_T *alarm_id)
{
    (void)alarm_cfg;
    (void)alarm_id;
    return OPRT_OK;
}

OPERATE_RET wukong_tm_alarm_ack(CONST CHAR_T *alarm_id)
{
    (void)alarm_id;
    return OPRT_OK;
}

OPERATE_RET wukong_tm_alarm_update(CONST CHAR_T *alarm_id, CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg)
{
    return wukong_alarm_update(alarm_id, (CONST WUKONG_ALARM_CFG_T *)alarm_cfg);
}

OPERATE_RET wukong_tm_alarm_get(CONST CHAR_T *alarm_id, WUKONG_TM_ALARM_CFG_T *alarm_cfg)
{
    (void)alarm_id;
    (void)alarm_cfg;
    return OPRT_NOT_FOUND;
}

OPERATE_RET wukong_tm_alarm_remove(CONST CHAR_T *alarm_id)
{
    return wukong_alarm_remove(alarm_id);
}

OPERATE_RET wukong_tm_alarm_remove_by_time(CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg, UINT_T *removed_count)
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

OPERATE_RET wukong_tm_alarm_find_by_time(CONST WUKONG_TM_ALARM_CFG_T *alarm_cfg, CHAR_T *alarm_id, UINT_T alarm_id_len)
{
    return wukong_alarm_find_by_time((CONST WUKONG_ALARM_CFG_T *)alarm_cfg, alarm_id, alarm_id_len);
}

OPERATE_RET wukong_tm_alarm_ack_active(VOID)
{
    return OPRT_OK;
}

TIME_T wukong_clock_time_mktime(CHAR_T *iso_8601_time_str)
{
    (void)iso_8601_time_str;
    return 0;
}

OPERATE_RET wukong_clock_set_countdown_timer(TY_AI_CLOCK_TIMER_OPR_TYPE_E opr, INT_T hours, INT_T minutes, INT_T seconds)
{
    (void)opr;
    (void)hours;
    (void)minutes;
    (void)seconds;
    return OPRT_OK;
}

OPERATE_RET wukong_clock_set_stopwatch_timer(TY_AI_CLOCK_TIMER_OPR_TYPE_E opr)
{
    (void)opr;
    return OPRT_OK;
}

OPERATE_RET wukong_clock_set_pomodoro_timer(TY_AI_CLOCK_TIMER_OPR_TYPE_E opr, TY_AI_CLOCK_POMODORO_TIMER_CFG_T *pomodoro)
{
    (void)opr;
    (void)pomodoro;
    return OPRT_OK;
}

OPERATE_RET wukong_clock_set_schedule(TY_AI_CLOCK_SCHED_OPR_TYPE_E opr, TY_AI_CLOCK_SCHED_CFG_T *sched)
{
    (void)opr;
    (void)sched;
    return OPRT_OK;
}

CHAR_T *wukong_clock_set_schedule_query(TY_AI_CLOCK_SCHED_QUERY_METHOD_E query_method, TY_AI_CLOCK_SCHED_QUERY_CFG_T *sched_query)
{
    (void)query_method;
    (void)sched_query;
    return strdup("[]");
}

TIME_T tal_time_mktime(POSIX_TM_S *tm_info)
{
    if (tm_info == NULL) {
        return 0;
    }
    return (TIME_T)timegm(tm_info);
}

OPERATE_RET tal_time_get_local_time_custom(TIME_T ts, POSIX_TM_S *tm_info)
{
    time_t raw = (time_t)(ts + TM_TOOLS_TEST_TZ_OFFSET_SEC);
    return (gmtime_r(&raw, tm_info) == NULL) ? OPRT_INVALID_PARM : OPRT_OK;
}

TIME_T tal_time_get_posix(VOID)
{
    return g_fake_now;
}

OPERATE_RET wukong_tm_reminder_add(CONST WUKONG_TM_REMINDER_CFG_T *reminder_cfg,
                                   CONST CHAR_T *reminder_id)
{
    if (reminder_cfg == NULL) {
        return OPRT_INVALID_PARM;
    }

    g_reminder_add_called = TRUE;
    g_last_reminder_add_cfg = *reminder_cfg;
    (VOID)reminder_id;
    return OPRT_OK;
}

OPERATE_RET wukong_tm_reminder_update(CONST CHAR_T *reminder_id, CONST WUKONG_TM_REMINDER_CFG_T *reminder_cfg)
{
    if (reminder_id == NULL || reminder_cfg == NULL) {
        return OPRT_INVALID_PARM;
    }

    g_reminder_update_called = TRUE;
    strncpy(g_last_reminder_update_id, reminder_id, sizeof(g_last_reminder_update_id) - 1);
    g_last_reminder_update_id[sizeof(g_last_reminder_update_id) - 1] = '\0';
    g_last_reminder_update_cfg = *reminder_cfg;
    return OPRT_OK;
}

OPERATE_RET wukong_tm_reminder_get(CONST CHAR_T *reminder_id, WUKONG_TM_REMINDER_CFG_T *reminder_cfg)
{
    if (reminder_id == NULL || reminder_cfg == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (strcmp(reminder_id, g_stub_reminder_id) != 0) {
        return OPRT_NOT_FOUND;
    }

    *reminder_cfg = g_stub_reminder_cfg;
    return OPRT_OK;
}

OPERATE_RET wukong_tm_reminder_remove(CONST CHAR_T *reminder_id)
{
    (void)reminder_id;
    return OPRT_OK;
}

OPERATE_RET wukong_tm_reminder_remove_by_time(TIME_T start_time, UINT_T *removed_count)
{
    (void)start_time;
    if (removed_count != NULL) {
        *removed_count = 1;
    }
    return OPRT_OK;
}

OPERATE_RET wukong_tm_reminder_find_by_time(TIME_T start_time, CHAR_T *reminder_id, UINT_T reminder_id_len)
{
    (void)start_time;
    if (reminder_id != NULL && reminder_id_len > 0) {
        reminder_id[0] = '\0';
    }
    return OPRT_OK;
}

CHAR_T *wukong_tm_reminder_query_text(TIME_T start_time, TIME_T end_time, CONST CHAR_T *keyword)
{
    (void)start_time;
    (void)end_time;
    (void)keyword;
    return strdup("{\"reminders\":[]}");
}

void test_set_reminder_snapshot(const char *reminder_id, TIME_T start_time, const char *message)
{
    memset(&g_stub_reminder_cfg, 0, sizeof(g_stub_reminder_cfg));
    memset(&g_last_reminder_add_cfg, 0, sizeof(g_last_reminder_add_cfg));
    memset(&g_last_reminder_update_cfg, 0, sizeof(g_last_reminder_update_cfg));
    memset(g_stub_reminder_id, 0, sizeof(g_stub_reminder_id));
    memset(g_last_reminder_update_id, 0, sizeof(g_last_reminder_update_id));
    g_reminder_add_called = FALSE;
    g_reminder_update_called = FALSE;

    if (reminder_id != NULL) {
        strncpy(g_stub_reminder_id, reminder_id, sizeof(g_stub_reminder_id) - 1);
        g_stub_reminder_id[sizeof(g_stub_reminder_id) - 1] = '\0';
    }

    g_stub_reminder_cfg.enabled = TRUE;
    g_stub_reminder_cfg.start_time = start_time;
    if (message != NULL) {
        strncpy(g_stub_reminder_cfg.message, message, sizeof(g_stub_reminder_cfg.message) - 1);
        g_stub_reminder_cfg.message[sizeof(g_stub_reminder_cfg.message) - 1] = '\0';
    }
}

void test_set_now(TIME_T now)
{
    g_fake_now = now;
}

BOOL_T test_reminder_add_called(void)
{
    return g_reminder_add_called;
}

TIME_T test_last_reminder_add_start_time(void)
{
    return g_last_reminder_add_cfg.start_time;
}

const CHAR_T *test_last_reminder_add_message(void)
{
    return g_last_reminder_add_cfg.message;
}

BOOL_T test_reminder_update_called(void)
{
    return g_reminder_update_called;
}

const CHAR_T *test_last_reminder_update_id(void)
{
    return g_last_reminder_update_id;
}

TIME_T test_last_reminder_update_start_time(void)
{
    return g_last_reminder_update_cfg.start_time;
}

const CHAR_T *test_last_reminder_update_message(void)
{
    return g_last_reminder_update_cfg.message;
}

OPERATE_RET wukong_tm_countdown_create(INT_T hours, INT_T minutes, INT_T seconds)
{
    TIME_T total = 0;

    if (g_countdown_active) {
        return OPRT_COM_ERROR;
    }
    total = (TIME_T)hours * 3600 + (TIME_T)minutes * 60 + (TIME_T)seconds;
    g_countdown_active = TRUE;
    g_countdown_paused = FALSE;
    g_countdown_duration_sec = total;
    g_countdown_remaining_sec = total;
    return OPRT_OK;
}
OPERATE_RET wukong_tm_countdown_pause(VOID)
{
    if (!g_countdown_active) {
        return OPRT_NOT_FOUND;
    }
    g_countdown_paused = TRUE;
    return OPRT_OK;
}
OPERATE_RET wukong_tm_countdown_resume(VOID)
{
    if (!g_countdown_active) {
        return OPRT_NOT_FOUND;
    }
    g_countdown_paused = FALSE;
    return OPRT_OK;
}
OPERATE_RET wukong_tm_countdown_delete(VOID)
{
    if (!g_countdown_active) {
        return OPRT_NOT_FOUND;
    }
    g_countdown_active = FALSE;
    g_countdown_paused = FALSE;
    g_countdown_remaining_sec = 0;
    g_countdown_duration_sec = 0;
    return OPRT_OK;
}
OPERATE_RET wukong_tm_countdown_query(WUKONG_TM_COUNTDOWN_SNAPSHOT_T *snapshot)
{
    TIME_T elapsed = 0;

    if (!g_countdown_active || snapshot == NULL) {
        return OPRT_NOT_FOUND;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->active = TRUE;
    snapshot->state = g_countdown_paused ? WUKONG_TM_COUNTDOWN_STATE_PAUSED : WUKONG_TM_COUNTDOWN_STATE_RUNNING;
    snapshot->duration_sec = g_countdown_duration_sec;
    snapshot->remaining_sec = g_countdown_remaining_sec;
    elapsed = snapshot->duration_sec - snapshot->remaining_sec;
    if (elapsed < 0) {
        elapsed = 0;
    }
    snapshot->elapsed_sec = elapsed;
    return OPRT_OK;
}
OPERATE_RET wukong_tm_stopwatch_start(VOID)
{
    if (g_stopwatch_active) {
        return OPRT_COM_ERROR;
    }
    g_stopwatch_active = TRUE;
    g_stopwatch_paused = FALSE;
    g_stopwatch_accum = 0;
    g_stopwatch_seg_start = g_fake_now;
    return OPRT_OK;
}
OPERATE_RET wukong_tm_stopwatch_pause(VOID)
{
    if (!g_stopwatch_active || g_stopwatch_paused) {
        return OPRT_NOT_FOUND;
    }
    g_stopwatch_accum += (g_fake_now - g_stopwatch_seg_start);
    g_stopwatch_paused = TRUE;
    return OPRT_OK;
}
OPERATE_RET wukong_tm_stopwatch_resume(VOID)
{
    if (!g_stopwatch_active || !g_stopwatch_paused) {
        return OPRT_NOT_FOUND;
    }
    g_stopwatch_seg_start = g_fake_now;
    g_stopwatch_paused = FALSE;
    return OPRT_OK;
}
OPERATE_RET wukong_tm_stopwatch_stop(VOID)
{
    if (!g_stopwatch_active) {
        return OPRT_NOT_FOUND;
    }
    g_stopwatch_active = FALSE;
    g_stopwatch_paused = FALSE;
    g_stopwatch_accum = 0;
    g_stopwatch_seg_start = 0;
    return OPRT_OK;
}
OPERATE_RET wukong_tm_stopwatch_reset(VOID)
{
    if (!g_stopwatch_active) {
        return OPRT_NOT_FOUND;
    }
    g_stopwatch_active = FALSE;
    g_stopwatch_paused = FALSE;
    g_stopwatch_accum = 0;
    g_stopwatch_seg_start = 0;
    return OPRT_OK;
}
OPERATE_RET wukong_tm_stopwatch_query(WUKONG_TM_STOPWATCH_STATE_T *state)
{
    TIME_T elapsed = 0;

    if (!g_stopwatch_active || state == NULL) {
        return OPRT_NOT_FOUND;
    }
    if (g_stopwatch_paused) {
        elapsed = g_stopwatch_accum;
    } else {
        elapsed = g_stopwatch_accum + (g_fake_now - g_stopwatch_seg_start);
    }
    state->active = TRUE;
    state->paused = g_stopwatch_paused;
    state->elapsed_sec = elapsed;
    return OPRT_OK;
}
OPERATE_RET wukong_tm_pomodoro_start(CONST WUKONG_TM_POMODORO_CFG_T *pomodoro_cfg)
{
    if (g_pomodoro_active) {
        return OPRT_COM_ERROR;
    }
    g_pomodoro_active = TRUE;
    (void)pomodoro_cfg;
    return OPRT_OK;
}
OPERATE_RET wukong_tm_pomodoro_pause(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_pomodoro_resume(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_pomodoro_stop(VOID) { g_pomodoro_active = FALSE; return OPRT_OK; }
OPERATE_RET wukong_tm_pomodoro_query(WUKONG_TM_POMODORO_STATE_T *state)
{
    if (state == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (!g_pomodoro_active) {
        return OPRT_NOT_FOUND;
    }

    memset(state, 0, sizeof(*state));
    state->active = TRUE;
    state->paused = FALSE;
    state->session_id = 7;
    state->phase = WUKONG_TM_POMODORO_PHASE_SHORT_BREAK;
    state->current_cycle = 2;
    state->completed_work_count = 1;
    state->phase_start_ts = 100;
    state->phase_end_ts = 400;
    state->remaining_sec = 300;
    state->cfg.work_duration = 25;
    state->cfg.short_break_duration = 5;
    state->cfg.long_break_duration = 15;
    state->cfg.work_sessions_before_long_break = 4;
    return OPRT_OK;
}

int test_has_tool(const char *name)
{
    int i = 0;
    for (i = 0; i < g_registered_tool_count; i++) {
        if (strcmp(g_registered_tools[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

const char *test_tool_description(const char *name)
{
    int i = 0;
    for (i = 0; i < g_registered_tool_count; i++) {
        if (strcmp(g_registered_tools[i].name, name) == 0) {
            return g_registered_tools[i].description;
        }
    }
    return NULL;
}

const char *test_prop_description(const char *tool_name, const char *prop_name)
{
    int i = 0;
    int j = 0;

    for (i = 0; i < g_registered_tool_count; i++) {
        if (strcmp(g_registered_tools[i].name, tool_name) != 0) {
            continue;
        }

        for (j = 0; j < g_registered_tools[i].prop_count; j++) {
            if (strcmp(g_registered_tools[i].prop_names[j], prop_name) == 0) {
                return g_registered_tools[i].prop_descriptions[j];
            }
        }
    }

    return NULL;
}

MCP_TOOL_HANDLER_CB test_get_tool_handler(const char *name)
{
    int i = 0;

    for (i = 0; i < g_registered_tool_count; i++) {
        if (strcmp(g_registered_tools[i].name, name) == 0) {
            return g_registered_tools[i].handler;
        }
    }

    return NULL;
}

BOOL_T test_prop_required(const char *tool_name, const char *prop_name)
{
    int i = 0;
    int j = 0;

    for (i = 0; i < g_registered_tool_count; i++) {
        if (strcmp(g_registered_tools[i].name, tool_name) != 0) {
            continue;
        }

        for (j = 0; j < g_registered_tools[i].prop_count; j++) {
            if (strcmp(g_registered_tools[i].prop_names[j], prop_name) == 0) {
                return g_registered_tools[i].prop_required[j];
            }
        }
    }

    return FALSE;
}
