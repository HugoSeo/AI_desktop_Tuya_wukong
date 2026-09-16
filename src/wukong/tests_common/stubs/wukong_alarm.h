#ifndef __WUKONG_ALARM_H__
#define __WUKONG_ALARM_H__

#include "tuya_cloud_types.h"

#define WUKONG_ALARM_ID_LEN 32
#define WUKONG_ALARM_CRON_JOB_ID_LEN 32

typedef enum {
    WUKONG_ALARM_REPEAT_ONCE = 0,
    WUKONG_ALARM_REPEAT_DAILY = 1,
    WUKONG_ALARM_REPEAT_WEEKLY = 2,
    WUKONG_ALARM_REPEAT_MONTHLY = 3,
} WUKONG_ALARM_REPEAT_TYPE_E;

typedef struct {
    BOOL_T enabled;
    WUKONG_ALARM_REPEAT_TYPE_E repeat_type;
    UINT_T hour;
    UINT_T minute;
    UINT_T weekday_mask;
    UINT_T month_day;
    CHAR_T cron_job_id[WUKONG_ALARM_CRON_JOB_ID_LEN + 1];
} WUKONG_ALARM_CFG_T;

OPERATE_RET wukong_alarm_add(CONST WUKONG_ALARM_CFG_T *alarm_cfg,
                             CHAR_T *alarm_id, UINT_T alarm_id_len);
OPERATE_RET wukong_alarm_update(CONST CHAR_T *alarm_id,
                                CONST WUKONG_ALARM_CFG_T *alarm_cfg);
OPERATE_RET wukong_alarm_remove(CONST CHAR_T *alarm_id);
OPERATE_RET wukong_alarm_list(CHAR_T **alarm_list_json);
OPERATE_RET wukong_alarm_find_by_time(
    CONST WUKONG_ALARM_CFG_T *alarm_cfg,
    CHAR_T *alarm_id, UINT_T alarm_id_len);

#endif
