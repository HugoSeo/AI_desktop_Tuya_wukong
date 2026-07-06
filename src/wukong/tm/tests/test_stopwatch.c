#include <stdio.h>
#include <string.h>

#include "wukong_test.h"
#include "wukong_tm.h"
#include "wukong_tm_internal.h"

extern int g_last_event_type;
extern unsigned char g_last_sw_tlv[48];
extern void stub_set_posix_time(TIME_T t);
extern void stub_advance_posix_time(TIME_T delta);

static int tlv_has_elapsed(UINT8_T expected_opr, UINT_T expected_sec)
{
    UINT16_T tag2 = 0;
    UINT16_T len2 = 0;
    UINT_T val = 0;

    if (g_last_sw_tlv[4] != expected_opr) {
        return 0;
    }
    memcpy(&tag2, g_last_sw_tlv + 5, sizeof(tag2));
    memcpy(&len2, g_last_sw_tlv + 7, sizeof(len2));
    memcpy(&val, g_last_sw_tlv + 9, sizeof(val));
    if (tag2 != (UINT16_T)WUKONG_TM_TAG_STOPWATCH_ELAPSED_SEC) {
        return 0;
    }
    if (len2 != sizeof(UINT_T)) {
        return 0;
    }
    return val == expected_sec ? 1 : 0;
}

int main(void)
{
    WUKONG_TM_STOPWATCH_STATE_T state = {0};
    stub_set_posix_time(1000000);

    EXPECT_OK(wukong_tm_stopwatch_init(), "init should succeed");
    EXPECT_OK(wukong_tm_stopwatch_start(), "start should succeed");
    EXPECT_EQ(g_last_event_type, 66, "start should emit event 66");
    EXPECT(wukong_tm_stopwatch_start() != OPRT_OK, "duplicate start should fail");

    stub_advance_posix_time(30);
    EXPECT_OK(wukong_tm_stopwatch_query(&state), "query should succeed");
    EXPECT_EQ(state.elapsed_sec, 30, "elapsed should be 30s after 30s");

    EXPECT_OK(wukong_tm_stopwatch_pause(), "pause should succeed");
    EXPECT(tlv_has_elapsed(1, 30), "pause TLV should carry elapsed_sec=30");
    stub_advance_posix_time(100);
    EXPECT_OK(wukong_tm_stopwatch_query(&state), "query when paused");
    EXPECT_EQ(state.elapsed_sec, 30, "elapsed stays 30s while paused");

    EXPECT_OK(wukong_tm_stopwatch_resume(), "resume should succeed");
    stub_advance_posix_time(20);
    EXPECT_OK(wukong_tm_stopwatch_query(&state), "query after resume");
    EXPECT_EQ(state.elapsed_sec, 50, "elapsed should be 50s (30+20)");

    EXPECT_OK(wukong_tm_stopwatch_stop(), "stop should succeed");
    EXPECT(tlv_has_elapsed(3, 50), "stop TLV should carry elapsed_sec=50");
    EXPECT_OK(wukong_tm_stopwatch_start(), "start again after stop");

    EXPECT_OK(wukong_tm_stopwatch_query(&state), "query new session");
    EXPECT_EQ(state.elapsed_sec, 0, "fresh start should have 0 elapsed");

    EXPECT_OK(wukong_tm_stopwatch_stop(), "final stop");
    EXPECT_OK(wukong_tm_stopwatch_deinit(), "deinit should succeed");

    TEST_END();
}
