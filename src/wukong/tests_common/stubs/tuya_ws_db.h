#ifndef __TUYA_WS_DB_H__
#define __TUYA_WS_DB_H__

#include "tuya_cloud_types.h"

OPERATE_RET wd_common_write(CONST CHAR_T *key, CONST BYTE_T *value, UINT_T len);
OPERATE_RET wd_common_read(CONST CHAR_T *key, BYTE_T **value, UINT_T *len);
VOID wd_common_free_data(BYTE_T *data);

#endif
