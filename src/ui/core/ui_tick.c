#include "ui_tick.h"
#include "ui_state.h"
#include "ui_route.h"
#include "ui_comp_statusbar.h"
#include "tal_time_service.h"
#include "lvgl.h"

#define UI_TICK_PERIOD_MS       30
#define UI_TIME_CHECK_INTERVAL  (1000 / UI_TICK_PERIOD_MS)

static uint32_t s_time_check_cnt = 0;
static uint8_t  s_last_hour = 0xFF;
static uint8_t  s_last_min  = 0xFF;
static uint8_t  s_last_sec  = 0xFF;

static void ui_tick_cb(lv_timer_t *timer) {
    (void)timer;
    uint32_t dirty = ui_state_consume_dirty();

    /* Periodically poll local clock (~1s) and mark dirty if minute changed */
    if (++s_time_check_cnt >= UI_TIME_CHECK_INTERVAL) {
        s_time_check_cnt = 0;
        TIME_T posix = tal_time_get_posix();
        POSIX_TM_S tm;
        if (OPRT_OK == tal_time_get_local_time_custom(posix, &tm)) {
            uint8_t h = (uint8_t)tm.tm_hour;
            uint8_t m = (uint8_t)tm.tm_min;
            uint8_t s = (uint8_t)tm.tm_sec;
            /* Second-granular so pages can render seconds (e.g. call duration).
             * Statusbar / other SYSTEM consumers dedup, so the per-second tick
             * is effectively free for them. */
            if (h != s_last_hour || m != s_last_min || s != s_last_sec) {
                s_last_hour = h;
                s_last_min  = m;
                s_last_sec  = s;
                uint32_t ts = (uint32_t)h * 3600 + (uint32_t)m * 60 + (uint32_t)s;
                ui_state_set_timestamp(ts);
                dirty |= (1u << UI_STATE_GROUP_SYSTEM);
            }
        }
    }

    if (dirty) {
        if (dirty & (1u << UI_STATE_GROUP_SYSTEM)) {
            const ui_state_system_t *sys = ui_state_get_system();
            ui_comp_statusbar_props_t sb = {
                .hour = (uint8_t)(sys->timestamp / 3600 % 24),
                .minute = (uint8_t)(sys->timestamp / 60 % 60),
                .calendar_count = sys->calendar_count,
                .alarm_count = sys->alarm_count,
                .wifi_strength = sys->wifi_strength,
                .wifi_connected = sys->is_online,
                .battery_level = sys->battery_level,
                .is_charging = sys->is_charging,
            };
            ui_comp_statusbar_refresh(&sb);
        }
        ui_route_refresh_current(dirty);
    }
}

void ui_tick_init(void) {
    lv_timer_create(ui_tick_cb, UI_TICK_PERIOD_MS, NULL);
}
