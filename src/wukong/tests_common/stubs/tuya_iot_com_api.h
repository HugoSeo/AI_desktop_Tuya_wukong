#ifndef __TUYA_IOT_COM_API_H__
#define __TUYA_IOT_COM_API_H__

#include "tuya_cloud_types.h"

typedef enum {
    PROP_VALUE,
} DP_PROP_TYPE_E;

typedef struct {
    UINT_T dpid;
    DP_PROP_TYPE_E type;
    union {
        INT_T dp_value;
    } value;
} TY_OBJ_DP_S;

CHAR_T *tuya_iot_get_gw_id(VOID);
OPERATE_RET tuya_report_dp_async(CONST CHAR_T *devid, CONST TY_OBJ_DP_S *dp,
                                  UINT_T cnt, VOID *cb);

#endif
