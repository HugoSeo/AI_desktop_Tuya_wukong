/**
 * @file test_mcp_tools.c
 * @brief MCP tool schema + integration tests (from test_wukong_mcp_tm_tools.sh).
 * Run via pytest test_suite.py -k mcp_tools.
 */
#include <stdio.h>
#include <string.h>

#include "wukong_test.h"
#include "ty_cJSON.h"
#include "mcp_tool_tm.h"
#include "wukong_ai_mcp.h"

int test_has_tool(const char *name);
const char *test_tool_description(const char *name);
const char *test_prop_description(const char *tool_name, const char *prop_name);
BOOL_T test_prop_required(const char *tool_name, const char *prop_name);
MCP_TOOL_HANDLER_CB test_get_tool_handler(const char *name);
void test_set_now(TIME_T now);
void test_set_reminder_snapshot(const char *reminder_id, TIME_T start_time, const char *message);
BOOL_T test_reminder_add_called(void);
TIME_T test_last_reminder_add_start_time(void);
const CHAR_T *test_last_reminder_add_message(void);
BOOL_T test_reminder_update_called(void);
const CHAR_T *test_last_reminder_update_id(void);
TIME_T test_last_reminder_update_start_time(void);
const CHAR_T *test_last_reminder_update_message(void);

int main(void)
{
    const char *alarm_desc = NULL;
    const char *alarm_query_desc = NULL;
    const char *schedule_desc = NULL;
    const char *schedule_query_desc = NULL;
    const char *alarm_message_desc = NULL;
    const char *alarm_repeat_desc = NULL;
    const char *alarm_new_repeat_desc = NULL;
    const char *schedule_message_desc = NULL;
    const char *schedule_description_desc = NULL;
    const char *schedule_year_desc = NULL;
    const char *schedule_month_desc = NULL;
    const char *schedule_day_desc = NULL;
    const char *schedule_hour_desc = NULL;
    const char *schedule_minute_desc = NULL;
    const char *schedule_new_hour_desc = NULL;
    const char *schedule_new_message_desc = NULL;
    const char *schedule_start_ts_set_desc = NULL;
    const char *schedule_query_start_desc = NULL;
    const char *schedule_query_end_desc = NULL;
    const char *schedule_query_start_time_desc = NULL;
    const char *schedule_id_desc = NULL;
    MCP_TOOL_HANDLER_CB countdown_handler = NULL;
    MCP_TOOL_HANDLER_CB stopwatch_handler = NULL;
    MCP_TOOL_HANDLER_CB pomodoro_handler = NULL;
    MCP_TOOL_HANDLER_CB schedule_handler = NULL;
    ty_cJSON *args = NULL;
    ty_cJSON *out_content = NULL;
    BOOL_T is_error = FALSE;
    const char *query_text = NULL;

        EXPECT(mcp_tool_tm_init() == 0, "expected unified time-management init to succeed");


        EXPECT(test_has_tool("device_alarm_set"), "expected device_alarm_set to be registered");

        EXPECT(test_has_tool("device_alarm_query"), "expected device_alarm_query to be registered");

        EXPECT(test_has_tool("device_countdown_timer_set"), "expected device_countdown_timer_set to be registered");

        EXPECT(test_has_tool("device_stopwatch_timer_set"), "expected device_stopwatch_timer_set to be registered");

        EXPECT(test_has_tool("device_pomodoro_timer_set"), "expected device_pomodoro_timer_set to be registered");

        EXPECT(test_has_tool("device_schedule_set"), "expected device_schedule_set to be registered");

        EXPECT(test_has_tool("device_schedule_query"), "expected device_schedule_query to be registered");


    alarm_desc = test_tool_description("device_alarm_set");
    alarm_query_desc = test_tool_description("device_alarm_query");
    schedule_desc = test_tool_description("device_schedule_set");
    schedule_query_desc = test_tool_description("device_schedule_query");
    alarm_message_desc = test_prop_description("device_alarm_set", "message");
    alarm_repeat_desc = test_prop_description("device_alarm_set", "repeat_type");
    alarm_new_repeat_desc = test_prop_description("device_alarm_set", "new_repeat_type");
    schedule_message_desc = test_prop_description("device_schedule_set", "message");
    schedule_description_desc = test_prop_description("device_schedule_set", "description");
    schedule_year_desc = test_prop_description("device_schedule_set", "year");
    schedule_month_desc = test_prop_description("device_schedule_set", "month");
    schedule_day_desc = test_prop_description("device_schedule_set", "day");
    schedule_hour_desc = test_prop_description("device_schedule_set", "hour");
    schedule_minute_desc = test_prop_description("device_schedule_set", "minute");
    schedule_id_desc = test_prop_description("device_schedule_set", "id");
    schedule_new_hour_desc = test_prop_description("device_schedule_set", "new_hour");
    schedule_new_message_desc = test_prop_description("device_schedule_set", "new_message");
    schedule_start_ts_set_desc = test_prop_description("device_schedule_set", "start_timestamp");
    schedule_query_start_desc = test_prop_description("device_schedule_query", "start_timestamp");
    schedule_query_end_desc = test_prop_description("device_schedule_query", "end_timestamp");
    schedule_query_start_time_desc = test_prop_description("device_schedule_query", "start_time");

        EXPECT(alarm_desc != NULL &&
               strstr(alarm_desc, "Manage local alarms") != NULL &&
               strstr(alarm_desc, "RINGTONE") != NULL &&
               strstr(alarm_desc, "device_schedule_set") != NULL &&
               strstr(alarm_desc, "repeat_type") != NULL &&
               strstr(alarm_desc, "weekday_mask") != NULL &&
               strstr(alarm_desc, "Update merges") != NULL,
               "expected alarm tool description to focus on alarms");

        /* Reason codes added for issue 2 (storage_full vs id_exists vs
         * already_exists) must appear in the schema so the model can pick
         * the right phrasing when add fails. */
        EXPECT(alarm_desc != NULL &&
               strstr(alarm_desc, "storage_full") != NULL &&
               strstr(alarm_desc, "id_exists") != NULL &&
               strstr(alarm_desc, "already_exists") != NULL,
               "expected alarm_set description to enumerate failure reason codes");

        /* Issue 3: alarm_query description must lock down item-type so the
         * model never re-classifies an alarm as a reminder based on the
         * `message` text. */
        EXPECT(alarm_query_desc != NULL &&
               strstr(alarm_query_desc, "LOCAL ALARMS only") != NULL &&
               strstr(alarm_query_desc, "Every returned entry IS an alarm") != NULL &&
               strstr(alarm_query_desc, "regardless of what the per-item `message`") != NULL &&
               strstr(alarm_query_desc, "MUST NOT be reinterpreted") != NULL,
               "expected alarm query description to pin item type to alarm regardless of message");

        EXPECT(schedule_desc != NULL &&
               strstr(schedule_desc, "reminder") != NULL &&
               strstr(schedule_desc, "year") != NULL &&
               strstr(schedule_desc, "minute") != NULL &&
               strstr(schedule_desc, "offset_minutes") != NULL &&
               strstr(schedule_desc, "Update merges") != NULL &&
               strstr(schedule_desc, "start_timestamp") != NULL &&
               strstr(schedule_desc, "明天") == NULL &&
               strstr(schedule_desc, "小时和分钟") == NULL,
               "expected schedule tool description to focus on reminders");

        EXPECT(schedule_query_desc != NULL &&
               strstr(schedule_query_desc, "Query one-time spoken reminders") != NULL &&
               strstr(schedule_query_desc, "start_timestamp") != NULL,
               "expected schedule query description to be in English and use timestamps");

        EXPECT(alarm_message_desc != NULL &&
               strstr(alarm_message_desc, "Text note") != NULL &&
               strstr(alarm_message_desc, "query") != NULL,
               "expected alarm message field description to mention semantic record");

        EXPECT(alarm_repeat_desc != NULL &&
               strstr(alarm_repeat_desc, "daily") != NULL &&
               strstr(alarm_repeat_desc, "weekly") != NULL,
               "expected alarm repeat_type description to guide recurring alarms");

        EXPECT(alarm_new_repeat_desc == NULL,
               "expected alarm new_repeat_type field to be removed from schema");

        EXPECT(schedule_message_desc != NULL &&
               strstr(schedule_message_desc, "Reminder message") != NULL &&
               strstr(schedule_message_desc, "description") == NULL,
               "expected schedule message field description to focus on message only");

        EXPECT(schedule_description_desc == NULL,
               "expected schedule description field to be removed");

        EXPECT(schedule_year_desc != NULL &&
               strstr(schedule_year_desc, "year") != NULL,
               "expected schedule year field description");

        EXPECT(schedule_month_desc != NULL &&
               strstr(schedule_month_desc, "month") != NULL,
               "expected schedule month field description");

        EXPECT(schedule_day_desc != NULL &&
               strstr(schedule_day_desc, "day") != NULL,
               "expected schedule day field description");

        EXPECT(schedule_hour_desc != NULL &&
               strstr(schedule_hour_desc, "hour") != NULL,
               "expected schedule hour field description");

        EXPECT(schedule_minute_desc != NULL &&
               strstr(schedule_minute_desc, "minute") != NULL,
               "expected schedule minute field description");

        EXPECT(schedule_id_desc != NULL &&
               strstr(schedule_id_desc, "identifier") != NULL,
               "expected schedule id field description");

        EXPECT(schedule_new_hour_desc == NULL,
               "expected schedule new_hour field to be removed");

        EXPECT(schedule_new_message_desc == NULL,
               "expected schedule new_message field to be removed");

        EXPECT(schedule_start_ts_set_desc == NULL,
               "expected schedule set start_timestamp field to be removed");

        EXPECT(test_prop_required("device_schedule_set", "categories") == FALSE,
               "expected schedule categories field to be optional");

        EXPECT(schedule_query_start_desc != NULL &&
               strstr(schedule_query_start_desc, "lower bound") != NULL,
               "expected schedule query start_timestamp property");

        EXPECT(schedule_query_end_desc != NULL &&
               strstr(schedule_query_end_desc, "upper bound") != NULL,
               "expected schedule query end_timestamp property");

        EXPECT(schedule_query_start_time_desc == NULL,
               "expected legacy start_time query property to be removed");


    countdown_handler = test_get_tool_handler("device_countdown_timer_set");
    stopwatch_handler = test_get_tool_handler("device_stopwatch_timer_set");
    pomodoro_handler = test_get_tool_handler("device_pomodoro_timer_set");
    schedule_handler = test_get_tool_handler("device_schedule_set");
        EXPECT(countdown_handler != NULL, "expected countdown tool handler to be registered");

        EXPECT(stopwatch_handler != NULL, "expected stopwatch tool handler to be registered");

        EXPECT(pomodoro_handler != NULL, "expected pomodoro tool handler to be registered");

        EXPECT(schedule_handler != NULL, "expected schedule tool handler to be registered");


    test_set_now(1774832400);

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 0);
    ty_cJSON_AddNumberToObject(args, "hour_duration", 0);
    ty_cJSON_AddNumberToObject(args, "minute_duration", 1);
    ty_cJSON_AddNumberToObject(args, "second_duration", 30);
        EXPECT(countdown_handler("device_countdown_timer_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected countdown create to succeed");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 0);
    ty_cJSON_AddNumberToObject(args, "hour_duration", 0);
    ty_cJSON_AddNumberToObject(args, "minute_duration", 2);
    ty_cJSON_AddNumberToObject(args, "second_duration", 0);
        EXPECT(countdown_handler("device_countdown_timer_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected duplicate countdown create tool call to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":false") != NULL &&
               strstr(query_text, "\"reason\":\"already_exists\"") != NULL,
               "expected duplicate countdown create to return already_exists");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 4);
        EXPECT(countdown_handler("device_countdown_timer_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected countdown query to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL &&
               strstr(query_text, "\"duration_sec\":90") != NULL &&
               strstr(query_text, "\"remaining_sec\":90") != NULL &&
               strstr(query_text, "\"elapsed_sec\":0") != NULL,
               "expected countdown query JSON with duration/remaining/elapsed");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 1);
        EXPECT(countdown_handler("device_countdown_timer_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected countdown pause to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL &&
               strstr(query_text, "\"remaining_sec\":90") != NULL &&
               strstr(query_text, "\"elapsed_sec\":0") != NULL,
               "expected countdown pause MCP result with remaining_sec and elapsed_sec");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 0);
    ty_cJSON_AddStringToObject(args, "id", "reminder-new");
    ty_cJSON_AddNumberToObject(args, "year", 2026);
    ty_cJSON_AddNumberToObject(args, "month", 3);
    ty_cJSON_AddNumberToObject(args, "day", 30);
    ty_cJSON_AddNumberToObject(args, "hour", 17);
    ty_cJSON_AddNumberToObject(args, "minute", 20);
    ty_cJSON_AddStringToObject(args, "message", "提醒喝水");
        EXPECT(schedule_handler("device_schedule_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected schedule add with time only to return tool result");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL,
               "expected schedule add with time only to succeed");

        EXPECT(test_reminder_add_called() == TRUE,
               "expected schedule add with time only to call reminder_add");

        EXPECT(test_last_reminder_add_start_time() == 1774862400,
               "expected schedule add with time only to default date to today");

        EXPECT(strcmp(test_last_reminder_add_message(), "提醒喝水") == 0,
               "expected schedule add with time only to preserve message");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    test_set_now(1000000);
    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 0);
    ty_cJSON_AddStringToObject(args, "id", "reminder-offset-shell");
    ty_cJSON_AddNumberToObject(args, "offset_minutes", 7);
    ty_cJSON_AddStringToObject(args, "message", "offset-msg");
        EXPECT(schedule_handler("device_schedule_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected schedule add with offset_minutes to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL &&
               strstr(query_text, "\"start_timestamp\":1000420") != NULL,
               "expected offset_minutes add to use device now + delta");

        EXPECT(test_last_reminder_add_start_time() == 1000420,
               "expected reminder start_time from offset_minutes");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    test_set_now(1774832400);

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 0);
        EXPECT(stopwatch_handler("device_stopwatch_timer_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected stopwatch start to succeed");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 0);
        EXPECT(stopwatch_handler("device_stopwatch_timer_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected duplicate stopwatch start tool call to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":false") != NULL &&
               strstr(query_text, "\"reason\":\"already_exists\"") != NULL &&
               strstr(query_text, "\"active\":true") != NULL,
               "expected duplicate stopwatch start to return already_exists with current snapshot");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    test_set_now(1774832500);
    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 5);
        EXPECT(stopwatch_handler("device_stopwatch_timer_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected stopwatch query to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL &&
               strstr(query_text, "\"elapsed_sec\":100") != NULL &&
               strstr(query_text, "\"paused\":false") != NULL,
               "expected stopwatch query to return elapsed_sec while running");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 1);
        EXPECT(stopwatch_handler("device_stopwatch_timer_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected stopwatch pause to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL &&
               strstr(query_text, "\"elapsed_sec\":100") != NULL,
               "expected stopwatch pause MCP result to include elapsed_sec");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 2);
        EXPECT(stopwatch_handler("device_stopwatch_timer_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected stopwatch resume to succeed");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    test_set_now(1774832510);
    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 5);
        EXPECT(stopwatch_handler("device_stopwatch_timer_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected stopwatch query after resume to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"elapsed_sec\":110") != NULL,
               "expected stopwatch query to reflect time after resume");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 3);
        EXPECT(stopwatch_handler("device_stopwatch_timer_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected stopwatch stop to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL &&
               strstr(query_text, "\"elapsed_sec\":110") != NULL,
               "expected stopwatch stop MCP result to include final elapsed_sec");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 5);
        EXPECT(stopwatch_handler("device_stopwatch_timer_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected stopwatch query when idle to return tool result");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL &&
               strstr(query_text, "\"active\":false") != NULL,
               "expected stopwatch query when idle to report inactive");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 0);
        EXPECT(pomodoro_handler("device_pomodoro_timer_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected pomodoro start tool call to succeed");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 4);
        EXPECT(pomodoro_handler("device_pomodoro_timer_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected pomodoro query tool call to succeed");

        EXPECT(out_content != NULL && out_content->child != NULL && out_content->child->valuestring != NULL,
               "expected pomodoro query tool call to return text content");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"active\":true") != NULL,
               "expected pomodoro query result to include active state");

        EXPECT(strstr(query_text, "\"phase\":1") != NULL &&
               strstr(query_text, "\"remaining_sec\":300") != NULL &&
               strstr(query_text, "\"work_sessions_before_long_break\":4") != NULL,
               "expected pomodoro query result to include runtime snapshot");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 0);
    ty_cJSON_AddNumberToObject(args, "current_cycle", 3);
        EXPECT(pomodoro_handler("device_pomodoro_timer_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected active pomodoro pseudo-update tool call to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":false") != NULL &&
               strstr(query_text, "\"reason\":\"already_exists\"") != NULL &&
               strstr(query_text, "\"remaining_sec\":300") != NULL &&
               strstr(query_text, "\"current_cycle\":2") != NULL,
               "expected active pomodoro pseudo-update to return current snapshot unchanged");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 0);
        EXPECT(pomodoro_handler("device_pomodoro_timer_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected duplicate pomodoro start tool call to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":false") != NULL &&
               strstr(query_text, "\"reason\":\"already_exists\"") != NULL &&
               strstr(query_text, "\"session_id\":7") != NULL,
               "expected duplicate pomodoro start to return already_exists with current snapshot");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);

    test_set_reminder_snapshot("reminder-2", 1774850400, "今天下午五点开会");
    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 2);
    ty_cJSON_AddStringToObject(args, "id", "reminder-2");
    ty_cJSON_AddNumberToObject(args, "year", 2026);
    ty_cJSON_AddNumberToObject(args, "month", 3);
    ty_cJSON_AddNumberToObject(args, "day", 30);
    ty_cJSON_AddNumberToObject(args, "hour", 18);
    ty_cJSON_AddNumberToObject(args, "minute", 30);
    ty_cJSON_AddStringToObject(args, "message", "今天下午六点半开会");
        EXPECT(schedule_handler("device_schedule_set", args, &out_content, &is_error, NULL) == OPRT_OK,
               "expected schedule update tool call to succeed");

        EXPECT(test_reminder_update_called() == TRUE,
               "expected schedule update to call reminder_update");

        EXPECT(strcmp(test_last_reminder_update_id(), "reminder-2") == 0,
               "expected schedule update to target the matched reminder id");

        EXPECT(test_last_reminder_update_start_time() == 1774866600,
               "expected schedule update to refresh reminder start_time");

        EXPECT(strcmp(test_last_reminder_update_message(), "今天下午六点半开会") == 0,
               "expected schedule update to pass new_message into reminder_update");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    TEST_END();
}
