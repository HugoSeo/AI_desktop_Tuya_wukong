#ifndef __BASE_EVENT_H__
#define __BASE_EVENT_H__

#include "tuya_cloud_types.h"

#define EVENT_TIME_SYNC     1001
#define SUBSCRIBE_TYPE_NORMAL 0

typedef OPERATE_RET (*SUBSCRIBE_CALLBACK)(VOID *data);

OPERATE_RET ty_subscribe_event(INT_T event_id, CONST CHAR_T *name,
                               SUBSCRIBE_CALLBACK cb, INT_T type);
OPERATE_RET ty_unsubscribe_event(INT_T event_id, CONST CHAR_T *name,
                                 SUBSCRIBE_CALLBACK cb);

#endif
