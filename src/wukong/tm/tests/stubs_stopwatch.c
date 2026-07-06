#include "wukong_tm.h"
#include "wukong_ai_agent.h"
#include "tal_time_service.h"
#include "base_event.h"
#include "wukong_cron.h"

#include <string.h>
#include <time.h>

INT_T g_last_event_type = -1;
UINT8_T g_last_sw_tlv[48];
static TIME_T s_fake_posix = 0;

OPERATE_RET wukong_tm_alarm_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_alarm_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_reminder_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_reminder_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_countdown_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_countdown_deinit(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_pomodoro_init(VOID) { return OPRT_OK; }
OPERATE_RET wukong_tm_pomodoro_deinit(VOID) { return OPRT_OK; }

TIME_T tal_time_get_posix(VOID)
{
    if (s_fake_posix == 0) {
        s_fake_posix = (TIME_T)time(NULL);
    }
    return s_fake_posix;
}

void stub_set_posix_time(TIME_T t) { s_fake_posix = t; }
void stub_advance_posix_time(TIME_T delta) { s_fake_posix += delta; }

VOID wukong_ai_event_notify(WUKONG_AI_EVENT_TYPE_E type, VOID *data)
{
    if (type == WUKONG_AI_EVENT_CLOCK_MCP_STOPWATCH_TIMER && data != NULL) {
        memcpy(g_last_sw_tlv, data, sizeof(g_last_sw_tlv));
    }
    g_last_event_type = (INT_T)type;
}

OPERATE_RET ty_subscribe_event(INT_T event_id, CONST CHAR_T *name,
                               SUBSCRIBE_CALLBACK cb, INT_T type)
{
    (VOID)event_id; (VOID)name; (VOID)cb; (VOID)type;
    return OPRT_OK;
}

OPERATE_RET ty_unsubscribe_event(INT_T event_id, CONST CHAR_T *name,
                                 SUBSCRIBE_CALLBACK cb)
{
    (VOID)event_id; (VOID)name; (VOID)cb;
    return OPRT_OK;
}

OPERATE_RET tal_time_check_time_sync(VOID) { return OPRT_OK; }
OPERATE_RET tal_time_check_time_zone_sync(VOID) { return OPRT_OK; }
OPERATE_RET wukong_cron_time_ready_notify(VOID) { return OPRT_OK; }
