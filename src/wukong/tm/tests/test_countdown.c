/*
 * Countdown unit test migrated from test_wukong_tm_countdown.sh.
 * Uses wukong_test.h TAP framework with continue-on-failure.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "wukong_test.h"
#include "tal_time_service.h"
#include "wukong_cron.h"
#include "wukong_tm.h"
#include "ty_cJSON.h"

extern INT_T g_last_event_type;
extern INT_T g_event_count;
extern TIME_T g_fake_now;
extern INT_T g_cron_add_count;
extern INT_T g_cron_remove_count;
extern INT_T g_force_cron_add_fail;
extern INT_T g_local_time_convert_count;
extern CHAR_T g_last_cron_job_json[512];
extern CHAR_T g_last_removed_job_id[64];
extern CHAR_T g_registered_method[64];
extern WUKONG_CRON_RPC_HANDLER g_registered_handler;

static void build_expected_cron(TIME_T target_ts,
                                char *buffer, size_t buffer_len)
{
    POSIX_TM_S tm_info;

    if (tal_time_get_local_time_custom(target_ts, &tm_info)
        != OPRT_OK) {
        buffer[0] = '\0';
        return;
    }
    (void)snprintf(buffer, buffer_len, "%d %d %d %d %d *",
                   tm_info.tm_sec, tm_info.tm_min,
                   tm_info.tm_hour, tm_info.tm_mday,
                   tm_info.tm_mon + 1);
}

int main(void)
{
    char expected_cron[32] = {0};
    ty_cJSON *params = NULL;

    g_fake_now = 1735689600; /* 2025-01-01 00:00:00 UTC */

    /* --- init --- */
    EXPECT_OK(wukong_time_manage_init(),
              "countdown init should succeed");
    EXPECT_STR_EQ(g_registered_method, "tm.countdown.tick",
                  "countdown should register tick rpc method");

    /* --- create first countdown (1m30s) --- */
    EXPECT_OK(wukong_tm_countdown_create(0, 1, 30),
              "countdown create should succeed");
    EXPECT(g_last_event_type == 65 && g_event_count == 1,
           "countdown create should emit one countdown event");
    EXPECT_EQ(g_cron_add_count, 1,
              "countdown create should add one cron job");
    EXPECT(g_local_time_convert_count > 0,
           "countdown create should call tal_time_get_local_time_custom");
    build_expected_cron(g_fake_now + 10,
                        expected_cron, sizeof(expected_cron));
    EXPECT_STR_CONTAINS(g_last_cron_job_json, expected_cron,
        "countdown create should schedule the first 10-second tick");

    /* --- pause --- */
    g_fake_now += 20;
    EXPECT_OK(wukong_tm_countdown_pause(),
              "countdown pause should succeed");
    EXPECT_EQ(g_cron_remove_count, 1,
              "countdown pause should remove cron job");
    EXPECT_STR_EQ(g_last_removed_job_id, "countdown-job-1",
                  "countdown pause should remove the bound cron id");

    /* --- resume --- */
    g_fake_now += 30;
    EXPECT_OK(wukong_tm_countdown_resume(),
              "countdown resume should succeed");
    EXPECT_EQ(g_cron_add_count, 2,
              "countdown resume should re-add cron job");
    build_expected_cron(g_fake_now + 10,
                        expected_cron, sizeof(expected_cron));
    EXPECT_STR_CONTAINS(g_last_cron_job_json, expected_cron,
        "countdown resume should schedule next tick from remaining");

    /* --- tick RPC --- */
    g_fake_now += 10;
    params = ty_cJSON_CreateObject();
    ty_cJSON_AddNumberToObject(params, "handle", 1);
    EXPECT_NOT_NULL(g_registered_handler,
                    "tick handler should be registered");
    if (g_registered_handler != NULL) {
        EXPECT_OK(g_registered_handler(params, NULL),
                  "countdown tick rpc should succeed");
    }
    ty_cJSON_Delete(params);
    EXPECT_EQ(g_event_count, 4,
              "countdown tick should emit a progress event");
    build_expected_cron(g_fake_now + 10,
                        expected_cron, sizeof(expected_cron));
    EXPECT_STR_CONTAINS(g_last_cron_job_json, expected_cron,
        "countdown tick should keep 10-second cadence above 10s");

    /* --- delete --- */
    EXPECT_OK(wukong_tm_countdown_delete(),
              "countdown delete should succeed");
    EXPECT_EQ(g_cron_remove_count, 2,
              "countdown delete should remove cron job");

    /* --- second countdown: resume fail path --- */
    EXPECT_OK(wukong_tm_countdown_create(0, 0, 20),
              "second countdown create should succeed");
    EXPECT_OK(wukong_tm_countdown_pause(),
              "second countdown pause should succeed");
    g_force_cron_add_fail = 1;
    EXPECT(wukong_tm_countdown_resume() != OPRT_OK,
           "countdown resume should fail when cron add fails");
    g_force_cron_add_fail = 0;
    EXPECT_ERR(wukong_tm_countdown_delete(), OPRT_NOT_FOUND,
        "failed resume should not leave a zombie countdown");

    /* --- third countdown: expired resume folds to finish --- */
    EXPECT_OK(wukong_tm_countdown_create(0, 0, 5),
              "third countdown create should succeed");
    g_fake_now += 10;
    EXPECT_OK(wukong_tm_countdown_pause(),
              "expired countdown pause should still succeed");
    EXPECT_OK(wukong_tm_countdown_resume(),
              "expired countdown resume should fold into finish");
    EXPECT_ERR(wukong_tm_countdown_delete(), OPRT_NOT_FOUND,
        "resume with zero remaining should finish and clear");

    /* --- fourth countdown: 1-second cadence within 10s --- */
    EXPECT_OK(wukong_tm_countdown_create(0, 0, 10),
              "fourth countdown create should succeed");
    build_expected_cron(g_fake_now + 1,
                        expected_cron, sizeof(expected_cron));
    EXPECT_STR_CONTAINS(g_last_cron_job_json, expected_cron,
        "countdown within 10 seconds should use 1-second cadence");
    EXPECT_OK(wukong_tm_countdown_delete(),
              "fourth countdown delete should succeed");

    /* --- deinit --- */
    EXPECT_OK(wukong_time_manage_deinit(),
              "countdown deinit should succeed");

    TEST_END();
}
