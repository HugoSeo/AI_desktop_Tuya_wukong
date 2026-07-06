#ifndef __WUKONG_CRON_H__
#define __WUKONG_CRON_H__

#include "tuya_cloud_types.h"
#include "ty_cJSON.h"
typedef OPERATE_RET (*WUKONG_CRON_RPC_HANDLER)(CONST ty_cJSON *params,
                                               ty_cJSON **result);

OPERATE_RET wukong_cron_method_register(CONST CHAR_T *method,
                                        WUKONG_CRON_RPC_HANDLER handler);
OPERATE_RET wukong_cron_method_unregister(CONST CHAR_T *method);
OPERATE_RET wukong_cron_job_add(CONST CHAR_T *job_json, CHAR_T *job_id,
                                UINT_T job_id_len);
OPERATE_RET wukong_cron_job_remove(CONST CHAR_T *job_id);
OPERATE_RET wukong_cron_time_ready_notify(VOID);

#endif
