#include "wukong_test.h"
#include "wukong_tm.h"

int stub_get_subscribe_calls(void);
int stub_get_unsubscribe_calls(void);
int stub_get_time_ready_notify_calls(void);
void stub_set_time_sync_ready(int ready);
int stub_fire_time_sync_event(void);

int main(void)
{
    stub_set_time_sync_ready(1);

    EXPECT_OK(wukong_time_manage_init(), "time manage init should succeed");
    EXPECT_EQ(stub_get_subscribe_calls(), 1,
              "init should subscribe EVENT_TIME_SYNC");
    EXPECT_EQ(stub_get_time_ready_notify_calls(), 1,
              "init should notify cron when time ready");
    EXPECT_OK(stub_fire_time_sync_event(),
              "EVENT_TIME_SYNC callback should be callable");
    EXPECT_EQ(stub_get_time_ready_notify_calls(), 2,
              "callback should notify cron again");
    EXPECT_OK(wukong_time_manage_deinit(),
              "time manage deinit should succeed");
    EXPECT_EQ(stub_get_unsubscribe_calls(), 1,
              "deinit should unsubscribe EVENT_TIME_SYNC");

    TEST_END();
}
