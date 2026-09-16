/* No-op double for base_event.h's string-keyed subscribe/unsubscribe: the
 * skill catalog's storage.ready rescan is exercised directly in
 * test_wukong_skill.c via wukong_skill_reload()/wukong_skill_init(), not
 * through a real event dispatch, so wukong_skill_init()'s subscription call
 * just needs to succeed without side effects. */
#include "base_event.h"

OPERATE_RET ty_subscribe_event(CONST CHAR_T *name, CONST CHAR_T *desc,
                               CONST EVENT_SUBSCRIBE_CB cb, SUBSCRIBE_TYPE_E type)
{
    (VOID)name; (VOID)desc; (VOID)cb; (VOID)type;
    return OPRT_OK;
}

OPERATE_RET ty_unsubscribe_event(CONST CHAR_T *name, CONST CHAR_T *desc,
                                 EVENT_SUBSCRIBE_CB cb)
{
    (VOID)name; (VOID)desc; (VOID)cb;
    return OPRT_OK;
}
