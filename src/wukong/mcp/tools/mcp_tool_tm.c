/**
 * @file mcp_tool_tm.c
 * @brief Unified MCP tools for time-management features.
 *
 * This file centralizes the MCP-facing time-management implementations for:
 * - alarm CRUD/query
 * - reminder/schedule CRUD/query
 * - countdown / stopwatch / pomodoro
 *
 * Public registration is exposed through one unified entry:
 * `mcp_tool_tm_init()`.
 */

#include "mcp_tool_tm.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "tal_log.h"
#include "tal_memory.h"
#include "tal_time_service.h"
#include "wukong_ai_mcp.h"
#include "wukong_tm.h"

/* ---------------------------------------------------------------------------
 * Shared format buffer (MCP handlers are dispatched serially on one thread)
 * --------------------------------------------------------------------------- */
#define MCP_TM_FMT_BUF_SIZE 768
STATIC CHAR_T s_fmt_buf[MCP_TM_FMT_BUF_SIZE];

/* ---------------------------------------------------------------------------
 * Alarm helpers
 * --------------------------------------------------------------------------- */
/**
 * @brief Alarm tool operation: add one alarm.
 */
#define MCP_ALARM_OP_ADD     0
/**
 * @brief Alarm tool operation: delete one alarm.
 */
#define MCP_ALARM_OP_DELETE  1
/**
 * @brief Alarm tool operation: update one alarm.
 */
#define MCP_ALARM_OP_UPDATE  2
/**
 * @brief Alarm tool operation: acknowledge the current ringing alarm.
 */
#define MCP_ALARM_OP_ACK     3
/**
 * @brief Schedule/reminder tool operation: add one reminder.
 */
#define MCP_SCHEDULE_OP_ADD     0
/**
 * @brief Schedule/reminder tool operation: delete one reminder.
 */
#define MCP_SCHEDULE_OP_DELETE  1
/**
 * @brief Schedule/reminder tool operation: update one reminder.
 */
#define MCP_SCHEDULE_OP_UPDATE  2

/**
 * @brief Maximum `offset_minutes` for one-time reminder add (365 days).
 */
#define MCP_SCHEDULE_OFFSET_MINUTES_MAX 525600

#define MCP_COUNTDOWN_OP_CREATE  0
#define MCP_COUNTDOWN_OP_PAUSE   1
#define MCP_COUNTDOWN_OP_RESUME  2
#define MCP_COUNTDOWN_OP_DELETE  3
#define MCP_COUNTDOWN_OP_QUERY   4

#define MCP_STOPWATCH_OP_START   0
#define MCP_STOPWATCH_OP_PAUSE   1
#define MCP_STOPWATCH_OP_RESUME  2
#define MCP_STOPWATCH_OP_STOP    3
#define MCP_STOPWATCH_OP_RESET   4
#define MCP_STOPWATCH_OP_QUERY   5

#define MCP_POMODORO_OP_START    0
#define MCP_POMODORO_OP_PAUSE    1
#define MCP_POMODORO_OP_RESUME   2
#define MCP_POMODORO_OP_STOP     3
#define MCP_POMODORO_OP_QUERY    4

/**
 * @brief Read one integer field from a tool argument object.
 *
 * @param[in]  args           Tool argument object.
 * @param[in]  key            Field name.
 * @param[out] value          Output integer value.
 * @param[in]  default_value  Fallback value when the field is absent.
 */
STATIC VOID __read_int_field(CONST ty_cJSON *args, CONST CHAR_T *key, INT_T *value, INT_T default_value)
{
    ty_cJSON *item = NULL;

    if (value == NULL) {
        return;
    }

    *value = default_value;
    if (args == NULL || key == NULL) {
        return;
    }

    item = ty_cJSON_GetObjectItem(args, key);
    if (item != NULL && ty_cJSON_IsNumber(item)) {
        *value = item->valueint;
    }
}

/**
 * @brief Read one string field from a tool argument object.
 *
 * @param[in] args Tool argument object.
 * @param[in] key  Field name.
 * @return String pointer when present, otherwise NULL.
 */
STATIC CONST CHAR_T *__read_string_field(CONST ty_cJSON *args, CONST CHAR_T *key)
{
    ty_cJSON *item = NULL;

    if (args == NULL || key == NULL) {
        return NULL;
    }

    item = ty_cJSON_GetObjectItem(args, key);
    if (item != NULL && ty_cJSON_IsString(item) && item->valuestring != NULL) {
        return item->valuestring;
    }

    return NULL;
}

/**
 * @brief Check whether one numeric argument is explicitly present.
 *
 * @param[in] args Tool argument object.
 * @param[in] key  Field name.
 * @return TRUE when the field exists and is numeric, otherwise FALSE.
 */
STATIC BOOL_T __has_int_field(CONST ty_cJSON *args, CONST CHAR_T *key)
{
    ty_cJSON *item = NULL;

    if (args == NULL || key == NULL) {
        return FALSE;
    }

    item = ty_cJSON_GetObjectItem(args, key);
    return (item != NULL && ty_cJSON_IsNumber(item)) ? TRUE : FALSE;
}

/**
 * @brief Convert one civil date to days since Unix epoch.
 *
 * @param[in] year  Full year (for example 2026).
 * @param[in] month Month in range [1, 12].
 * @param[in] day   Day of month in range [1, 31].
 * @return Day offset relative to 1970-01-01.
 */
STATIC TIME_T __schedule_days_from_civil(INT_T year, INT_T month, INT_T day)
{
    INT_T era = 0;
    UINT_T yoe = 0;
    UINT_T doy = 0;
    UINT_T doe = 0;

    year -= (month <= 2) ? 1 : 0;
    era = (year >= 0) ? (year / 400) : ((year - 399) / 400);
    yoe = (UINT_T)(year - era * 400);
    doy = (UINT_T)((153 * (month + ((month > 2) ? -3 : 9)) + 2) / 5 + day - 1);
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

    return (TIME_T)(era * 146097 + (INT_T)doe - 719468);
}

STATIC BOOL_T __schedule_local_tm_equals(CONST POSIX_TM_S *lhs, CONST POSIX_TM_S *rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return FALSE;
    }

    return (lhs->tm_year == rhs->tm_year &&
            lhs->tm_mon == rhs->tm_mon &&
            lhs->tm_mday == rhs->tm_mday &&
            lhs->tm_hour == rhs->tm_hour &&
            lhs->tm_min == rhs->tm_min &&
            lhs->tm_sec == rhs->tm_sec) ? TRUE : FALSE;
}

STATIC TIME_T __schedule_tm_to_plain_epoch(CONST POSIX_TM_S *tm_info)
{
    TIME_T days = 0;

    if (tm_info == NULL) {
        return 0;
    }

    days = __schedule_days_from_civil(tm_info->tm_year + 1900,
                                      tm_info->tm_mon + 1,
                                      tm_info->tm_mday);
    return days * 24 * 60 * 60 +
           (TIME_T)tm_info->tm_hour * 60 * 60 +
           (TIME_T)tm_info->tm_min * 60 +
           (TIME_T)tm_info->tm_sec;
}

STATIC OPERATE_RET __schedule_local_tm_to_timestamp(CONST POSIX_TM_S *local_tm, TIME_T *start_ts)
{
    POSIX_TM_S input_tm = {0};
    POSIX_TM_S actual_tm = {0};
    TIME_T candidate_ts = 0;
    TIME_T adjust_sec = 0;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(local_tm, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(start_ts, OPRT_INVALID_PARM);

    input_tm = *local_tm;
    candidate_ts = tal_time_mktime(&input_tm);
    rt = tal_time_get_local_time_custom(candidate_ts, &actual_tm);
    if (rt != OPRT_OK) {
        return rt;
    }

    if (!__schedule_local_tm_equals(local_tm, &actual_tm)) {
        adjust_sec = __schedule_tm_to_plain_epoch(local_tm) - __schedule_tm_to_plain_epoch(&actual_tm);
        candidate_ts += adjust_sec;
        rt = tal_time_get_local_time_custom(candidate_ts, &actual_tm);
        if (rt != OPRT_OK) {
            return rt;
        }
        if (!__schedule_local_tm_equals(local_tm, &actual_tm)) {
            return OPRT_INVALID_PARM;
        }
    }

    *start_ts = candidate_ts;
    return OPRT_OK;
}

/**
 * @brief Read one local-time field from args into POSIX_TM_S, tracking presence.
 *
 * @param[in]     args      Tool argument object.
 * @param[in]     prefix    Field name prefix ("" or "new_").
 * @param[in]     field     Bare field name ("year", "month", etc.).
 * @param[in,out] tm_val    Pointer to the target tm member.
 * @param[in]     offset    Offset applied when reading (e.g. 1900 for year, 1 for month, 0 otherwise).
 * @param[in]     has_base  Whether a fallback base value was loaded.
 * @return TRUE when the field was explicitly present in args.
 */
STATIC BOOL_T __schedule_read_tm_field(CONST ty_cJSON *args, CONST CHAR_T *prefix,
                                       CONST CHAR_T *field, INT_T *tm_val,
                                       INT_T offset, BOOL_T has_base)
{
    CHAR_T key[32] = {0};
    INT_T value = 0;
    BOOL_T present = FALSE;

    (VOID)snprintf(key, sizeof(key), "%s%s", prefix, field);
    present = __has_int_field(args, key);
    __read_int_field(args, key, &value, *tm_val + offset);
    if (present || has_base) {
        *tm_val = value - offset;
    }
    return present;
}

/**
 * @brief Parse local time from args (all five fields required).
 *
 * @param[in]  args      Tool argument object.
 * @param[out] start_ts  Converted UTC timestamp.
 * @return OPRT_OK on success, OPRT_INVALID_PARM when required fields are missing.
 */
STATIC OPERATE_RET __schedule_parse_local_time(CONST ty_cJSON *args, TIME_T *start_ts)
{
    POSIX_TM_S local_tm = {0};
    BOOL_T has_all = FALSE;

    TUYA_CHECK_NULL_RETURN(start_ts, OPRT_INVALID_PARM);

    has_all = __schedule_read_tm_field(args, "", "year", &local_tm.tm_year, 1900, FALSE);
    has_all = __schedule_read_tm_field(args, "", "month", &local_tm.tm_mon, 1, FALSE) && has_all;
    has_all = __schedule_read_tm_field(args, "", "day", &local_tm.tm_mday, 0, FALSE) && has_all;
    has_all = __schedule_read_tm_field(args, "", "hour", &local_tm.tm_hour, 0, FALSE) && has_all;
    has_all = __schedule_read_tm_field(args, "", "minute", &local_tm.tm_min, 0, FALSE) && has_all;
    local_tm.tm_sec = 0;

    if (!has_all) {
        return OPRT_INVALID_PARM;
    }
    return __schedule_local_tm_to_timestamp(&local_tm, start_ts);
}

/**
 * @brief Resolve add start time: optional `offset_minutes` from device clock, else local y/m/d/h/m.
 *
 * @param[in]  args      Tool argument object.
 * @param[out] start_ts   Resolved UTC timestamp for the reminder.
 * @return OPRT_OK on success, OPRT_INVALID_PARM when parameters are inconsistent or out of range.
 */
STATIC OPERATE_RET __schedule_resolve_add_start_time(CONST ty_cJSON *args, TIME_T *start_ts)
{
    INT_T offset_minutes = 0;
    TIME_T now = 0;
    TIME_T delta_sec = 0;

    TUYA_CHECK_NULL_RETURN(start_ts, OPRT_INVALID_PARM);

    if (__has_int_field(args, "offset_minutes")) {
        __read_int_field(args, "offset_minutes", &offset_minutes, 0);
        if (offset_minutes < 1 || offset_minutes > MCP_SCHEDULE_OFFSET_MINUTES_MAX) {
            return OPRT_INVALID_PARM;
        }
        now = tal_time_get_posix();
        delta_sec = (TIME_T)offset_minutes * 60;
        /* `offset_minutes` is capped; do not use `(TIME_T)-1 - delta` (broken for signed TIME_T). */
        *start_ts = now + delta_sec;
        return OPRT_OK;
    }

    return __schedule_parse_local_time(args, start_ts);
}


/**
 * @brief Wrap one text string as MCP tool content.
 *
 * @param[in]  text         Text content to report.
 * @param[out] out_content  Output content array.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __make_text_result(CONST CHAR_T *text, ty_cJSON **out_content)
{
    TUYA_CHECK_NULL_RETURN(out_content, OPRT_INVALID_PARM);

    *out_content = ty_cJSON_CreateArray();
    if (*out_content == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    ty_cJSON_AddItemToArray(*out_content, mcp_content_make_text(text));
    return OPRT_OK;
}

/**
 * @brief Wrap one boolean result as MCP tool content.
 *
 * @param[in]  success      Whether the operation succeeded.
 * @param[out] out_content  Output content array.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __make_bool_result(BOOL_T success, ty_cJSON **out_content)
{
    return __make_text_result(success ? "true" : "false", out_content);
}

/**
 * @brief Wrap a schedule/reminder result as MCP tool content.
 *
 * @param[in]  success      Whether the operation succeeded.
 * @param[in]  start_time   Resolved start timestamp (included when > 0).
 * @param[in]  reminder_id  Reminder identifier (included when non-NULL).
 * @param[in]  reason       Failure reason string (included when non-NULL on failure).
 * @param[out] out_content  Output content array.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __make_schedule_result(BOOL_T success, TIME_T start_time,
                                          CONST CHAR_T *reminder_id,
                                          CONST CHAR_T *reason,
                                          ty_cJSON **out_content)
{
    CHAR_T text[256] = {0};
    INT_T pos = 0;

    pos = snprintf(text, sizeof(text), "{\"success\":%s", success ? "true" : "false");
    if (reason != NULL && reason[0] != '\0') {
        pos += snprintf(text + pos, sizeof(text) - pos, ",\"reason\":\"%s\"", reason);
    }
    if (start_time > 0) {
        pos += snprintf(text + pos, sizeof(text) - pos,
                        ",\"start_timestamp\":%lld", (long long)start_time);
    }
    if (reminder_id != NULL && reminder_id[0] != '\0') {
        pos += snprintf(text + pos, sizeof(text) - pos,
                        ",\"reminder_id\":\"%s\"", reminder_id);
    }
    (VOID)snprintf(text + pos, sizeof(text) - pos, "}");

    return __make_text_result(text, out_content);
}

/**
 * @brief Wrap one pomodoro runtime snapshot as MCP tool content.
 *
 * @param[in]  success      Whether query succeeded.
 * @param[in]  state        Queried pomodoro state when success is TRUE.
 * @param[out] out_content  Output content array.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __make_pomodoro_query_result(BOOL_T success,
                                                CONST WUKONG_TM_POMODORO_STATE_T *state,
                                                ty_cJSON **out_content)
{
    TIME_T total_phase_sec = 0;
    TIME_T elapsed_sec = 0;

    if (!success || state == NULL) {
        (VOID)snprintf(s_fmt_buf, MCP_TM_FMT_BUF_SIZE, "{\"success\":false}");
        return __make_text_result(s_fmt_buf, out_content);
    }

    if (state->phase == WUKONG_TM_POMODORO_PHASE_SHORT_BREAK) {
        total_phase_sec = (TIME_T)state->cfg.short_break_duration * 60;
    } else if (state->phase == WUKONG_TM_POMODORO_PHASE_LONG_BREAK) {
        total_phase_sec = (TIME_T)state->cfg.long_break_duration * 60;
    } else {
        total_phase_sec = (TIME_T)state->cfg.work_duration * 60;
    }
    elapsed_sec = total_phase_sec - state->remaining_sec;
    if (elapsed_sec < 0) {
        elapsed_sec = 0;
    }

    (VOID)snprintf(s_fmt_buf, MCP_TM_FMT_BUF_SIZE,
                   "{\"success\":true,\"active\":%s,\"paused\":%s,"
                   "\"session_id\":%u,\"phase\":%d,\"current_cycle\":%u,"
                   "\"completed_work_count\":%u,\"phase_start_ts\":%lld,"
                   "\"phase_end_ts\":%lld,\"remaining_sec\":%lld,"
                   "\"elapsed_sec\":%lld,"
                   "\"work_duration\":%d,\"short_break_duration\":%d,"
                   "\"long_break_duration\":%d,"
                   "\"work_sessions_before_long_break\":%d}",
                   state->active ? "true" : "false",
                   state->paused ? "true" : "false",
                   state->session_id,
                   state->phase,
                   state->current_cycle,
                   state->completed_work_count,
                   (long long)state->phase_start_ts,
                   (long long)state->phase_end_ts,
                   (long long)state->remaining_sec,
                   (long long)elapsed_sec,
                   state->cfg.work_duration,
                   state->cfg.short_break_duration,
                   state->cfg.long_break_duration,
                   state->cfg.work_sessions_before_long_break);
    return __make_text_result(s_fmt_buf, out_content);
}

/**
 * @brief Wrap one stopwatch runtime snapshot as MCP tool content.
 *
 * @param[in]  success      Whether query succeeded.
 * @param[in]  state        Queried stopwatch state when success is TRUE.
 * @param[out] out_content  Output content array.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __make_stopwatch_query_result(BOOL_T success,
                                                 CONST WUKONG_TM_STOPWATCH_STATE_T *state,
                                                 ty_cJSON **out_content)
{
    if (!success || state == NULL) {
        (VOID)snprintf(s_fmt_buf, MCP_TM_FMT_BUF_SIZE, "{\"success\":false}");
        return __make_text_result(s_fmt_buf, out_content);
    }

    (VOID)snprintf(s_fmt_buf, MCP_TM_FMT_BUF_SIZE,
                   "{\"success\":true,\"active\":%s,\"paused\":%s,\"elapsed_sec\":%lld}",
                   state->active ? "true" : "false",
                   state->paused ? "true" : "false",
                   (long long)state->elapsed_sec);
    return __make_text_result(s_fmt_buf, out_content);
}

/**
 * @brief Wrap stopwatch success with cumulative elapsed seconds (seconds since start).
 *
 * @param[in]  elapsed_sec  Elapsed seconds to report.
 * @param[out] out_content  Output content array.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __make_stopwatch_elapsed_result(TIME_T elapsed_sec, ty_cJSON **out_content)
{
    (VOID)snprintf(s_fmt_buf, MCP_TM_FMT_BUF_SIZE,
                   "{\"success\":true,\"elapsed_sec\":%lld}", (long long)elapsed_sec);
    return __make_text_result(s_fmt_buf, out_content);
}

/**
 * @brief Wrap one countdown runtime snapshot as MCP tool content.
 *
 * @param[in]  success      Whether query succeeded.
 * @param[in]  snap         Queried countdown snapshot when success is TRUE.
 * @param[out] out_content  Output content array.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __make_countdown_query_result(BOOL_T success,
                                                 CONST WUKONG_TM_COUNTDOWN_SNAPSHOT_T *snap,
                                                 ty_cJSON **out_content)
{
    if (!success || snap == NULL) {
        (VOID)snprintf(s_fmt_buf, MCP_TM_FMT_BUF_SIZE, "{\"success\":false}");
        return __make_text_result(s_fmt_buf, out_content);
    }

    (VOID)snprintf(s_fmt_buf, MCP_TM_FMT_BUF_SIZE,
                   "{\"success\":true,\"active\":%s,\"state\":%d,"
                   "\"remaining_sec\":%lld,\"duration_sec\":%lld,\"elapsed_sec\":%lld}",
                   snap->active ? "true" : "false",
                   (INT_T)snap->state,
                   (long long)snap->remaining_sec,
                   (long long)snap->duration_sec,
                   (long long)snap->elapsed_sec);
    return __make_text_result(s_fmt_buf, out_content);
}

/**
 * @brief Wrap countdown pause/delete success with remaining and elapsed seconds.
 *
 * @param[in]  remaining_sec  Remaining seconds at pause or before delete.
 * @param[in]  elapsed_sec    Elapsed seconds (duration - remaining).
 * @param[out] out_content    Output content array.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __make_countdown_times_result(TIME_T remaining_sec, TIME_T elapsed_sec,
                                                 ty_cJSON **out_content)
{
    (VOID)snprintf(s_fmt_buf, MCP_TM_FMT_BUF_SIZE,
                   "{\"success\":true,\"remaining_sec\":%lld,\"elapsed_sec\":%lld}",
                   (long long)remaining_sec, (long long)elapsed_sec);
    return __make_text_result(s_fmt_buf, out_content);
}

/**
 * @brief Wrap one duplicate-create response with the current instance snapshot.
 *
 * @param[in]  current_json  JSON fragment of the current singleton snapshot.
 * @param[out] out_content   Output content array.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __make_already_exists_result(CONST CHAR_T *current_json,
                                                ty_cJSON **out_content)
{
    TUYA_CHECK_NULL_RETURN(current_json, OPRT_INVALID_PARM);
    (VOID)snprintf(s_fmt_buf, MCP_TM_FMT_BUF_SIZE,
                   "{\"success\":false,\"reason\":\"already_exists\",\"current\":%s}",
                   current_json);
    return __make_text_result(s_fmt_buf, out_content);
}

/**
 * @brief Build one alarm configuration from MCP tool arguments.
 *
 * When @p fallback is non-NULL (update path), only explicitly present fields
 * override the fallback values; missing fields keep their existing values.
 *
 * @param[in]  args        Tool argument object.
 * @param[in]  fallback    Fallback config used for update operations (NULL for add).
 * @param[out] alarm_cfg   Parsed alarm configuration.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __alarm_cfg_from_args(CONST ty_cJSON *args,
                                         CONST WUKONG_TM_ALARM_CFG_T *fallback,
                                         WUKONG_TM_ALARM_CFG_T *alarm_cfg)
{
    INT_T value = 0;
    CONST CHAR_T *message = NULL;
    BOOL_T keep_missing = FALSE;

    TUYA_CHECK_NULL_RETURN(alarm_cfg, OPRT_INVALID_PARM);

    if (fallback != NULL) {
        *alarm_cfg = *fallback;
    } else {
        memset(alarm_cfg, 0, sizeof(*alarm_cfg));
        alarm_cfg->enabled = TRUE;
        alarm_cfg->repeat_type = WUKONG_TM_ALARM_REPEAT_ONCE;
    }

    keep_missing = (fallback != NULL) ? TRUE : FALSE;

    if (!keep_missing || __has_int_field(args, "enabled")) {
        __read_int_field(args, "enabled", &value, alarm_cfg->enabled ? 1 : 0);
        alarm_cfg->enabled = (value != 0) ? TRUE : FALSE;
    }
    if (!keep_missing || __has_int_field(args, "repeat_type")) {
        __read_int_field(args, "repeat_type", &value, (INT_T)alarm_cfg->repeat_type);
        alarm_cfg->repeat_type = (WUKONG_TM_ALARM_REPEAT_TYPE_E)value;
    }
    if (!keep_missing || __has_int_field(args, "hour")) {
        __read_int_field(args, "hour", &value, (INT_T)alarm_cfg->hour);
        alarm_cfg->hour = (UINT_T)value;
    }
    if (!keep_missing || __has_int_field(args, "minute")) {
        __read_int_field(args, "minute", &value, (INT_T)alarm_cfg->minute);
        alarm_cfg->minute = (UINT_T)value;
    }
    if (!keep_missing || __has_int_field(args, "weekday_mask")) {
        __read_int_field(args, "weekday_mask", &value, (INT_T)alarm_cfg->weekday_mask);
        alarm_cfg->weekday_mask = (UINT_T)value;
    }
    if (!keep_missing || __has_int_field(args, "month_day")) {
        __read_int_field(args, "month_day", &value, (INT_T)alarm_cfg->month_day);
        alarm_cfg->month_day = (UINT_T)value;
    }

    message = __read_string_field(args, "message");
    if (message != NULL) {
        strncpy(alarm_cfg->message, message, sizeof(alarm_cfg->message) - 1);
        alarm_cfg->message[sizeof(alarm_cfg->message) - 1] = '\0';
    }

    if (alarm_cfg->weekday_mask != 0 && alarm_cfg->repeat_type != WUKONG_TM_ALARM_REPEAT_WEEKLY) {
        alarm_cfg->repeat_type = WUKONG_TM_ALARM_REPEAT_WEEKLY;
    }
    if (alarm_cfg->month_day != 0 && alarm_cfg->repeat_type != WUKONG_TM_ALARM_REPEAT_MONTHLY) {
        alarm_cfg->repeat_type = WUKONG_TM_ALARM_REPEAT_MONTHLY;
    }

    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Alarm handlers
 * --------------------------------------------------------------------------- */
/**
 * @brief Wrapper used by the MCP server for `device_alarm_set`.
 */
STATIC OPERATE_RET __set_alarm(CONST CHAR_T *name, CONST ty_cJSON *args,
                               ty_cJSON **out_content, BOOL_T *is_error,
                               VOID *user_data)
{
    (VOID)name;
    (VOID)user_data;
    return mcp_tool_alarm_set_exec(args, out_content, is_error);
}

/**
 * @brief Wrapper used by the MCP server for `device_alarm_query`.
 */
STATIC OPERATE_RET __query_alarm(CONST CHAR_T *name, CONST ty_cJSON *args,
                                 ty_cJSON **out_content, BOOL_T *is_error,
                                 VOID *user_data)
{
    (VOID)name;
    (VOID)user_data;
    return mcp_tool_alarm_query_exec(args, out_content, is_error);
}

/* ---------------------------------------------------------------------------
 * Relative timer handlers
 * --------------------------------------------------------------------------- */
/**
 * @brief Wrapper used by the MCP server for `device_countdown_timer_set`.
 */
STATIC OPERATE_RET __set_countdown_timer(CONST CHAR_T *name, CONST ty_cJSON *args,
                                         ty_cJSON **out_content, BOOL_T *is_error,
                                         VOID *user_data)
{
    INT_T operation = 0;
    INT_T hours = 0;
    INT_T minutes = 0;
    INT_T seconds = 0;
    WUKONG_TM_COUNTDOWN_SNAPSHOT_T snapshot = {0};
    CHAR_T current_json[384] = {0};
    TIME_T saved_remaining = 0;
    TIME_T saved_elapsed = 0;
    OPERATE_RET rt = OPRT_OK;

    (VOID)name;
    (VOID)user_data;
    (VOID)is_error;

    TAL_PR_DEBUG("__set_countdown_timer enter");
    __read_int_field(args, "operation", &operation, 0);
    __read_int_field(args, "hour_duration", &hours, 0);
    __read_int_field(args, "minute_duration", &minutes, 0);
    __read_int_field(args, "second_duration", &seconds, 0);

    TAL_PR_DEBUG("__set_countdown_timer exit");
    switch (operation) {
    case MCP_COUNTDOWN_OP_CREATE:
        rt = wukong_tm_countdown_create(hours, minutes, seconds);
        if (rt != OPRT_OK) {
            if (wukong_tm_countdown_query(&snapshot) == OPRT_OK) {
                (VOID)snprintf(current_json, sizeof(current_json),
                               "{\"active\":%s,\"state\":%d,\"remaining_sec\":%lld,"
                               "\"duration_sec\":%lld,\"elapsed_sec\":%lld}",
                               snapshot.active ? "true" : "false",
                               snapshot.state,
                               (long long)snapshot.remaining_sec,
                               (long long)snapshot.duration_sec,
                               (long long)snapshot.elapsed_sec);
                return __make_already_exists_result(current_json, out_content);
            }
            return __make_bool_result(FALSE, out_content);
        }
        return __make_bool_result(TRUE, out_content);
    case MCP_COUNTDOWN_OP_PAUSE:
        if (wukong_tm_countdown_pause() != OPRT_OK) {
            return __make_bool_result(FALSE, out_content);
        }
        if (wukong_tm_countdown_query(&snapshot) != OPRT_OK) {
            return __make_bool_result(FALSE, out_content);
        }
        return __make_countdown_times_result(snapshot.remaining_sec, snapshot.elapsed_sec, out_content);
    case MCP_COUNTDOWN_OP_RESUME:
        return __make_bool_result(wukong_tm_countdown_resume() == OPRT_OK, out_content);
    case MCP_COUNTDOWN_OP_DELETE:
        if (wukong_tm_countdown_query(&snapshot) != OPRT_OK) {
            return __make_bool_result(FALSE, out_content);
        }
        saved_remaining = snapshot.remaining_sec;
        saved_elapsed = snapshot.elapsed_sec;
        if (wukong_tm_countdown_delete() != OPRT_OK) {
            return __make_bool_result(FALSE, out_content);
        }
        return __make_countdown_times_result(saved_remaining, saved_elapsed, out_content);
    case MCP_COUNTDOWN_OP_QUERY:
        rt = wukong_tm_countdown_query(&snapshot);
        if (rt == OPRT_NOT_FOUND) {
            return __make_text_result("{\"success\":true,\"active\":false}", out_content);
        }
        return __make_countdown_query_result(rt == OPRT_OK, &snapshot, out_content);
    default:
        return __make_bool_result(FALSE, out_content);
    }
}

/**
 * @brief Wrapper used by the MCP server for `device_stopwatch_timer_set`.
 */
STATIC OPERATE_RET __set_stopwatch_timer(CONST CHAR_T *name, CONST ty_cJSON *args,
                                         ty_cJSON **out_content, BOOL_T *is_error,
                                         VOID *user_data)
{
    INT_T operation = 0;
    WUKONG_TM_STOPWATCH_STATE_T state = {0};
    TIME_T saved_elapsed = 0;
    CHAR_T current_json[128] = {0};
    OPERATE_RET rt = OPRT_OK;

    (VOID)name;
    (VOID)user_data;
    (VOID)is_error;

    TAL_PR_DEBUG("__set_stopwatch_timer enter");
    __read_int_field(args, "operation", &operation, 0);

    TAL_PR_DEBUG("__set_stopwatch_timer exit");
    switch (operation) {
    case MCP_STOPWATCH_OP_START:
        rt = wukong_tm_stopwatch_start();
        if (rt != OPRT_OK) {
            if (wukong_tm_stopwatch_query(&state) == OPRT_OK) {
                (VOID)snprintf(current_json, sizeof(current_json),
                               "{\"active\":%s,\"paused\":%s,\"elapsed_sec\":%lld}",
                               state.active ? "true" : "false",
                               state.paused ? "true" : "false",
                               (long long)state.elapsed_sec);
                return __make_already_exists_result(current_json, out_content);
            }
            return __make_bool_result(FALSE, out_content);
        }
        return __make_bool_result(TRUE, out_content);
    case MCP_STOPWATCH_OP_PAUSE:
        if (wukong_tm_stopwatch_pause() != OPRT_OK) {
            return __make_bool_result(FALSE, out_content);
        }
        if (wukong_tm_stopwatch_query(&state) != OPRT_OK) {
            return __make_bool_result(FALSE, out_content);
        }
        return __make_stopwatch_elapsed_result(state.elapsed_sec, out_content);
    case MCP_STOPWATCH_OP_RESUME:
        return __make_bool_result(wukong_tm_stopwatch_resume() == OPRT_OK, out_content);
    case MCP_STOPWATCH_OP_STOP:
        if (wukong_tm_stopwatch_query(&state) != OPRT_OK) {
            return __make_bool_result(FALSE, out_content);
        }
        saved_elapsed = state.elapsed_sec;
        if (wukong_tm_stopwatch_stop() != OPRT_OK) {
            return __make_bool_result(FALSE, out_content);
        }
        return __make_stopwatch_elapsed_result(saved_elapsed, out_content);
    case MCP_STOPWATCH_OP_RESET:
        if (wukong_tm_stopwatch_query(&state) != OPRT_OK) {
            return __make_bool_result(FALSE, out_content);
        }
        saved_elapsed = state.elapsed_sec;
        if (wukong_tm_stopwatch_reset() != OPRT_OK) {
            return __make_bool_result(FALSE, out_content);
        }
        return __make_stopwatch_elapsed_result(saved_elapsed, out_content);
    case MCP_STOPWATCH_OP_QUERY:
        rt = wukong_tm_stopwatch_query(&state);
        if (rt == OPRT_NOT_FOUND) {
            return __make_text_result("{\"success\":true,\"active\":false}", out_content);
        }
        return __make_stopwatch_query_result(rt == OPRT_OK, &state, out_content);
    default:
        return __make_bool_result(FALSE, out_content);
    }
}

/**
 * @brief Wrapper used by the MCP server for `device_pomodoro_timer_set`.
 */
STATIC OPERATE_RET __set_pomodoro_timer(CONST CHAR_T *name, CONST ty_cJSON *args,
                                        ty_cJSON **out_content, BOOL_T *is_error,
                                        VOID *user_data)
{
    INT_T work_duration = 25;
    INT_T short_break = 5;
    INT_T long_break = 15;
    INT_T work_sessions_before_long_break = 4;
    INT_T operation = 0;
    WUKONG_TM_POMODORO_CFG_T pomodoro = {0};
    WUKONG_TM_POMODORO_STATE_T state = {0};
    CHAR_T current_json[512] = {0};
    OPERATE_RET rt = OPRT_OK;

    (VOID)name;
    (VOID)user_data;
    (VOID)is_error;

    TAL_PR_DEBUG("__set_pomodoro_timer enter");
    __read_int_field(args, "operation", &operation, 0);
    __read_int_field(args, "work_duration", &work_duration, 25);
    __read_int_field(args, "short_break_duration", &short_break, 5);
    __read_int_field(args, "long_break_duration", &long_break, 15);
    __read_int_field(args, "work_sessions_before_long_break", &work_sessions_before_long_break, 4);

    pomodoro.work_duration = work_duration;
    pomodoro.short_break_duration = short_break;
    pomodoro.long_break_duration = long_break;
    pomodoro.work_sessions_before_long_break = work_sessions_before_long_break;

    TAL_PR_DEBUG("__set_pomodoro_timer exit");
    switch (operation) {
    case MCP_POMODORO_OP_START:
        rt = wukong_tm_pomodoro_start(&pomodoro);
        if (rt != OPRT_OK) {
            if (wukong_tm_pomodoro_query(&state) == OPRT_OK) {
                (VOID)snprintf(current_json, sizeof(current_json),
                               "{\"active\":%s,\"paused\":%s,"
                               "\"session_id\":%u,\"phase\":%d,\"current_cycle\":%u,"
                               "\"completed_work_count\":%u,\"phase_start_ts\":%lld,"
                               "\"phase_end_ts\":%lld,\"remaining_sec\":%lld,"
                               "\"work_duration\":%d,\"short_break_duration\":%d,"
                               "\"long_break_duration\":%d,"
                               "\"work_sessions_before_long_break\":%d}",
                               state.active ? "true" : "false",
                               state.paused ? "true" : "false",
                               state.session_id,
                               state.phase,
                               state.current_cycle,
                               state.completed_work_count,
                               (long long)state.phase_start_ts,
                               (long long)state.phase_end_ts,
                               (long long)state.remaining_sec,
                               state.cfg.work_duration,
                               state.cfg.short_break_duration,
                               state.cfg.long_break_duration,
                               state.cfg.work_sessions_before_long_break);
                return __make_already_exists_result(current_json, out_content);
            }
            return __make_bool_result(FALSE, out_content);
        }
        return __make_bool_result(TRUE, out_content);
    case MCP_POMODORO_OP_PAUSE:
        return __make_bool_result(wukong_tm_pomodoro_pause() == OPRT_OK, out_content);
    case MCP_POMODORO_OP_RESUME:
        return __make_bool_result(wukong_tm_pomodoro_resume() == OPRT_OK, out_content);
    case MCP_POMODORO_OP_STOP:
        return __make_bool_result(wukong_tm_pomodoro_stop() == OPRT_OK, out_content);
    case MCP_POMODORO_OP_QUERY:
        rt = wukong_tm_pomodoro_query(&state);
        if (rt == OPRT_NOT_FOUND) {
            return __make_text_result("{\"success\":true,\"active\":false}", out_content);
        }
        return __make_pomodoro_query_result(rt == OPRT_OK, &state, out_content);
    default:
        return __make_bool_result(FALSE, out_content);
    }
}

/* ---------------------------------------------------------------------------
 * Schedule handlers
 * --------------------------------------------------------------------------- */
/**
 * @brief Wrapper used by the MCP server for `device_schedule_set`.
 */
STATIC OPERATE_RET __set_schedule(CONST CHAR_T *name, CONST ty_cJSON *args,
                                  ty_cJSON **out_content, BOOL_T *is_error,
                                  VOID *user_data)
{
    INT_T operation = MCP_SCHEDULE_OP_ADD;
    CONST CHAR_T *id = NULL;
    CONST CHAR_T *message = NULL;
    WUKONG_TM_REMINDER_CFG_T cfg = {0};
    WUKONG_TM_REMINDER_CFG_T existing = {0};
    POSIX_TM_S local_tm = {0};
    TIME_T start_ts = 0;
    BOOL_T has_any_time = FALSE;
    OPERATE_RET rt = OPRT_OK;

    (VOID)name;
    (VOID)user_data;
    (VOID)is_error;

    __read_int_field(args, "operation", &operation, MCP_SCHEDULE_OP_ADD);
    id = __read_string_field(args, "id");
    if (id == NULL || id[0] == '\0') {
        return __make_text_result("{\"success\":false,\"reason\":\"missing_id\"}", out_content);
    }

    if (operation == MCP_SCHEDULE_OP_ADD) {
        rt = __schedule_resolve_add_start_time(args, &start_ts);
        message = __read_string_field(args, "message");
        if (rt != OPRT_OK) {
            if (__has_int_field(args, "offset_minutes")) {
                return __make_schedule_result(FALSE, 0, NULL,
                                              "invalid_offset", out_content);
            }
            return __make_schedule_result(FALSE, start_ts, NULL,
                                          "invalid_params", out_content);
        }
        if (message == NULL || message[0] == '\0') {
            return __make_schedule_result(FALSE, start_ts, NULL,
                                          "invalid_params", out_content);
        }
        if (start_ts <= tal_time_get_posix()) {
            return __make_schedule_result(FALSE, start_ts, NULL,
                                          "time_in_the_past", out_content);
        }
        cfg.enabled = TRUE;
        cfg.start_time = start_ts;
        strncpy(cfg.message, message, sizeof(cfg.message) - 1);
        rt = wukong_tm_reminder_add(&cfg, id);
    } else if (operation == MCP_SCHEDULE_OP_DELETE) {
        rt = wukong_tm_reminder_remove(id);
    } else if (operation == MCP_SCHEDULE_OP_UPDATE) {
        rt = wukong_tm_reminder_get(id, &existing);
        if (rt != OPRT_OK) {
            return __make_text_result("{\"success\":false,\"reason\":\"not_found\"}", out_content);
        }

        rt = tal_time_get_local_time_custom(existing.start_time, &local_tm);
        if (rt != OPRT_OK) {
            return __make_schedule_result(FALSE, 0, NULL, NULL, out_content);
        }
        local_tm.tm_sec = 0;

        has_any_time  = __schedule_read_tm_field(args, "", "year", &local_tm.tm_year, 1900, TRUE);
        has_any_time |= __schedule_read_tm_field(args, "", "month", &local_tm.tm_mon, 1, TRUE);
        has_any_time |= __schedule_read_tm_field(args, "", "day", &local_tm.tm_mday, 0, TRUE);
        has_any_time |= __schedule_read_tm_field(args, "", "hour", &local_tm.tm_hour, 0, TRUE);
        has_any_time |= __schedule_read_tm_field(args, "", "minute", &local_tm.tm_min, 0, TRUE);

        if (has_any_time) {
            rt = __schedule_local_tm_to_timestamp(&local_tm, &start_ts);
            if (rt != OPRT_OK) {
                return __make_schedule_result(FALSE, 0, NULL,
                                              "invalid_time", out_content);
            }
            existing.start_time = start_ts;
        } else {
            start_ts = existing.start_time;
        }

        message = __read_string_field(args, "message");
        if (message != NULL && message[0] != '\0') {
            strncpy(existing.message, message, sizeof(existing.message) - 1);
            existing.message[sizeof(existing.message) - 1] = '\0';
        }

        existing.enabled = TRUE;
        rt = wukong_tm_reminder_update(id, &existing);
        if (rt == OPRT_OK) {
            start_ts = existing.start_time;
        }
    } else {
        rt = OPRT_INVALID_PARM;
    }

    TAL_PR_DEBUG("schedule tool set -> operation=%d rt=%d", operation, rt);
    return __make_schedule_result(rt == OPRT_OK, start_ts,
                                  (operation == MCP_SCHEDULE_OP_ADD || operation == MCP_SCHEDULE_OP_UPDATE) ? id : NULL,
                                  NULL, out_content);
}

/**
 * @brief Wrapper used by the MCP server for `device_schedule_query_set`.
 */
STATIC OPERATE_RET __set_schedule_query(CONST CHAR_T *name, CONST ty_cJSON *args,
                                        ty_cJSON **out_content, BOOL_T *is_error,
                                        VOID *user_data)
{
    INT_T start_timestamp = 0;
    INT_T end_timestamp = 0;
    CONST CHAR_T *keyword = NULL;
    ty_cJSON *node = NULL;
    CHAR_T *ret_content = NULL;
    TIME_T start_ts = 0;
    TIME_T end_ts = 0;

    (VOID)name;
    (VOID)user_data;
    (VOID)is_error;

    TAL_PR_DEBUG("__set_schedule_query enter");

    if (args != NULL) {
        __read_int_field(args, "start_timestamp", &start_timestamp, 0);
        __read_int_field(args, "end_timestamp", &end_timestamp, 0);
        node = ty_cJSON_GetObjectItem(args, "keyword");
        if (node != NULL && ty_cJSON_IsString(node)) {
            keyword = node->valuestring;
        }
    }

    start_ts = (TIME_T)start_timestamp;
    end_ts = (TIME_T)end_timestamp;
    ret_content = wukong_tm_reminder_query_text(start_ts, end_ts, keyword);
    *out_content = ty_cJSON_CreateArray();
    if (*out_content == NULL) {
        if (ret_content != NULL) {
            tal_free(ret_content);
        }
        return OPRT_MALLOC_FAILED;
    }

    if (ret_content != NULL) {
        ty_cJSON_AddItemToArray(*out_content, mcp_content_make_text(ret_content));
        tal_free(ret_content);
    } else {
        ty_cJSON_AddItemToArray(*out_content, mcp_content_make_text("false"));
    }

    TAL_PR_DEBUG("__set_schedule_query exit");
    return OPRT_OK;
}

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */
/**
 * @brief Execute the alarm set tool logic directly.
 *
 * @param[in]  args         Tool argument object.
 * @param[out] out_content  Tool result content array.
 * @param[out] is_error     Whether execution should be surfaced as an MCP error.
 * @return OPRT_OK on success.
 */
OPERATE_RET mcp_tool_alarm_set_exec(CONST ty_cJSON *args, ty_cJSON **out_content, BOOL_T *is_error)
{
    INT_T operation = MCP_ALARM_OP_ADD;
    CONST CHAR_T *alarm_id = NULL;
    WUKONG_TM_ALARM_CFG_T alarm_cfg = {0};
    WUKONG_TM_ALARM_CFG_T existing_cfg = {0};
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(out_content, OPRT_INVALID_PARM);
    if (is_error != NULL) {
        *is_error = FALSE;
    }

    __read_int_field(args, "operation", &operation, MCP_ALARM_OP_ADD);
    alarm_id = __read_string_field(args, "id");
    if (alarm_id == NULL || alarm_id[0] == '\0') {
        return __make_text_result("{\"success\":false,\"reason\":\"missing_id\"}", out_content);
    }

    if (operation == MCP_ALARM_OP_ADD) {
        CHAR_T existing_id[WUKONG_TM_ALARM_ID_LEN + 1] = {0};
        WUKONG_TM_ALARM_CFG_T probe_cfg = {0};

        rt = __alarm_cfg_from_args(args, NULL, &alarm_cfg);
        if (rt != OPRT_OK) {
            return __make_text_result("{\"success\":false,\"reason\":\"invalid_params\"}", out_content);
        }
        if (alarm_cfg.repeat_type == WUKONG_TM_ALARM_REPEAT_ONCE) {
            TIME_T alarm_ts = 0;
            rt = __schedule_parse_local_time(args, &alarm_ts);
            if (rt != OPRT_OK) {
                return __make_text_result("{\"success\":false,\"reason\":\"invalid_time\"}", out_content);
            }
            alarm_cfg.start_time = alarm_ts;
        }
        /* Pre-check id collision so the storage-full path below can be
         * reported with a distinct, accurate reason. */
        if (wukong_tm_alarm_get(alarm_id, &probe_cfg) == OPRT_OK) {
            CHAR_T id_buf[128] = {0};
            (VOID)snprintf(id_buf, sizeof(id_buf),
                           "{\"success\":false,\"reason\":\"id_exists\",\"existing_id\":\"%s\"}",
                           alarm_id);
            return __make_text_result(id_buf, out_content);
        }
        if (wukong_tm_alarm_find_by_time(&alarm_cfg, existing_id, sizeof(existing_id)) == OPRT_OK) {
            CHAR_T dup_buf[128] = {0};
            (VOID)snprintf(dup_buf, sizeof(dup_buf),
                           "{\"success\":false,\"reason\":\"already_exists\",\"existing_id\":\"%s\"}",
                           existing_id);
            return __make_text_result(dup_buf, out_content);
        }
        rt = wukong_tm_alarm_add(&alarm_cfg, alarm_id);
    } else if (operation == MCP_ALARM_OP_DELETE) {
        rt = wukong_tm_alarm_remove(alarm_id);
    } else if (operation == MCP_ALARM_OP_UPDATE) {
        rt = wukong_tm_alarm_get(alarm_id, &existing_cfg);
        if (rt != OPRT_OK) {
            return __make_text_result("{\"success\":false,\"reason\":\"not_found\"}", out_content);
        }
        rt = __alarm_cfg_from_args(args, &existing_cfg, &alarm_cfg);
        if (rt != OPRT_OK) {
            return __make_text_result("false", out_content);
        }
        if (alarm_cfg.repeat_type == WUKONG_TM_ALARM_REPEAT_ONCE) {
            POSIX_TM_S local_tm = {0};
            TIME_T base_ts = 0;
            TIME_T new_ts = 0;
            BOOL_T has_any_time = FALSE;

            base_ts = (existing_cfg.start_time > 0)
                      ? existing_cfg.start_time : tal_time_get_posix();
            rt = tal_time_get_local_time_custom(base_ts, &local_tm);
            if (rt != OPRT_OK) {
                return __make_text_result("false", out_content);
            }
            local_tm.tm_sec = 0;

            has_any_time  = __schedule_read_tm_field(args, "", "year",
                                &local_tm.tm_year, 1900, TRUE);
            has_any_time |= __schedule_read_tm_field(args, "", "month",
                                &local_tm.tm_mon, 1, TRUE);
            has_any_time |= __schedule_read_tm_field(args, "", "day",
                                &local_tm.tm_mday, 0, TRUE);
            has_any_time |= __schedule_read_tm_field(args, "", "hour",
                                &local_tm.tm_hour, 0, TRUE);
            has_any_time |= __schedule_read_tm_field(args, "", "minute",
                                &local_tm.tm_min, 0, TRUE);

            if (has_any_time) {
                rt = __schedule_local_tm_to_timestamp(&local_tm, &new_ts);
                if (rt != OPRT_OK) {
                    return __make_text_result("{\"success\":false,\"reason\":\"invalid_time\"}", out_content);
                }
                alarm_cfg.start_time = new_ts;
                alarm_cfg.hour = (UINT_T)local_tm.tm_hour;
                alarm_cfg.minute = (UINT_T)local_tm.tm_min;
                if (!__has_int_field(args, "enabled")) {
                    alarm_cfg.enabled = TRUE;
                }
            }

        }
        rt = wukong_tm_alarm_update(alarm_id, &alarm_cfg);
    } else if (operation == MCP_ALARM_OP_ACK) {
        rt = wukong_tm_alarm_ack(alarm_id);
    } else {
        rt = OPRT_INVALID_PARM;
    }

    TAL_PR_DEBUG("alarm tool set -> operation=%d rt=%d", operation, rt);
    if (rt == OPRT_OK) {
        return __make_text_result("{\"success\":true}", out_content);
    }
    {
        /* Map the underlying OPRT_* into a stable, model-readable reason.
         * For ADD the id-collision case has been pre-checked above, so
         * remaining OPRT_COM_ERROR almost always means storage_full
         * (slot allocation failed) or a rare cron-binding failure; both
         * are surfaced as `storage_full` so the agent stops blaming
         * "time conflict" when the real cause is capacity. */
        CONST CHAR_T *reason = "internal_error";
        CHAR_T buf[96] = {0};

        if (rt == OPRT_INVALID_PARM) {
            reason = "invalid_params";
        } else if (rt == OPRT_NOT_FOUND) {
            reason = "not_found";
        } else if (rt == OPRT_COM_ERROR) {
            reason = (operation == MCP_ALARM_OP_ADD) ? "storage_full"
                                                    : "operation_failed";
        }
        (VOID)snprintf(buf, sizeof(buf),
                       "{\"success\":false,\"reason\":\"%s\"}", reason);
        return __make_text_result(buf, out_content);
    }
}

/**
 * @brief Execute the alarm query tool logic directly.
 *
 * @param[in]  args         Tool argument object.
 * @param[out] out_content  Tool result content array.
 * @param[out] is_error     Whether execution should be surfaced as an MCP error.
 * @return OPRT_OK on success.
 */
OPERATE_RET mcp_tool_alarm_query_exec(CONST ty_cJSON *args, ty_cJSON **out_content, BOOL_T *is_error)
{
    CHAR_T *alarm_list_json = NULL;
    OPERATE_RET rt = OPRT_OK;

    (VOID)args;
    TUYA_CHECK_NULL_RETURN(out_content, OPRT_INVALID_PARM);
    if (is_error != NULL) {
        *is_error = FALSE;
    }

    rt = wukong_tm_alarm_list(&alarm_list_json);
    if (rt != OPRT_OK || alarm_list_json == NULL) {
        return __make_text_result("false", out_content);
    }

    rt = __make_text_result(alarm_list_json, out_content);
    ty_cJSON_FreeBuffer(alarm_list_json);
    return rt;
}

/**
 * @brief Register alarm MCP tools with the local MCP server.
 *
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __register_alarm_tools(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    rt = MCP_TOOL_ADD(
        "device_alarm_set",
        "Manage local alarms (one-time or recurring) that fire an audible RINGTONE. "
        "`message` is a stored note only and is NOT spoken; for spoken reminders use `device_schedule_set`.\n"
        "`repeat_type`: 0=once (default; needs year+month+day+hour+minute; if today's "
        "time already passed it rolls to tomorrow), "
        "1=daily, 2=weekly (`weekday_mask` bit0=Sun..bit6=Sat; workdays=62, weekend=65), "
        "3=monthly (`month_day`). Set >0 ONLY on explicit recurrence ('every day/morning/weekday/month').\n"
        "Result is JSON: success=true => {\"success\":true}; failure => "
        "{\"success\":false,\"reason\":<code>[,\"existing_id\":<id>]}. "
        "`reason` codes: `already_exists` (same time/repeat already scheduled; "
        "`existing_id` returned), `id_exists` (the requested `id` is already used; "
        "pick a new id or use operation=2 to update), `storage_full` (alarm slots "
        "exhausted; ask the user to delete an unwanted alarm first — DO NOT claim "
        "the time is taken), `invalid_time`, "
        "`invalid_params`, `not_found`, `operation_failed`. "
        "Update merges only provided fields.",
        __set_alarm, NULL,
        MCP_SCHEMA_INT_RANGE("operation", "Operation (0=add, 1=delete, 2=update, 3=ack ringing alarm)", 0, 3),
        MCP_SCHEMA_STR("id", "Alarm identifier (unique on add; required for all operations)"),
        MCP_SCHEMA_INT_OPT_RANGE("repeat_type", "Repeat type 0=once|1=daily|2=weekly|3=monthly", 0, 3),
        MCP_SCHEMA_INT_OPT_RANGE("year", "Local year", 1970, 2099),
        MCP_SCHEMA_INT_OPT_RANGE("month", "Local month", 1, 12),
        MCP_SCHEMA_INT_OPT_RANGE("day", "Local day of month", 1, 31),
        MCP_SCHEMA_INT_OPT_RANGE("hour", "Local hour", 0, 23),
        MCP_SCHEMA_INT_OPT_RANGE("minute", "Local minute", 0, 59),
        MCP_SCHEMA_INT_OPT_RANGE("weekday_mask", "Weekly bitmask bit0=Sun..bit6=Sat (workdays=62, weekend=65)", 0, 127),
        MCP_SCHEMA_INT_OPT_RANGE("month_day", "Day of month for repeat_type=3", 1, 31),
        MCP_SCHEMA_STR_OPT("message", "Text note for query/display; NOT spoken when the alarm fires"),
        MCP_SCHEMA_INT_OPT_RANGE("enabled", "Enable=1, disable=0 (kept in list but won't fire)", 0, 1)
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    return MCP_TOOL_ADD(
        "device_alarm_query",
        "Query LOCAL ALARMS only (alarm clock, ringtone-based; one-time and recurring). "
        "Every returned entry IS an alarm — the item type is fixed by this tool, "
        "regardless of what the per-item `message` field contains. Free-text "
        "`message` strings such as 'remind me to drink water', 'remind me ...' or anything "
        "containing 'remind/schedule' MUST NOT be reinterpreted as a "
        "reminder/schedule item; they are still alarms. "
        "This tool NEVER returns spoken reminders/schedules; use "
        "`device_schedule_query` for those. "
        "Reply rules: when the user explicitly asks about 'alarm', call "
        "ONLY this tool and describe the results using the word 'alarm'; do NOT "
        "use 'reminder' or 'schedule' in the spoken reply, even if a `message` contains "
        "those words. Only call BOTH this tool and `device_schedule_query` when "
        "the user uses an ambiguous term that covers both (e.g. 'what to-dos do I have').",
        __query_alarm, NULL
    );
}

/**
 * @brief Register relative timer MCP tools with the local MCP server.
 *
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __register_counter_tools(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    rt = MCP_TOOL_ADD(
        "device_countdown_timer_set",
        "Single-instance countdown timer; not modifiable in place. "
        "Duplicate create returns {success:false,reason:already_exists,current:...}. "
        "Pause/delete return {success:true,remaining_sec,elapsed_sec}; "
        "query returns active/state/remaining_sec/duration_sec/elapsed_sec. "
        "All duration fields are integers; convert fractional minutes to seconds (e.g. 0.5min -> minute_duration=0, second_duration=30).",
        __set_countdown_timer, NULL,
        MCP_SCHEMA_INT_OPT_RANGE("hour_duration", "Hours (create only)", 0, 24),
        MCP_SCHEMA_INT_OPT_RANGE("minute_duration", "Whole minutes (create only)", 0, 60),
        MCP_SCHEMA_INT_OPT_RANGE("second_duration", "Seconds (create only)", 0, 60),
        MCP_SCHEMA_INT_RANGE("operation", "Operation (0=create, 1=pause, 2=resume, 3=delete, 4=query)", 0, 4)
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = MCP_TOOL_ADD(
        "device_stopwatch_timer_set",
        "Single-instance count-up stopwatch from 0. "
        "Duplicate start returns {success:false,reason:already_exists,current:...}. "
        "Pause/stop/reset return {success:true,elapsed_sec}; query returns active/paused/elapsed_sec.",
        __set_stopwatch_timer, NULL,
        MCP_SCHEMA_INT_RANGE("operation",
            "Operation (0=start, 1=pause, 2=resume, 3=stop, 4=reset, 5=query)", 0, 5)
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    return MCP_TOOL_ADD(
        "device_pomodoro_timer_set",
        "Pomodoro focus timer (use for 'pomodoro/focus session/concentrate for X minutes'). "
        "Single instance; stop before recreating with new durations. "
        "Duplicate start returns {success:false,reason:already_exists,current:...}. "
        "`remaining_sec`/`elapsed_sec` are seconds — convert to minutes when speaking to user.",
        __set_pomodoro_timer, NULL,
        MCP_SCHEMA_INT_OPT_RANGE("work_duration", "Work minutes (default 25)", 1, 120),
        MCP_SCHEMA_INT_OPT_RANGE("short_break_duration", "Short break minutes (default 5)", 1, 30),
        MCP_SCHEMA_INT_OPT_RANGE("long_break_duration", "Long break minutes (default 15)", 5, 60),
        MCP_SCHEMA_INT_OPT_RANGE("work_sessions_before_long_break",
            "Work sessions before a long break (default 4)", WUKONG_TM_POMODORO_WORK_BEFORE_LONG_MIN,
            WUKONG_TM_POMODORO_WORK_BEFORE_LONG_MAX),
        MCP_SCHEMA_INT_RANGE("operation", "Operation (0=start, 1=pause, 2=resume, 3=stop, 4=query)", 0, 4)
    );
}

/**
 * @brief Register reminder/schedule MCP tools with the local MCP server.
 *
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __register_schedule_tools(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    rt = MCP_TOOL_ADD(
        "device_schedule_set",
        "Manage ONE-TIME spoken reminders (device SPEAKS `message` via TTS at fire time).\n"
        "Use for 'remind me at <time> to ...'. For ringtone-only alarms use `device_alarm_set`.\n"
        "Recurring spoken reminders not supported; create the next occurrence and tell user it fires once.\n"
        "Add requires `message` and EITHER `offset_minutes` OR full local year+month+day+hour+minute. "
        "Prefer `offset_minutes` for relative time ('in N minutes/hours'); device clock is authoritative. "
        "If both given, `offset_minutes` wins. Resolved time MUST be in the future. "
        "If the user specifies a date but no time of day, you MUST ask ONLY for the time before calling this tool; do NOT guess or assume a default time (e.g. do NOT default to 9:00); do NOT ask about id, categories, or other fields.\n"
        "Update merges only provided fields. "
        "Returns `success`, `start_timestamp`, `reminder_id`; "
        "failure `reason`: `time_in_the_past` | `invalid_offset` | `invalid_params`.",
        __set_schedule, NULL,
        MCP_SCHEMA_INT_RANGE("operation", "Operation (0=add, 1=delete, 2=update)", 0, 2),
        MCP_SCHEMA_STR("id", "Reminder identifier; auto-generate if user does not provide one (do not ask the user)"),
        MCP_SCHEMA_INT_OPT_RANGE("categories", "Reminder category (0=meeting, 1=work, 2=personal, 3=health, 4=learning, 5=social, 6=other); infer from context, do not ask the user", 0, 6),
        MCP_SCHEMA_INT_OPT_RANGE("offset_minutes",
            "Minutes from device now until the reminder; preferred for relative time on add",
            1, MCP_SCHEDULE_OFFSET_MINUTES_MAX),
        MCP_SCHEMA_INT_OPT_RANGE("year", "Reminder local year", 1970, 2099),
        MCP_SCHEMA_INT_OPT_RANGE("month", "Reminder local month", 1, 12),
        MCP_SCHEMA_INT_OPT_RANGE("day", "Reminder local day of month", 1, 31),
        MCP_SCHEMA_INT_OPT_RANGE("hour", "Reminder local hour", 0, 23),
        MCP_SCHEMA_INT_OPT_RANGE("minute", "Reminder local minute", 0, 59),
        MCP_SCHEMA_STR_OPT("message", "Reminder message text (required on add)")
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    return MCP_TOOL_ADD(
        "device_schedule_query",
        "Query one-time spoken reminders (TTS-spoken `message`). "
        "Does NOT return ringtone alarms; use `device_alarm_query` for those, "
        "and call BOTH when user asks 'what reminders do I have' without specifying type. "
        "`start_timestamp`/`end_timestamp` are inclusive UTC Unix-second bounds.",
        __set_schedule_query, NULL,
        MCP_SCHEMA_INT_OPT_RANGE("query_method", "Query method (0=time range, 1=category, 2=keyword)", 0, 2),
        MCP_SCHEMA_INT_OPT_RANGE("start_timestamp", "Inclusive lower bound, UTC Unix seconds", 0, 2147483647),
        MCP_SCHEMA_INT_OPT_RANGE("end_timestamp", "Inclusive upper bound, UTC Unix seconds", 0, 2147483647),
        MCP_SCHEMA_INT_OPT_RANGE("categories", "Category filter 0-6", 0, 6),
        MCP_SCHEMA_STR_OPT("keyword", "Keyword matched against reminder message")
    );
}

/**
 * @brief Register all time-management MCP tools with the local MCP server.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET mcp_tool_tm_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    rt = __register_alarm_tools();
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = __register_counter_tools();
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = __register_schedule_tools();
    if (rt != OPRT_OK) {
        return rt;
    }

    return OPRT_OK;
}
