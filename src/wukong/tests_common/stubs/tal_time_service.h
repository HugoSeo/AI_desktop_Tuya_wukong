#ifndef __TAL_TIME_SERVICE_H__
#define __TAL_TIME_SERVICE_H__

#include <time.h>
#include "tuya_cloud_types.h"

typedef struct tm POSIX_TM_S;

TIME_T tal_time_get_posix(VOID);
TIME_T tal_time_mktime(POSIX_TM_S *tm_info);
OPERATE_RET tal_time_get_local_time_custom(TIME_T posix_time, POSIX_TM_S *local_tm);
OPERATE_RET tal_time_check_time_sync(VOID);
OPERATE_RET tal_time_check_time_zone_sync(VOID);

#endif
