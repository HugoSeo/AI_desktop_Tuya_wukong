
#include "wukong_audio_input.h"
#include "wukong_audio_input_frame.h"
#include "wukong_audio_pipeline.h"
#include "wukong_audio_frontend.h"
#include "tuya_device_cfg.h"
#include "tuya_queue.h"
#include "base_event.h"
#include "tal_mutex.h"
#include "tal_semaphore.h"
#include "tal_queue.h"
#include "tal_log.h"
#include "tal_thread.h"
#include "tal_memory.h"

#include "audio_dump.h"
#include <string.h>

typedef struct {
    //! flags below are written/read across the producer callback, the record task and
    //! app threads; they are word-sized and accessed as simple flags, volatile keeps the
    //! compiler from caching them in the record task loop
    volatile BOOL_T              enable;
    volatile BOOL_T              task_exit;      //! request the record task to exit

    //! wakeup
    volatile BOOL_T              wakeup_flag;

    //! vad
    volatile WUKONG_AUDIO_VAD_MODE_E vad_mode;
    volatile WUKONG_AUDIO_VAD_FLAG_E vad_flag;
    UINT32_T                     vad_size;
    THREAD_HANDLE                vad_task;
    SEM_HANDLE                   exit_sem;       //! posted by the record task right before it stops touching recorder
    TUYA_QUEUE_HANDLE            queue;          //! FIFO of fixed-size mic frames
    UINT16_T                     item_size;      //! bytes per queued frame (one mic frame)
    UINT16_T                     frames_per_slice; //! frames assembled into one output slice
    MUTEX_HANDLE                 mutex;

    UINT32_T                     slice_size;
    UINT8_T                      *slice_buf;     //! preallocated slice assembly buffer (record task only)
    WUKONG_AUDIO_OUTPUT          output_cb;
    VOID                         *user_data;
} AUDIO_RECODER_T;

STATIC AUDIO_RECODER_T *recorder = NULL;

STATIC OPERATE_RET __audio_slice_check_and_send(BOOL_T *more_data)
{
    *more_data = FALSE;
    if (recorder->vad_flag != WUKONG_AUDIO_VAD_START) {
        return OPRT_OK;
    }

    UINT32_T read_len = 0;
    BOOL_T   ready    = FALSE;

    //! assemble one slice into the preallocated buffer while holding the lock; never
    //! allocate here so the producer (IPC sync context) is not blocked behind malloc
    tal_mutex_lock(recorder->mutex);
    UINT32_T used = tuya_queue_get_used_num(recorder->queue);
    if (used >= recorder->frames_per_slice) {
        UINT16_T i;
        BOOL_T ok = TRUE;
        for (i = 0; i < recorder->frames_per_slice; i++) {
            if (OPRT_OK != tuya_queue_output(recorder->queue, recorder->slice_buf + i * recorder->item_size)) {
                ok = FALSE;
                break;
            }
        }
        if (ok) {
            read_len  = (UINT32_T)recorder->frames_per_slice * recorder->item_size;
            ready     = TRUE;
            *more_data = TRUE;
        } else {
            //! unreachable in practice: lock is held and used >= frames_per_slice was checked
            TAL_PR_ERR("wukong audio input -> dequeue failed at frame %d/%d", i, recorder->frames_per_slice);
        }
    }
    tal_mutex_unlock(recorder->mutex);

    //! deliver outside the lock; slice_buf is owned by the record task only
    if (ready && read_len > 0) {
        recorder->output_cb(recorder->slice_buf, (UINT16_T)read_len);
#if defined(TUYA_MODULE_T5) && (TUYA_MODULE_T5 == 1)
        audio_dump_write(AUDIO_DUMP_VAD, recorder->slice_buf, read_len);
#endif
    }
    return OPRT_OK;
}

STATIC INT_T __audio_frame_put(UINT8_T *buf, UINT32_T len)
{
    TUYA_CHECK_NULL_RETURN(recorder, OPRT_RESOURCE_NOT_READY);
    TUYA_CHECK_NULL_RETURN(buf, OPRT_INVALID_PARM);
    INT_T rt = 0;

    if (!recorder->enable) {
        return 0;
    }

    //! queue stores fixed-size processed mono frames
    if (len != recorder->item_size) {
        TAL_PR_DEBUG("wukong audio input -> frame size %d != item_size %d, drop", (INT_T)len, recorder->item_size);
        return 0;
    }

    if (recorder->vad_mode == WUKONG_AUDIO_VAD_MANUAL) {
        //! frames arrive from CP1 over an IPC sync message; we must not block in that
        //! context, so only cache while woken and let the record task assemble slices
        if (recorder->vad_flag == WUKONG_AUDIO_VAD_START) {
            tal_mutex_lock(recorder->mutex);
            //! queue full -> drop newest frame (same as ringbuffer OVERFLOW_STOP)
            rt = (OPRT_OK == tuya_queue_input(recorder->queue, buf)) ? (INT_T)len : 0;
            tal_mutex_unlock(recorder->mutex);
            if (0 == rt) {
                //! consumer stalled (slow downstream IO): audio is being lost, shout
                //! rate-limited so a long stall does not flood the log (256 frames ~5s)
                STATIC UINT32_T s_manual_drop_cnt = 0;
                if ((++s_manual_drop_cnt & 0xFF) == 1) {
                    TAL_PR_WARN("wukong audio input -> manual queue full, %u frames dropped", s_manual_drop_cnt);
                }
            }
        }
    } else {
        tal_mutex_lock(recorder->mutex);
        if (0 == tuya_queue_get_free_num(recorder->queue)) {
            //! if queue is full, drop the oldest frame to make room
            tuya_queue_output(recorder->queue, NULL);
        }
        if (OPRT_OK == tuya_queue_input(recorder->queue, buf)) {
            rt = (INT_T)len;
        } else {
            rt = 0;
            TAL_PR_DEBUG("wukong audio input -> overflow %d", len);
        }
        tal_mutex_unlock(recorder->mutex);
    }

    return rt;
}

STATIC VOID __update_vad_flag(WUKONG_AUDIO_VAD_FLAG_E flag)
{
    TAL_PR_DEBUG("wukong audio input -> vad stat change to flag %d", flag);
    if (recorder->wakeup_flag) {
        ty_publish_event(EVENT_AUDIO_VAD, (VOID*)flag);
    }
}


/**
 * @brief Release all recorder resources and clear the global handle
 * @return none
 * @note Only call once the record task is guaranteed not to touch recorder any more
 *       (creation error path, or deinit after the record task confirmed exit).
 */
STATIC VOID_T __audio_recorder_destroy(VOID_T)
{
    if (NULL == recorder) {
        return;
    }

    if (recorder->queue) {
        tuya_queue_release(recorder->queue);
        recorder->queue = NULL;
    }
    if (recorder->slice_buf) {
        MEM_FREE(recorder->slice_buf);
        recorder->slice_buf = NULL;
    }
    if (recorder->exit_sem) {
        tal_semaphore_release(recorder->exit_sem);
        recorder->exit_sem = NULL;
    }
    if (recorder->mutex) {
        tal_mutex_release(recorder->mutex);
        recorder->mutex = NULL;
    }

    tal_free(recorder);
    recorder = NULL;
}

STATIC VOID_T __record_task(PVOID_T arg)
{
    BOOL_T more_data = FALSE;

    while (!recorder->task_exit) {
        //! mic disable or not wakeup, dont need to send vad stat change
        if (!recorder->enable || !recorder->wakeup_flag) {
            tal_system_sleep(10);
            continue;
        }

        __audio_slice_check_and_send(&more_data);

        //! manual mode dont need send vad stat change
        if (recorder->vad_mode == WUKONG_AUDIO_VAD_AUTO) {
            WUKONG_AUDIO_VAD_FLAG_E stat = wukong_audio_frontend_vad_get_flag();
            if (stat != recorder->vad_flag) {
                TAL_PR_DEBUG("wukong audio input -> wakup flag is %d, auto vad set from %d to %d!", recorder->wakeup_flag, recorder->vad_flag, stat);
                recorder->vad_flag = stat;
                __update_vad_flag(recorder->vad_flag);
            }
        }

        //! no full slice was assembled this round -> wait roughly one frame for new data
        //! instead of spinning the CPU and hammering the queue lock
        //! 无满片时按"采集节奏"等待新数据：数据以采集帧(16ms)为心跳入队，
        //! 用处理帧(32ms)粒度会多睡一拍、平白增加组片与上行延迟
        if (!more_data) {
            tal_system_sleep(AUDIO_INPUT_CAPTURE_FRAME_MS);
        }
    }

    //! hand control back to deinit: from here we no longer touch the recorder context
    if (recorder->exit_sem) {
        tal_semaphore_post(recorder->exit_sem);
    }
}

STATIC AUDIO_RECODER_T *__audio_recorder_create(WUKONG_AUDIO_INPUT_CFG_T *cfg)
{
    if (recorder) {
        return recorder;
    }

    OPERATE_RET rt = OPRT_OK;
    TUYA_CHECK_NULL_RETURN(recorder = tal_calloc(1, sizeof(AUDIO_RECODER_T)), NULL);

    recorder->user_data = cfg->board.user_data;
    recorder->output_cb = cfg->board.output_cb;
    recorder->vad_mode  = cfg->board.vad_mode;

    UINT32_T audio_1ms_size = (UINT32_T)cfg->board.sample_rate * cfg->board.sample_bits * cfg->board.channel / 8 / 1000;
    if (0 == audio_1ms_size) {
        TAL_PR_ERR("invalid audio params: sample %d, bits %d, channel %d", cfg->board.sample_rate, cfg->board.sample_bits, cfg->board.channel);
        __audio_recorder_destroy();
        return NULL;
    }

    //! item/slice sizing (compute in 32-bit to avoid truncation); everything past this
    //! point (queue_create / slice_buf alloc / mutex / sem) is shared.
    UINT32_T item_size;
    UINT32_T slice_size;
    UINT32_T vad_size = ((UINT32_T)cfg->board.vad_active_ms + 400) * audio_1ms_size + 1;   //! add 400ms offset

    //! one queued item == one process/output frame the process task puts. Both topologies
    //! deal in process frames: SPRS 32ms, tuya 20ms (== its capture frame); both equal
    //! AUDIO_INPUT_PROCESS_FRAME_MS. slice is aligned up to a whole number of process frames
    //! so the frames_per_slice/item_size relation stays exact.
    item_size = (UINT32_T)AUDIO_INPUT_PROCESS_FRAME_MS * audio_1ms_size;
    UINT32_T frames_per_slice = cfg->board.slice_ms / AUDIO_INPUT_PROCESS_FRAME_MS;
    if ((cfg->board.slice_ms % AUDIO_INPUT_PROCESS_FRAME_MS) != 0) {
        frames_per_slice++;   //! not an exact multiple -> round up one extra frame
    }
    slice_size = frames_per_slice * item_size;
    //! item_size feeds the queue; slice_size is delivered through output_cb whose length
    //! argument is UINT16_T, so both must stay within UINT16_T range
    if (0 == item_size || item_size > 0xFFFF || 0 == slice_size || slice_size > 0xFFFF) {
        TAL_PR_ERR("frame/slice size invalid: item %d, slice %d (must be non-zero within 64KB)", item_size, slice_size);
        __audio_recorder_destroy();
        return NULL;
    }

    recorder->item_size        = (UINT16_T)item_size;
    recorder->slice_size       = slice_size;
    recorder->vad_size         = vad_size;
    recorder->frames_per_slice = (UINT16_T)(slice_size / item_size);

    //! queue depth in frames, round up so capacity is at least vad_size bytes, but never
    //! below one full slice so a slice can always be assembled
    UINT32_T queue_depth = (vad_size + item_size - 1) / item_size;
    if (queue_depth < recorder->frames_per_slice) {
        queue_depth = recorder->frames_per_slice;
    }

    recorder->slice_buf = (UINT8_T *)MEM_MALLOC(recorder->slice_size);
    if (NULL == recorder->slice_buf) {
        TAL_PR_ERR("alloc slice buffer %d failed", recorder->slice_size);
        __audio_recorder_destroy();
        return NULL;
    }

    TUYA_CALL_ERR_GOTO(tuya_queue_create(queue_depth, recorder->item_size, &recorder->queue), __error);
    TUYA_CALL_ERR_GOTO(tal_mutex_create_init(&recorder->mutex), __error);
    TUYA_CALL_ERR_GOTO(tal_semaphore_create_init(&recorder->exit_sem, 0, 1), __error);
    TAL_PR_DEBUG("recorder vad mode %d", cfg->board.vad_mode);
    TAL_PR_DEBUG("recorder queue depth %d frames, frame %d B, frames/slice %d, slice ms %d, vad active %d ms, vad off timeout %d",
                 queue_depth, recorder->item_size, recorder->frames_per_slice, cfg->board.slice_ms, cfg->board.vad_active_ms, cfg->board.vad_off_ms);
    return recorder;

__error:
    __audio_recorder_destroy();
    return NULL;
}

OPERATE_RET wukong_audio_board_input_start(VOID)
{
    TUYA_CHECK_NULL_RETURN(recorder, OPRT_RESOURCE_NOT_READY);

    TAL_PR_NOTICE("wukong audio input -> start! mode is %d, task is %p", recorder->vad_mode, recorder->vad_task);
    recorder->enable = TRUE;

    if (recorder->vad_mode == WUKONG_AUDIO_VAD_AUTO) {
        TAL_PR_DEBUG("need hunman voice detect, start __record_task");
        wukong_audio_frontend_vad_start();
    }

    return OPRT_OK;
}

OPERATE_RET wukong_audio_board_input_stop(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    TUYA_CHECK_NULL_RETURN(recorder, OPRT_OK);

    TAL_PR_NOTICE("wukong audio input -> stop! mode is %d, task is %p", recorder->vad_mode, recorder->vad_task);
    recorder->enable = FALSE;
    if (recorder->vad_mode == WUKONG_AUDIO_VAD_AUTO) {
        wukong_audio_frontend_vad_stop();
    }

    //! enable is now FALSE, so neither the producer nor the record task touches the
    //! queue; drop cached frames so the next session never starts with stale audio
    if (recorder->queue && recorder->mutex) {
        tal_mutex_lock(recorder->mutex);
        tuya_queue_clear(recorder->queue);
        tal_mutex_unlock(recorder->mutex);
    }

    return rt;
}

OPERATE_RET wukong_audio_board_input_deinit(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    TUYA_CHECK_NULL_RETURN(recorder, OPRT_OK);

    //! stop input (disable capture, stop frontend vad, drop cache)
    TUYA_CALL_ERR_LOG(wukong_audio_board_input_stop());

    //! pipeline 先停：停采集、等 process task 确认退出并释放采集侧资源，
    //! 之后不再有人调 __audio_frame_put，recorder 才可安全回收
    TUYA_CALL_ERR_LOG(wukong_audio_pipeline_deinit());

    //! ask the record task to exit and wait until it stops touching the recorder, then it
    //! is safe to delete the thread and free everything synchronously (no UAF / re-init race)
    if (recorder->vad_task) {
        recorder->task_exit = TRUE;
        if (recorder->exit_sem) {
            tal_semaphore_wait(recorder->exit_sem, SEM_WAIT_FOREVER);
        }
        tal_thread_delete(recorder->vad_task);
        recorder->vad_task = NULL;
    }

    //! release all resources and clear the global handle
    __audio_recorder_destroy();

    return rt;
}

OPERATE_RET wukong_audio_board_input_init(WUKONG_AUDIO_INPUT_CFG_T *cfg)
{
    TUYA_CHECK_NULL_RETURN(cfg, OPRT_INVALID_PARM);

    OPERATE_RET rt = OPRT_OK;

    //! __audio_recorder_create owns and sets the global recorder on success
    TUYA_CHECK_NULL_RETURN(__audio_recorder_create(cfg), OPRT_MALLOC_FAILED);
    TAL_PR_DEBUG("spk io %d, spk io level %d", cfg->board.spk_io, cfg->board.spk_io_level);

    //! 采集/算法/process task 全部归 pipeline；处理后 mono 帧经 output_cb 回填 recorder 队列
    WUKONG_AUDIO_PIPELINE_CFG_T pipe_cfg = {
        .sample_rate   = cfg->board.sample_rate,
        .sample_bits   = cfg->board.sample_bits,
        .channel       = cfg->board.channel,
        .vad_active_ms = cfg->board.vad_active_ms,
        .vad_off_ms    = cfg->board.vad_off_ms,
        .output_cb     = __audio_frame_put,
    };
    TUYA_CALL_ERR_GOTO(wukong_audio_pipeline_init(&pipe_cfg), __error);

    if (!recorder->vad_task) {
        THREAD_CFG_T thrd_cfg = {
            .priority = THREAD_PRIO_1,
            .stackDepth = INPUT_BOARD_STACK_SIZE,
            .thrdname = "record_task",
            #if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
            .psram_mode = 1,
            #endif
        };
        TUYA_CALL_ERR_GOTO(tal_thread_create_and_start(&recorder->vad_task, NULL, NULL, __record_task, NULL, &thrd_cfg), __error);
    }

    wukong_audio_board_input_start();
    return OPRT_OK;
__error:
    wukong_audio_board_input_deinit();
    return rt;
}

OPERATE_RET wukong_audio_board_input_wakeup_mode_set(WUKONG_AUDIO_VAD_MODE_E mode)
{
    TUYA_CHECK_NULL_RETURN(recorder, OPRT_RESOURCE_NOT_READY);

    OPERATE_RET rt = OPRT_OK;
    TAL_PR_NOTICE("wukong audio input -> wakeup mode set from %d to %d!", recorder->vad_mode, mode);
    if (mode != recorder->vad_mode) {
        TUYA_CALL_ERR_LOG(wukong_audio_board_input_stop());
        recorder->vad_mode = mode;
        TUYA_CALL_ERR_LOG(wukong_audio_board_input_start());
    }

    return rt;
}

OPERATE_RET wukong_audio_board_input_set_vol(UINT8_T volume)
{
    return wukong_audio_pipeline_set_vol(volume);
}

OPERATE_RET wukong_audio_board_input_reset()
{
    TUYA_CHECK_NULL_RETURN(recorder, OPRT_RESOURCE_NOT_READY);
    TAL_PR_NOTICE("wukong audio input -> reset queue!");

    tal_mutex_lock(recorder->mutex);
    tuya_queue_clear(recorder->queue);
    tal_mutex_unlock(recorder->mutex);
    if (WUKONG_AUDIO_VAD_AUTO == recorder->vad_mode) {
        TAL_PR_NOTICE("wukong audio input -> vad stop!");
        wukong_audio_frontend_vad_stop();
        wukong_audio_frontend_vad_start();
        TAL_PR_NOTICE("wukong audio input -> vad start!");
    }

    return OPRT_OK;
}

OPERATE_RET wukong_audio_board_input_wakeup_set(BOOL_T is_wakeup)
{
    TUYA_CHECK_NULL_RETURN(recorder, OPRT_RESOURCE_NOT_READY);
    OPERATE_RET rt = OPRT_OK;

    //! only manial mode support set vad flag
    //! auto mode will update by audio vad detect
    if (recorder->wakeup_flag != is_wakeup) {
        TAL_PR_NOTICE("wukong audio input -> mode is %d, wakeup set to %d, vad flag is %d!", recorder->vad_mode, is_wakeup, recorder->vad_flag);
        recorder->wakeup_flag = is_wakeup;
        if (recorder->vad_mode == WUKONG_AUDIO_VAD_MANUAL) {
            recorder->vad_flag = is_wakeup ? WUKONG_AUDIO_VAD_START : WUKONG_AUDIO_VAD_STOP;
            __update_vad_flag(recorder->vad_flag);
        }
    }

    return rt;
}

WUKONG_AUDIO_INPUT_PRODUCER_T g_audio_input_producer = {
    .init = wukong_audio_board_input_init,
    .deinit = wukong_audio_board_input_deinit,
    .start = wukong_audio_board_input_start,
    .stop = wukong_audio_board_input_stop,
    .reset = wukong_audio_board_input_reset,
    .set_wakeup = wukong_audio_board_input_wakeup_set,
    .set_vad_mode = wukong_audio_board_input_wakeup_mode_set,
    .set_vol = wukong_audio_board_input_set_vol,
};
