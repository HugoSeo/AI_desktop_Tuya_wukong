/**
 * @file test_mcp_tools.c
 * @brief MCP tool schema + integration tests (from test_wukong_mcp_tm_tools.sh).
 * Run via pytest test_suite.py -k mcp_tools.
 */
#include <stdio.h>
#include <string.h>

#include "wukong_test.h"
#include "ty_cJSON.h"
#include "tool_tm.h"
#include "wukong_tool.h"
#include "wukong_tm.h"

int test_has_tool(const char *name);
const char *test_tool_description(const char *name);
const char *test_prop_description(const char *tool_name, const char *prop_name);
const char *test_prop_type(const char *tool_name, const char *prop_name);
const char *test_prop_enum_values(const char *tool_name, const char *prop_name);
BOOL_T test_prop_required(const char *tool_name, const char *prop_name);
WUKONG_TOOL_HANDLER_CB test_get_tool_handler(const char *name);
void test_set_now(TIME_T now);
void test_set_reminder_snapshot(const char *reminder_id, TIME_T start_time, const char *message);
void test_reset_alarm_stub(void);
BOOL_T test_reminder_add_called(void);
TIME_T test_last_reminder_add_start_time(void);
const CHAR_T *test_last_reminder_add_message(void);
BOOL_T test_reminder_update_called(void);
const CHAR_T *test_last_reminder_update_id(void);
TIME_T test_last_reminder_update_start_time(void);
const CHAR_T *test_last_reminder_update_message(void);
BOOL_T test_last_alarm_add_cfg_valid(void);
WUKONG_TM_REPEAT_TYPE_E test_last_alarm_add_repeat_type(void);
BOOL_T test_last_alarm_add_enabled(void);

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
    const char *schedule_start_time_desc = NULL;
    const char *schedule_id_desc = NULL;
    const char *schedule_new_hour_desc = NULL;
    const char *schedule_new_message_desc = NULL;
    const char *schedule_start_ts_set_desc = NULL;
    const char *schedule_query_start_desc = NULL;
    const char *schedule_query_end_desc = NULL;
    const char *schedule_query_start_time_desc = NULL;
    const char *schedule_repeat_type_desc = NULL;
    const char *schedule_weekday_mask_desc = NULL;
    const char *schedule_month_day_desc = NULL;
    const char *alarm_action_desc = NULL;
    const char *alarm_enabled_desc = NULL;
    const char *alarm_operation_desc = NULL;
    const char *countdown_action_enum = NULL;
    const char *stopwatch_action_enum = NULL;
    const char *pomodoro_action_enum = NULL;
    const char *schedule_action_enum = NULL;
    WUKONG_TOOL_HANDLER_CB countdown_handler = NULL;
    WUKONG_TOOL_HANDLER_CB alarm_handler = NULL;
    WUKONG_TOOL_HANDLER_CB stopwatch_handler = NULL;
    WUKONG_TOOL_HANDLER_CB pomodoro_handler = NULL;
    WUKONG_TOOL_HANDLER_CB pomodoro_control_handler = NULL;
    WUKONG_TOOL_HANDLER_CB schedule_handler = NULL;
    ty_cJSON *args = NULL;
    ty_cJSON *out_content = NULL;
    const char *query_text = NULL;

        EXPECT(tool_tm_init() == 0, "expected unified time-management init to succeed");


        EXPECT(test_has_tool("device_alarm_set"), "expected device_alarm_set to be registered");

        EXPECT(test_has_tool("device_alarm_query"), "expected device_alarm_query to be registered");

        EXPECT(test_has_tool("device_countdown_timer_set"), "expected device_countdown_timer_set to be registered");

        EXPECT(test_has_tool("device_stopwatch_timer_set"), "expected device_stopwatch_timer_set to be registered");

        EXPECT(test_has_tool("device_pomodoro_start"), "expected device_pomodoro_start to be registered");

        EXPECT(test_has_tool("device_pomodoro_control"), "expected device_pomodoro_control to be registered");

        EXPECT(test_has_tool("device_pomodoro_query"), "expected device_pomodoro_query to be registered");

        EXPECT(test_has_tool("device_schedule_set"), "expected device_schedule_set to be registered");

        EXPECT(test_has_tool("device_schedule_query"), "expected device_schedule_query to be registered");

        EXPECT(test_has_tool("device_alarm_delete"), "expected device_alarm_delete to be registered");

        EXPECT(test_has_tool("device_alarm_delete_all"), "expected device_alarm_delete_all to be registered");

        EXPECT(test_has_tool("device_alarm_ack"), "expected device_alarm_ack to be registered");

        EXPECT(test_has_tool("device_schedule_delete"), "expected device_schedule_delete to be registered");

        EXPECT(test_has_tool("device_schedule_delete_all"), "expected device_schedule_delete_all to be registered");

        EXPECT(test_has_tool("device_countdown_timer_query"), "expected device_countdown_timer_query to be registered");

        EXPECT(test_has_tool("device_stopwatch_timer_query"), "expected device_stopwatch_timer_query to be registered");


    alarm_desc = test_tool_description("device_alarm_set");
    alarm_query_desc = test_tool_description("device_alarm_query");
    schedule_desc = test_tool_description("device_schedule_set");
    schedule_query_desc = test_tool_description("device_schedule_query");
    alarm_message_desc = test_prop_description("device_alarm_set", "message");
    alarm_repeat_desc = test_prop_description("device_alarm_set", "repeat_type");
    alarm_enabled_desc = test_prop_description("device_alarm_set", "enabled");
    alarm_new_repeat_desc = test_prop_description("device_alarm_set", "new_repeat_type");
    alarm_action_desc = test_prop_description("device_alarm_set", "action");
    alarm_operation_desc = test_prop_description("device_alarm_set", "operation");
    countdown_action_enum = test_prop_enum_values("device_countdown_timer_set", "action");
    stopwatch_action_enum = test_prop_enum_values("device_stopwatch_timer_set", "action");
    pomodoro_action_enum = test_prop_enum_values("device_pomodoro_start", "action");
    schedule_action_enum = test_prop_enum_values("device_schedule_set", "action");
    schedule_message_desc = test_prop_description("device_schedule_set", "message");
    schedule_description_desc = test_prop_description("device_schedule_set", "description");
    schedule_start_time_desc = test_prop_description("device_schedule_set", "start_time");
    schedule_id_desc = test_prop_description("device_schedule_set", "id");
    schedule_new_hour_desc = test_prop_description("device_schedule_set", "new_hour");
    schedule_new_message_desc = test_prop_description("device_schedule_set", "new_message");
    schedule_start_ts_set_desc = test_prop_description("device_schedule_set", "start_timestamp");
    schedule_repeat_type_desc = test_prop_description("device_schedule_set", "repeat_type");
    schedule_weekday_mask_desc = test_prop_description("device_schedule_set", "weekday_mask");
    schedule_month_day_desc = test_prop_description("device_schedule_set", "month_day");
    schedule_query_start_desc = test_prop_description("device_schedule_query", "start_date");
    schedule_query_end_desc = test_prop_description("device_schedule_query", "end_date");
    schedule_query_start_time_desc = test_prop_description("device_schedule_query", "start_time");

        EXPECT(alarm_desc != NULL &&
               strstr(alarm_desc, "Manage local alarms") != NULL &&
               strstr(alarm_desc, "RINGTONE") != NULL &&
               strstr(alarm_desc, "device_schedule_set") != NULL &&
               strstr(alarm_desc, "repeat_type") != NULL &&
               strstr(alarm_desc, "weekday_mask") != NULL &&
               strstr(alarm_desc, "Update merges") != NULL,
               "expected alarm tool description to focus on alarms");

        /* Alarm add failure conditions must be described in the schema so the
         * model knows to handle id conflicts, time conflicts, and full slots. */
        EXPECT(alarm_desc != NULL &&
               strstr(alarm_desc, "id is taken") != NULL &&
               strstr(alarm_desc, "slots are full") != NULL,
               "expected alarm_set description to document add failure conditions");

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
               strstr(schedule_desc, "start_time") != NULL &&
               strstr(schedule_desc, "offset_minutes") != NULL &&
               strstr(schedule_desc, "start_time wins") != NULL &&
               strstr(schedule_desc, "repeat_type") != NULL &&
               strstr(schedule_desc, "Recurring") != NULL &&
               strstr(schedule_desc, "明天") == NULL &&
               strstr(schedule_desc, "小时和分钟") == NULL,
               "expected schedule tool description to focus on reminders and state start_time/repeat_type");

        EXPECT(schedule_query_desc != NULL &&
               strstr(schedule_query_desc, "Query spoken reminders") != NULL &&
               strstr(schedule_query_desc, "start_date") != NULL,
               "expected schedule query description to be in English and use start_date/end_date");

        EXPECT(alarm_message_desc != NULL &&
               strstr(alarm_message_desc, "Text note") != NULL &&
               strstr(alarm_message_desc, "query") != NULL,
               "expected alarm message field description to mention semantic record");

        EXPECT(alarm_repeat_desc != NULL &&
               strcmp(test_prop_type("device_alarm_set", "repeat_type"), "string") == 0 &&
               strstr(test_prop_enum_values("device_alarm_set", "repeat_type"), "once") != NULL &&
               strstr(test_prop_enum_values("device_alarm_set", "repeat_type"), "daily") != NULL &&
               strstr(test_prop_enum_values("device_alarm_set", "repeat_type"), "weekly") != NULL &&
               strstr(test_prop_enum_values("device_alarm_set", "repeat_type"), "monthly") != NULL &&
               strstr(alarm_repeat_desc, "daily") != NULL &&
               strstr(alarm_repeat_desc, "weekly") != NULL,
               "expected alarm repeat_type to be a string enum");

        EXPECT(alarm_enabled_desc != NULL &&
               strcmp(test_prop_type("device_alarm_set", "enabled"), "boolean") == 0,
               "expected alarm enabled to be boolean");

        EXPECT(alarm_new_repeat_desc == NULL,
               "expected alarm new_repeat_type field to be removed from schema");

        EXPECT(alarm_action_desc == NULL,
               "expected device_alarm_set to have no action field (actions are split tools)");

        EXPECT(test_prop_required("device_alarm_delete", "id") == TRUE,
               "expected device_alarm_delete to require id");

        EXPECT(test_prop_required("device_alarm_ack", "id") == TRUE,
               "expected device_alarm_ack to require id");

        EXPECT(alarm_operation_desc == NULL,
               "expected numeric operation to be removed from alarm schema");

        EXPECT(countdown_action_enum != NULL &&
               strstr(countdown_action_enum, "create") != NULL &&
               strstr(countdown_action_enum, "pause") != NULL &&
               strstr(countdown_action_enum, "resume") != NULL &&
               strstr(countdown_action_enum, "delete") != NULL,
               "expected countdown action enum values");

        EXPECT(test_prop_enum_values("device_countdown_timer_query", "action") != NULL &&
               strstr(test_prop_enum_values("device_countdown_timer_query", "action"), "query") != NULL,
               "expected device_countdown_timer_query to have query action");

        EXPECT(stopwatch_action_enum != NULL &&
               strstr(stopwatch_action_enum, "start") != NULL &&
               strstr(stopwatch_action_enum, "pause") != NULL &&
               strstr(stopwatch_action_enum, "resume") != NULL &&
               strstr(stopwatch_action_enum, "stop") != NULL &&
               strstr(stopwatch_action_enum, "reset") != NULL,
               "expected stopwatch action enum values");

        EXPECT(test_prop_enum_values("device_stopwatch_timer_query", "action") != NULL &&
               strstr(test_prop_enum_values("device_stopwatch_timer_query", "action"), "query") != NULL,
               "expected device_stopwatch_timer_query to have query action");

        EXPECT(pomodoro_action_enum != NULL &&
               strstr(pomodoro_action_enum, "start") != NULL,
               "expected device_pomodoro_start to have start action");

        EXPECT(test_prop_enum_values("device_pomodoro_control", "action") != NULL &&
               strstr(test_prop_enum_values("device_pomodoro_control", "action"), "pause") != NULL &&
               strstr(test_prop_enum_values("device_pomodoro_control", "action"), "resume") != NULL &&
               strstr(test_prop_enum_values("device_pomodoro_control", "action"), "stop") != NULL,
               "expected device_pomodoro_control to have pause/resume/stop actions");

        EXPECT(test_prop_enum_values("device_pomodoro_query", "action") != NULL &&
               strstr(test_prop_enum_values("device_pomodoro_query", "action"), "query") != NULL,
               "expected device_pomodoro_query to have query action");

        EXPECT(schedule_action_enum == NULL,
               "expected device_schedule_set to have no action field (actions are split tools)");

        EXPECT(test_prop_required("device_schedule_delete", "id") == TRUE,
               "expected device_schedule_delete to require id");

        EXPECT(schedule_message_desc != NULL &&
               strstr(schedule_message_desc, "Reminder message") != NULL &&
               strstr(schedule_message_desc, "description") == NULL,
               "expected schedule message field description to focus on message only");

        EXPECT(schedule_description_desc == NULL,
               "expected schedule description field to be removed");

        EXPECT(schedule_start_time_desc != NULL &&
               strstr(schedule_start_time_desc, "ISO 8601") != NULL,
               "expected schedule start_time field description to mention ISO 8601");

        EXPECT(schedule_id_desc != NULL &&
               strstr(schedule_id_desc, "identifier") != NULL,
               "expected schedule id field description");

        EXPECT(schedule_new_hour_desc == NULL,
               "expected schedule new_hour field to be removed");

        EXPECT(schedule_new_message_desc == NULL,
               "expected schedule new_message field to be removed");

        EXPECT(schedule_start_ts_set_desc == NULL,
               "expected schedule set start_timestamp field to be removed");

        EXPECT(schedule_repeat_type_desc != NULL &&
               strstr(schedule_repeat_type_desc, "Repeat") != NULL,
               "expected schedule repeat_type field description");

        EXPECT(schedule_weekday_mask_desc != NULL,
               "expected schedule weekday_mask field description");

        EXPECT(schedule_month_day_desc != NULL,
               "expected schedule month_day field description");

        EXPECT(test_prop_required("device_schedule_set", "categories") == FALSE,
               "expected schedule categories field to be optional");

        EXPECT(schedule_query_start_desc != NULL &&
               strstr(schedule_query_start_desc, "lower bound") != NULL,
               "expected schedule query start_date property");

        EXPECT(schedule_query_end_desc != NULL &&
               strstr(schedule_query_end_desc, "upper bound") != NULL,
               "expected schedule query end_date property");

        EXPECT(schedule_query_start_time_desc == NULL,
               "expected legacy start_time query property to be removed");


    countdown_handler = test_get_tool_handler("device_countdown_timer_set");
    alarm_handler = test_get_tool_handler("device_alarm_set");
    stopwatch_handler = test_get_tool_handler("device_stopwatch_timer_set");
    pomodoro_handler = test_get_tool_handler("device_pomodoro_start");
    pomodoro_control_handler = test_get_tool_handler("device_pomodoro_control");
    schedule_handler = test_get_tool_handler("device_schedule_set");
        EXPECT(countdown_handler != NULL, "expected countdown tool handler to be registered");

        EXPECT(alarm_handler != NULL, "expected alarm tool handler to be registered");

        EXPECT(stopwatch_handler != NULL, "expected stopwatch tool handler to be registered");

        EXPECT(pomodoro_handler != NULL, "expected pomodoro tool handler to be registered");

        EXPECT(pomodoro_control_handler != NULL, "expected pomodoro control tool handler to be registered");

        EXPECT(schedule_handler != NULL, "expected schedule tool handler to be registered");

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(args, "operation", 0);
        EXPECT(countdown_handler("device_countdown_timer_set", args, &out_content, NULL) == OPRT_OK,
               "expected legacy numeric operation call to return tool result");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":false") != NULL &&
               strstr(query_text, "action' field is required") != NULL,
               "expected legacy numeric operation to be rejected as missing action");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "remove");
        EXPECT(countdown_handler("device_countdown_timer_set", args, &out_content, NULL) == OPRT_OK,
               "expected invalid action call to return tool result");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":false") != NULL &&
               strstr(query_text, "action' value is not valid") != NULL,
               "expected invalid action to be rejected");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "add");
    ty_cJSON_AddStringToObject(args, "id", "alarm-daily-disabled");
    ty_cJSON_AddStringToObject(args, "start_time", "2026-03-30T07:30:00+08:00");
    ty_cJSON_AddStringToObject(args, "repeat_type", "daily");
    ty_cJSON_AddBoolToObject(args, "enabled", 0);
        EXPECT(alarm_handler("device_alarm_set", args, &out_content, NULL) == OPRT_OK,
               "expected alarm add with string repeat_type/enabled to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL,
               "expected alarm add with daily/disabled to succeed");
        EXPECT(test_last_alarm_add_cfg_valid() == TRUE,
               "expected alarm add to capture config");
        EXPECT(test_last_alarm_add_repeat_type() == WUKONG_TM_REPEAT_DAILY,
               "expected alarm add to parse daily repeat_type");
        EXPECT(test_last_alarm_add_enabled() == FALSE,
               "expected alarm add to parse enabled=false");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "add");
    ty_cJSON_AddStringToObject(args, "id", "alarm-bad-repeat");
    ty_cJSON_AddStringToObject(args, "start_time", "2026-03-30T08:00:00+08:00");
    ty_cJSON_AddStringToObject(args, "repeat_type", "yearly");
        EXPECT(alarm_handler("device_alarm_set", args, &out_content, NULL) == OPRT_OK,
               "expected invalid repeat_type call to return tool result");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":false") != NULL &&
               strstr(query_text, "alarm fields are invalid") != NULL,
               "expected invalid repeat_type to be rejected");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "add");
    ty_cJSON_AddStringToObject(args, "id", "alarm-bad-enabled");
    ty_cJSON_AddStringToObject(args, "start_time", "2026-04-02T09:00:00+08:00");
    ty_cJSON_AddStringToObject(args, "repeat_type", "once");
    ty_cJSON_AddStringToObject(args, "enabled", "maybe");
        EXPECT(alarm_handler("device_alarm_set", args, &out_content, NULL) == OPRT_OK,
               "expected invalid enabled call to return tool result");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":false") != NULL &&
               strstr(query_text, "alarm fields are invalid") != NULL,
               "expected invalid enabled to be rejected");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    test_set_now(1774832400);

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "create");
    ty_cJSON_AddNumberToObject(args, "hour_duration", 0);
    ty_cJSON_AddNumberToObject(args, "minute_duration", 1);
    ty_cJSON_AddNumberToObject(args, "second_duration", 30);
        EXPECT(countdown_handler("device_countdown_timer_set", args, &out_content, NULL) == OPRT_OK,
               "expected countdown create to succeed");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "create");
    ty_cJSON_AddNumberToObject(args, "hour_duration", 0);
    ty_cJSON_AddNumberToObject(args, "minute_duration", 2);
    ty_cJSON_AddNumberToObject(args, "second_duration", 0);
        EXPECT(countdown_handler("device_countdown_timer_set", args, &out_content, NULL) == OPRT_OK,
               "expected duplicate countdown create tool call to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":false") != NULL &&
               strstr(query_text, "identical item already exists") != NULL,
               "expected duplicate countdown create to return already_exists");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "query");
        EXPECT(countdown_handler("device_countdown_timer_set", args, &out_content, NULL) == OPRT_OK,
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
    ty_cJSON_AddStringToObject(args, "action", "pause");
        EXPECT(countdown_handler("device_countdown_timer_set", args, &out_content, NULL) == OPRT_OK,
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
    ty_cJSON_AddStringToObject(args, "action", "add");
    ty_cJSON_AddStringToObject(args, "id", "reminder-new");
    ty_cJSON_AddStringToObject(args, "start_time", "2026-03-30T17:20:00+08:00");
    ty_cJSON_AddStringToObject(args, "message", "提醒喝水");
        EXPECT(schedule_handler("device_schedule_set", args, &out_content, NULL) == OPRT_OK,
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
    ty_cJSON_AddStringToObject(args, "action", "add");
    ty_cJSON_AddStringToObject(args, "id", "reminder-offset-shell");
    ty_cJSON_AddNumberToObject(args, "offset_minutes", 7);
    ty_cJSON_AddStringToObject(args, "message", "offset-msg");
        EXPECT(schedule_handler("device_schedule_set", args, &out_content, NULL) == OPRT_OK,
               "expected schedule add with offset_minutes to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL &&
               strstr(query_text, "\"start_time\":\"1970-01-12T21:53:40+08:00\"") != NULL,
               "expected offset_minutes add to use device now + delta");

        EXPECT(test_last_reminder_add_start_time() == 1000420,
               "expected reminder start_time from offset_minutes");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    /* -- schedule add: absolute time wins when both offset_minutes and
     *    start_time are provided (reproduces "每月一号提醒在9号执行" bug) -- */
    test_set_now(1774832400);  /* 2026-03-27 16:00:00 UTC = 2026-03-28 00:00 CST */
    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "add");
    ty_cJSON_AddStringToObject(args, "id", "reminder-both");
    ty_cJSON_AddNumberToObject(args, "offset_minutes", 7);  /* would give 1774832820 if used */
    ty_cJSON_AddStringToObject(args, "start_time", "2026-03-30T17:20:00+08:00");
    ty_cJSON_AddStringToObject(args, "message", "absolute-wins");
        EXPECT(schedule_handler("device_schedule_set", args, &out_content, NULL) == OPRT_OK,
               "expected schedule add with both offset and absolute time to return tool result");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL,
               "expected schedule add with both offset and absolute time to succeed");

        EXPECT(test_last_reminder_add_start_time() == 1774862400,
               "expected absolute time to win over offset_minutes when both provided");

        EXPECT(strcmp(test_last_reminder_add_message(), "absolute-wins") == 0,
               "expected message preserved when absolute time wins");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    /* -- schedule add: absolute time wins even when offset_minutes=1 (AI model
     *    "fix" pattern: first call offset_minutes=0 fails, second call uses
     *    offset_minutes=1 but keeps start_time) -- */
    test_set_now(1780989710);  /* 2026-06-09 15:21:50 UTC+8, same as bug report */
    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "add");
    ty_cJSON_AddStringToObject(args, "id", "monthly-first-day");
    ty_cJSON_AddNumberToObject(args, "offset_minutes", 1);  /* would give June 9th if used */
    ty_cJSON_AddStringToObject(args, "start_time", "2026-07-01T08:00:00+08:00");
    ty_cJSON_AddStringToObject(args, "message", "每月一号提醒");
        EXPECT(schedule_handler("device_schedule_set", args, &out_content, NULL) == OPRT_OK,
               "expected schedule add with offset_minutes=1 and July 1st absolute time to return tool result");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL,
               "expected schedule add to succeed when both provided");

        EXPECT(test_last_reminder_add_start_time() == 1782864000,
               "expected absolute July 1st 08:00 CST, NOT now+60s June 9th");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    test_set_now(1774832400);

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "start");
        EXPECT(stopwatch_handler("device_stopwatch_timer_set", args, &out_content, NULL) == OPRT_OK,
               "expected stopwatch start to succeed");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "start");
        EXPECT(stopwatch_handler("device_stopwatch_timer_set", args, &out_content, NULL) == OPRT_OK,
               "expected duplicate stopwatch start tool call to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":false") != NULL &&
               strstr(query_text, "identical item already exists") != NULL &&
               strstr(query_text, "\"active\":true") != NULL,
               "expected duplicate stopwatch start to return already_exists with current snapshot");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    test_set_now(1774832500);
    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "query");
        EXPECT(stopwatch_handler("device_stopwatch_timer_set", args, &out_content, NULL) == OPRT_OK,
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
    ty_cJSON_AddStringToObject(args, "action", "pause");
        EXPECT(stopwatch_handler("device_stopwatch_timer_set", args, &out_content, NULL) == OPRT_OK,
               "expected stopwatch pause to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL &&
               strstr(query_text, "\"elapsed_sec\":100") != NULL,
               "expected stopwatch pause MCP result to include elapsed_sec");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "resume");
        EXPECT(stopwatch_handler("device_stopwatch_timer_set", args, &out_content, NULL) == OPRT_OK,
               "expected stopwatch resume to succeed");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    test_set_now(1774832510);
    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "query");
        EXPECT(stopwatch_handler("device_stopwatch_timer_set", args, &out_content, NULL) == OPRT_OK,
               "expected stopwatch query after resume to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"elapsed_sec\":110") != NULL,
               "expected stopwatch query to reflect time after resume");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "stop");
        EXPECT(stopwatch_handler("device_stopwatch_timer_set", args, &out_content, NULL) == OPRT_OK,
               "expected stopwatch stop to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL &&
               strstr(query_text, "\"elapsed_sec\":110") != NULL,
               "expected stopwatch stop MCP result to include final elapsed_sec");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "query");
        EXPECT(stopwatch_handler("device_stopwatch_timer_set", args, &out_content, NULL) == OPRT_OK,
               "expected stopwatch query when idle to return tool result");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL &&
               strstr(query_text, "\"active\":false") != NULL,
               "expected stopwatch query when idle to report inactive");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "start");
        EXPECT(pomodoro_handler("device_pomodoro_timer", args, &out_content, NULL) == OPRT_OK,
               "expected pomodoro start tool call to succeed");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "query");
        EXPECT(pomodoro_handler("device_pomodoro_timer", args, &out_content, NULL) == OPRT_OK,
               "expected pomodoro query tool call to succeed");

        EXPECT(out_content != NULL && out_content->child != NULL && out_content->child->valuestring != NULL,
               "expected pomodoro query tool call to return text content");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"active\":true") != NULL,
               "expected pomodoro query result to include active state");

        EXPECT(strstr(query_text, "\"phase\":\"short_break\"") != NULL &&
               strstr(query_text, "\"remaining_sec\":300") != NULL &&
               strstr(query_text, "\"work_sessions_before_long_break\":4") != NULL,
               "expected pomodoro query result to include runtime snapshot");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    /* -- pomodoro control: pause must return structured JSON with remaining_sec -- */
    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "pause");
        EXPECT(pomodoro_control_handler("device_pomodoro_control", args, &out_content, NULL) == OPRT_OK,
               "expected pomodoro control pause to succeed");

        EXPECT(out_content != NULL && out_content->child != NULL && out_content->child->valuestring != NULL,
               "expected pomodoro control pause to return text content");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL &&
               strstr(query_text, "\"action\":\"pause\"") != NULL &&
               strstr(query_text, "\"remaining_sec\":300") != NULL &&
               strstr(query_text, "\"phase\":\"short_break\"") != NULL,
               "expected pomodoro pause to return structured JSON with remaining_sec and phase");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    /* -- pomodoro control: resume must also return structured JSON -- */
    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "resume");
        EXPECT(pomodoro_control_handler("device_pomodoro_control", args, &out_content, NULL) == OPRT_OK,
               "expected pomodoro control resume to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL &&
               strstr(query_text, "\"action\":\"resume\"") != NULL &&
               strstr(query_text, "\"remaining_sec\"") != NULL,
               "expected pomodoro resume to return structured JSON with remaining_sec");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    /* -- pomodoro control: stop returns inactive (instance destroyed) -- */
    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "stop");
        EXPECT(pomodoro_control_handler("device_pomodoro_control", args, &out_content, NULL) == OPRT_OK,
               "expected pomodoro control stop to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL &&
               strstr(query_text, "\"action\":\"stop\"") != NULL &&
               strstr(query_text, "\"active\":false") != NULL,
               "expected pomodoro stop to return success with active:false");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    /* -- restart pomodoro for subsequent duplicate-start tests -- */
    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "start");
        EXPECT(pomodoro_handler("device_pomodoro_timer", args, &out_content, NULL) == OPRT_OK,
               "expected pomodoro restart after stop to succeed");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "start");
    ty_cJSON_AddNumberToObject(args, "current_cycle", 3);
        EXPECT(pomodoro_handler("device_pomodoro_timer", args, &out_content, NULL) == OPRT_OK,
               "expected active pomodoro pseudo-update tool call to succeed");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "start");
        EXPECT(pomodoro_handler("device_pomodoro_timer", args, &out_content, NULL) == OPRT_OK,
               "expected duplicate pomodoro start tool call to succeed");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":false") != NULL &&
               strstr(query_text, "identical item already exists") != NULL &&
               strstr(query_text, "\"session_id\":7") != NULL,
               "expected duplicate pomodoro start to return already_exists with current snapshot");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);

    test_set_reminder_snapshot("reminder-2", 1774850400, "今天下午五点开会");
    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "update");
    ty_cJSON_AddStringToObject(args, "id", "reminder-2");
    ty_cJSON_AddStringToObject(args, "start_time", "2026-03-30T18:30:00+08:00");
    ty_cJSON_AddStringToObject(args, "message", "今天下午六点半开会");
        EXPECT(schedule_handler("device_schedule_set", args, &out_content, NULL) == OPRT_OK,
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
    out_content = NULL;

    /* -- NEW: schedule add with start_time ISO 8601 + repeat_type daily -- */
    test_set_now(1774832400);
    test_set_reminder_snapshot(NULL, 0, NULL);
    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "add");
    ty_cJSON_AddStringToObject(args, "id", "reminder-daily");
    ty_cJSON_AddStringToObject(args, "start_time", "2026-03-30T10:00:00+08:00");
    ty_cJSON_AddStringToObject(args, "repeat_type", "daily");
    ty_cJSON_AddStringToObject(args, "message", "每天喝水");
        EXPECT(schedule_handler("device_schedule_set", args, &out_content, NULL) == OPRT_OK,
               "expected schedule add with daily repeat_type to return tool result");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL,
               "expected schedule add with daily repeat to succeed");

        EXPECT(test_reminder_add_called() == TRUE,
               "expected schedule add with daily repeat to call reminder_add");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    /* -- NEW: schedule query with start_date/end_date -- */
    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "start_date", "2026-03-30");
    ty_cJSON_AddStringToObject(args, "end_date", "2026-03-30");
    {
        WUKONG_TOOL_HANDLER_CB query_handler = test_get_tool_handler("device_schedule_query");
        EXPECT(query_handler != NULL, "expected schedule query handler");
        EXPECT(query_handler("device_schedule_query", args, &out_content, NULL) == OPRT_OK,
               "expected schedule query with start_date/end_date to succeed");

        query_text = out_content->child->valuestring;
        EXPECT(query_text != NULL,
               "expected schedule query result text");
    }
    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    /* -- NEW: alarm add with start_time ISO 8601 -- */
    test_set_now(1774832400);
    test_reset_alarm_stub();
    args = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(args, "action", "add");
    ty_cJSON_AddStringToObject(args, "id", "alarm-iso8601");
    ty_cJSON_AddStringToObject(args, "start_time", "2026-06-15T07:00:00+08:00");
    ty_cJSON_AddStringToObject(args, "repeat_type", "once");
    ty_cJSON_AddBoolToObject(args, "enabled", 1);
        EXPECT(alarm_handler("device_alarm_set", args, &out_content, NULL) == OPRT_OK,
               "expected alarm add with ISO 8601 start_time to return tool result");

    query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL,
               "expected alarm add with ISO 8601 start_time to succeed");
        EXPECT(test_last_alarm_add_cfg_valid() == TRUE,
               "expected alarm add with ISO 8601 to capture config");
        EXPECT(test_last_alarm_add_repeat_type() == WUKONG_TM_REPEAT_ONCE,
               "expected alarm add with once repeat_type from ISO 8601");
        EXPECT(test_last_alarm_add_enabled() == TRUE,
               "expected alarm add to parse enabled=true");

    ty_cJSON_Delete(args);
    ty_cJSON_Delete(out_content);
    out_content = NULL;

    /* -- device_alarm_delete: missing id returns missing_id -- */
    {
        WUKONG_TOOL_HANDLER_CB alarm_delete_handler = test_get_tool_handler("device_alarm_delete");
        EXPECT(alarm_delete_handler != NULL, "expected device_alarm_delete handler registered");

        args = ty_cJSON_CreateObject();
        EXPECT(alarm_delete_handler("device_alarm_delete", args, &out_content, NULL) == OPRT_OK,
               "expected device_alarm_delete with missing id to return result");
        query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":false") != NULL &&
               strstr(query_text, "id' field is required") != NULL,
               "expected device_alarm_delete with missing id to return missing_id");
        ty_cJSON_Delete(args);
        ty_cJSON_Delete(out_content);
        out_content = NULL;

        /* valid id -> success */
        args = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(args, "id", "alarm-del-1");
        EXPECT(alarm_delete_handler("device_alarm_delete", args, &out_content, NULL) == OPRT_OK,
               "expected device_alarm_delete with valid id to succeed");
        query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL,
               "expected device_alarm_delete with valid id to return success");
        ty_cJSON_Delete(args);
        ty_cJSON_Delete(out_content);
        out_content = NULL;
    }

    /* -- device_alarm_delete_all: returns success with removed_count -- */
    {
        WUKONG_TOOL_HANDLER_CB alarm_delete_all_handler = test_get_tool_handler("device_alarm_delete_all");
        EXPECT(alarm_delete_all_handler != NULL, "expected device_alarm_delete_all handler registered");

        args = ty_cJSON_CreateObject();
        EXPECT(alarm_delete_all_handler("device_alarm_delete_all", args, &out_content, NULL) == OPRT_OK,
               "expected device_alarm_delete_all to succeed");
        query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL &&
               strstr(query_text, "\"removed_count\"") != NULL,
               "expected device_alarm_delete_all to return success with removed_count");
        ty_cJSON_Delete(args);
        ty_cJSON_Delete(out_content);
        out_content = NULL;
    }

    /* -- device_alarm_ack: acknowledge ringing alarm -- */
    {
        WUKONG_TOOL_HANDLER_CB alarm_ack_handler = test_get_tool_handler("device_alarm_ack");
        EXPECT(alarm_ack_handler != NULL, "expected device_alarm_ack handler registered");

        args = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(args, "id", "alarm-ringing-1");
        EXPECT(alarm_ack_handler("device_alarm_ack", args, &out_content, NULL) == OPRT_OK,
               "expected device_alarm_ack with valid id to succeed");
        query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL,
               "expected device_alarm_ack to return success");
        ty_cJSON_Delete(args);
        ty_cJSON_Delete(out_content);
        out_content = NULL;
    }

    /* -- device_schedule_delete: missing id returns missing_id -- */
    {
        WUKONG_TOOL_HANDLER_CB sched_delete_handler = test_get_tool_handler("device_schedule_delete");
        EXPECT(sched_delete_handler != NULL, "expected device_schedule_delete handler registered");

        args = ty_cJSON_CreateObject();
        EXPECT(sched_delete_handler("device_schedule_delete", args, &out_content, NULL) == OPRT_OK,
               "expected device_schedule_delete with missing id to return result");
        query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":false") != NULL &&
               strstr(query_text, "id' field is required") != NULL,
               "expected device_schedule_delete with missing id to return missing_id");
        ty_cJSON_Delete(args);
        ty_cJSON_Delete(out_content);
        out_content = NULL;

        /* valid id -> success */
        args = ty_cJSON_CreateObject();
        ty_cJSON_AddStringToObject(args, "id", "reminder-del-1");
        EXPECT(sched_delete_handler("device_schedule_delete", args, &out_content, NULL) == OPRT_OK,
               "expected device_schedule_delete with valid id to succeed");
        query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL,
               "expected device_schedule_delete with valid id to return success");
        ty_cJSON_Delete(args);
        ty_cJSON_Delete(out_content);
        out_content = NULL;
    }

    /* -- device_schedule_delete_all: returns success with removed_count -- */
    {
        WUKONG_TOOL_HANDLER_CB sched_delete_all_handler = test_get_tool_handler("device_schedule_delete_all");
        EXPECT(sched_delete_all_handler != NULL, "expected device_schedule_delete_all handler registered");

        args = ty_cJSON_CreateObject();
        EXPECT(sched_delete_all_handler("device_schedule_delete_all", args, &out_content, NULL) == OPRT_OK,
               "expected device_schedule_delete_all to succeed");
        query_text = out_content->child->valuestring;
        EXPECT(strstr(query_text, "\"success\":true") != NULL &&
               strstr(query_text, "\"removed_count\"") != NULL,
               "expected device_schedule_delete_all to return success with removed_count");
        ty_cJSON_Delete(args);
        ty_cJSON_Delete(out_content);
        out_content = NULL;
    }

    TEST_END();
}
