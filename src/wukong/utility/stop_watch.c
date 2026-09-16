#include "stop_watch.h"
#include <string.h>

STATIC VOID __restart(VOID *p_sw)
{
    stop_watch *sw = (stop_watch *)p_sw;
    sw->_begin_time = tal_system_get_millisecond();
    sw->_end_time = 0;
}

STATIC VOID __stop(VOID *p_sw)
{
    stop_watch *sw = (stop_watch *)p_sw;
    sw->_end_time = tal_system_get_millisecond();
}

STATIC SYS_TIME_T __elapsed_ms(VOID *p_sw)
{
    stop_watch *sw = (stop_watch *)p_sw;
    if (sw->_end_time == 0) {
        return tal_system_get_millisecond() - sw->_begin_time;
    } else {
        return sw->_end_time - sw->_begin_time;
    }
}

STATIC unsigned long __time_left(VOID *p_sw, unsigned long timeout)
{
    stop_watch *sw = (stop_watch *)p_sw;
    SYS_TIME_T es = sw->elapsed_ms(sw);
    if (es >= timeout) {
        return 0;
    } else {
        return timeout - es;
    }
}

VOID sw_init(stop_watch *sw)
{
    memset(sw, 0, sizeof(stop_watch));
    sw->_end_time = sw->_begin_time = tal_system_get_millisecond();
    sw->restart = __restart;
    sw->stop = __stop;
    sw->elapsed_ms = __elapsed_ms;
    sw->time_left = __time_left;
}