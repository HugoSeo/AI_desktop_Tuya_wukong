#ifndef __WUKONG_CLOCK_H__
#define __WUKONG_CLOCK_H__

#include "tuya_cloud_types.h"

typedef enum {
    AI_CLOCK_TIMER_OPR_START = 0,
    AI_CLOCK_TIMER_OPR_PAUSE = 1,
    AI_CLOCK_TIMER_OPR_RESUME = 2,
    AI_CLOCK_TIMER_OPR_STOP = 3,
    AI_CLOCK_TIMER_OPR_RESET = 4,
} TY_AI_CLOCK_TIMER_OPR_TYPE_E;

typedef enum {
    AI_CLOCK_SCHED_OPR_ADD = 0,
    AI_CLOCK_SCHED_OPR_DELETE = 1,
    AI_CLOCK_SCHED_OPR_UPDATE = 2,
} TY_AI_CLOCK_SCHED_OPR_TYPE_E;

typedef enum {
    AI_CLOCK_SCHED_QUERY_BY_TIME = 0,
    AI_CLOCK_SCHED_QUERY_BY_CATEGORY = 1,
    AI_CLOCK_SCHED_QUERY_BY_KEYWORD = 2,
} TY_AI_CLOCK_SCHED_QUERY_METHOD_E;

typedef struct {
    INT_T work_duration;
    INT_T short_break_duration;
    INT_T long_break_duration;
} TY_AI_CLOCK_POMODORO_TIMER_CFG_T;

typedef struct {
    TIME_T start_time;
    TIME_T end_time;
    CHAR_T *location;
    CHAR_T *description;
    INT_T categories;
} TY_AI_CLOCK_SCHED_CFG_T;

typedef struct {
    INT_T categories;
    TIME_T start_time;
    TIME_T end_time;
    CHAR_T *keyword;
} TY_AI_CLOCK_SCHED_QUERY_CFG_T;

TIME_T wukong_clock_time_mktime(CHAR_T *iso_8601_time_str);
OPERATE_RET wukong_clock_set_countdown_timer(
    TY_AI_CLOCK_TIMER_OPR_TYPE_E opr,
    INT_T hours, INT_T minutes, INT_T seconds);
OPERATE_RET wukong_clock_set_stopwatch_timer(
    TY_AI_CLOCK_TIMER_OPR_TYPE_E opr);
OPERATE_RET wukong_clock_set_pomodoro_timer(
    TY_AI_CLOCK_TIMER_OPR_TYPE_E opr,
    TY_AI_CLOCK_POMODORO_TIMER_CFG_T *pomodoro);
OPERATE_RET wukong_clock_set_schedule(
    TY_AI_CLOCK_SCHED_OPR_TYPE_E opr,
    TY_AI_CLOCK_SCHED_CFG_T *sched);
CHAR_T *wukong_clock_set_schedule_query(
    TY_AI_CLOCK_SCHED_QUERY_METHOD_E query_method,
    TY_AI_CLOCK_SCHED_QUERY_CFG_T *sched_query);

#endif
