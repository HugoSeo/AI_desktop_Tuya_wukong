#include <stdio.h>
#include <string.h>

#include "wukong_test.h"
#include "wukong_tm.h"
#include "ty_cJSON.h"
#include "wukong_cron.h"

extern INT_T  g_last_event_type;
extern TIME_T g_fake_now;
extern CHAR_T g_last_registered_method[64];
extern CHAR_T g_last_cron_job_json[512];
extern CHAR_T g_last_removed_job_id[64];
extern INT_T  g_cron_add_count;
extern INT_T  g_cron_remove_count;
extern WUKONG_CRON_RPC_HANDLER g_phase_end_handler;

static OPERATE_RET simulate_phase_end(UINT_T session_id)
{
    ty_cJSON *params = ty_cJSON_CreateObject();
    ty_cJSON *result = NULL;
    OPERATE_RET rt;
    ty_cJSON_AddNumberToObject(params, "session_id", (INT_T)session_id);
    rt = g_phase_end_handler(params, &result);
    ty_cJSON_Delete(params);
    if (result) ty_cJSON_Delete(result);
    return rt;
}

int main(void)
{
    WUKONG_TM_POMODORO_CFG_T cfg = {25, 5, 15, 4};
    WUKONG_TM_POMODORO_STATE_T state;
    int initial_add_count;

    /* ---- init ---- */
    EXPECT_OK(wukong_tm_pomodoro_init(), "init should succeed");
    EXPECT_STR_EQ(g_last_registered_method, "tm.pomodoro.phase_end",
                  "init should register phase_end method");
    EXPECT_NOT_NULL(g_phase_end_handler, "init should store RPC handler");

    /* ---- query before start ---- */
    EXPECT_ERR(wukong_tm_pomodoro_query(&state), OPRT_NOT_FOUND,
               "query before start should return not found");

    /* ---- start ---- */
    initial_add_count = g_cron_add_count;
    EXPECT_OK(wukong_tm_pomodoro_start(&cfg), "start should succeed");
    EXPECT_EQ(g_last_event_type, 67, "start should emit pomodoro event");
    EXPECT_EQ(g_cron_add_count, initial_add_count + 1,
              "start should schedule one cron job");
    EXPECT_STR_CONTAINS(g_last_cron_job_json, "tm.pomodoro.phase_end",
                        "cron job should target phase_end method");

    /* ---- query running ---- */
    EXPECT_OK(wukong_tm_pomodoro_query(&state), "query should succeed");
    EXPECT(state.active == TRUE && state.paused == FALSE, "should be active");
    EXPECT_EQ(state.phase, WUKONG_TM_POMODORO_PHASE_WORK,
              "should start in work phase");
    EXPECT_EQ(state.session_id, 1, "first session id should be 1");
    EXPECT_EQ(state.remaining_sec, 1500, "work=25min=1500s");
    EXPECT_EQ(state.phase_end_ts, 2500, "end_ts = 1000 + 1500");
    EXPECT(wukong_tm_pomodoro_start(&cfg) != OPRT_OK,
           "duplicate start should fail when one instance already exists");
    EXPECT_OK(wukong_tm_pomodoro_query(&state),
              "query after duplicate start");
    EXPECT(state.session_id == 1 &&
           state.phase == WUKONG_TM_POMODORO_PHASE_WORK,
           "duplicate start must not replace existing session");

    /* ---- time passes ---- */
    g_fake_now = 1120;
    EXPECT_OK(wukong_tm_pomodoro_query(&state), "query running");
    EXPECT_EQ(state.remaining_sec, 1380, "120s elapsed -> 1380 remaining");

    /* ---- pause ---- */
    initial_add_count = g_cron_remove_count;
    EXPECT_OK(wukong_tm_pomodoro_pause(), "pause should succeed");
    EXPECT_EQ(g_cron_remove_count, initial_add_count + 1,
              "pause should remove cron job");
    EXPECT_OK(wukong_tm_pomodoro_query(&state), "query paused");
    EXPECT(state.paused == TRUE, "should be paused");
    EXPECT_EQ(state.remaining_sec, 1380, "pause freezes remaining");
    EXPECT_EQ(state.phase_end_ts, 0, "phase_end_ts cleared on pause");

    /* ---- resume ---- */
    g_fake_now = 1300;
    initial_add_count = g_cron_add_count;
    EXPECT_OK(wukong_tm_pomodoro_resume(), "resume should succeed");
    EXPECT_EQ(g_cron_add_count, initial_add_count + 1,
              "resume should schedule new cron job");
    EXPECT_OK(wukong_tm_pomodoro_query(&state), "query resumed");
    EXPECT(state.paused == FALSE, "should be running after resume");
    EXPECT_EQ(state.remaining_sec, 1380, "remaining preserved after resume");
    EXPECT_EQ(state.phase_end_ts, 2680, "end_ts = 1300 + 1380");

    /* ---- simulate work phase end -> short break ---- */
    g_fake_now = 2680;
    initial_add_count = g_cron_add_count;
    EXPECT_OK(simulate_phase_end(1), "phase_end for work should succeed");
    EXPECT_EQ(g_cron_add_count, initial_add_count + 1,
              "phase transition should schedule new cron");

    EXPECT_OK(wukong_tm_pomodoro_query(&state), "query after transition");
    EXPECT_EQ(state.phase, WUKONG_TM_POMODORO_PHASE_SHORT_BREAK,
              "after work -> short_break");
    EXPECT_EQ(state.completed_work_count, 1, "one work completed");
    EXPECT_EQ(state.remaining_sec, 300, "short_break=5min=300s");
    EXPECT_EQ(state.current_cycle, 1, "still cycle 1");

    /* ---- simulate short break end -> work ---- */
    g_fake_now += 300;
    EXPECT_OK(simulate_phase_end(1),
              "phase_end for short_break should succeed");
    EXPECT_OK(wukong_tm_pomodoro_query(&state), "query after break end");
    EXPECT_EQ(state.phase, WUKONG_TM_POMODORO_PHASE_WORK,
              "after break -> work");
    EXPECT_EQ(state.current_cycle, 2, "advanced to cycle 2");

    /* ---- simulate 3 more work+short_break cycles to reach long break ---- */
    /* cycle 2: work end */
    g_fake_now += 1500;
    EXPECT_OK(simulate_phase_end(1), "cycle2 work end");
    EXPECT_OK(wukong_tm_pomodoro_query(&state), "q");
    EXPECT_EQ(state.completed_work_count, 2, "2 work done");
    EXPECT_EQ(state.phase, WUKONG_TM_POMODORO_PHASE_SHORT_BREAK,
              "short break");

    /* cycle 2: short break end */
    g_fake_now += 300;
    EXPECT_OK(simulate_phase_end(1), "cycle2 break end");

    /* cycle 3: work end */
    g_fake_now += 1500;
    EXPECT_OK(simulate_phase_end(1), "cycle3 work end");
    EXPECT_OK(wukong_tm_pomodoro_query(&state), "q");
    EXPECT_EQ(state.completed_work_count, 3, "3 work done");

    /* cycle 3: short break end */
    g_fake_now += 300;
    EXPECT_OK(simulate_phase_end(1), "cycle3 break end");

    /* cycle 4: work end -> should trigger LONG break */
    g_fake_now += 1500;
    EXPECT_OK(simulate_phase_end(1), "cycle4 work end");
    EXPECT_OK(wukong_tm_pomodoro_query(&state), "q");
    EXPECT_EQ(state.completed_work_count, 4, "4 work done");
    EXPECT_EQ(state.phase, WUKONG_TM_POMODORO_PHASE_LONG_BREAK,
              "4th work done -> long break");
    EXPECT_EQ(state.remaining_sec, 900, "long_break=15min=900s");

    /* ---- stop ---- */
    EXPECT_OK(wukong_tm_pomodoro_stop(), "stop should succeed");
    EXPECT_ERR(wukong_tm_pomodoro_query(&state), OPRT_NOT_FOUND,
               "gone after stop");

    /* ---- restart session id advances ---- */
    g_fake_now = 100000;
    EXPECT_OK(wukong_tm_pomodoro_start(&cfg), "restart ok");
    EXPECT_OK(wukong_tm_pomodoro_query(&state), "q restart");
    EXPECT_EQ(state.session_id, 2, "session id advanced to 2");

    /* ---- stale session_id ignored ---- */
    EXPECT_ERR(simulate_phase_end(1), OPRT_NOT_FOUND,
               "stale session should be rejected");

    /* ---- input validation ---- */
    EXPECT_OK(wukong_tm_pomodoro_stop(), "stop before validation tests");
    {
        WUKONG_TM_POMODORO_CFG_T bad = {0, 5, 15, 4};
        EXPECT_ERR(wukong_tm_pomodoro_start(&bad), OPRT_INVALID_PARM,
                   "zero work duration should be rejected");
    }
    {
        WUKONG_TM_POMODORO_CFG_T bad_iv = {25, 5, 15, 0};
        EXPECT_ERR(wukong_tm_pomodoro_start(&bad_iv), OPRT_INVALID_PARM,
                   "work_sessions_before_long_break 0 should be rejected");
    }
    {
        WUKONG_TM_POMODORO_CFG_T bad_iv = {25, 5, 15, 13};
        EXPECT_ERR(wukong_tm_pomodoro_start(&bad_iv), OPRT_INVALID_PARM,
                   "work_sessions_before_long_break above max rejected");
    }

    /* ---- long break after N=2 completed work phases ---- */
    {
        WUKONG_TM_POMODORO_CFG_T cfg_n2 = {25, 5, 15, 2};
        UINT_T sid_n2;

        EXPECT_OK(wukong_tm_pomodoro_start(&cfg_n2), "start with N=2");
        EXPECT_OK(wukong_tm_pomodoro_query(&state), "query N=2 start");
        sid_n2 = state.session_id;
        g_fake_now += 1500;
        EXPECT_OK(simulate_phase_end(sid_n2), "N=2 first work end");
        EXPECT_OK(wukong_tm_pomodoro_query(&state), "q");
        EXPECT_EQ(state.phase, WUKONG_TM_POMODORO_PHASE_SHORT_BREAK,
                  "N=2: first work -> short break");
        g_fake_now += 300;
        EXPECT_OK(simulate_phase_end(sid_n2), "N=2 short break end");
        g_fake_now += 1500;
        EXPECT_OK(simulate_phase_end(sid_n2), "N=2 second work end");
        EXPECT_OK(wukong_tm_pomodoro_query(&state), "q after 2nd work");
        EXPECT_EQ(state.phase, WUKONG_TM_POMODORO_PHASE_LONG_BREAK,
                  "N=2: second completed work -> long break");
        EXPECT_OK(wukong_tm_pomodoro_stop(), "stop N=2 session");
    }

    /* ---- deinit ---- */
    EXPECT_OK(wukong_tm_pomodoro_deinit(), "deinit should succeed");
    EXPECT_NULL(g_phase_end_handler, "deinit should unregister handler");

    TEST_END();
}
