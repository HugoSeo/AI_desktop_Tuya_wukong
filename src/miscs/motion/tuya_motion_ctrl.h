#ifndef __TUYA_MOTION_CTRL_H__
#define __TUYA_MOTION_CTRL_H__

#include "tuya_cloud_types.h"
#include "tal_log.h"
#include "tal_queue.h"
#include "tal_thread.h"

typedef enum
{
    MOTION_CTRL_IDLE = -1,          
    MOTION_CTRL_LEFT = 0,           //左转
    MOTION_CTRL_RIGHT,              //右转
    MOTION_CTRL_APPOINT,            //指定角度
    MOTION_CTRL_CLOCKWISE,          //顺时针
    MOTION_CTRL_COUNTERCLOCKWISE,   //逆时针
    MOTION_CTRL_POINT_MOVE,         //转一点
    MOTION_CTRL_RESET,              //复位
    MOTION_CTRL_STOP,               //停止
    MOTION_CTRL_FINSH,              //运动结束
    MOTION_CTRL_MAX
}TUYA_MOTION_CTRL_EN;

typedef enum
{
    CTRL_STATE_IDLE = 0,
    CTRL_STATE_START,
    CTRL_STATE_RUNNING,
    CTRL_STATE_FINSH,
    CTRL_STATE_STOP,
    CTRL_STATE_MAX
}TUYA_CTRL_STATE_EN;

typedef struct
{
    TUYA_MOTION_CTRL_EN motion_mode;
    UINT_T rotate_value;
}tuya_motion_data_t;

typedef struct
{
    THREAD_HANDLE heart_task;
    BOOL_T is_alive;
    BOOL_T is_running;
    BOOL_T is_finsh_motion;
    UINT_T last_heartbeat_time;
    UINT_T current_time;
}tuya_motion_heart_t;

typedef struct
{
    THREAD_HANDLE ctrl_task;
    QUEUE_HANDLE queue;
    BOOL_T is_running;
    TUYA_CTRL_STATE_EN motion_state;
}tuya_motion_ctrl_t;

OPERATE_RET tuya_motion_ctrl_init(VOID_T);

OPERATE_RET tuya_motion_left(UINT32_T angle);

OPERATE_RET tuya_motion_right(UINT32_T angle);

OPERATE_RET tuya_motion_set_absolute_angle(UINT32_T angle);

OPERATE_RET tuya_motion_send_msg(UINT_T mode, UINT_T rotate_value);

#endif // __TUYA_MOTION_CTRL_H__