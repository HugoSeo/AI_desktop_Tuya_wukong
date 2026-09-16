/*
 * MCP tool unit test — run via pytest test_suite.py -k mcp (see also test_mcp_tools.c).
 * Uses wukong_test.h TAP framework with continue-on-failure.
 */
#include <stdio.h>
#include <string.h>

#include "wukong_test.h"
#include "tool_tm.h"
#include "ty_cJSON.h"

/* observable state from stubs_mcp.c */
extern int g_skill_clock_schedule_called;
extern int g_skill_clock_schedule_query_called;
extern int g_time_manage_add_called;
extern int g_time_manage_remove_called;
extern int g_time_manage_update_called;
extern int g_time_manage_query_called;
extern int g_alarm_ack_called;
extern int g_alarm_add_called;
extern int g_alarm_update_called;
extern int g_alarm_remove_called;
extern int g_countdown_create_called;
extern int g_countdown_pause_called;
extern int g_countdown_resume_called;
extern int g_countdown_delete_called;
extern TIME_T g_last_reminder_find_time;
extern TIME_T g_legacy_reminder_find_time;
extern TIME_T g_last_removed_reminder_time;
extern char g_last_time_manage_message[128];
extern char g_last_alarm_message[128];
extern char g_last_alarm_id[64];
extern char g_last_reminder_id[64];
extern unsigned int g_last_update_alarm_hour;
extern unsigned int g_last_update_alarm_minute;
extern unsigned int g_last_update_alarm_repeat_type;
extern unsigned int g_last_alarm_add_repeat_type;
extern char g_last_tool_text[256];
extern char g_last_reminder_local_time[64];

OPERATE_RET call_registered_tool(const char *name,
                                 const ty_cJSON *args);

int main(void)
{
    ty_cJSON *alarm_args = ty_cJSON_CreateObject();
    ty_cJSON *args = ty_cJSON_CreateObject();
    ty_cJSON *query_args = ty_cJSON_CreateObject();
    ty_cJSON *update_args = ty_cJSON_CreateObject();
    ty_cJSON *reminder_update_args = ty_cJSON_CreateObject();
    ty_cJSON *reminder_delete_args = ty_cJSON_CreateObject();
    ty_cJSON *reminder_delete_by_time_args = ty_cJSON_CreateObject();
    ty_cJSON *alarm_delete_args = ty_cJSON_CreateObject();
    ty_cJSON *alarm_ack_args = ty_cJSON_CreateObject();
    ty_cJSON *countdown_create_args = ty_cJSON_CreateObject();
    ty_cJSON *countdown_pause_args = ty_cJSON_CreateObject();
    ty_cJSON *countdown_resume_args = ty_cJSON_CreateObject();
    ty_cJSON *countdown_delete_args = ty_cJSON_CreateObject();
    ty_cJSON *partial_alarm_args = ty_cJSON_CreateObject();
    ty_cJSON *reminder_time_only_args = ty_cJSON_CreateObject();

    EXPECT(alarm_args != NULL && args != NULL &&
           query_args != NULL && update_args != NULL &&
           reminder_update_args != NULL &&
           reminder_delete_args != NULL &&
           reminder_delete_by_time_args != NULL &&
           alarm_delete_args != NULL &&
           alarm_ack_args != NULL &&
           countdown_create_args != NULL &&
           countdown_pause_args != NULL &&
           countdown_resume_args != NULL &&
           countdown_delete_args != NULL &&
           partial_alarm_args != NULL &&
           reminder_time_only_args != NULL,
           "json objects allocated");

    EXPECT_OK(tool_tm_init(), "tm mcp init should succeed");

    /* --- alarm add --- */
    ty_cJSON_AddStringToObject(alarm_args, "action", "add");
    ty_cJSON_AddStringToObject(alarm_args, "id", "alarm-1");
    ty_cJSON_AddStringToObject(alarm_args, "start_time",
                               "2026-04-02T11:05:00+08:00");
    ty_cJSON_AddStringToObject(alarm_args, "repeat_type", "once");
    ty_cJSON_AddStringToObject(alarm_args, "message",
                               "\xe8\xb5\xb6\xe7\xb4\xa7\xe5\x8e"
                               "\xbb\xe5\x96\x9d\xe6\xb0\xb4");

    EXPECT_OK(call_registered_tool("device_alarm_set", alarm_args),
              "alarm add should succeed");
    EXPECT_EQ(g_alarm_add_called, 1,
              "alarm add should call tm alarm add");
    EXPECT_EQ(g_last_alarm_add_repeat_type, 0U,
              "alarm add should pass once repeat_type");

    EXPECT_STR_EQ(g_last_alarm_message,
                  "\xe8\xb5\xb6\xe7\xb4\xa7\xe5\x8e"
                  "\xbb\xe5\x96\x9d\xe6\xb0\xb4",
                  "alarm add should pass message into config");

    /* --- alarm update by id --- */
    ty_cJSON_AddStringToObject(update_args, "action", "update");
    ty_cJSON_AddStringToObject(update_args, "id", "alarm-1");
    ty_cJSON_AddStringToObject(update_args, "repeat_type", "daily");
    ty_cJSON_AddStringToObject(update_args, "message",
        "\xe6\xaf\x8f\xe6\x97\xa5\xe5\x8d\x81\xe5\x85\xad\xe7\x82\xb9"
        "\xe4\xb8\x89\xe5\x8d\x81\xe5\x88\x86\xe9\x97\xb9\xe9\x92\x9f");

    EXPECT_OK(call_registered_tool("device_alarm_set", update_args),
              "alarm update by id should succeed");
    EXPECT_EQ(g_alarm_update_called, 1,
              "alarm update should call tm alarm update");
    EXPECT_STR_EQ(g_last_alarm_id, "alarm-1",
                  "alarm update should pass alarm id");
    EXPECT_STR_EQ(g_last_alarm_message,
        "\xe6\xaf\x8f\xe6\x97\xa5\xe5\x8d\x81\xe5\x85\xad\xe7\x82\xb9"
        "\xe4\xb8\x89\xe5\x8d\x81\xe5\x88\x86\xe9\x97\xb9\xe9\x92\x9f",
        "alarm update should pass new message");
    EXPECT(g_last_update_alarm_hour == 11 &&
           g_last_update_alarm_minute == 5,
           "alarm partial update should preserve hour/minute");
    EXPECT_EQ(g_last_update_alarm_repeat_type, 1U,
              "alarm update should pass daily repeat_type");

    /* --- alarm message-only update --- */
    ty_cJSON_AddStringToObject(partial_alarm_args, "action", "update");
    ty_cJSON_AddStringToObject(partial_alarm_args, "id", "alarm-1");
    ty_cJSON_AddStringToObject(partial_alarm_args, "message",
        "\xe7\xac\xac\xe4\xb8\x89\xe6\xac\xa1\xe5\x8f\xaa"
        "\xe6\x94\xb9\xe6\x96\x87\xe6\xa1\x88");

    EXPECT_OK(call_registered_tool("device_alarm_set",
                                   partial_alarm_args),
              "alarm message-only update should succeed");
    EXPECT(g_last_update_alarm_hour == 11 &&
           g_last_update_alarm_minute == 5,
           "message-only update should keep prior hour/minute");
    EXPECT_STR_EQ(g_last_alarm_message,
        "\xe7\xac\xac\xe4\xb8\x89\xe6\xac\xa1\xe5\x8f\xaa"
        "\xe6\x94\xb9\xe6\x96\x87\xe6\xa1\x88",
        "message-only update should refresh message");

    /* --- alarm delete by id --- */
    ty_cJSON_AddStringToObject(alarm_delete_args, "action", "delete");
    ty_cJSON_AddStringToObject(alarm_delete_args, "id", "alarm-1");

    EXPECT_OK(call_registered_tool("device_alarm_delete",
                                   alarm_delete_args),
              "alarm delete by id should succeed");
    EXPECT_EQ(g_alarm_remove_called, 1,
              "alarm delete should call tm alarm remove");
    EXPECT_STR_EQ(g_last_alarm_id, "alarm-1",
                  "alarm delete should pass alarm id");

    /* --- schedule add --- */
    ty_cJSON_AddStringToObject(args, "action", "add");
    ty_cJSON_AddStringToObject(args, "id", "reminder-1");
    ty_cJSON_AddNumberToObject(args, "categories", 1);
    ty_cJSON_AddStringToObject(args, "start_time",
                               "2026-03-06T20:30:00+08:00");
    ty_cJSON_AddStringToObject(args, "message",
        "\xe8\xb5\xb6\xe7\xb4\xa7\xe5\x8e"
        "\xbb\xe5\x96\x9d\xe6\xb0\xb4");

    EXPECT_OK(call_registered_tool("device_schedule_set", args),
              "schedule add should succeed");
    EXPECT_EQ(g_time_manage_add_called, 1,
              "schedule add should call reminder add");
    EXPECT_EQ(g_skill_clock_schedule_called, 0,
              "schedule add should not call legacy skill_clock");
    EXPECT_STR_EQ(g_last_time_manage_message,
        "\xe8\xb5\xb6\xe7\xb4\xa7\xe5\x8e"
        "\xbb\xe5\x96\x9d\xe6\xb0\xb4",
        "schedule add should prefer message over description");
    EXPECT_STR_EQ(g_last_reminder_local_time,
                  "2026-03-06T20:30:00",
                  "schedule add should build local reminder datetime");
    EXPECT_STR_CONTAINS(g_last_tool_text,
                        "\"success\":true",
                        "schedule add should return success json");
    EXPECT_STR_CONTAINS(g_last_tool_text,
                        "\"start_time\":\"2026-03-06T20:30:00+08:00\"",
                        "schedule add should return start_time ISO 8601");

    /* --- reminder update with time-only (start_time change) --- */
    {
        ty_cJSON_AddStringToObject(reminder_time_only_args,
                                   "action", "update");
        ty_cJSON_AddNumberToObject(reminder_time_only_args,
                                   "categories", 1);
        ty_cJSON_AddStringToObject(reminder_time_only_args,
                                   "id", "reminder-1");
        ty_cJSON_AddStringToObject(reminder_time_only_args,
                                   "start_time", "2026-03-06T21:30:00+08:00");

        EXPECT_OK(call_registered_tool("device_schedule_set",
                                       reminder_time_only_args),
            "reminder update with start_time should succeed");
        EXPECT_STR_EQ(g_last_time_manage_message,
            "\xe8\xb5\xb6\xe7\xb4\xa7\xe5\x8e"
            "\xbb\xe5\x96\x9d\xe6\xb0\xb4",
            "reminder update without message should preserve text");
        EXPECT_STR_EQ(g_last_reminder_local_time,
                      "2026-03-06T21:30:00",
            "reminder update should use new start_time");
        EXPECT_STR_CONTAINS(g_last_tool_text,
                            "\"start_time\":\"2026-03-06T21:30:00+08:00\"",
            "time-only reminder update should return new "
            "start_time in result");
    }

    /* --- reminder update with new local date --- */
    ty_cJSON_AddStringToObject(reminder_update_args, "action", "update");
    ty_cJSON_AddNumberToObject(reminder_update_args, "categories", 1);
    ty_cJSON_AddStringToObject(reminder_update_args, "id",
                               "reminder-1");
    ty_cJSON_AddStringToObject(reminder_update_args, "start_time",
                               "2026-03-07T21:30:00+08:00");
    ty_cJSON_AddStringToObject(reminder_update_args, "message",
        "\xe5\x8e\xbb\xe5\x96\x9d\xe6\xb0\xb4");

    EXPECT_OK(call_registered_tool("device_schedule_set",
                                   reminder_update_args),
              "reminder update with new date should succeed");
    EXPECT_EQ(g_time_manage_update_called, 2,
              "reminder updates should call tm update twice total");
    EXPECT_STR_CONTAINS(g_last_tool_text, "\"success\":true",
        "reminder update should return success json");
    EXPECT_STR_CONTAINS(g_last_tool_text,
                        "\"start_time\":\"2026-03-07T21:30:00+08:00\"",
        "reminder update should return start_time ISO 8601");
    EXPECT_STR_EQ(g_last_reminder_local_time,
                  "2026-03-07T21:30:00",
        "reminder update should use replacement local date");
    EXPECT_STR_EQ(g_last_time_manage_message,
        "\xe5\x8e\xbb\xe5\x96\x9d\xe6\xb0\xb4",
        "reminder update should refresh message");

    /* --- reminder delete by id --- */
    ty_cJSON_AddStringToObject(reminder_delete_args, "action", "delete");
    ty_cJSON_AddStringToObject(reminder_delete_args, "id",
                               "reminder-1");

    EXPECT_OK(call_registered_tool("device_schedule_delete",
                                   reminder_delete_args),
              "reminder delete by id should succeed");
    EXPECT_EQ(g_time_manage_remove_called, 1,
              "reminder delete should call reminder remove");
    EXPECT_STR_EQ(g_last_reminder_id, "reminder-1",
                  "reminder delete should pass reminder id");

    /* --- reminder delete without id --- */
    ty_cJSON_AddStringToObject(reminder_delete_by_time_args,
                               "action", "delete");

    EXPECT_OK(call_registered_tool("device_schedule_set",
                                   reminder_delete_by_time_args),
              "reminder delete without id should return missing_id");
    EXPECT_STR_CONTAINS(g_last_tool_text,
                        "id' field is required",
        "reminder delete without id should report missing id reason");

    /* --- schedule query --- */
    ty_cJSON_AddStringToObject(query_args, "keyword",
        "\xe4\xb8\x8b\xe7\x8f\xad");

    EXPECT_OK(call_registered_tool("device_schedule_query",
                                   query_args),
              "schedule query should succeed");
    EXPECT_EQ(g_time_manage_query_called, 1,
              "schedule query should call reminder query");
    EXPECT_EQ(g_skill_clock_schedule_query_called, 0,
              "schedule query should not call legacy skill_clock");

    /* --- alarm ack --- */
    ty_cJSON_AddStringToObject(alarm_ack_args, "action", "ack");
    ty_cJSON_AddStringToObject(alarm_ack_args, "id", "alarm-1");

    EXPECT_OK(call_registered_tool("device_alarm_ack",
                                   alarm_ack_args),
              "alarm ack should succeed");
    EXPECT_EQ(g_alarm_ack_called, 1,
              "alarm ack should call tm alarm ack");

    /* --- countdown create --- */
    ty_cJSON_AddStringToObject(countdown_create_args, "action", "create");
    ty_cJSON_AddNumberToObject(countdown_create_args,
                               "minute_duration", 1);
    ty_cJSON_AddNumberToObject(countdown_create_args,
                               "second_duration", 30);

    EXPECT_OK(call_registered_tool("device_countdown_timer_set",
                                   countdown_create_args),
              "countdown create should succeed");
    EXPECT_EQ(g_countdown_create_called, 1,
              "countdown create should call tm countdown create");
    EXPECT_STR_EQ(g_last_tool_text, "true",
                  "countdown create should return success text");

    /* --- countdown pause --- */
    ty_cJSON_AddStringToObject(countdown_pause_args, "action", "pause");

    EXPECT_OK(call_registered_tool("device_countdown_timer_set",
                                   countdown_pause_args),
              "countdown pause should succeed");
    EXPECT_EQ(g_countdown_pause_called, 1,
              "countdown pause should call tm countdown pause");

    /* --- countdown resume --- */
    ty_cJSON_AddStringToObject(countdown_resume_args, "action", "resume");

    EXPECT_OK(call_registered_tool("device_countdown_timer_set",
                                   countdown_resume_args),
              "countdown resume should succeed");
    EXPECT_EQ(g_countdown_resume_called, 1,
              "countdown resume should call tm countdown resume");

    /* --- countdown delete --- */
    ty_cJSON_AddStringToObject(countdown_delete_args, "action", "delete");

    EXPECT_OK(call_registered_tool("device_countdown_timer_set",
                                   countdown_delete_args),
              "countdown delete should succeed");
    EXPECT_EQ(g_countdown_delete_called, 1,
              "countdown delete should call tm countdown delete");

    /* --- schedule add via offset_minutes (after reminder-1 tests: stub is single-slot) --- */
    {
        ty_cJSON *offset_args = ty_cJSON_CreateObject();

        EXPECT(offset_args != NULL, "offset_args allocated");
        ty_cJSON_AddStringToObject(offset_args, "action", "add");
        ty_cJSON_AddStringToObject(offset_args, "id", "reminder-offset-1");
        ty_cJSON_AddNumberToObject(offset_args, "offset_minutes", 3);
        ty_cJSON_AddStringToObject(offset_args, "message", "offset path");

        EXPECT_OK(call_registered_tool("device_schedule_set", offset_args),
                  "schedule add with offset_minutes should succeed");
        EXPECT_STR_CONTAINS(g_last_tool_text, "\"success\":true",
                            "offset schedule add should return success");
        EXPECT_STR_CONTAINS(g_last_tool_text, "\"start_time\":\"1970-01-01T08:19:40+08:00\"",
            "offset_minutes=3 should add 180s to stub now=1000");
        ty_cJSON_Delete(offset_args);
    }

    /* --- schedule add invalid offset_minutes --- */
    {
        ty_cJSON *bad_off = ty_cJSON_CreateObject();

        ty_cJSON_AddStringToObject(bad_off, "action", "add");
        ty_cJSON_AddStringToObject(bad_off, "id", "reminder-bad-off");
        ty_cJSON_AddNumberToObject(bad_off, "offset_minutes", 0);
        ty_cJSON_AddStringToObject(bad_off, "message", "x");

        EXPECT_OK(call_registered_tool("device_schedule_set", bad_off),
                  "schedule tool should return JSON for bad offset");
        EXPECT_STR_CONTAINS(g_last_tool_text, "offset_minutes' value must be greater than 0",
                            "offset_minutes=0 should fail with descriptive reason");
        ty_cJSON_Delete(bad_off);
    }

    /* --- cleanup --- */
    ty_cJSON_Delete(alarm_args);
    ty_cJSON_Delete(args);
    ty_cJSON_Delete(query_args);
    ty_cJSON_Delete(update_args);
    ty_cJSON_Delete(reminder_update_args);
    ty_cJSON_Delete(reminder_delete_args);
    ty_cJSON_Delete(reminder_delete_by_time_args);
    ty_cJSON_Delete(alarm_delete_args);
    ty_cJSON_Delete(alarm_ack_args);
    ty_cJSON_Delete(countdown_create_args);
    ty_cJSON_Delete(countdown_pause_args);
    ty_cJSON_Delete(countdown_resume_args);
    ty_cJSON_Delete(countdown_delete_args);
    ty_cJSON_Delete(partial_alarm_args);
    ty_cJSON_Delete(reminder_time_only_args);

    TEST_END();
}
