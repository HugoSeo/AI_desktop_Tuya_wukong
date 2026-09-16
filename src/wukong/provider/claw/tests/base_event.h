/*
 * Suite-local double for base_event.h (string-keyed event API), front-loaded
 * (test_suite.py's "skill" entry) ahead of tests_common/stubs/base_event.h.
 * That shared stub uses an unrelated int-keyed API (built for the tm
 * module's own EVENT_TIME_SYNC double) and would reject the string
 * EVENT_WUKONG_STORAGE_READY macro wukong_skill.c now subscribes with. This
 * double instead mirrors the real component's public surface — name/desc/cb/
 * type in that order, VOID_T* callback payload — see
 * components/base_event/include/base_event.h.
 */
#ifndef __WUKONG_TEST_CLAW_BASE_EVENT_H__
#define __WUKONG_TEST_CLAW_BASE_EVENT_H__

#include "tuya_cloud_types.h"

typedef INT_T (*EVENT_SUBSCRIBE_CB)(VOID_T *data);
typedef BYTE_T SUBSCRIBE_TYPE_E;
#define SUBSCRIBE_TYPE_NORMAL    0
#define SUBSCRIBE_TYPE_EMERGENCY 1
#define SUBSCRIBE_TYPE_ONETIME   2

OPERATE_RET ty_subscribe_event(CONST CHAR_T *name, CONST CHAR_T *desc,
                               CONST EVENT_SUBSCRIBE_CB cb, SUBSCRIBE_TYPE_E type);
OPERATE_RET ty_unsubscribe_event(CONST CHAR_T *name, CONST CHAR_T *desc,
                                 EVENT_SUBSCRIBE_CB cb);

#endif /* __WUKONG_TEST_CLAW_BASE_EVENT_H__ */
