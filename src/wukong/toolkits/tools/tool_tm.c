/**
 * @file tool_tm.c
 * @brief Unified MCP tools for time-management features.
 *
 * This file centralizes the MCP-facing time-management implementations for:
 * - alarm CRUD/query
 * - reminder/schedule CRUD/query
 * - countdown / stopwatch / pomodoro
 *
 * Public registration is exposed through one unified entry:
 * `tool_tm_init()`.
 */

#include "tool_tm.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "tal_log.h"
#include "tal_memory.h"
#include "tal_time_service.h"
#include "wukong_tool.h"
#include "wukong_tm.h"
#include "wukong_iso8601.h"

/* ---------------------------------------------------------------------------
 * Result formatting
 * --------------------------------------------------------------------------- */
/* Upper bound for one formatted tool result. The buffer is allocated per call
 * in __make_fmt_result() and freed before it returns — the text is copied into
 * the cJSON node, so holding it in BSS would waste this much for the whole
 * uptime. */
#define MCP_TM_FMT_BUF_SIZE 768

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
 * @brief Alarm tool operation: delete all alarms.
 */
#define MCP_ALARM_OP_DELETE_ALL  4
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
 * @brief Schedule/reminder tool operation: delete all reminders.
 */
#define MCP_SCHEDULE_OP_DELETE_ALL  3

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

typedef struct {
    CONST CHAR_T *action;
    INT_T operation;
} MCP_TM_ACTION_MAP_T;

STATIC CONST MCP_TM_ACTION_MAP_T s_alarm_actions[] = {
    {"add", MCP_ALARM_OP_ADD},
    {"delete", MCP_ALARM_OP_DELETE},
    {"update", MCP_ALARM_OP_UPDATE},
    {"ack", MCP_ALARM_OP_ACK},
    {"delete_all", MCP_ALARM_OP_DELETE_ALL},
    {NULL, 0}
};

STATIC CONST MCP_TM_ACTION_MAP_T s_countdown_actions[] = {
    {"create", MCP_COUNTDOWN_OP_CREATE},
    {"pause", MCP_COUNTDOWN_OP_PAUSE},
    {"resume", MCP_COUNTDOWN_OP_RESUME},
    {"delete", MCP_COUNTDOWN_OP_DELETE},
    {"query", MCP_COUNTDOWN_OP_QUERY},
    {NULL, 0}
};

STATIC CONST MCP_TM_ACTION_MAP_T s_stopwatch_actions[] = {
    {"start", MCP_STOPWATCH_OP_START},
    {"pause", MCP_STOPWATCH_OP_PAUSE},
    {"resume", MCP_STOPWATCH_OP_RESUME},
    {"stop", MCP_STOPWATCH_OP_STOP},
    {"reset", MCP_STOPWATCH_OP_RESET},
    {"query", MCP_STOPWATCH_OP_QUERY},
    {NULL, 0}
};

STATIC CONST MCP_TM_ACTION_MAP_T s_pomodoro_actions[] = {
    {"start", MCP_POMODORO_OP_START},
    {"pause", MCP_POMODORO_OP_PAUSE},
    {"resume", MCP_POMODORO_OP_RESUME},
    {"stop", MCP_POMODORO_OP_STOP},
    {"query", MCP_POMODORO_OP_QUERY},
    {NULL, 0}
};

STATIC OPERATE_RET __make_text_result(CONST CHAR_T *text, ty_cJSON **out_content);

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
 * @brief Resolve string `action` argument to one internal operation value.
 *
 * @param[in]  args       Tool argument object.
 * @param[in]  map        Null-terminated action map.
 * @param[out] operation  Resolved internal operation.
 * @return OPRT_OK on success, OPRT_NOT_FOUND when action is absent, OPRT_INVALID_PARM for invalid action.
 */
STATIC OPERATE_RET __resolve_action(CONST ty_cJSON *args, CONST MCP_TM_ACTION_MAP_T *map, INT_T *operation)
{
    CONST CHAR_T *action = NULL;
    INT_T i = 0;

    TUYA_CHECK_NULL_RETURN(map, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(operation, OPRT_INVALID_PARM);

    action = __read_string_field(args, "action");
    if (action == NULL || action[0] == '\0') {
        return OPRT_NOT_FOUND;
    }

    for (i = 0; map[i].action != NULL; i++) {
        if (strcmp(action, map[i].action) == 0) {
            *operation = map[i].operation;
            return OPRT_OK;
        }
    }

    return OPRT_INVALID_PARM;
}

/**
 * @brief Make a stable action-parse failure result for MCP tools.
 *
 * @param[in]  rt           Resolve result from __resolve_action().
 * @param[out] out_content  Output content array.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __make_action_error(OPERATE_RET rt, ty_cJSON **out_content)
{
    if (rt == OPRT_NOT_FOUND) {
        return __make_text_result("{\"success\":false,\"reason\":\"The 'action' field is required.\"}", out_content);
    }

    return __make_text_result("{\"success\":false,\"reason\":\"The specified 'action' value is not valid.\"}", out_content);
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
 * @brief Check whether one string argument is explicitly present.
 *
 * @param[in] args Tool argument object.
 * @param[in] key  Field name.
 * @return TRUE when the field exists and is a non-empty string, otherwise FALSE.
 */
STATIC BOOL_T __has_string_field(CONST ty_cJSON *args, CONST CHAR_T *key)
{
    ty_cJSON *item = NULL;

    if (args == NULL || key == NULL) {
        return FALSE;
    }

    item = ty_cJSON_GetObjectItem(args, key);
    return (item != NULL && ty_cJSON_IsString(item) &&
            item->valuestring != NULL && item->valuestring[0] != '\0') ? TRUE : FALSE;
}

/**
 * @brief Read one boolean field from a tool argument object.
 *
 * @param[in]  args           Tool argument object.
 * @param[in]  key            Field name.
 * @param[out] value          Output boolean value.
 * @param[in]  default_value  Fallback value when the field is absent.
 */
STATIC VOID __read_bool_field(CONST ty_cJSON *args, CONST CHAR_T *key,
                              BOOL_T *value, BOOL_T default_value)
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
    if (item != NULL && ty_cJSON_IsBool(item)) {
        *value = ty_cJSON_IsTrue(item) ? TRUE : FALSE;
    }
}

/**
 * @brief Parse alarm repeat_type string enum.
 *
 * @param[in]  value        Repeat type string.
 * @param[out] repeat_type  Parsed repeat type.
 * @return OPRT_OK on success, OPRT_INVALID_PARM when value is invalid.
 */
STATIC OPERATE_RET __parse_repeat_type_string(CONST CHAR_T *value,
                                              WUKONG_TM_REPEAT_TYPE_E *repeat_type)
{
    TUYA_CHECK_NULL_RETURN(value, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(repeat_type, OPRT_INVALID_PARM);

    if (strcmp(value, "once") == 0) {
        *repeat_type = WUKONG_TM_REPEAT_ONCE;
        return OPRT_OK;
    }
    if (strcmp(value, "daily") == 0) {
        *repeat_type = WUKONG_TM_REPEAT_DAILY;
        return OPRT_OK;
    }
    if (strcmp(value, "weekly") == 0) {
        *repeat_type = WUKONG_TM_REPEAT_WEEKLY;
        return OPRT_OK;
    }
    if (strcmp(value, "monthly") == 0) {
        *repeat_type = WUKONG_TM_REPEAT_MONTHLY;
        return OPRT_OK;
    }

    return OPRT_INVALID_PARM;
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

    ty_cJSON_AddItemToArray(*out_content, wukong_tool_make_text(text));
    return OPRT_OK;
}

/**
 * @brief Format a JSON payload and wrap it as MCP tool content.
 *
 * The scratch buffer is allocated per call and released before returning: the
 * text is copied into the cJSON node by __make_text_result(), so nothing
 * outlives this function. Keeping it in BSS would reserve MCP_TM_FMT_BUF_SIZE
 * for the whole uptime just to serve these short-lived formatting steps.
 *
 * @param[out] out_content  Output content array.
 * @param[in]  fmt          printf-style format string.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __make_fmt_result(ty_cJSON **out_content, CONST CHAR_T *fmt, ...)
{
    CHAR_T *buf = NULL;
    OPERATE_RET rt = OPRT_OK;
    va_list ap;

    TUYA_CHECK_NULL_RETURN(fmt, OPRT_INVALID_PARM);

    buf = tal_malloc(MCP_TM_FMT_BUF_SIZE);
    if (buf == NULL) {
        TAL_PR_ERR("mcp_tm: format buffer alloc %d bytes failed", (INT_T)MCP_TM_FMT_BUF_SIZE);
        return OPRT_MALLOC_FAILED;
    }

    va_start(ap, fmt);
    (VOID)vsnprintf(buf, MCP_TM_FMT_BUF_SIZE, fmt, ap);
    va_end(ap);

    rt = __make_text_result(buf, out_content);
    tal_free(buf);
    return rt;
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
    CHAR_T text[320] = {0};
    INT_T pos = 0;

    pos = snprintf(text, sizeof(text), "{\"success\":%s", success ? "true" : "false");
    if (reason != NULL && reason[0] != '\0') {
        pos += snprintf(text + pos, sizeof(text) - pos, ",\"reason\":\"%s\"", reason);
    }
    if (start_time > 0) {
        CHAR_T start_iso[32] = {0};
        wukong_iso8601_format(start_time, start_iso, sizeof(start_iso));
        pos += snprintf(text + pos, sizeof(text) - pos,
                        ",\"start_time\":\"%s\"", start_iso);
    }
    if (reminder_id != NULL && reminder_id[0] != '\0') {
        pos += snprintf(text + pos, sizeof(text) - pos,
                        ",\"reminder_id\":\"%s\"", reminder_id);
    }
    (VOID)snprintf(text + pos, sizeof(text) - pos, "}");

    return __make_text_result(text, out_content);
}

/**
 * @brief Convert a pomodoro phase enum to a human-readable string for MCP output.
 *
 * @param[in] phase Pomodoro phase enum value.
 * @return Static string such as "work", "short_break", etc.
 */
STATIC CONST CHAR_T *__pomodoro_phase_str(WUKONG_TM_POMODORO_PHASE_E phase)
{
    switch (phase) {
    case WUKONG_TM_POMODORO_PHASE_WORK:        return "work";
    case WUKONG_TM_POMODORO_PHASE_SHORT_BREAK: return "short_break";
    case WUKONG_TM_POMODORO_PHASE_LONG_BREAK:  return "long_break";
    default:                                   return "unknown";
    }
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
    CHAR_T start_iso[32] = {0};
    CHAR_T end_iso[32] = {0};

    if (!success || state == NULL) {
        return __make_fmt_result(out_content, "{\"success\":false}");
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

    if (state->phase_start_ts > 0) {
        (VOID)wukong_iso8601_format(state->phase_start_ts, start_iso, sizeof(start_iso));
    }
    if (state->phase_end_ts > 0) {
        (VOID)wukong_iso8601_format(state->phase_end_ts, end_iso, sizeof(end_iso));
    }

    return __make_fmt_result(out_content,
                             "{\"success\":true,\"active\":%s,\"paused\":%s,"
                             "\"session_id\":%u,\"phase\":\"%s\",\"current_cycle\":%u,"
                             "\"completed_work_count\":%u,\"phase_start_time\":\"%s\","
                             "\"phase_end_time\":\"%s\",\"remaining_sec\":%lld,"
                             "\"elapsed_sec\":%lld,"
                             "\"work_duration\":%d,\"short_break_duration\":%d,"
                             "\"long_break_duration\":%d,"
                             "\"work_sessions_before_long_break\":%d}",
                             state->active ? "true" : "false",
                             state->paused ? "true" : "false",
                             state->session_id,
                             __pomodoro_phase_str(state->phase),
                             state->current_cycle,
                             state->completed_work_count,
                             start_iso,
                             end_iso,
                             (long long)state->remaining_sec,
                             (long long)elapsed_sec,
                             state->cfg.work_duration,
                             state->cfg.short_break_duration,
                             state->cfg.long_break_duration,
                             state->cfg.work_sessions_before_long_break);
}

/**
 * @brief Wrap one pomodoro control (pause/resume/stop) result as MCP tool content.
 *
 * On failure returns {"success":false}.  On success queries the current
 * pomodoro state so the caller (the LLM) receives remaining_sec and phase
 * without a separate query call.  When the instance is already gone (stop),
 * falls back to {"success":true,"action":"...","active":false}.
 *
 * @param[in]  success      Whether the control operation succeeded.
 * @param[in]  action       Action label: "pause", "resume", or "stop".
 * @param[out] out_content  Output content array.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __make_pomodoro_control_result(BOOL_T success,
                                                  CONST CHAR_T *action,
                                                  ty_cJSON **out_content)
{
    WUKONG_TM_POMODORO_STATE_T state = {0};
    CHAR_T start_iso[32] = {0};
    CHAR_T end_iso[32] = {0};

    if (!success) {
        return __make_text_result("{\"success\":false}", out_content);
    }

    /* Instance may be gone after stop — report inactive in that case. */
    if (wukong_tm_pomodoro_query(&state) != OPRT_OK) {
        return __make_fmt_result(out_content,
                                 "{\"success\":true,\"action\":\"%s\",\"active\":false}", action);
    }

    if (state.phase_start_ts > 0) {
        (VOID)wukong_iso8601_format(state.phase_start_ts, start_iso, sizeof(start_iso));
    }
    if (state.phase_end_ts > 0) {
        (VOID)wukong_iso8601_format(state.phase_end_ts, end_iso, sizeof(end_iso));
    }

    return __make_fmt_result(out_content,
                             "{\"success\":true,\"action\":\"%s\","
                             "\"active\":%s,\"paused\":%s,"
                             "\"phase\":\"%s\",\"remaining_sec\":%lld,"
                             "\"phase_start_time\":\"%s\",\"phase_end_time\":\"%s\"}",
                             action,
                             state.active ? "true" : "false",
                             state.paused ? "true" : "false",
                             __pomodoro_phase_str(state.phase),
                             (long long)state.remaining_sec,
                             start_iso, end_iso);
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
        return __make_fmt_result(out_content, "{\"success\":false}");
    }

    return __make_fmt_result(out_content,
                             "{\"success\":true,\"active\":%s,\"paused\":%s,\"elapsed_sec\":%lld}",
                             state->active ? "true" : "false",
                             state->paused ? "true" : "false",
                             (long long)state->elapsed_sec);
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
    return __make_fmt_result(out_content,
                             "{\"success\":true,\"elapsed_sec\":%lld}", (long long)elapsed_sec);
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
        return __make_fmt_result(out_content, "{\"success\":false}");
    }

    return __make_fmt_result(out_content,
                             "{\"success\":true,\"active\":%s,\"state\":%d,"
                             "\"remaining_sec\":%lld,\"duration_sec\":%lld,\"elapsed_sec\":%lld}",
                             snap->active ? "true" : "false",
                             (INT_T)snap->state,
                             (long long)snap->remaining_sec,
                             (long long)snap->duration_sec,
                             (long long)snap->elapsed_sec);
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
    return __make_fmt_result(out_content,
                             "{\"success\":true,\"remaining_sec\":%lld,\"elapsed_sec\":%lld}",
                             (long long)remaining_sec, (long long)elapsed_sec);
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
    return __make_fmt_result(out_content,
                             "{\"success\":false,\"reason\":\"An identical item already exists.\",\"current\":%s}",
                             current_json);
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
    CONST CHAR_T *repeat_type_str = NULL;
    ty_cJSON *enabled_item = NULL;
    BOOL_T keep_missing = FALSE;
    BOOL_T repeat_explicit = FALSE;
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(alarm_cfg, OPRT_INVALID_PARM);

    if (fallback != NULL) {
        *alarm_cfg = *fallback;
    } else {
        memset(alarm_cfg, 0, sizeof(*alarm_cfg));
        alarm_cfg->enabled = TRUE;
        alarm_cfg->repeat_type = WUKONG_TM_REPEAT_ONCE;
    }

    keep_missing = (fallback != NULL) ? TRUE : FALSE;

    enabled_item = ty_cJSON_GetObjectItem(args, "enabled");
    if (!keep_missing || enabled_item != NULL) {
        if (enabled_item != NULL) {
            if (!ty_cJSON_IsBool(enabled_item)) {
                return OPRT_INVALID_PARM;
            }
            __read_bool_field(args, "enabled", &alarm_cfg->enabled, alarm_cfg->enabled);
        }
    }
    repeat_explicit = __has_string_field(args, "repeat_type");
    if (!keep_missing || repeat_explicit) {
        if (__has_string_field(args, "repeat_type")) {
            repeat_type_str = __read_string_field(args, "repeat_type");
            rt = __parse_repeat_type_string(repeat_type_str, &alarm_cfg->repeat_type);
            if (rt != OPRT_OK) {
                return OPRT_INVALID_PARM;
            }
        }
    }

    /* Parse start_time (ISO 8601) instead of year/month/day/hour/minute */
    {
        CONST CHAR_T *start_time_str = __read_string_field(args, "start_time");
        if (start_time_str != NULL && start_time_str[0] != '\0') {
            WUKONG_ISO8601_RESULT_T iso_result = {0};
            rt = wukong_iso8601_parse(start_time_str, FALSE, &iso_result);
            if (rt != OPRT_OK) {
                return OPRT_INVALID_PARM;
            }
            alarm_cfg->hour = (UINT_T)iso_result.tm.tm_hour;
            alarm_cfg->minute = (UINT_T)iso_result.tm.tm_min;
            /* For ONCE alarms, also compute and store the full timestamp */
            if (alarm_cfg->repeat_type == WUKONG_TM_REPEAT_ONCE) {
                TIME_T start_ts = 0;
                rt = wukong_iso8601_parse_to_timestamp(start_time_str, FALSE, &start_ts);
                if (rt != OPRT_OK) {
                    return OPRT_INVALID_PARM;
                }
                alarm_cfg->start_time = start_ts;
            }
        } else if (fallback != NULL) {
            /* Update path: keep existing values when start_time not provided */
            /* alarm_cfg already has fallback values copied at the top */
        }
        /* If no start_time and no fallback, the alarm still needs hour/minute
         * but we don't reject here — validation happens later */
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

    /* Only infer the repeat type from incidental date fields when the caller did
     * not explicitly provide repeat_type. Otherwise an over-specified one-time
     * alarm (e.g. repeat_type="once" plus a stray month_day) would be silently
     * coerced into a recurring alarm. The explicit repeat_type field always wins. */
    if (!repeat_explicit) {
        if (alarm_cfg->weekday_mask != 0 && alarm_cfg->repeat_type != WUKONG_TM_REPEAT_WEEKLY) {
            alarm_cfg->repeat_type = WUKONG_TM_REPEAT_WEEKLY;
        }
        if (alarm_cfg->month_day != 0 && alarm_cfg->repeat_type != WUKONG_TM_REPEAT_MONTHLY) {
            alarm_cfg->repeat_type = WUKONG_TM_REPEAT_MONTHLY;
        }
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
                               ty_cJSON **out_content,
                               VOID *user_data)
{
    ty_cJSON *patched = NULL;
    OPERATE_RET rt = OPRT_OK;
    CONST CHAR_T *alarm_id = NULL;
    BOOL_T is_update = FALSE;

    (VOID)name;
    (VOID)user_data;

    patched = (args != NULL) ? ty_cJSON_Duplicate((ty_cJSON *)args, TRUE) : ty_cJSON_CreateObject();
    if (patched == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    alarm_id = __read_string_field(patched, "id");
    if (alarm_id != NULL && alarm_id[0] != '\0') {
        WUKONG_TM_ALARM_CFG_T probe_cfg = {0};
        if (wukong_tm_alarm_get(alarm_id, &probe_cfg) == OPRT_OK) {
            is_update = TRUE;
        }
    }

    ty_cJSON_DeleteItemFromObject(patched, "action");
    ty_cJSON_AddStringToObject(patched, "action", is_update ? "update" : "add");

    rt = tool_alarm_set_exec(patched, out_content);
    ty_cJSON_Delete(patched);
    return rt;
}

/**
 * @brief Wrapper used by the MCP server for `device_alarm_query`.
 */
STATIC OPERATE_RET __query_alarm(CONST CHAR_T *name, CONST ty_cJSON *args,
                                 ty_cJSON **out_content,
                                 VOID *user_data)
{
    (VOID)name;
    (VOID)user_data;
    return tool_alarm_query_exec(args, out_content);
}

/**
 * @brief Wrapper used by the MCP server for `device_alarm_ack`.
 *
 * Injects action="ack" so the shared tool_alarm_set_exec() can be reused.
 */
STATIC OPERATE_RET __ack_alarm(CONST CHAR_T *name, CONST ty_cJSON *args,
                               ty_cJSON **out_content,
                               VOID *user_data)
{
    ty_cJSON *patched = NULL;
    OPERATE_RET rt = OPRT_OK;

    (VOID)name;
    (VOID)user_data;

    /* Duplicate args and inject action field */
    patched = (args != NULL) ? ty_cJSON_Duplicate((ty_cJSON *)args, TRUE) : ty_cJSON_CreateObject();
    if (patched == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_DeleteItemFromObject(patched, "action");
    ty_cJSON_AddStringToObject(patched, "action", "ack");
    rt = tool_alarm_set_exec(patched, out_content);
    ty_cJSON_Delete(patched);
    return rt;
}

/**
 * @brief Wrapper used by the MCP server for `device_alarm_delete`.
 */
STATIC OPERATE_RET __delete_alarm(CONST CHAR_T *name, CONST ty_cJSON *args,
                                  ty_cJSON **out_content,
                                  VOID *user_data)
{
    ty_cJSON *patched = NULL;
    OPERATE_RET rt = OPRT_OK;
    CONST CHAR_T *alarm_id = NULL;

    (VOID)name;
    (VOID)user_data;

    alarm_id = __read_string_field(args, "id");
    if (alarm_id == NULL || alarm_id[0] == '\0') {
        return __make_text_result("{\"success\":false,\"reason\":\"The 'id' field is required.\"}", out_content);
    }

    patched = (args != NULL) ? ty_cJSON_Duplicate((ty_cJSON *)args, TRUE) : ty_cJSON_CreateObject();
    if (patched == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    ty_cJSON_DeleteItemFromObject(patched, "action");
    ty_cJSON_AddStringToObject(patched, "action", "delete");
    rt = tool_alarm_set_exec(patched, out_content);
    ty_cJSON_Delete(patched);
    return rt;
}

/**
 * @brief Wrapper used by the MCP server for `device_alarm_delete_all`.
 */
STATIC OPERATE_RET __delete_alarm_all(CONST CHAR_T *name, CONST ty_cJSON *args,
                                      ty_cJSON **out_content,
                                      VOID *user_data)
{
    ty_cJSON *patched = NULL;
    OPERATE_RET rt = OPRT_OK;

    (VOID)name;
    (VOID)user_data;

    (VOID)args;

    patched = ty_cJSON_CreateObject();
    if (patched == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    ty_cJSON_AddStringToObject(patched, "action", "delete_all");
    rt = tool_alarm_set_exec(patched, out_content);
    ty_cJSON_Delete(patched);
    return rt;
}

/* ---------------------------------------------------------------------------
 * Relative timer handlers
 * --------------------------------------------------------------------------- */
/**
 * @brief Wrapper used by the MCP server for `device_countdown_timer_set`.
 */
STATIC OPERATE_RET __set_countdown_timer(CONST CHAR_T *name, CONST ty_cJSON *args,
                                         ty_cJSON **out_content,
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

    TAL_PR_DEBUG("__set_countdown_timer enter");
    rt = __resolve_action(args, s_countdown_actions, &operation);
    if (rt != OPRT_OK) {
        return __make_action_error(rt, out_content);
    }
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
                                         ty_cJSON **out_content,
                                         VOID *user_data)
{
    INT_T operation = 0;
    WUKONG_TM_STOPWATCH_STATE_T state = {0};
    TIME_T saved_elapsed = 0;
    CHAR_T current_json[128] = {0};
    OPERATE_RET rt = OPRT_OK;

    (VOID)name;
    (VOID)user_data;

    TAL_PR_DEBUG("__set_stopwatch_timer enter");
    rt = __resolve_action(args, s_stopwatch_actions, &operation);
    if (rt != OPRT_OK) {
        return __make_action_error(rt, out_content);
    }

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
 * @brief Wrapper used by the MCP server for `device_pomodoro_timer`.
 */
STATIC OPERATE_RET __set_pomodoro_timer(CONST CHAR_T *name, CONST ty_cJSON *args,
                                        ty_cJSON **out_content,
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

    TAL_PR_DEBUG("__set_pomodoro_timer enter");
    rt = __resolve_action(args, s_pomodoro_actions, &operation);
    if (rt != OPRT_OK) {
        return __make_action_error(rt, out_content);
    }
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
                CHAR_T cur_start_iso[32] = {0};
                CHAR_T cur_end_iso[32] = {0};
                if (state.phase_start_ts > 0) {
                    (VOID)wukong_iso8601_format(state.phase_start_ts, cur_start_iso, sizeof(cur_start_iso));
                }
                if (state.phase_end_ts > 0) {
                    (VOID)wukong_iso8601_format(state.phase_end_ts, cur_end_iso, sizeof(cur_end_iso));
                }
                (VOID)snprintf(current_json, sizeof(current_json),
                               "{\"active\":%s,\"paused\":%s,"
                               "\"session_id\":%u,\"phase\":\"%s\",\"current_cycle\":%u,"
                               "\"completed_work_count\":%u,\"phase_start_time\":\"%s\","
                               "\"phase_end_time\":\"%s\",\"remaining_sec\":%lld,"
                               "\"work_duration\":%d,\"short_break_duration\":%d,"
                               "\"long_break_duration\":%d,"
                               "\"work_sessions_before_long_break\":%d}",
                               state.active ? "true" : "false",
                               state.paused ? "true" : "false",
                               state.session_id,
                               __pomodoro_phase_str(state.phase),
                               state.current_cycle,
                               state.completed_work_count,
                               cur_start_iso,
                               cur_end_iso,
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
        return __make_pomodoro_control_result(
            wukong_tm_pomodoro_pause() == OPRT_OK, "pause", out_content);
    case MCP_POMODORO_OP_RESUME:
        return __make_pomodoro_control_result(
            wukong_tm_pomodoro_resume() == OPRT_OK, "resume", out_content);
    case MCP_POMODORO_OP_STOP:
        return __make_pomodoro_control_result(
            wukong_tm_pomodoro_stop() == OPRT_OK, "stop", out_content);
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
 * @brief Core schedule operation logic shared by all schedule action handlers.
 *
 * @param[in]  operation   One of the MCP_SCHEDULE_OP_* constants.
 * @param[in]  args        Tool argument object (may inject synthetic fields).
 * @param[out] out_content Tool result content array.
 * @return OPRT_OK on success.
 */
STATIC OPERATE_RET __schedule_exec(INT_T operation, CONST ty_cJSON *args,
                                   ty_cJSON **out_content)
{
    CONST CHAR_T *id = NULL;
    CONST CHAR_T *message = NULL;
    WUKONG_TM_REMINDER_CFG_T cfg = {0};
    WUKONG_TM_REMINDER_CFG_T existing = {0};
    TIME_T start_ts = 0;
    OPERATE_RET rt = OPRT_OK;

    id = __read_string_field(args, "id");
    if (operation != MCP_SCHEDULE_OP_DELETE_ALL && (id == NULL || id[0] == '\0')) {
        return __make_text_result("{\"success\":false,\"reason\":\"The 'id' field is required.\"}", out_content);
    }

    if (operation == MCP_SCHEDULE_OP_ADD) {
        CONST CHAR_T *start_time_str = __read_string_field(args, "start_time");
        if (start_time_str != NULL && start_time_str[0] != '\0') {
            rt = wukong_iso8601_parse_to_timestamp(start_time_str, FALSE, &start_ts);
            /* Also parse to get hour/minute for the cfg */
            WUKONG_ISO8601_RESULT_T iso_result = {0};
            if (rt == OPRT_OK) {
                (VOID)wukong_iso8601_parse(start_time_str, FALSE, &iso_result);
                cfg.hour = (UINT_T)iso_result.tm.tm_hour;
                cfg.minute = (UINT_T)iso_result.tm.tm_min;
            }
        } else if (__has_int_field(args, "offset_minutes")) {
            INT_T offset_minutes = 0;
            __read_int_field(args, "offset_minutes", &offset_minutes, 0);
            if (offset_minutes < 1 || offset_minutes > MCP_SCHEDULE_OFFSET_MINUTES_MAX) {
                return __make_schedule_result(FALSE, 0, NULL, "The 'offset_minutes' value must be greater than 0.", out_content);
            }
            TIME_T now = tal_time_get_posix();
            start_ts = now + (TIME_T)offset_minutes * 60;
            /* For offset_minutes, extract hour/minute from the computed start_ts */
            POSIX_TM_S local_tm = {0};
            if (tal_time_get_local_time_custom(start_ts, &local_tm) == OPRT_OK) {
                cfg.hour = (UINT_T)local_tm.tm_hour;
                cfg.minute = (UINT_T)local_tm.tm_min;
            }
            rt = OPRT_OK;
        } else {
            /* Neither start_time nor offset_minutes provided — tell the LLM exactly what is missing */
            return __make_schedule_result(FALSE, 0, NULL, "Either 'start_time' (ISO 8601) or 'offset_minutes' is required.", out_content);
        }

        /* Parse repeat_type, weekday_mask, month_day */
        {
            CONST CHAR_T *repeat_str = __read_string_field(args, "repeat_type");
            if (repeat_str != NULL && repeat_str[0] != '\0') {
                if (strcmp(repeat_str, "once") == 0) { cfg.repeat_type = WUKONG_TM_REPEAT_ONCE; }
                else if (strcmp(repeat_str, "daily") == 0) { cfg.repeat_type = WUKONG_TM_REPEAT_DAILY; }
                else if (strcmp(repeat_str, "weekly") == 0) { cfg.repeat_type = WUKONG_TM_REPEAT_WEEKLY; }
                else if (strcmp(repeat_str, "monthly") == 0) { cfg.repeat_type = WUKONG_TM_REPEAT_MONTHLY; }
            } else {
                cfg.repeat_type = WUKONG_TM_REPEAT_ONCE;
            }
            __read_int_field(args, "weekday_mask", (INT_T *)&cfg.weekday_mask, 0);
            __read_int_field(args, "month_day", (INT_T *)&cfg.month_day, 0);
        }

        message = __read_string_field(args, "message");
        if (rt != OPRT_OK) {
            return __make_schedule_result(FALSE, 0, NULL, "Invalid 'start_time' format; use ISO 8601, e.g. 2025-06-12T14:30:00+08:00.", out_content);
        }
        if (message == NULL || message[0] == '\0') {
            return __make_schedule_result(FALSE, start_ts, NULL,
                                          "The 'message' field is required.", out_content);
        }
        if (cfg.repeat_type == WUKONG_TM_REPEAT_ONCE && start_ts <= tal_time_get_posix()) {
            return __make_schedule_result(FALSE, start_ts, NULL,
                                          "The specified time has already passed; provide a future time or use offset_minutes.", out_content);
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
            return __make_text_result("{\"success\":false,\"reason\":\"No reminder found with the specified id.\"}", out_content);
        }

        /* Parse start_time if provided */
        {
            CONST CHAR_T *start_time_str = __read_string_field(args, "start_time");
            if (start_time_str != NULL && start_time_str[0] != '\0') {
                WUKONG_ISO8601_RESULT_T iso_result = {0};
                rt = wukong_iso8601_parse(start_time_str, FALSE, &iso_result);
                if (rt != OPRT_OK) {
                    return __make_schedule_result(FALSE, 0, NULL, "The 'start_time' value is not a valid ISO 8601 datetime.", out_content);
                }
                rt = wukong_iso8601_parse_to_timestamp(start_time_str, FALSE, &start_ts);
                if (rt != OPRT_OK) {
                    return __make_schedule_result(FALSE, 0, NULL, "The 'start_time' value is not a valid ISO 8601 datetime.", out_content);
                }
                existing.start_time = start_ts;
                existing.hour = (UINT_T)iso_result.tm.tm_hour;
                existing.minute = (UINT_T)iso_result.tm.tm_min;
            } else {
                start_ts = existing.start_time;
            }
        }

        /* Parse repeat_type if provided */
        {
            CONST CHAR_T *repeat_str = __read_string_field(args, "repeat_type");
            if (repeat_str != NULL && repeat_str[0] != '\0') {
                if (strcmp(repeat_str, "once") == 0) { existing.repeat_type = WUKONG_TM_REPEAT_ONCE; }
                else if (strcmp(repeat_str, "daily") == 0) { existing.repeat_type = WUKONG_TM_REPEAT_DAILY; }
                else if (strcmp(repeat_str, "weekly") == 0) { existing.repeat_type = WUKONG_TM_REPEAT_WEEKLY; }
                else if (strcmp(repeat_str, "monthly") == 0) { existing.repeat_type = WUKONG_TM_REPEAT_MONTHLY; }
            }
        }
        __read_int_field(args, "weekday_mask", (INT_T *)&existing.weekday_mask, existing.weekday_mask);
        __read_int_field(args, "month_day", (INT_T *)&existing.month_day, existing.month_day);

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
    } else if (operation == MCP_SCHEDULE_OP_DELETE_ALL) {
        UINT_T removed = 0;
        rt = wukong_tm_reminder_remove_all(&removed);
        if (rt == OPRT_OK) {
            CHAR_T buf[64] = {0};
            (VOID)snprintf(buf, sizeof(buf),
                           "{\"success\":true,\"removed_count\":%u}", removed);
            return __make_text_result(buf, out_content);
        }
    } else {
        rt = OPRT_INVALID_PARM;
    }

    TAL_PR_DEBUG("schedule exec -> operation=%d rt=%d", operation, rt);
    if (rt != OPRT_OK) {
        (VOID_T)__make_schedule_result(FALSE, start_ts,
                                       (operation == MCP_SCHEDULE_OP_ADD || operation == MCP_SCHEDULE_OP_UPDATE) ? id : NULL,
                                       NULL, out_content);
        return rt;
    }
    return __make_schedule_result(TRUE, start_ts,
                                  (operation == MCP_SCHEDULE_OP_ADD || operation == MCP_SCHEDULE_OP_UPDATE) ? id : NULL,
                                  NULL, out_content);
}

/**
 * @brief Wrapper used by the MCP server for `device_schedule_set`.
 */
STATIC OPERATE_RET __set_schedule(CONST CHAR_T *name, CONST ty_cJSON *args,
                                  ty_cJSON **out_content,
                                  VOID *user_data)
{
    CONST CHAR_T *id = NULL;
    BOOL_T is_update = FALSE;

    (VOID)name;
    (VOID)user_data;

    id = __read_string_field(args, "id");
    if (id != NULL && id[0] != '\0') {
        WUKONG_TM_REMINDER_CFG_T existing = {0};
        if (wukong_tm_reminder_get(id, &existing) == OPRT_OK) {
            is_update = TRUE;
        }
    }

    return __schedule_exec(is_update ? MCP_SCHEDULE_OP_UPDATE : MCP_SCHEDULE_OP_ADD,
                           args, out_content);
}

/**
 * @brief Wrapper used by the MCP server for `device_schedule_delete`.
 */
STATIC OPERATE_RET __delete_schedule(CONST CHAR_T *name, CONST ty_cJSON *args,
                                     ty_cJSON **out_content,
                                     VOID *user_data)
{
    CONST CHAR_T *id = NULL;

    (VOID)name;
    (VOID)user_data;

    id = __read_string_field(args, "id");
    if (id == NULL || id[0] == '\0') {
        return __make_text_result("{\"success\":false,\"reason\":\"The 'id' field is required.\"}", out_content);
    }

    return __schedule_exec(MCP_SCHEDULE_OP_DELETE, args, out_content);
}

/**
 * @brief Wrapper used by the MCP server for `device_schedule_delete_all`.
 */
STATIC OPERATE_RET __delete_schedule_all(CONST CHAR_T *name, CONST ty_cJSON *args,
                                         ty_cJSON **out_content,
                                         VOID *user_data)
{
    (VOID)name;
    (VOID)args;
    (VOID)user_data;

    return __schedule_exec(MCP_SCHEDULE_OP_DELETE_ALL, NULL, out_content);
}

/**
 * @brief Wrapper used by the MCP server for `device_schedule_query_set`.
 */
STATIC OPERATE_RET __set_schedule_query(CONST CHAR_T *name, CONST ty_cJSON *args,
                                        ty_cJSON **out_content,
                                        VOID *user_data)
{
    CONST CHAR_T *keyword = NULL;
    CHAR_T *ret_content = NULL;
    TIME_T start_ts = 0;
    TIME_T end_ts = 0;

    (VOID)name;
    (VOID)user_data;

    TAL_PR_DEBUG("__set_schedule_query enter");

    if (args != NULL) {
        CONST CHAR_T *start_date_str = __read_string_field(args, "start_date");
        CONST CHAR_T *end_date_str = __read_string_field(args, "end_date");

        if (start_date_str != NULL && start_date_str[0] != '\0') {
            OPERATE_RET rt = wukong_iso8601_parse_to_timestamp(start_date_str, FALSE, &start_ts);
            if (rt != OPRT_OK) {
                return __make_text_result("{\"success\":false,\"reason\":\"invalid_start_date\"}", out_content);
            }
        }
        if (end_date_str != NULL && end_date_str[0] != '\0') {
            OPERATE_RET rt = wukong_iso8601_parse_to_timestamp(end_date_str, TRUE, &end_ts);
            if (rt != OPRT_OK) {
                return __make_text_result("{\"success\":false,\"reason\":\"invalid_end_date\"}", out_content);
            }
        }

        ty_cJSON *node = ty_cJSON_GetObjectItem(args, "keyword");
        if (node != NULL && ty_cJSON_IsString(node)) {
            keyword = node->valuestring;
        }
    }

    ret_content = wukong_tm_reminder_query_text(start_ts, end_ts, keyword);
    *out_content = ty_cJSON_CreateArray();
    if (*out_content == NULL) {
        if (ret_content != NULL) {
            tal_free(ret_content);
        }
        return OPRT_MALLOC_FAILED;
    }

    if (ret_content != NULL) {
        ty_cJSON_AddItemToArray(*out_content, wukong_tool_make_text(ret_content));
        tal_free(ret_content);
    } else {
        ty_cJSON_AddItemToArray(*out_content, wukong_tool_make_text("false"));
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
 * @return OPRT_OK on success.
 */
OPERATE_RET tool_alarm_set_exec(CONST ty_cJSON *args, ty_cJSON **out_content)
{
    INT_T operation = MCP_ALARM_OP_ADD;
    CONST CHAR_T *alarm_id = NULL;
    WUKONG_TM_ALARM_CFG_T alarm_cfg = {0};
    WUKONG_TM_ALARM_CFG_T existing_cfg = {0};
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(out_content, OPRT_INVALID_PARM);

    rt = __resolve_action(args, s_alarm_actions, &operation);
    if (rt != OPRT_OK) {
        return __make_action_error(rt, out_content);
    }
    alarm_id = __read_string_field(args, "id");
    if (operation != MCP_ALARM_OP_DELETE_ALL &&
        (alarm_id == NULL || alarm_id[0] == '\0')) {
        return __make_text_result("{\"success\":false,\"reason\":\"The 'id' field is required.\"}", out_content);
    }

    if (operation == MCP_ALARM_OP_ADD) {
        CHAR_T existing_id[WUKONG_TM_ALARM_ID_LEN + 1] = {0};
        WUKONG_TM_ALARM_CFG_T probe_cfg = {0};

        rt = __alarm_cfg_from_args(args, NULL, &alarm_cfg);
        if (rt != OPRT_OK) {
            return __make_text_result("{\"success\":false,\"reason\":\"One or more alarm fields are invalid; check 'start_time' (ISO 8601) and 'repeat_type'.\"}", out_content);
        }
        if (alarm_cfg.repeat_type == WUKONG_TM_REPEAT_ONCE) {
            if (alarm_cfg.start_time <= 0) {
                return __make_text_result("{\"success\":false,\"reason\":\"The 'start_time' value is not a valid ISO 8601 datetime.\"}", out_content);
            }
            if (alarm_cfg.start_time <= tal_time_get_posix()) {
                return __make_text_result("{\"success\":false,\"reason\":\"The specified time has already passed; provide a future time.\"}", out_content);
            }
        }
        /* Pre-check id collision so the storage-full path below can be
         * reported with a distinct, accurate reason. */
        if (wukong_tm_alarm_get(alarm_id, &probe_cfg) == OPRT_OK) {
            CHAR_T id_buf[128] = {0};
            (VOID)snprintf(id_buf, sizeof(id_buf),
                           "{\"success\":false,\"reason\":\"An alarm with this id already exists.\",\"existing_id\":\"%s\"}",
                           alarm_id);
            return __make_text_result(id_buf, out_content);
        }
        if (wukong_tm_alarm_find_by_time(&alarm_cfg, existing_id, sizeof(existing_id)) == OPRT_OK) {
            CHAR_T dup_buf[128] = {0};
            (VOID)snprintf(dup_buf, sizeof(dup_buf),
                           "{\"success\":false,\"reason\":\"An alarm at the same time already exists.\",\"existing_id\":\"%s\"}",
                           existing_id);
            return __make_text_result(dup_buf, out_content);
        }
        rt = wukong_tm_alarm_add(&alarm_cfg, alarm_id);
        /* For once-alarms: detect whether the backend rolled the trigger to
         * tomorrow (requested time was already in the past).  When detected,
         * return a richer result so the LLM can tell the user the correct
         * fire date instead of silently claiming success for today. */
        if (rt == OPRT_OK && alarm_cfg.repeat_type == WUKONG_TM_REPEAT_ONCE) {
            WUKONG_TM_ALARM_CFG_T stored_cfg = {0};
            if (wukong_tm_alarm_get(alarm_id, &stored_cfg) == OPRT_OK &&
                stored_cfg.start_time > alarm_cfg.start_time) {
                CHAR_T roll_buf[256] = {0};
                CHAR_T start_iso[32] = {0};
                wukong_iso8601_format(stored_cfg.start_time, start_iso, sizeof(start_iso));
                (VOID)snprintf(roll_buf, sizeof(roll_buf),
                               "{\"success\":true,\"rolled_to_next_day\":true,\"start_time\":\"%s\","
                               "\"detail\":\"The requested time has already passed today; alarm set for tomorrow at the same time.\"}",
                               start_iso);
                return __make_text_result(roll_buf, out_content);
            }
        }
    } else if (operation == MCP_ALARM_OP_DELETE) {
        rt = wukong_tm_alarm_remove(alarm_id);
    } else if (operation == MCP_ALARM_OP_UPDATE) {
        rt = wukong_tm_alarm_get(alarm_id, &existing_cfg);
        if (rt != OPRT_OK) {
            return __make_text_result("{\"success\":false,\"reason\":\"No alarm found with the specified id.\"}", out_content);
        }
        rt = __alarm_cfg_from_args(args, &existing_cfg, &alarm_cfg);
        if (rt != OPRT_OK) {
            return __make_text_result("{\"success\":false,\"reason\":\"One or more alarm fields are invalid; check 'start_time' (ISO 8601) and 'repeat_type'.\"}", out_content);
        }
        if (alarm_cfg.repeat_type == WUKONG_TM_REPEAT_ONCE && alarm_cfg.start_time > 0) {
            if (alarm_cfg.start_time <= tal_time_get_posix()) {
                return __make_text_result("{\"success\":false,\"reason\":\"The specified time has already passed; provide a future time.\"}", out_content);
            }
        }
        rt = wukong_tm_alarm_update(alarm_id, &alarm_cfg);
    } else if (operation == MCP_ALARM_OP_ACK) {
        rt = wukong_tm_alarm_ack(alarm_id);
    } else if (operation == MCP_ALARM_OP_DELETE_ALL) {
        UINT_T removed = 0;
        rt = wukong_tm_alarm_remove_all(&removed);
        if (rt == OPRT_OK) {
            CHAR_T buf[64] = {0};
            (VOID)snprintf(buf, sizeof(buf),
                           "{\"success\":true,\"removed_count\":%u}", removed);
            return __make_text_result(buf, out_content);
        }
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
        CONST CHAR_T *reason = "An internal error occurred; please retry.";
        CHAR_T buf[256] = {0};

        if (rt == OPRT_INVALID_PARM) {
            reason = "The operation was rejected due to an invalid parameter.";
        } else if (rt == OPRT_NOT_FOUND) {
            reason = "No alarm found with the specified id.";
        } else if (rt == OPRT_COM_ERROR) {
            if (operation == MCP_ALARM_OP_ADD) {
                return __make_text_result(
                    "{\"success\":false,\"reason\":\"All alarm slots are full; ask the user to delete an existing alarm first.\"}",
                    out_content);
            }
            reason = "The operation failed unexpectedly.";
        }
        (VOID)snprintf(buf, sizeof(buf),
                       "{\"success\":false,\"reason\":\"%s\"}", reason);
        (VOID_T)__make_text_result(buf, out_content);
        return rt;
    }
}

/**
 * @brief Execute the alarm query tool logic directly.
 *
 * @param[in]  args         Tool argument object.
 * @param[out] out_content  Tool result content array.
 * @return OPRT_OK on success.
 */
OPERATE_RET tool_alarm_query_exec(CONST ty_cJSON *args, ty_cJSON **out_content)
{
    CHAR_T *alarm_list_json = NULL;
    OPERATE_RET rt = OPRT_OK;

    (VOID)args;
    TUYA_CHECK_NULL_RETURN(out_content, OPRT_INVALID_PARM);

    rt = wukong_tm_alarm_list(&alarm_list_json);
    if (rt != OPRT_OK || alarm_list_json == NULL) {
        return __make_text_result("{\"success\":false,\"reason\":\"An internal error occurred; please retry.\"}", out_content);
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

    rt = WUKONG_TOOL_ADD(
        "device_alarm_set",
        "Manage local alarms (RINGTONE only). `message` is a stored note, NOT spoken — use `device_schedule_set` for spoken content.\n"
        "`start_time` is ISO 8601 (e.g. \"2025-06-12T14:30:00+08:00\"); for recurring alarms only the time-of-day portion is used. "
        "`repeat_type`: once (needs full date+time; past time auto-rolls to tomorrow; "
        "if the user does not state an exact hour and minute in the current message, ask for it before calling; do NOT borrow from context), "
        "daily, weekly (`weekday_mask` bit0=Sun..bit6=Sat; workdays=62, weekend=65), "
        "monthly (`month_day`). Use non-once ONLY on explicit recurrence request. "
        "Update merges provided fields only. Add may fail if the id is taken, the time conflicts, or all slots are full.",
        __set_alarm, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR("id", "Alarm identifier; auto-generate a short unique ID if the user does not specify one"),
        TOOL_SCHEMA_STR_OPT("start_time", "ISO 8601 datetime (e.g. 2025-06-12T14:30:00+08:00); required for add; time-of-day extracted for recurring"),
        TOOL_SCHEMA_STR_ENUM_OPT("repeat_type", "Repeat: once, daily, weekly, or monthly",
                                "once|daily|weekly|monthly"),
        TOOL_SCHEMA_INT_OPT_RANGE("weekday_mask", "Weekly bitmask bit0=Sun..bit6=Sat (workdays=62, weekend=65)", 0, 127),
        TOOL_SCHEMA_INT_OPT_RANGE("month_day", "Day of month for monthly repeat", 1, 31),
        TOOL_SCHEMA_STR_OPT("message", "Text note for query/display; NOT spoken when the alarm fires"),
        TOOL_SCHEMA_BOOL_OPT("enabled", "Alarm enabled state (kept in list but won't fire when false)")
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = WUKONG_TOOL_ADD(
        "device_alarm_delete",
        "Delete a local alarm by its `id`.",
        __delete_alarm, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR("id", "Alarm identifier to delete")
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = WUKONG_TOOL_ADD(
        "device_alarm_delete_all",
        "Delete ALL local alarms at once (irreversible). Only use when user explicitly asks to clear/delete all alarms.",
        __delete_alarm_all, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = WUKONG_TOOL_ADD(
        "device_alarm_ack",
        "Acknowledge (dismiss) the currently ringing alarm. Requires the `id` of the ringing alarm.",
        __ack_alarm, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR("id", "Alarm identifier of the ringing alarm to acknowledge")
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    return WUKONG_TOOL_ADD(
        "device_alarm_query",
        "Query LOCAL ALARMS only (ringtone-based; one-time and recurring). "
        "Every returned entry IS an alarm regardless of what the per-item `message` text says; it MUST NOT be reinterpreted as a reminder. "
        "This tool NEVER returns spoken schedule items; use `device_schedule_query` for those. "
        "When the user explicitly asks about 'alarm', call ONLY this tool and describe results as 'alarm(s)'. "
        "Only call BOTH this tool and `device_schedule_query` when the user uses an ambiguous term covering both (e.g. 'what to-dos do I have').",
        __query_alarm, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT
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

    rt = WUKONG_TOOL_ADD(
        "device_countdown_timer_set",
        "Single-instance countdown timer counting down from a required target duration; not modifiable in place. "
        "Duplicate create returns {success:false,current:{...}} with the existing timer state. "
        "Pause/delete return {success:true,remaining_sec,elapsed_sec}. "
        "All duration fields are integers; convert fractional minutes to seconds (e.g. 0.5min -> minute_duration=0, second_duration=30).",
        __set_countdown_timer, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_INT_OPT_RANGE("hour_duration", "Hours (create only)", 0, 24),
        TOOL_SCHEMA_INT_OPT_RANGE("minute_duration", "Whole minutes (create only)", 0, 60),
        TOOL_SCHEMA_INT_OPT_RANGE("second_duration", "Seconds (create only)", 0, 60),
        TOOL_SCHEMA_STR_ENUM("action", "Action: create, pause, resume, or delete",
                            "create|pause|resume|delete")
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = WUKONG_TOOL_ADD(
        "device_countdown_timer_query",
        "Single-instance countdown timer; query returns active/state/remaining_sec/duration_sec/elapsed_sec.",
        __set_countdown_timer, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR_ENUM("action", "Fixed: query", "query")
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = WUKONG_TOOL_ADD(
        "device_stopwatch_timer_set",
        "Single-instance count-up stopwatch from 0; the default timer when no target duration is given (e.g. 'start timing'). "
        "Duplicate start returns {success:false,current:{...}} with the existing timer state. "
        "Pause/stop/reset return {success:true,elapsed_sec}.",
        __set_stopwatch_timer, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR_ENUM("action", "Action: start, pause, resume, stop, or reset",
                            "start|pause|resume|stop|reset")
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = WUKONG_TOOL_ADD(
        "device_stopwatch_timer_query",
        "Single-instance count-up stopwatch from 0; query returns active/paused/elapsed_sec.",
        __set_stopwatch_timer, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR_ENUM("action", "Fixed: query", "query")
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = WUKONG_TOOL_ADD(
        "device_pomodoro_start",
        "Pomodoro focus timer (use for 'pomodoro/focus session/concentrate for X minutes'). "
        "Single instance; stop before recreating with new durations. "
        "Duplicate start returns {success:false,current:{...}} with the existing timer state. "
        "Durations are in minutes; keep short_break_duration < long_break_duration.",
        __set_pomodoro_timer, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR_ENUM("action", "Fixed: start", "start"),
        TOOL_SCHEMA_INT_OPT_RANGE("work_duration", "Work minutes (default 25)", 1, 120),
        TOOL_SCHEMA_INT_OPT_RANGE("short_break_duration", "Short break minutes (default 5)", 1, 30),
        TOOL_SCHEMA_INT_OPT_RANGE("long_break_duration", "Long break minutes (default 15)", 5, 60),
        TOOL_SCHEMA_INT_OPT_RANGE("work_sessions_before_long_break",
            "Work sessions before a long break (default 4)", WUKONG_TM_POMODORO_WORK_BEFORE_LONG_MIN,
            WUKONG_TM_POMODORO_WORK_BEFORE_LONG_MAX)
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = WUKONG_TOOL_ADD(
        "device_pomodoro_control",
        "Pomodoro focus timer (use for 'pomodoro/focus session/concentrate for X minutes'). "
        "Pause, resume, or stop the running Pomodoro session.",
        __set_pomodoro_timer, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR_ENUM("action", "Action: pause, resume, or stop",
                            "pause|resume|stop")
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    return WUKONG_TOOL_ADD(
        "device_pomodoro_query",
        "Pomodoro focus timer (use for 'pomodoro/focus session/concentrate for X minutes'). "
        "Query the current Pomodoro session state. "
        "Use `remaining_sec` (seconds) directly for remaining time. "
        "Durations are in minutes; `phase_end_time` is ISO 8601.",
        __set_pomodoro_timer, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR_ENUM("action", "Fixed: query", "query")
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

    rt = WUKONG_TOOL_ADD(
        "device_schedule_set",
        "Manage spoken reminders; the device SPEAKS `message` via TTS at the scheduled time. "
        "For ringtone-only wake-ups use `device_alarm_set` instead.\n"
        "`start_time` is ISO 8601 (e.g. \"2025-06-12T14:30:00+08:00\"); required on add (with message). "
        "Alternatively use `offset_minutes` for relative time (device clock is authoritative). "
        "If both given, start_time wins. Resolved time MUST be in the future.\n"
        "Recurring reminders (daily/weekly/monthly) fire repeatedly and do NOT auto-delete. "
        "Use `repeat_type` with `weekday_mask` (weekly) or `month_day` (monthly) for recurrence.\n"
        "If user does not state an exact hour and minute in the current message, ask ONLY for the missing time — do NOT borrow from context or guess a default.",
        __set_schedule, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR("id", "Schedule identifier; auto-generate if user does not provide one (do not ask the user)"),
        TOOL_SCHEMA_STR_OPT("start_time", "ISO 8601 datetime (e.g. 2025-06-12T14:30:00+08:00); preferred for absolute time"),
        TOOL_SCHEMA_INT_OPT_RANGE("offset_minutes",
            "Minutes from device now until the schedule fires; preferred for relative time on add",
            1, MCP_SCHEDULE_OFFSET_MINUTES_MAX),
        TOOL_SCHEMA_STR_ENUM_OPT("repeat_type", "Repeat: once, daily, weekly, or monthly", "once|daily|weekly|monthly"),
        TOOL_SCHEMA_INT_OPT_RANGE("weekday_mask", "Weekly bitmask bit0=Sun..bit6=Sat (workdays=62, weekend=65)", 0, 127),
        TOOL_SCHEMA_INT_OPT_RANGE("month_day", "Day of month for monthly repeat", 1, 31),
        TOOL_SCHEMA_STR_OPT("message", "Reminder message text spoken at fire time (required on add)")
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = WUKONG_TOOL_ADD(
        "device_schedule_delete",
        "Delete a spoken reminder by its `id`.",
        __delete_schedule, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR("id", "Schedule identifier of the reminder to delete")
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    rt = WUKONG_TOOL_ADD(
        "device_schedule_delete_all",
        "Delete ALL spoken reminders at once (irreversible). Only use when user explicitly asks to clear/delete all reminders.",
        __delete_schedule_all, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT
    );
    if (rt != OPRT_OK) {
        return rt;
    }

    return WUKONG_TOOL_ADD(
        "device_schedule_query",
        "Query spoken reminders (the device speaks `message` via TTS at fire time). "
        "Does NOT return ringtone alarms; use `device_alarm_query` for those, "
        "and call BOTH when the user asks an ambiguous 'what do I have scheduled' without specifying type. "
        "`start_date`/`end_date` are ISO 8601 date or datetime strings; date-only values cover the whole day. "
        "`keyword` matches the schedule message.",
        __set_schedule_query, NULL, WUKONG_TOOL_MCP | WUKONG_TOOL_AGENT,
        TOOL_SCHEMA_STR_OPT("start_date", "Inclusive lower bound, ISO 8601 date or datetime (e.g. 2025-06-12 or 2025-06-12T00:00:00+08:00)"),
        TOOL_SCHEMA_STR_OPT("end_date", "Inclusive upper bound, ISO 8601 date or datetime (e.g. 2025-06-12 or 2025-06-12T23:59:59+08:00)"),
        TOOL_SCHEMA_STR_OPT("keyword", "Keyword matched against the schedule message")
    );
}

/**
 * @brief Register all time-management MCP tools with the local MCP server.
 *
 * @return OPRT_OK on success.
 */
OPERATE_RET tool_tm_init(VOID)
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
