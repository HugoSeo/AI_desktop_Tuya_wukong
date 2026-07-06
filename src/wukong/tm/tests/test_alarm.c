#include <stdio.h>
#include <string.h>

#include "wukong_test.h"
#include "wukong_tm.h"
#include "ty_cJSON.h"

/* observable state from stubs_alarm.c */
extern int g_alert_play_count;
extern int g_event_notify_count;
extern char g_last_cron_job_json[512];
extern char g_prev_cron_job_json[512];
extern char g_last_removed_cron_job_id[64];
extern char g_last_registered_method[64];
extern char g_last_unregistered_method[64];
extern int g_cron_job_add_count;
extern int g_cron_job_remove_count;

OPERATE_RET call_registered_cron_method(const CHAR_T *method,
                                        const CHAR_T *alarm_id,
                                        INT_T ring_seq);
BOOL_T is_cron_method_registered(const CHAR_T *method);

int main(void)
{
    WUKONG_TM_ALARM_CFG_T alarm_cfg = {0};
    CONST CHAR_T *alarm_id = "test-alarm-1";
    CONST CHAR_T *duplicate_alarm_id = "test-alarm-dup";
    UINT_T removed_count = 0;
    CHAR_T *alarm_list_json = NULL;
    CHAR_T find_id[WUKONG_TM_ALARM_ID_LEN + 1] = {0};

    alarm_cfg.enabled = TRUE;
    alarm_cfg.repeat_type = WUKONG_TM_ALARM_REPEAT_DAILY;
    alarm_cfg.hour = 7;
    alarm_cfg.minute = 30;
    strncpy(alarm_cfg.message, "赶紧去喝水",
            sizeof(alarm_cfg.message) - 1);

    /* --- init & add --- */
    EXPECT_OK(wukong_tm_alarm_init(), "tm alarm init should succeed");

    EXPECT_OK(wukong_tm_alarm_add(&alarm_cfg, alarm_id),
              "alarm add should succeed");

    EXPECT(is_cron_method_registered("alarm.fire"),
           "tm alarm should register alarm.fire");
    EXPECT(is_cron_method_registered("alarm.snooze.timeout"),
           "tm alarm should register snooze timeout method");
    EXPECT(is_cron_method_registered("alarm.snooze.fire"),
           "tm alarm should register snooze fire method");

    EXPECT_STR_CONTAINS(g_last_cron_job_json,
                        "\"method\":\"alarm.fire\"",
                        "alarm should map to alarm.fire");
    EXPECT_STR_CONTAINS(g_last_cron_job_json,
                        "\"message\":\"赶紧去喝水\"",
                        "alarm message should be stored in cron params");

    /* --- update --- */
    {
        WUKONG_TM_ALARM_CFG_T updated = alarm_cfg;
        updated.hour = 16;
        updated.minute = 50;
        strncpy(updated.message, "十六点五十的闹钟",
                sizeof(updated.message) - 1);

        EXPECT_OK(wukong_tm_alarm_update(alarm_id, &updated),
                  "alarm update should succeed");
        EXPECT(g_cron_job_remove_count >= 1,
               "alarm update should remove previous cron job");
        EXPECT_STR_EQ(g_last_removed_cron_job_id, "cron-job-1",
                      "alarm update should remove the previous cron job id");
        EXPECT_STR_CONTAINS(g_last_cron_job_json,
                            "\"cron\":\"0 50 16 * * *\"",
                            "alarm update should add the new cron expression");
    }

    /* --- list --- */
    EXPECT_OK(wukong_tm_alarm_list(&alarm_list_json),
              "alarm list should succeed");
    EXPECT_NOT_NULL(alarm_list_json,
                    "alarm list json should not be null");
    EXPECT_STR_CONTAINS(alarm_list_json,
                        "\"message\":\"十六点五十的闹钟\"",
                        "alarm list should export updated message");
    ty_cJSON_FreeBuffer(alarm_list_json);

    /* --- fire --- */
    EXPECT_OK(wukong_tm_alarm_fire(alarm_id),
              "tm alarm fire should succeed");
    EXPECT(g_event_notify_count >= 1,
           "tm alarm fire should emit event");
    EXPECT_STR_CONTAINS(g_last_cron_job_json,
                        "\"method\":\"alarm.snooze.timeout\"",
                        "alarm fire should create unanswered timeout cron");

    /* --- snooze timeout rpc --- */
    EXPECT_OK(call_registered_cron_method("alarm.snooze.timeout",
                                          alarm_id, 1),
              "timeout rpc should succeed");
    EXPECT_STR_CONTAINS(g_last_cron_job_json,
                        "\"method\":\"alarm.snooze.fire\"",
                        "timeout rpc should create snooze fire cron");
    EXPECT_STR_CONTAINS(g_prev_cron_job_json,
                        "\"method\":\"alarm.snooze.timeout\"",
                        "snooze fire cron scheduled after timeout cron");

    /* --- ack --- */
    {
        INT_T remove_count_before_ack = g_cron_job_remove_count;

        EXPECT_OK(wukong_tm_alarm_ack(alarm_id),
                  "alarm ack should succeed");
        EXPECT_EQ(g_cron_job_remove_count, remove_count_before_ack + 1,
                  "alarm ack should remove one pending snooze cron");
        EXPECT(g_last_removed_cron_job_id[0] != '\0',
               "alarm ack should record removed snooze cron id");
    }

    /* --- remove --- */
    EXPECT_OK(wukong_tm_alarm_remove(alarm_id),
              "tm alarm remove should succeed");
    EXPECT(strcmp(g_last_removed_cron_job_id, "cron-job-1") == 0 ||
           strcmp(g_last_removed_cron_job_id, "cron-job-2") == 0 ||
           strcmp(g_last_removed_cron_job_id, "cron-job-3") == 0,
           "tm alarm remove should delete one mapped cron job");

    /* --- snooze-delete regression --- */
    alarm_id = "test-alarm-snooze";
    EXPECT_OK(wukong_tm_alarm_add(&alarm_cfg, alarm_id),
              "alarm add for snooze-delete regression should succeed");
    EXPECT_OK(wukong_tm_alarm_fire(alarm_id),
              "alarm fire for snooze-delete regression should succeed");
    EXPECT_OK(call_registered_cron_method("alarm.snooze.timeout",
                                          alarm_id, 1),
              "timeout rpc for snooze-delete regression should succeed");
    {
        INT_T remove_count_before_delete = g_cron_job_remove_count;

        EXPECT_OK(wukong_tm_alarm_remove(alarm_id),
                  "alarm remove should succeed when snooze cron is pending");
        EXPECT_EQ(g_cron_job_remove_count,
                  remove_count_before_delete + 2,
                  "alarm remove should delete both snooze and main cron");
        EXPECT(g_last_removed_cron_job_id[0] != '\0',
               "alarm remove should record final removed cron id");
    }

    /* --- duplicate alarm removal --- */
    alarm_id = "test-alarm-dup1";
    EXPECT_OK(wukong_tm_alarm_add(&alarm_cfg, alarm_id),
              "first duplicate alarm add should succeed");

    duplicate_alarm_id = "test-alarm-dup2";
    EXPECT_OK(wukong_tm_alarm_add(&alarm_cfg, duplicate_alarm_id),
              "second duplicate alarm add should succeed");

    removed_count = 0;
    EXPECT_OK(wukong_tm_alarm_remove_by_time(&alarm_cfg, &removed_count),
              "remove by time should delete duplicate alarms");
    EXPECT_EQ(removed_count, 2,
              "remove by time should report two deleted alarms");
    EXPECT_ERR(wukong_tm_alarm_find_by_time(&alarm_cfg, find_id,
                                            sizeof(find_id)),
               OPRT_NOT_FOUND,
               "duplicate alarms should be gone after remove by time");

    /* --- ack active when nothing is ringing --- */
    EXPECT_ERR(wukong_tm_alarm_ack_active(), OPRT_NOT_FOUND,
               "ack active should report not found when no alarm ringing");

    /* --- deinit --- */
    EXPECT_OK(wukong_tm_alarm_deinit(),
              "tm alarm deinit should succeed");
    EXPECT_STR_EQ(g_last_unregistered_method, "alarm.snooze.fire",
                  "alarm deinit should unregister snooze fire method last");

    TEST_END();
}
