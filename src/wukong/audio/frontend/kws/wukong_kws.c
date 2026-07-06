/**
 * @file wukong_kws.c
 * @brief Wukong AI keyword spotting module implementation
 *
 * Uses ring buffer and worker thread; PCM from mic or VAD is fed in and engine
 * detect callback is invoked. VAD throttling: is_detect_vad=1 triggers only on
 * voice or VAD end to reduce redundant computation.
 *
 * @copyright Copyright (c) Tuya Inc.
 */

#include "wukong_kws.h"
#include "tuya_app_config.h"
#include "tuya_ringbuf.h"
#include "base_event.h"
#include "tal_thread.h"
#include "tal_memory.h"
#include "tal_system.h"
#include "tal_mutex.h"
#include "tal_semaphore.h"
#include "tal_log.h"
#include "tal_workq_service.h"
#include "tutuclear/tutuclear.h"
#include "sndx/sndx.h"
#include "uart/uart.h"
#include "audio_dump.h"

#if defined(USING_IFLY_DUAL_MIC) && (USING_IFLY_DUAL_MIC == 1)
#include "ifly/ifly_kws.h"
#endif

#include <string.h>

#if !defined(SEM_WAIT_FOREVER)
#define SEM_WAIT_FOREVER  (0xFFFFFFFFUL)
#endif

/** @brief Single frame PCM bytes (20ms @ 16kHz 16bit mono) */
#define WUKONG_KWS_ONE_FRAME       (320 * 2)
/** @brief VAD timeout; frames to drop after (1000ms) */
#define WUKONG_KWS_VAD_FRAME       ((WUKONG_KWS_ONE_FRAME * 50))
/** @brief Ring buffer capacity (2s) */
#define WUKONG_KWS_BUFSZ           ((WUKONG_KWS_ONE_FRAME * 40) * 2)
/** @brief Min frames to accumulate before triggering detect (100ms) */
#define WUKONG_KWS_DETECT_FRAME    (WUKONG_KWS_ONE_FRAME * 5)

/** @brief Internal command enum (reserved for extension) */
typedef enum {
    WUKONG_KWS_ENABLE_CMD = 0,
    WUKONG_KWS_DISABLE_CMD,
    WUKONG_KWS_VAD_DETECT_CMD
} WUKONG_KWS_CMD_E;

/** @brief KWS module internal management structure */
typedef struct {
    BOOL_T                      init;
    BOOL_T                      enable;
    BOOL_T                      thread_running;
    UINT8_T                     vadflag;
    WUKONG_KWS_CTX_T            ctx;
    WUKONG_KWS_CFG_T            cfg;
    TUYA_RINGBUFF_T             rb;
    MUTEX_HANDLE                mutex;
    SEM_HANDLE                  sem;
    THREAD_HANDLE               thread;
    UINT32_T                    bufsz;
    UINT8_T                     *oneframe;
    UINT8_T                     *buffer;
} WUKONG_KWS_T;

STATIC WUKONG_KWS_T s_wukong_kws_mgr = {0};


/**
 * @brief Workqueue callback: publish KWS wakeup event on HIGHTPRI work queue
 * @param[in] data Wakeup index encoded as (VOID_T *)(uintptr_t)wakeup_kws_index
 * @return none
 * @note Called asynchronously so the detect thread is not blocked by
 *       downstream event subscribers (e.g. stop-TTS, reset-ringbuf, play-alert).
 */
STATIC VOID_T __kws_event_work(VOID_T *data)
{
    ty_publish_event(EVENT_WUKONG_KWS_WAKEUP, data);
}

/**
 * @brief Report wake event; schedules EVENT_WUKONG_KWS_WAKEUP on HIGHTPRI work queue
 * @param wakeup_kws_index Detected wake word index
 * @return none
 * @note Must return quickly; do NOT call ty_publish_event directly here because
 *       its subscribers execute synchronously and would block the detect thread.
 */
VOID wukong_kws_event(WUKONG_KWS_INDEX_E wakeup_kws_index)
{
    OPERATE_RET rt = tal_workq_schedule(WORKQ_HIGHTPRI, __kws_event_work,
                                        (VOID_T *)(uintptr_t)wakeup_kws_index);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("wukong_kws_event: schedule failed rt=%d, fallback sync", rt);
        ty_publish_event(EVENT_WUKONG_KWS_WAKEUP, (VOID_T *)(uintptr_t)wakeup_kws_index);
    }
}

#if defined(USING_BOARD_AUDIO_INPUT) && (USING_BOARD_AUDIO_INPUT == 1)
/**
 * @brief KWS detect worker thread: read PCM from ring buffer, call engine detect
 * @param args Unused
 */
VOID __wukong_kws_thread(VOID *args)
{
    (void)args;
    INT_T rt = 0;
    UINT32_T  readlen;

    tal_system_sleep(200);

    WUKONG_KWS_T *wwm = &s_wukong_kws_mgr;

    if (wwm->cfg.create) {
        rt = wwm->cfg.create(&wwm->ctx);
    }
    if (rt != 0) {
        TAL_PR_DEBUG("wukong kws thread create failed %d.\n", rt);
        goto __err_exit;
    }

    UINT_T timeout = 100;
    while (!wwm->thread_running && (tal_thread_get_state(wwm->thread) == THREAD_STATE_RUNNING)) {
        timeout = wwm->cfg.is_detect_vad ? SEM_WAIT_FOREVER : 100;
        rt = tal_semaphore_wait(wwm->sem, timeout);
        // if (rt != OPRT_OK) {
            // timeout = TKL_QUEUE_WAIT_FROEVER;
            // if (wwm->cfg.reset) {
            //     wwm->cfg.reset(&wwm->ctx);
            // }
            // continue;
        // }
        // timeout = 500;
        tal_mutex_lock(wwm->mutex);
        rt = tuya_ring_buff_used_size_get(wwm->rb);
        if (wwm->cfg.detect_sample_bytes > 0) {
            UINT32_T unit  = wwm->cfg.detect_sample_bytes;
            UINT32_T avail = (rt < wwm->bufsz) ? rt : wwm->bufsz;
            UINT32_T aligned = (avail / unit) * unit;
            if (aligned == 0) {
                tal_mutex_unlock(wwm->mutex);
                continue;
            }
            // TAL_PR_DEBUG("kws detect: rb_used=%d aligned=%u", rt, aligned);
            readlen = tuya_ring_buff_read(wwm->rb, wwm->buffer, aligned);
        } else {
            readlen = tuya_ring_buff_read(wwm->rb, wwm->buffer, wwm->bufsz);
        }
        tal_mutex_unlock(wwm->mutex);
        // TAL_PR_DEBUG("wakeupword used len %d, read len %d, bufsz %d.\n", rt, readlen, wwm->bufsz);
        if (readlen <= 0) {
            // TAL_PR_DEBUG("wakeupword need more data %d.\n", readlen);
            continue;
        }
#if !defined(WUKONG_BOARD_UBUNTU) || (WUKONG_BOARD_UBUNTU == 0)
        audio_dump_write(AUDIO_DUMP_KWS, wwm->buffer, readlen);
#endif
        if (wwm->cfg.detect && readlen) {
            rt = wwm->cfg.detect(&wwm->ctx, wwm->buffer, readlen);
            if (OPRT_OK == rt && wwm->cfg.reset) {
                tal_mutex_lock(wwm->mutex);
                tuya_ring_buff_reset(wwm->rb);
                tal_mutex_unlock(wwm->mutex);
                wwm->cfg.reset(&wwm->ctx);
            }
        }
    }

    TAL_PR_DEBUG("wukong_kws_thread is exit");
__err_exit:
    wukong_kws_deinit(wwm);
}
#endif

/**
 * @brief Internal deinit; release KWS resources (thread, buffer, engine)
 * @param wwm Module manager pointer; must not be NULL
 * @return 0 or error code
 */
INT_T wukong_kws_deinit(WUKONG_KWS_T *wwm)
{
    INT_T rt = 0;

    TUYA_CHECK_NULL_RETURN(wwm, OPRT_INVALID_PARM);

#if defined(USING_BOARD_AUDIO_INPUT) && (USING_BOARD_AUDIO_INPUT == 1)
    wwm->init = FALSE;

    if (wwm->thread) {
        rt = tal_thread_delete(wwm->thread);
        TAL_PR_DEBUG("wukong_kws_thread is del: %d", rt);
        wwm->thread = NULL;
    }
    if (wwm->oneframe) {
        tal_free(wwm->oneframe);
    }
    if (wwm->buffer) {
        tal_free(wwm->buffer);
    }
    if (wwm->mutex) {
        rt = tal_mutex_release(wwm->mutex);
    }
    if (wwm->sem) {
        tal_semaphore_release(wwm->sem);
    }
    if (wwm->rb) {
        rt |= tuya_ring_buff_free(wwm->rb);
    }
    if (wwm->cfg.deinit) {
        rt |= wwm->cfg.deinit(&wwm->ctx);
    }
#else
    rt = wukong_kws_uart_deinit();
#endif
    TAL_PR_DEBUG("wukong_kws_deinit ret %d", rt);

    return rt;
}

#if defined(USING_BOARD_AUDIO_INPUT) && (USING_BOARD_AUDIO_INPUT == 1)
/**
 * @brief Write one frame PCM to ring buffer
 * @param data PCM data pointer
 * @param datalen Data byte length
 * @return Bytes written; OPRT_RESOURCE_NOT_READY if not inited or disabled
 */
STATIC INT_T __wukong_kws_feed(UINT8_T *data, UINT16_T datalen)
{
    INT_T rt = 0;
    WUKONG_KWS_T *wwm = &s_wukong_kws_mgr;

    if (!wwm->init || !wwm->enable) {
        return OPRT_RESOURCE_NOT_READY;
    }

    tal_mutex_lock(wwm->mutex);
    rt = tuya_ring_buff_write(wwm->rb, data, datalen);
    tal_mutex_unlock(wwm->mutex);

    if (rt != datalen) {
        TAL_PR_DEBUG("wukong kws feed overflow data %d, write %d", datalen, rt);
    }

    return rt;
}

/**
 * @brief Notify detect thread data available (post semaphore)
 * @param force 1=force post; 0=post only when buffer data >= DETECT_FRAME
 * @return OPRT_OK
 */
STATIC OPERATE_RET __wukong_kws_post(UINT8_T force)
{
    OPERATE_RET rt = OPRT_OK;
    WUKONG_KWS_T *wwm = &s_wukong_kws_mgr;

    if (tuya_ring_buff_used_size_get(wwm->rb) >= WUKONG_KWS_DETECT_FRAME || force) {
        rt = tal_semaphore_post(wwm->sem);
    }
    return rt;
}

/**
 * @brief Drop excess data after VAD timeout; keep last WUKONG_KWS_VAD_FRAME bytes
 * @return OPRT_OK or OPRT_RESOURCE_NOT_READY when nothing dropped
 */
STATIC INT_T __wukong_kws_drop(VOID)
{
    WUKONG_KWS_T *wwm = &s_wukong_kws_mgr;

    if (!wwm->init || !wwm->enable) {
        return OPRT_RESOURCE_NOT_READY;
    }

    UINT32_T rblen =  tuya_ring_buff_used_size_get(wwm->rb);
    if (rblen >= WUKONG_KWS_VAD_FRAME) {
        /*! FIXME: avoid dummy read; discard via rb directly */
        UINT32_T drop_len   = rblen - WUKONG_KWS_VAD_FRAME;
        INT_T     frame_count = drop_len / WUKONG_KWS_ONE_FRAME;
        tal_mutex_lock(wwm->mutex);
        while (frame_count > 0) {
            tuya_ring_buff_read(wwm->rb, wwm->oneframe, WUKONG_KWS_ONE_FRAME);
            drop_len -= WUKONG_KWS_ONE_FRAME;
            frame_count--;
        }
        
        if (drop_len) {
            tuya_ring_buff_read(wwm->rb, wwm->oneframe, drop_len);
        }
        tal_mutex_unlock(wwm->mutex);
    }

    return OPRT_OK;
}

INT_T wukong_kws_feed_with_vad(UINT8_T *data, UINT16_T datalen, UINT8_T vadflag)
{
    WUKONG_KWS_T *wwm = &s_wukong_kws_mgr;

    if (!wwm->init || !wwm->enable) {
        return OPRT_RESOURCE_NOT_READY;
    }

    (void)__wukong_kws_feed(data, datalen);

    if (wwm->cfg.is_detect_vad) {
        /* VAD-gated path: only drive detect when voice is active or just ended */
        UINT8_T vad_change = 0;
        if (vadflag != wwm->vadflag) {
            if (1 == vadflag && 0 == wwm->vadflag) {
                TAL_PR_DEBUG("wukong kws vad on");
            } else if (0 == vadflag && 1 == wwm->vadflag) {
                TAL_PR_DEBUG("wukong kws vad end");
                vad_change = 1;
            }
            wwm->vadflag = vadflag;
        }
        /* 0-0: vad off -> drop; 0-1/1-1: vad active -> post; 1-0: vad end -> force post */
        if (wwm->vadflag || vad_change) {
            __wukong_kws_post(vad_change);
        } else {
            __wukong_kws_drop();
        }
    } else {
        /* Non-VAD path (e.g. ifly IVW): post every frame so detect runs continuously */
        __wukong_kws_post(0);
    }

    return OPRT_OK;
}
#else
INT_T wukong_kws_feed_with_vad(UINT8_T *data, UINT16_T datalen, UINT8_T vadflag)
{
    (void)data;
    (void)datalen;
    (void)vadflag;
    return OPRT_OK;
}
#endif

/**
 * @brief Initialize KWS with custom engine config
 * @param cfg Engine callbacks and config; must not be NULL
 * @return OPRT_OK on success; see tuya_error_code.h for others
 */
INT_T wukong_kws_init(WUKONG_KWS_CFG_T *cfg)
{
    TUYA_CHECK_NULL_RETURN(cfg, OPRT_INVALID_PARM);

#if defined(USING_BOARD_AUDIO_INPUT) && (USING_BOARD_AUDIO_INPUT == 1)
    WUKONG_KWS_T *wwm = &s_wukong_kws_mgr;
    if (wwm->init) {
        return OPRT_OK;
    }

    INT_T rt = 0;
    memset(wwm, 0, sizeof(WUKONG_KWS_T));
    wwm->thread_running = FALSE;
    wwm->bufsz    = WUKONG_KWS_BUFSZ;
#ifdef TUYA_PSARM_SUPPORT    
    wwm->oneframe = tal_psram_malloc(WUKONG_KWS_ONE_FRAME);
    wwm->buffer   = tal_psram_malloc(wwm->bufsz);
#else
    wwm->oneframe = tal_malloc(WUKONG_KWS_ONE_FRAME);
    wwm->buffer   = tal_malloc(wwm->bufsz);
#endif
    TUYA_CHECK_NULL_RETURN(wwm->buffer, OPRT_MALLOC_FAILED);
    TUYA_CHECK_NULL_RETURN(wwm->oneframe, OPRT_MALLOC_FAILED);
    memcpy(&wwm->cfg, cfg, sizeof(WUKONG_KWS_CFG_T));

    TUYA_CALL_ERR_GOTO(tal_semaphore_create_init(&wwm->sem, 0, 500), __err_exit);
    TUYA_CALL_ERR_GOTO(tal_mutex_create_init(&wwm->mutex), __err_exit);
    #ifdef ENABLE_EXT_RAM
    TUYA_CALL_ERR_GOTO(tuya_ring_buff_create(wwm->bufsz + 1, OVERFLOW_PSRAM_STOP_TYPE, &wwm->rb), __err_exit);
    #else
    TUYA_CALL_ERR_GOTO(tuya_ring_buff_create(wwm->bufsz + 1, OVERFLOW_STOP_TYPE, &wwm->rb), __err_exit);
    #endif

    THREAD_CFG_T thread_param = {0};
    thread_param.stackDepth = 1024*6;
    thread_param.priority = THREAD_PRIO_1;
    thread_param.thrdname = "wukong_kws";
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    thread_param.psram_mode = 1;
#endif
    TUYA_CALL_ERR_GOTO(tal_thread_create_and_start(&wwm->thread, NULL, NULL, __wukong_kws_thread, NULL, &thread_param), __err_exit);

    wwm->init = TRUE;
    TAL_PR_DEBUG("wukong kws init success vad %d", wwm->cfg.is_detect_vad);

    return OPRT_OK;

__err_exit:
    wukong_kws_deinit(wwm);
    TAL_PR_DEBUG("wukong kws init failed %d", rt);
    return rt;
#else
    return wukong_kws_uart_init();
#endif
}


INT_T wukong_kws_enable(VOID)
{
#if defined(USING_BOARD_AUDIO_INPUT) && (USING_BOARD_AUDIO_INPUT == 1)
    WUKONG_KWS_T *wwm = &s_wukong_kws_mgr;
    if (!wwm->init || !wwm->rb) {
        return OPRT_RESOURCE_NOT_READY;
    }
    tal_mutex_lock(wwm->mutex);
    tuya_ring_buff_reset(wwm->rb);
    tal_mutex_unlock(wwm->mutex);
    wwm->enable = TRUE;
#else
    wukong_kws_uart_start();
#endif
    return OPRT_OK;
}


INT_T wukong_kws_disable(VOID)
{
#if defined(USING_BOARD_AUDIO_INPUT) && (USING_BOARD_AUDIO_INPUT == 1)
    WUKONG_KWS_T *wwm = &s_wukong_kws_mgr;
    if (!wwm->init || !wwm->rb) {
        return OPRT_RESOURCE_NOT_READY;
    }
    wwm->enable = FALSE;
    tal_mutex_lock(wwm->mutex);
    tuya_ring_buff_reset(wwm->rb);
    tal_mutex_unlock(wwm->mutex);
#else
    wukong_kws_uart_stop();
#endif
    return OPRT_OK;
}


INT_T wukong_kws_set_vad_detect(UINT8_T is_detect_vad)
{
#if defined(USING_BOARD_AUDIO_INPUT) && (USING_BOARD_AUDIO_INPUT == 1)
    WUKONG_KWS_T *wwm = &s_wukong_kws_mgr;
    wwm->cfg.is_detect_vad = is_detect_vad;
#else

#endif
    return OPRT_OK;
}

INT_T wukong_kws_uninit(VOID)
{
#if defined(USING_BOARD_AUDIO_INPUT) && (USING_BOARD_AUDIO_INPUT == 1)
    WUKONG_KWS_T *wwm = &s_wukong_kws_mgr;
    wwm->thread_running = TRUE;  /* signal thread to exit */
    if (wwm->sem) {
        tal_semaphore_post(wwm->sem);  /* wake blocked thread */
    }
#else
    wukong_kws_uart_deinit();
#endif
    return OPRT_OK;
}


/**
 * @brief Initialize KWS with default engine (TUTUClear)
 * @return OPRT_OK on success; see tuya_error_code.h for others
 */
INT_T wukong_kws_default_init(VOID)
{
#if defined(USING_BOARD_AUDIO_INPUT) && (USING_BOARD_AUDIO_INPUT == 1)
#if defined(TUYA_MODULE_T5) && (TUYA_MODULE_T5 == 1)
    WUKONG_KWS_CFG_T cfg;
    memset(&cfg, 0, sizeof(WUKONG_KWS_CFG_T));
#if defined(USING_IFLY_DUAL_MIC) && (USING_IFLY_DUAL_MIC == 1)
    cfg.create = ifly_kws_create;
    cfg.detect = ifly_kws_detect;
    cfg.reset  = ifly_kws_reset;
    cfg.deinit = ifly_kws_deinit;
    cfg.is_detect_vad = 1;
    cfg.detect_sample_bytes = 160 * 2;
    TAL_PR_DEBUG("default KWS ifly ivw");
#else
    // cfg.create = SNDX_kws_create;
    // cfg.detect = SNDX_kws_detect;
    // cfg.reset  = SNDX_kws_reset;
    // cfg.deinit = SNDX_kws_deinit;
    cfg.create = TUTUClear_kws_create;
    cfg.detect = TUTUClear_kws_detect;
    cfg.reset  = TUTUClear_kws_reset;
    cfg.deinit = TUTUClear_kws_deinit;
    cfg.is_detect_vad = 1;
    TAL_PR_DEBUG("default KWS tutuclear ver %x", TUTUCLEAR_VERSION);
#endif
    return wukong_kws_init(&cfg);
#else
    return OPRT_OK;
#endif
#else
    return wukong_kws_uart_init();
#endif
}
