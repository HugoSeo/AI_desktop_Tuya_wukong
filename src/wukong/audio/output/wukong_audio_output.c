/**
 * @file wukong_audio_output.c
 * @brief 
 * @version 0.1
 * @date 2025-12-10
 * 
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 * 
 * Permission is hereby granted, to any person obtaining a copy of this software and
 * associated documentation files (the "Software"), Under the premise of complying 
 * with the license of the third-party open source software contained in the software,
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software.
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 */

#include "uni_log.h"
#include "wukong_audio_output.h"
#include "tal_mutex.h"

/* control_lock serializes owner transitions. write_lock is a drain barrier:
 * preemption stops the old backend, waits for any already-admitted write to
 * return, and only then starts the new owner. owner_lock keeps the fast owner
 * check independent from those potentially blocking backend calls. */
STATIC MUTEX_HANDLE s_control_lock = NULL;
STATIC MUTEX_HANDLE s_owner_lock = NULL;
STATIC MUTEX_HANDLE s_write_lock = NULL;
STATIC WUKONG_AUDIO_OUTPUT_OWNER_E s_owner = WUKONG_AUDIO_OUTPUT_OWNER_NONE;

STATIC BOOL_T __owner_valid(WUKONG_AUDIO_OUTPUT_OWNER_E owner)
{
    return owner > WUKONG_AUDIO_OUTPUT_OWNER_NONE &&
           owner <= WUKONG_AUDIO_OUTPUT_OWNER_P2P;
}

STATIC OPERATE_RET __locks_init(VOID)
{
    if (s_control_lock == NULL &&
        tal_mutex_create_init(&s_control_lock) != OPRT_OK) {
        return OPRT_MALLOC_FAILED;
    }
    if (s_owner_lock == NULL &&
        tal_mutex_create_init(&s_owner_lock) != OPRT_OK) {
        return OPRT_MALLOC_FAILED;
    }
    if (s_write_lock == NULL &&
        tal_mutex_create_init(&s_write_lock) != OPRT_OK) {
        return OPRT_MALLOC_FAILED;
    }
    return OPRT_OK;
}

OPERATE_RET wukong_audio_output_init(WUKONG_AUDIO_OUTPUT_CFG_T *cfg)
{
    OPERATE_RET rt;

    TUYA_CALL_ERR_RETURN(__locks_init());
    TUYA_CHECK_NULL_RETURN(g_audio_output_consumer.init, OPRT_RESOURCE_NOT_READY);

    tal_mutex_lock(s_control_lock);
    tal_mutex_lock(s_owner_lock);
    if (s_owner != WUKONG_AUDIO_OUTPUT_OWNER_NONE &&
        s_owner != WUKONG_AUDIO_OUTPUT_OWNER_PLAYER) {
        tal_mutex_unlock(s_owner_lock);
        tal_mutex_unlock(s_control_lock);
        return OPRT_RESOURCE_NOT_READY;
    }
    tal_mutex_unlock(s_owner_lock);
    rt = g_audio_output_consumer.init(cfg);
    tal_mutex_unlock(s_control_lock);
    return rt;
}

OPERATE_RET wukong_audio_output_deinit(VOID)
{
    OPERATE_RET rt;

    WUKONG_AUDIO_OUTPUT_OWNER_E old_owner;

    TUYA_CHECK_NULL_RETURN(g_audio_output_consumer.deinit, OPRT_RESOURCE_NOT_READY);
    if (!s_control_lock || !s_owner_lock || !s_write_lock) {
        return OPRT_RESOURCE_NOT_READY;
    }

    tal_mutex_lock(s_control_lock);
    tal_mutex_lock(s_owner_lock);
    if (s_owner != WUKONG_AUDIO_OUTPUT_OWNER_NONE &&
        s_owner != WUKONG_AUDIO_OUTPUT_OWNER_PLAYER) {
        tal_mutex_unlock(s_owner_lock);
        tal_mutex_unlock(s_control_lock);
        return OPRT_RESOURCE_NOT_READY;
    }
    old_owner = s_owner;
    s_owner = WUKONG_AUDIO_OUTPUT_OWNER_NONE;
    tal_mutex_unlock(s_owner_lock);

    if (old_owner == WUKONG_AUDIO_OUTPUT_OWNER_PLAYER) {
        if (!g_audio_output_consumer.stop) goto deinit_failed;
        rt = g_audio_output_consumer.stop();
        if (rt != OPRT_OK) goto deinit_failed;
    }

    tal_mutex_lock(s_write_lock);
    rt = g_audio_output_consumer.deinit();
    tal_mutex_unlock(s_write_lock);
    tal_mutex_unlock(s_control_lock);
    return rt;

deinit_failed:
    tal_mutex_lock(s_owner_lock);
    s_owner = old_owner;
    tal_mutex_unlock(s_owner_lock);
    tal_mutex_unlock(s_control_lock);
    return OPRT_RESOURCE_NOT_READY;
}

OPERATE_RET wukong_audio_output_start(VOID)
{
    return wukong_audio_output_start_owned(WUKONG_AUDIO_OUTPUT_OWNER_PLAYER);
}

OPERATE_RET wukong_audio_output_write(UINT8_T *data, UINT_T datalen)
{
    return wukong_audio_output_write_owned(WUKONG_AUDIO_OUTPUT_OWNER_PLAYER,
                                           data, datalen);
}

OPERATE_RET wukong_audio_output_stop(VOID)
{
    return wukong_audio_output_stop_owned(WUKONG_AUDIO_OUTPUT_OWNER_PLAYER);
}

OPERATE_RET wukong_audio_output_start_owned(WUKONG_AUDIO_OUTPUT_OWNER_E owner)
{
    OPERATE_RET rt;
    WUKONG_AUDIO_OUTPUT_OWNER_E old_owner;

    if (!__owner_valid(owner) || !s_control_lock || !s_owner_lock ||
        !s_write_lock) {
        return OPRT_RESOURCE_NOT_READY;
    }
    TUYA_CHECK_NULL_RETURN(g_audio_output_consumer.stop, OPRT_RESOURCE_NOT_READY);
    TUYA_CHECK_NULL_RETURN(g_audio_output_consumer.start, OPRT_RESOURCE_NOT_READY);

    tal_mutex_lock(s_control_lock);
    tal_mutex_lock(s_owner_lock);
    if (s_owner == owner) {
        tal_mutex_unlock(s_owner_lock);
        tal_mutex_unlock(s_control_lock);
        return OPRT_OK;
    }
    old_owner = s_owner;
    if (old_owner != WUKONG_AUDIO_OUTPUT_OWNER_NONE) {
        if (owner != WUKONG_AUDIO_OUTPUT_OWNER_P2P) {
            TAL_PR_WARN("audio output busy: owner=%d requester=%d", old_owner, owner);
            tal_mutex_unlock(s_owner_lock);
            tal_mutex_unlock(s_control_lock);
            return OPRT_RESOURCE_NOT_READY;
        }

        TAL_PR_NOTICE("audio output owner preempted: %d -> %d", old_owner, owner);
        s_owner = WUKONG_AUDIO_OUTPUT_OWNER_NONE;
        tal_mutex_unlock(s_owner_lock);
        rt = g_audio_output_consumer.stop();
        if (rt != OPRT_OK) {
            tal_mutex_lock(s_owner_lock);
            s_owner = old_owner;
            tal_mutex_unlock(s_owner_lock);
            tal_mutex_unlock(s_control_lock);
            return rt;
        }
    } else {
        tal_mutex_unlock(s_owner_lock);
    }

    /* stop() wakes a backend write, while this barrier waits until that old
     * call has actually returned. No new write is admitted while owner=NONE. */
    tal_mutex_lock(s_write_lock);
    rt = g_audio_output_consumer.start();
    tal_mutex_lock(s_owner_lock);
    s_owner = rt == OPRT_OK ? owner : WUKONG_AUDIO_OUTPUT_OWNER_NONE;
    tal_mutex_unlock(s_owner_lock);
    tal_mutex_unlock(s_write_lock);
    tal_mutex_unlock(s_control_lock);
    return rt;
}

OPERATE_RET wukong_audio_output_write_owned(WUKONG_AUDIO_OUTPUT_OWNER_E owner,
                                            UINT8_T *data, UINT_T datalen)
{
    OPERATE_RET rt = OPRT_RESOURCE_NOT_READY;
    BOOL_T allowed;

    if (!__owner_valid(owner) || !s_owner_lock || !s_write_lock) {
        return OPRT_RESOURCE_NOT_READY;
    }
    TUYA_CHECK_NULL_RETURN(g_audio_output_consumer.write, OPRT_RESOURCE_NOT_READY);

    tal_mutex_lock(s_write_lock);
    tal_mutex_lock(s_owner_lock);
    allowed = (s_owner == owner);
    tal_mutex_unlock(s_owner_lock);
    if (allowed) rt = g_audio_output_consumer.write(data, datalen);
    tal_mutex_unlock(s_write_lock);
    return rt;
}

OPERATE_RET wukong_audio_output_stop_owned(WUKONG_AUDIO_OUTPUT_OWNER_E owner)
{
    OPERATE_RET rt;

    if (!__owner_valid(owner) || !s_control_lock || !s_owner_lock ||
        !s_write_lock) {
        return OPRT_RESOURCE_NOT_READY;
    }
    TUYA_CHECK_NULL_RETURN(g_audio_output_consumer.stop, OPRT_RESOURCE_NOT_READY);

    tal_mutex_lock(s_control_lock);
    tal_mutex_lock(s_owner_lock);
    if (s_owner != owner) {
        tal_mutex_unlock(s_owner_lock);
        tal_mutex_unlock(s_control_lock);
        return OPRT_OK;
    }
    s_owner = WUKONG_AUDIO_OUTPUT_OWNER_NONE;
    tal_mutex_unlock(s_owner_lock);

    rt = g_audio_output_consumer.stop();
    if (rt != OPRT_OK) {
        tal_mutex_lock(s_owner_lock);
        s_owner = owner;
        tal_mutex_unlock(s_owner_lock);
        tal_mutex_unlock(s_control_lock);
        return rt;
    }

    tal_mutex_lock(s_write_lock);
    tal_mutex_unlock(s_write_lock);
    tal_mutex_unlock(s_control_lock);
    return rt;
}

WUKONG_AUDIO_OUTPUT_OWNER_E wukong_audio_output_owner_get(VOID)
{
    WUKONG_AUDIO_OUTPUT_OWNER_E owner;

    if (!s_owner_lock) return WUKONG_AUDIO_OUTPUT_OWNER_NONE;
    tal_mutex_lock(s_owner_lock);
    owner = s_owner;
    tal_mutex_unlock(s_owner_lock);
    return owner;
}

OPERATE_RET wukong_audio_output_set_vol(INT32_T volume)
{
    TUYA_CHECK_NULL_RETURN(g_audio_output_consumer.set_vol, OPRT_RESOURCE_NOT_READY);
    return g_audio_output_consumer.set_vol(volume);
}
