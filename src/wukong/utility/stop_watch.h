#ifndef __STOP_WATCH__
#define __STOP_WATCH__

#include "tal_system.h"

typedef struct {
    VOID (*restart)(VOID *p_sw);
    VOID (*stop)(VOID *p_sw);
	SYS_TIME_T (*elapsed_ms)(VOID *p_sw);
	unsigned long (*time_left)(VOID *p_sw, unsigned long timeout);
	SYS_TIME_T _begin_time;
	SYS_TIME_T _end_time;
} stop_watch;


#define NewStopWatch(sw) \
    stop_watch sw; \
    sw_init(&sw);

VOID sw_init(stop_watch *sw);

#endif