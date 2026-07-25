/*
 * Reminder unit tests — migrated from test_wukong_tm_reminder.sh
 * Uses wukong_test.h TAP framework (continue-on-failure).
 */
#include <stdio.h>
#include <string.h>

#include "wukong_test.h"
#include "wukong_tm.h"

/* Internal API not in public header */
OPERATE_RET wukong_tm_reminder_remove_by_time(TIME_T start_time,
                                              UINT_T *removed_count);

/* Tracking globals from stubs_reminder.c */
extern char g_last_cron_job_json[512];
extern char g_last_registered_method[64];
extern char g_last_removed_job_id[64];
extern char g_last_ai_send_text[1024];

int main(void)
{
    WUKONG_TM_REMINDER_CFG_T cfg;
    CHAR_T reminder_id[WUKONG_TM_REMINDER_ID_LEN + 1];
    CHAR_T dup_reminder_id[WUKONG_TM_REMINDER_ID_LEN + 1];
    UINT_T removed_count = 0;

    memset(&cfg, 0, sizeof(cfg));
    cfg.enabled = TRUE;
    cfg.start_time = 1772829000;
    strncpy(cfg.message, "8点30提醒您下班啦！",
            sizeof(cfg.message) - 1);

    /* -- init -- */
    EXPECT_OK(wukong_tm_reminder_init(),
              "reminder init should succeed");

    /* -- add -- */
    memset(reminder_id, 0, sizeof(reminder_id));
    strncpy(reminder_id, "test-reminder-1", sizeof(reminder_id) - 1);
    EXPECT_OK(wukong_tm_reminder_add(&cfg, reminder_id),
              "reminder add should succeed");

    EXPECT_STR_EQ(g_last_registered_method, "reminder.fire",
                  "reminder service should register reminder.fire");

    EXPECT_STR_CONTAINS(g_last_cron_job_json,
                        "\"method\":\"reminder.fire\"",
                        "reminder should map to reminder.fire");

    EXPECT_STR_CONTAINS(g_last_cron_job_json,
                        "\"message\":\"8点30提醒您下班啦！\"",
                        "reminder message should be in cron params");

    /* -- fire -- */
    EXPECT_OK(wukong_tm_reminder_fire(reminder_id),
              "reminder fire should succeed");

    EXPECT_STR_CONTAINS(g_last_ai_send_text,
                        "[SYS_TTS_NOTIFY]",
                        "fire should use TTS system prompt prefix");
    EXPECT_STR_CONTAINS(g_last_ai_send_text,
                        "8点30提醒您下班啦！",
                        "fire should send message to AI via send_text");

    EXPECT_STR_EQ(g_last_removed_job_id, "cron-reminder-1",
                  "fire should remove one-shot cron job");

    /* -- duplicate add + remove_by_time -- */
    memset(reminder_id, 0, sizeof(reminder_id));
    strncpy(reminder_id, "dup-reminder-1",
            sizeof(reminder_id) - 1);
    EXPECT_OK(wukong_tm_reminder_add(&cfg, reminder_id),
              "first duplicate reminder add should succeed");

    memset(dup_reminder_id, 0, sizeof(dup_reminder_id));
    strncpy(dup_reminder_id, "dup-reminder-2",
            sizeof(dup_reminder_id) - 1);
    EXPECT_OK(wukong_tm_reminder_add(&cfg, dup_reminder_id),
              "second duplicate reminder add should succeed");

    removed_count = 0;
    EXPECT_OK(wukong_tm_reminder_remove_by_time(cfg.start_time,
                                                &removed_count),
              "remove by time should delete duplicate reminders");

    EXPECT_EQ(removed_count, 2,
              "remove by time should report two deleted reminders");

    memset(reminder_id, 0, sizeof(reminder_id));
    EXPECT_ERR(wukong_tm_reminder_find_by_time(cfg.start_time,
                   reminder_id, sizeof(reminder_id)),
               OPRT_NOT_FOUND,
               "duplicates should be gone after remove by time");

    /* -- deinit -- */
    EXPECT_OK(wukong_tm_reminder_deinit(),
              "reminder deinit should succeed");

    TEST_END();
}
