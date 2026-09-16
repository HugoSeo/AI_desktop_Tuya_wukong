/**
 * @file tuya_tmm_manager_queue.c
 * @brief Message queue for tuya tmm manager (T5 port)
 * @copyright Copyright (c) Tuya Inc.
 */
#include <string.h>

#include "tal_memory.h"
#include "uni_log.h"
#include "tal_queue.h"
#include "tuya_tmm_manager_queue.h"

struct tuya_tmm_mgr_queue {
    QUEUE_HANDLE queue;
};

OPERATE_RET tuya_tmm_mgr_queue_create(TUYA_TMM_QUEUE_HANDLE_T *handle, INT_T msgsize, INT_T msgcount)
{
    TUYA_TMM_QUEUE_HANDLE_T queue_handle = NULL;

    if (handle == NULL || msgsize <= 0 || msgcount <= 0) {
        return OPRT_INVALID_PARM;
    }

    queue_handle = (TUYA_TMM_QUEUE_HANDLE_T)tal_malloc(sizeof(struct tuya_tmm_mgr_queue));
    if (queue_handle == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    memset(queue_handle, 0, sizeof(struct tuya_tmm_mgr_queue));
    if (tal_queue_create_init(&queue_handle->queue, msgsize, msgcount) != OPRT_OK) {
        tal_free(queue_handle);
        return OPRT_MALLOC_FAILED;
    }

    *handle = queue_handle;
    return OPRT_OK;
}

OPERATE_RET tuya_tmm_mgr_queue_destroy(TUYA_TMM_QUEUE_HANDLE_T handle)
{
    if (handle == NULL) {
        return OPRT_OK;
    }

    if (handle->queue != NULL) {
        tal_queue_free(handle->queue);
        handle->queue = NULL;
    }

    tal_free(handle);
    return OPRT_OK;
}

OPERATE_RET tuya_tmm_mgr_queue_post(TUYA_TMM_QUEUE_HANDLE_T handle, VOID *data, UINT_T timeout)
{
    if (handle == NULL || data == NULL) {
        return OPRT_INVALID_PARM;
    }

    return tal_queue_post(handle->queue, data, timeout);
}

OPERATE_RET tuya_tmm_mgr_queue_fetch(TUYA_TMM_QUEUE_HANDLE_T handle, VOID *msg, UINT_T timeout)
{
    if (handle == NULL || msg == NULL) {
        return OPRT_INVALID_PARM;
    }

    return tal_queue_fetch(handle->queue, msg, timeout);
}
