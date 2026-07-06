/**
 * @file tuya_gui_base_event.c
 * @author maht@tuya.com
 * @brief tuya event,基于timer queue、message queue、work queue实现的事件中心
 * @version 0.1
 * @date 2019-10-30
 *
 * @copyright Copyright (c) tuya.inc 2019
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tuya_gui_base_event.h"
#ifdef TUYA_SUBSCRIBE_ASYNC
#include "tkl_queue.h"
#include "tkl_thread.h"

STATIC TKL_QUEUE_HANDLE s_event_queue = NULL;
STATIC TKL_THREAD_HANDLE s_event_task = NULL;
#endif
/*******************list********************/
/**
 * @brief bidirection list head
 * 
 */
typedef struct tuya_list_head 
{
    struct tuya_list_head *next, *prev;
}LIST_HEAD,*P_LIST_HEAD;

/**
 * @brief define and initialize bidirection list head
 * 
 */
#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) \
LIST_HEAD name = LIST_HEAD_INIT(name)

/**
 * @brief bidirection list initialization
 * 
 */
#define INIT_LIST_HEAD(ptr) do { \
(ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

/**
 * @brief create a new bidirection list, will call malloc
 * 
 */
#define NEW_LIST_NODE(type, node) \
{\
    node = (type *)Malloc(sizeof(type));\
}

/**
 * @brief free all objects in the bidirection list
 * 
 */
#define FREE_LIST(type, p, list_name)\
{\
    type *posnode;\
    while(!tuya_list_empty(&(p)->list_name)) {\
    posnode = tuya_list_entry((&(p)->list_name)->next, type, list_name);\
    tuya_list_del((&(p)->list_name)->next);\
    tal_free(posnode);\
    }\
}

/**
 * @brief get the first object of the bidirection list
 * 
 */
#define GetFirstNode(type,p,list_name,pGetNode)\
{\
    pGetNode = NULL;\
    while(!tuya_list_empty(&(p)->list_name)){\
    pGetNode = tuya_list_entry((&(p)->list_name)->next, type, list_name);\
    break;\
    }\
}

/**
 * @brief remove the object from bidirection list and free the memory
 * 
 * @note the pDelNode must be the object pointer
 */
#define DeleteNodeAndFree(pDelNode,list_name)\
{\
    tuya_list_del(&(pDelNode->list_name));\
    tal_free(pDelNode);\
}

/**
 * @brief remove the object from the bidirection list
 * 
 */
#define DeleteNode(pDelNode,list_name)\
{\
    tuya_list_del(&(pDelNode->list_name));\
}

/**
 * @brief free the object in bidirection list
 * 
 */
#define FreeNode(pDelNode)\
{\
    tal_free(pDelNode);\
}

/**
 * @brief cast the bidirection list node to object
 * 
 */
#define tuya_list_entry(ptr, type, member) \
((type *)((char *)(ptr)-(size_t)(&((type *)0)->member)))

/**
 * @brief traverse the bidirection list, cannot change the bidiretion list during traverse
 * 
 */
#define tuya_list_for_each(pos, head) \
for (pos = (head)->next; (pos != NULL) && (pos != (head)); pos = pos->next)

/**
 * @brief traverse the bidirection list, can change the bidiretion list during traverse
 * 
 */
#define tuya_list_for_each_safe(p, n, head) \
for (p = (head)->next; n = p->next, p != (head); p = n)

/**
 * @brief add a new to the liste between prev and next
 * 
 * @param[in] pNew  the new node
 * @param[in] pPrev the prev node
 * @param[in] pNext the next node
 * @return VOID
 */
STATIC VOID __list_add(IN CONST P_LIST_HEAD pNew, IN CONST P_LIST_HEAD pPrev,\
                       IN CONST P_LIST_HEAD pNext)
{
    pNext->prev = pNew;
    pNew->next = pNext;
    pNew->prev = pPrev;
    pPrev->next = pNew;
}

/**
 * @brief delete the node between prev and next
 * 
 * @param[in] pPrev the prev node
 * @param[in] pNext the next node
 * @return VOID
 */
STATIC VOID __list_del(IN CONST P_LIST_HEAD pPrev, IN CONST P_LIST_HEAD pNext)
{
    pNext->prev = pPrev;
    pPrev->next = pNext;
}

/**
 * @brief check if the bidirection list is empty
 * 
 * @param[in] pHead the bidirection list
 * @return 0 means empty, others means empty
 */
STATIC INT_T tuya_list_empty(IN CONST P_LIST_HEAD pHead)
{
    return pHead->next == pHead;
}

/**
 * @brief add new list node into bidirection list
 * 
 * @param[in] pNew the new list node
 * @param[in] pHead the bidirection list
 * @return VOID 
 */
STATIC VOID tuya_list_add(IN CONST P_LIST_HEAD pNew, IN CONST P_LIST_HEAD pHead)
{
    __list_add(pNew, pHead, pHead->next);
}

/**
 * @brief add new list node to the tail of the bidirection list
 * 
 * @param[in] pNew the new list node
 * @param[in] pHead the bidirection list
 * @return VOID 
 */
STATIC VOID tuya_list_add_tail(IN CONST P_LIST_HEAD pNew, IN CONST P_LIST_HEAD pHead)
{
    __list_add(pNew, pHead->prev, pHead);
}

/**
 * @brief splice two dibrection list
 * 
 * @param[in] pList the bidirection list need to splice
 * @param[in] pHead the bidirection list
 * @return VOID 
 */
STATIC VOID tuya_list_splice(IN CONST P_LIST_HEAD pList, IN CONST P_LIST_HEAD pHead)
{
    P_LIST_HEAD pFirst = pList->next;

    if (pFirst != pList) // 该链表不为空
    {
        P_LIST_HEAD pLast = pList->prev;
        P_LIST_HEAD pAt = pHead->next; // list接合处的节点

        pFirst->prev = pHead;
        pHead->next = pFirst;
        pLast->next = pAt;
        pAt->prev = pLast;
    }
}

/**
 * @brief remove a list node from bidirection list
 * 
 * @param[in] pEntry the list node need to remove
 * @return VOID 
 */
STATIC VOID tuya_list_del(IN CONST P_LIST_HEAD pEntry)
{
    __list_del(pEntry->prev, pEntry->next);
}

/**
 * @brief remove a list node from bidirection list and initialize it
 * 
 * @param[in] pEntry the list node need to remove and initialize
 * @return VOID 
 */
STATIC VOID tuya_list_del_init(IN CONST P_LIST_HEAD pEntry)
{
    __list_del(pEntry->prev, pEntry->next);
    INIT_LIST_HEAD(pEntry);
}
/*******************list********************/
/**
 * @brief the subscirbe node
 *
 */
typedef struct {
    CHAR_T name[EVENT_NAME_MAX_LEN + 1];  // name, used to record the the event info
    CHAR_T desc[EVENT_DESC_MAX_LEN + 1];  // description, used to record the subscribe info
    SUBSCRIBE_TYPE_E type;              // the subscribe type
    EVENT_SUBSCRIBE_CB cb;              // the subscribe callback function
    struct tuya_list_head node;         // list node, used to attch to the event node
} SUBSCRIBE_NODE_T;

/**
 * @brief the event node
 *
 */
typedef struct {
    MUTEX_HANDLE mutex;                         // mutex, protection the event publish and subscribe

    CHAR_T name[EVENT_NAME_MAX_LEN + 1];          // name, the event name
    struct tuya_list_head node;                 // list node, used to attach to the event manage module
    struct tuya_list_head subscribe_root;       // subscibe root, used to manage the subscriber

    EVENT_SUBSCRIBE_CB last_cb; //used to debug which cb is blocked
} EVENT_NODE_T;

/**
 * @brief the event manage node
 *
 */
typedef struct {
    INT_T inited;
    MUTEX_HANDLE mutex;                             // mutex, used to protection event manage node
    INT_T event_cnt;                                  // current event number
    struct tuya_list_head event_root;               // event root, used to manage the event
    struct tuya_list_head free_subscribe_root;      // free subscriber list, used to manage the subscribe which not found the event
} EVENT_MANAGE_T;

STATIC EVENT_MANAGE_T g_event_manager = {0};

STATIC OPERATE_RET ty_subscribe_event(CONST CHAR_T *name, CONST CHAR_T *desc, CONST EVENT_SUBSCRIBE_CB cb, SUBSCRIBE_TYPE_E type);
STATIC OPERATE_RET ty_unsubscribe_event(CONST CHAR_T *name, CONST CHAR_T *desc, EVENT_SUBSCRIBE_CB cb);

STATIC BOOL_T _event_name_is_valid(const char *name)
{
    if (!name) {
        return FALSE;
    }

    if (strlen(name) > EVENT_NAME_MAX_LEN || strlen(name) <= 0) {
        return FALSE;
    }

    return TRUE;
}

STATIC BOOL_T _event_desc_is_valid(const char *desc)
{
    if (!desc) {
        return FALSE;
    }

    if (strlen(desc) > EVENT_DESC_MAX_LEN || strlen(desc) <= 0) {
        return FALSE;
    }

    return TRUE;
}

STATIC EVENT_NODE_T *_event_node_create_init(const char *name)
{
    // allocate memory
    EVENT_NODE_T *event = Malloc(sizeof(EVENT_NODE_T));
    TUYA_CHECK_NULL_RETURN(event, NULL);
    memset(event, 0, sizeof(EVENT_NODE_T));

    // initialze the event node
    memcpy(event->name, name, strlen(name));
    event->name[strlen(name)] = '\0';
    INIT_LIST_HEAD(&event->subscribe_root);
    tal_mutex_create_init(&event->mutex);

    tal_mutex_lock(g_event_manager.mutex);

    // need check if there have free subscriber which subscribe this event
    struct tuya_list_head *free_pos = NULL;
    struct tuya_list_head *free_next = NULL;
    SUBSCRIBE_NODE_T *free_entry = NULL;
    tuya_list_for_each_safe(free_pos, free_next, &g_event_manager.free_subscribe_root) {
        // find by event name, and add it to subscribe list
        free_entry = tuya_list_entry(free_pos, SUBSCRIBE_NODE_T, node);
        if (0 == strcmp(free_entry->name, name)) {
            // del from free subscrbe list
            tuya_list_del(&free_entry->node);

            // add to different position according to the emergence flag
            if (free_entry->type == SUBSCRIBE_TYPE_EMERGENCY) {
                tuya_list_add(&free_entry->node, &event->subscribe_root);
            } else {
                tuya_list_add_tail(&free_entry->node, &event->subscribe_root);
            }
        }
    }

    // at last, need add this event to event manage root
    tuya_list_add_tail(&event->node, &g_event_manager.event_root);
    g_event_manager.event_cnt++;

    tal_mutex_unlock(g_event_manager.mutex);

    return event;
}

STATIC EVENT_NODE_T *_event_node_get(const char *name)
{
    // try to get event from event root
    EVENT_NODE_T *entry = NULL;
    struct tuya_list_head *pos = NULL;
    tuya_list_for_each(pos, &g_event_manager.event_root) {
        // find by name
        entry = tuya_list_entry(pos, EVENT_NODE_T, node);
        if (0 == strcmp(entry->name, name)) {
            return entry;
        }
    }

    return NULL;
}

STATIC SUBSCRIBE_NODE_T *_event_node_get_free_subscribe(SUBSCRIBE_NODE_T *subscribe)
{
    struct tuya_list_head *pos = NULL;
    SUBSCRIBE_NODE_T *entry = NULL;
    tuya_list_for_each(pos, &g_event_manager.free_subscribe_root) {
        // find by desc
        entry = tuya_list_entry(pos, SUBSCRIBE_NODE_T, node);
        if (0 == strcmp(entry->name, subscribe->name) &&
            0 == strcmp(entry->desc, subscribe->desc) &&
            entry->cb == subscribe->cb) {
            return entry;
        }
    }

    return NULL;
}

STATIC SUBSCRIBE_NODE_T *_event_node_get_subscribe(EVENT_NODE_T *event, SUBSCRIBE_NODE_T *subscribe)
{
    struct tuya_list_head *pos = NULL;
    SUBSCRIBE_NODE_T *entry = NULL;
    tuya_list_for_each(pos, &event->subscribe_root) {
        // find by desc
        entry = tuya_list_entry(pos, SUBSCRIBE_NODE_T, node);
        if (0 == strcmp(entry->desc, subscribe->desc) && entry->cb == subscribe->cb) {
            return entry;
        }
    }

    return NULL;
}

STATIC OPERATE_RET _event_node_dispatch(EVENT_NODE_T *event, void *data)
{
    OPERATE_RET rt = OPRT_OK;

    // dispatch in order
    struct tuya_list_head *p = NULL;
    struct tuya_list_head *n = NULL;
    SUBSCRIBE_NODE_T *entry = NULL;
    tuya_list_for_each_safe(p, n, &event->subscribe_root) {
        // find and call cb one by one
        entry = tuya_list_entry(p, SUBSCRIBE_NODE_T, node);
        if (entry->cb) {
            event->last_cb = entry->cb;
            TUYA_CALL_ERR_LOG(entry->cb(data));
            event->last_cb = NULL;
        }

        // one-time event should be removed after dispatch
        if (entry->type == SUBSCRIBE_TYPE_ONETIME) {
            tuya_list_del(&entry->node);
            Free(entry);
            entry = NULL;
        }
    }

    return rt;
}

STATIC OPERATE_RET _event_node_add_free_subscribe(SUBSCRIBE_NODE_T* subscribe)
{
    OPERATE_RET rt = OPRT_OK;

    // existed, return ok, dont care, pretend to success
    SUBSCRIBE_NODE_T *new_entry = _event_node_get_free_subscribe(subscribe);
    if (new_entry) {
        return OPRT_OK;
    }

    // malloc a new entry and prepare to add
    new_entry = (SUBSCRIBE_NODE_T *)Malloc(sizeof(SUBSCRIBE_NODE_T));
    TUYA_CHECK_NULL_RETURN(new_entry, OPRT_MALLOC_FAILED);
    memcpy(new_entry, subscribe, sizeof(SUBSCRIBE_NODE_T));

    tuya_list_add_tail(&new_entry->node, &g_event_manager.free_subscribe_root);
    return rt;
}

STATIC OPERATE_RET _event_node_add_subscribe(EVENT_NODE_T *event, SUBSCRIBE_NODE_T* subscribe)
{
    OPERATE_RET rt = OPRT_OK;

    // existed, return ok, dont care, pretend to success
    SUBSCRIBE_NODE_T *new_entry = _event_node_get_subscribe(event, subscribe);
    if (new_entry) {
        return OPRT_OK;
    }

    // malloc a new entry and prepare to add
    new_entry = (SUBSCRIBE_NODE_T *)Malloc(sizeof(SUBSCRIBE_NODE_T));
    TUYA_CHECK_NULL_RETURN(new_entry, OPRT_MALLOC_FAILED);
    memcpy(new_entry, subscribe, sizeof(SUBSCRIBE_NODE_T));

    // try to add, if emergence, add to first, otherwise, add to tail
    if (subscribe->type == SUBSCRIBE_TYPE_EMERGENCY) {
        tuya_list_add(&new_entry->node, &event->subscribe_root);
    } else {
        tuya_list_add_tail(&new_entry->node, &event->subscribe_root);
    }

    return rt;
}

STATIC OPERATE_RET _event_node_del_free_subscribe(SUBSCRIBE_NODE_T* subscribe)
{
    OPERATE_RET rt = OPRT_OK;
    // not existed, return ok, dont care, pretend to success
    SUBSCRIBE_NODE_T *new_entry = _event_node_get_free_subscribe(subscribe);
    if (new_entry == NULL) {
        return OPRT_OK;
    }

    // dont forget remove and free
    tuya_list_del(&new_entry->node);
    Free(new_entry);
    new_entry = NULL;

    return rt;
}

STATIC OPERATE_RET _event_node_del_subscribe(EVENT_NODE_T *event, SUBSCRIBE_NODE_T* subscribe)
{
    OPERATE_RET rt = OPRT_OK;
    SUBSCRIBE_NODE_T *new_entry = NULL;
    // not existed, return ok, dont care, pretend to success
    new_entry = _event_node_get_subscribe(event, subscribe);
    if (new_entry == NULL) {
        return OPRT_OK;
    }

    // dont forget remove and free
    tuya_list_del(&new_entry->node);
    Free(new_entry);
    new_entry = NULL;
    return rt;
}

#if 0
int _ty_event_dump()
{
    if (g_event_manager.inited == 0) {
        return OPRT_OK;
    }

    struct tuya_list_head *e_pos = NULL;
    struct tuya_list_head *s_pos = NULL;
    EVENT_NODE_T *event = NULL;

    // event and subscribe
    PR_DEBUG("------------------------------------------------------------------");
    PR_DEBUG("name                desc                                cb");
    PR_DEBUG("------------------------------------------------------------------");
    tuya_list_for_each(e_pos, &g_event_manager.event_root) {
        event = tuya_list_entry(e_pos, EVENT_NODE_T, node);
        if (event) {
            SUBSCRIBE_NODE_T *subscribe = NULL;
            if (!tuya_list_empty(&event->subscribe_root)) {
                tuya_list_for_each(s_pos, &event->subscribe_root) {
                    subscribe = tuya_list_entry(s_pos, SUBSCRIBE_NODE_T, node);
                    if (subscribe) {
                        PR_DEBUG("%-16s    %-32s    0x%08x", subscribe->name, subscribe->desc, subscribe->cb);
                    }
                }
            }
        }
    }

    // free subscribe
    PR_DEBUG("------------------------free--------------------------------------");
    PR_DEBUG("name                desc                                cb");
    PR_DEBUG("------------------------------------------------------------------");
    SUBSCRIBE_NODE_T * subscribe = NULL;
    tuya_list_for_each(s_pos, &g_event_manager.free_subscribe_root) {
        subscribe = tuya_list_entry(s_pos, SUBSCRIBE_NODE_T, node);
        if (subscribe) {
            PR_DEBUG("%-16s    %-32s    0x%08x", subscribe->name, subscribe->desc, subscribe->cb);
        }
    }

    PR_DEBUG("\n");

    return OPRT_OK;
}
#endif

#ifdef TUYA_SUBSCRIBE_ASYNC
STATIC VOID tuya_gui_base_event_task(VOID* data)
{
    CHAR_T _name[EVENT_NAME_MAX_LEN + 1] = {0x0};
    int name_cp_len = 0;

    while (1) {
        TY_EVENT_MSG_S evt;
        if (tkl_queue_fetch(s_event_queue, &evt, TKL_QUEUE_WAIT_FROEVER) == OPRT_OK) {
            memset(_name, 0, sizeof(_name));
            name_cp_len = snprintf(_name, sizeof(_name), "%s-%s", evt.desc, evt.name);
            if (name_cp_len >= sizeof(_name)) {
                PR_WARN("id: %s is too long to name:%s, len=%d", evt.desc, _name, name_cp_len);
            }
            if (evt.mode == BASE_EVENT_SUBSCRIBE) {
                ty_subscribe_event(_name, evt.desc, evt.cb, evt.type);
            }
            else {
                ty_unsubscribe_event(_name, evt.desc, evt.cb);
            }
        }
    }
}

STATIC OPERATE_RET tuya_gui_base_event_send(TY_EVENT_MSG_S* event)
{
    return tkl_queue_post(s_event_queue, event, 0);
}
#endif

STATIC OPERATE_RET ty_event_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    if (g_event_manager.inited == TRUE) {
        return OPRT_OK;
    }

#ifdef TUYA_SUBSCRIBE_ASYNC
    rt = tkl_queue_create_init(&s_event_queue, SIZEOF(TY_EVENT_MSG_S), 32);
    if (rt != OPRT_OK) {
        PR_ERR("tuya_gui_base_event create queue error, rt: %d", rt);
        return rt;
    }
    rt = tkl_thread_create(&s_event_task, "gui_base_event", 2048, 4, tuya_gui_base_event_task, NULL);
    if (rt != OPRT_OK) {
        PR_ERR("tuya_gui_base_event create task error, rt: %d", rt);
        tkl_queue_free(s_event_queue);
        return rt;
    }
#endif

    INIT_LIST_HEAD(&g_event_manager.event_root);
    INIT_LIST_HEAD(&g_event_manager.free_subscribe_root);
    tal_mutex_create_init(&g_event_manager.mutex);
    g_event_manager.event_cnt = 0;
    g_event_manager.inited = TRUE;

    return rt;
}

STATIC OPERATE_RET ty_publish_event(CONST CHAR_T* name, VOID_T *data)
{
    if (g_event_manager.inited != TRUE) {
        ty_event_init();
    }

    if (!_event_name_is_valid(name)) {
        return OPRT_BASE_EVENT_INVALID_EVENT_NAME;
    }

    OPERATE_RET rt = OPRT_OK;
    // try to get event, if not exist, create and init.
    EVENT_NODE_T *event = _event_node_get(name);
    if (!event) {
        event = _event_node_create_init(name);
        TUYA_CHECK_NULL_RETURN(event, OPRT_MALLOC_FAILED);
    }

    // to keep the consistency, dispatch will done in mutex lock
    tal_mutex_lock(event->mutex);
    // try to dispatch event to all subscribe
    // if one of the subscribe failed, it will continue but will return failed to record the execute status
    TUYA_CALL_ERR_LOG(_event_node_dispatch(event, data));

    tal_mutex_unlock(event->mutex);

    return rt;
}

STATIC OPERATE_RET ty_subscribe_event(CONST CHAR_T *name, CONST CHAR_T *desc, CONST EVENT_SUBSCRIBE_CB cb, SUBSCRIBE_TYPE_E type)
{
    if (g_event_manager.inited != TRUE) {
        ty_event_init();
    }

    if (!_event_desc_is_valid(desc)) {
        return OPRT_BASE_EVENT_INVALID_EVENT_DESC;
    }

    if (!_event_name_is_valid(name)) {
        return OPRT_BASE_EVENT_INVALID_EVENT_NAME;
    }

    OPERATE_RET rt = OPRT_OK;
    SUBSCRIBE_NODE_T subscribe = {{0}};
    subscribe.cb = cb;
    subscribe.type = type;
    memcpy(subscribe.name, name, strlen(name));
    subscribe.name[strlen(name)] = '\0';
    memcpy(subscribe.desc, desc, strlen(desc));
    subscribe.desc[strlen(desc)] = '\0';

    EVENT_NODE_T *event = _event_node_get(name);
    if (!event) {
        // if not found the event, add to the free list
        tal_mutex_lock(g_event_manager.mutex);
        TUYA_CALL_ERR_LOG(_event_node_add_free_subscribe(&subscribe));
        tal_mutex_unlock(g_event_manager.mutex);
    } else {
        // if found the event, add to the subscribe list
        tal_mutex_lock(event->mutex);
        TUYA_CALL_ERR_LOG(_event_node_add_subscribe(event, &subscribe));
        tal_mutex_unlock(event->mutex);
    }

    return rt;
}


STATIC OPERATE_RET ty_unsubscribe_event(CONST CHAR_T *name, CONST CHAR_T *desc, EVENT_SUBSCRIBE_CB cb)
{
    if (g_event_manager.inited != TRUE) {
        ty_event_init();
    }

    if (!_event_desc_is_valid(desc)) {
        return OPRT_BASE_EVENT_INVALID_EVENT_DESC;
    }

    if (!_event_name_is_valid(name)) {
        return OPRT_BASE_EVENT_INVALID_EVENT_NAME;
    }

    OPERATE_RET rt = OPRT_OK;
    SUBSCRIBE_NODE_T subscribe = {{0}};
    subscribe.cb = cb;
    memcpy(subscribe.name, name, strlen(name));
    subscribe.name[strlen(name)] = '\0';
    memcpy(subscribe.desc, desc, strlen(desc));
    subscribe.desc[strlen(desc)] = '\0';

    EVENT_NODE_T *event = _event_node_get(name);
    if (!event) {
        // if not found the event, del from the free list
        tal_mutex_lock(g_event_manager.mutex);
        TUYA_CALL_ERR_LOG(_event_node_del_free_subscribe(&subscribe));
        tal_mutex_unlock(g_event_manager.mutex);
    } else {
        // if found the event, del from the subscribe list
        tal_mutex_lock(event->mutex);
        TUYA_CALL_ERR_LOG(_event_node_del_subscribe(event, &subscribe));
        tal_mutex_unlock(event->mutex);
    }


    return rt;
}

int tuya_publish(const char *name, void *data)
{
    TUYA_CHECK_NULL_RETURN(name, OPRT_INVALID_PARM);

    if (strlen(name) > EVENT_NAME_MAX_LEN || strlen(name) <= 0) {
        PR_ERR("subscribe name:%s len:%zu error, except len small than %d", name, strlen(name),
               EVENT_NAME_MAX_LEN);
        return OPRT_INVALID_PARM;
    }

    return ty_publish_event(name, data);
}

int tuya_subscribe(const char *name, const char *desc, const EVENT_SUBSCRIBE_CB cb, int is_emergency)
{
#ifndef TUYA_SUBSCRIBE_ASYNC
    CHAR_T _name[EVENT_NAME_MAX_LEN + 1] = {0x0};
    int name_cp_len = snprintf(_name, sizeof(_name), "%s-%s", desc, name);
    if (name_cp_len >= sizeof(_name)) {
        PR_WARN("id: %s is too long to name:%s, len=%d", desc, _name, name_cp_len);
    }
    return ty_subscribe_event(_name, desc, cb, is_emergency);
#else
    OPERATE_RET rt = OPRT_OK;
    TUYA_CHECK_NULL_RETURN(name, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(desc, OPRT_INVALID_PARM);
    if (g_event_manager.inited != TRUE) {
        ty_event_init();
    }

    if (strlen(name) > EVENT_NAME_MAX_LEN || strlen(name) <= 0) {
        PR_ERR("desc:%s subscribe name:%s len:%zu error, except len small than %d", desc, name, strlen(name),
               EVENT_NAME_MAX_LEN);
        return OPRT_INVALID_PARM;
    }

    TY_EVENT_MSG_S msg = {0x0};
    snprintf(msg.name, sizeof(msg.name), "%s", name);
    snprintf(msg.desc, sizeof(msg.desc), "%s", desc);
    msg.type = is_emergency;
    msg.cb = cb;
    msg.mode = BASE_EVENT_SUBSCRIBE;
    rt = tuya_gui_base_event_send(&msg);
    if (rt != OPRT_OK) {
        PR_ERR("tuya_gui_base_event_send error, subscribe name:%s, desc:%s rt: %d", name, desc, rt);
	}
    return rt;
#endif
}

int tuya_unsubscribe(const char *name, const char *desc, EVENT_SUBSCRIBE_CB cb)
{
#ifndef TUYA_SUBSCRIBE_ASYNC
    CHAR_T _name[EVENT_NAME_MAX_LEN + 1] = {0x0};
    int name_cp_len = snprintf(_name, sizeof(_name), "%s-%s", desc, name);
    if (name_cp_len >= sizeof(_name)) {
        PR_WARN("id: %s is too long to name:%s, len=%d", desc, _name, name_cp_len);
    }
    return ty_unsubscribe_event(_name, desc, cb);
#else
    OPERATE_RET rt = OPRT_OK;

    TUYA_CHECK_NULL_RETURN(name, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(desc, OPRT_INVALID_PARM);
    if (g_event_manager.inited != TRUE) {
        ty_event_init();
    }

    if (strlen(name) > EVENT_NAME_MAX_LEN || strlen(name) <= 0) {
        PR_ERR("desc:%s subscribe name:%s len:%zu error, except len small than %d", desc, name, strlen(name),
               EVENT_NAME_MAX_LEN);
        return OPRT_INVALID_PARM;
    }

    TY_EVENT_MSG_S msg = {0x0};
    snprintf(msg.name, sizeof(msg.name), "%s", name);
    snprintf(msg.desc, sizeof(msg.desc), "%s", desc);
    msg.cb = cb;
    msg.mode = BASE_EVENT_UNSUBSCRIBE;
    rt = tuya_gui_base_event_send(&msg);
    if (rt != OPRT_OK) {
        PR_ERR("tuya_gui_base_event_send error, unsubscribe name:%s, desc:%s rt: %d", name, desc, rt);
	}
    return rt;
#endif
}
