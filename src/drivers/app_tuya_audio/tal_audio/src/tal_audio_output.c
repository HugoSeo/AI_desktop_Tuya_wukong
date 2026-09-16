/**
 * @file tal_audio_output.c
 * @brief TAL audio output implementation
 * @version 2.0
 * @date 2025-02-13
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

#include <string.h>

#include "tal_audio_output.h"
#include "audio_devices/tal_audio_dev.h"
#include "tuya_ringbuf.h"
#include "tkl_aud_dac.h"
#include "tkl_memory.h"
#include "tkl_thread.h"

#include "uni_log.h"

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define TAL_AO_WORKER_STACK_SIZE    4096
#define TAL_AO_WORKER_PRIORITY      7
#define TAL_AO_MSG_QUEUE_SIZE       8
#define TAL_AO_FRAME_DONE_TIMEOUT   25
#define TAL_AO_WRITE_RETRY_MS       10

#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
#define AO_MALLOC(s)    tkl_system_psram_malloc(s)
#define AO_FREE(p)      tkl_system_psram_free(p)
#define AO_RINGBUF_TYPE OVERFLOW_PSRAM_STOP_TYPE
#else
#define AO_MALLOC(s)    tkl_system_malloc(s)
#define AO_FREE(p)      tkl_system_free(p)
#define AO_RINGBUF_TYPE OVERFLOW_STOP_TYPE
#endif

// sr: sample rate, sb: sample bits, ch: channel num, t: frame time in ms
#define DAC_FRAME_SIZE(sr, sb, t, ch)  (((sr) * ((sb) / 8) / 1000) * (ch) * (t))

/**
 * @brief Convert I2S channel format to actual channel count
 * @param[in] fmt I2S channel format enum
 * @return Channel count (1 or 2), or 0 when unsupported
 */
STATIC UINT32_T __ao_i2s_channel_count(TUYA_I2S_CHANNEL_FMT_E fmt)
{
    switch (fmt) {
    case TUYA_I2S_CHANNEL_FMT_RIGHT_LEFT:
    case TUYA_I2S_CHANNEL_FMT_ALL_RIGHT:
    case TUYA_I2S_CHANNEL_FMT_ALL_LEFT:
        return 2;
    case TUYA_I2S_CHANNEL_FMT_ONLY_RIGHT:
    case TUYA_I2S_CHANNEL_FMT_ONLY_LEFT:
        return 1;
    default:
        return 0;
    }
}

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */

/**
 * @brief Check output init parameters
 * @param[in] config output configuration
 * @return OPRT_OK on success, error code on invalid parameter
 */
STATIC OPERATE_RET __ao_parameter_check(CONST TAL_AUDIO_OUTPUT_CFG_T *config)
{
    if (config == NULL || config->frame_time_ms == 0 || config->sample_rate == 0) {
        return OPRT_INVALID_PARM;
    }

    if (config->type >= TAL_AUDIO_OUTPUT_MAX) {
        PR_ERR("audio output error, not support type %d", config->type);
        return OPRT_INVALID_PARM;
    }

    if ((config->sample_bits != TUYA_AUDIO_SAMPLE_BITS_8) &&
        (config->sample_bits != TUYA_AUDIO_SAMPLE_BITS_16) &&
        (config->sample_bits != TUYA_AUDIO_SAMPLE_BITS_24) &&
        (config->sample_bits != TUYA_AUDIO_SAMPLE_BITS_32)) {
        PR_ERR("audio output error, not support sample bits %d", config->sample_bits);
        return OPRT_INVALID_PARM;
    }

    switch (config->type) {
#ifdef AUDIO_OUTPUT_DEVICE_DAC
    case TAL_AUDIO_OUTPUT_DAC:
        if (config->dev.dac_config.spk_num == 0) {
            PR_ERR("audio output error, invalid dac speaker number %d, at least 1",
                   config->dev.dac_config.spk_num);
            return OPRT_INVALID_PARM;
        }
        break;
#endif
    default:
        break;
    }

    return OPRT_OK;
}

/**
 * @brief Handle DAC frame-done interrupt callback
 * @param[in] event frame event type
 * @param[in] arg output control context
 * @return none
 * @note Posts a FRAME_DONE message to the worker queue. Non-blocking from ISR.
 */
VOID_T tal_audio_output_frame_cb(TUYA_AUDIO_DAC_FRAME_EVT_E event, VOID_T *arg)
{
    TAL_AO_CTRL_T *ctrl = (TAL_AO_CTRL_T *)arg;
    TAL_AO_MSG_T msg;
    (void)event;

    if ((ctrl == NULL) || (ctrl->running == FALSE)) {
        return;
    }

    memset(&msg, 0, sizeof(msg));
    msg.type = TAL_AO_MSG_FRAME_DONE;
    tkl_queue_post(ctrl->msg_queue, &msg, 0);
}

/**
 * @brief Abort a pending write request if one exists
 * @param[in] ctrl output control context
 * @param[in] err error code to set on the aborted request
 * @return none
 */
STATIC VOID_T __ao_cancel_write_user_data(TAL_AO_CTRL_T *ctrl, OPERATE_RET err)
{
    if (ctrl->pending_write == NULL) {
        return;
    }

    ctrl->pending_write->result = err;
    tkl_semaphore_post(ctrl->pending_write->done_sem);
    ctrl->pending_write = NULL;
}

/**
 * @brief Read one frame from ring buffer and send to DAC
 * @param[in] ctrl output control context
 * @return none
 */
STATIC VOID_T __ao_feed_frame_to_device(TAL_AO_CTRL_T *ctrl)
{
    OPERATE_RET ret = OPRT_OK;
    UINT32_T used_size;
    UINT32_T read_len;

    used_size = tuya_ring_buff_used_size_get(ctrl->ringbuf);
    if (used_size < ctrl->frame_size) {
        // ringbuffer大小远大于frame size,此时used_size不足一帧，表示ringbuffer中数据已经
        // 播放完成，但是上层仍然没有写入后续数据，且没有调用stop，底层tkl buffer中数据非0，
        // 导致喇叭异常播放,此时需要填充0x00到ringbuffer，tkl输出静音数据
        read_len = 0;
        if (used_size > 0) {
            read_len = tuya_ring_buff_read(ctrl->ringbuf, ctrl->frame_buf, used_size);
        }
        memset(ctrl->frame_buf + read_len, 0x00, ctrl->frame_size - read_len);
    } else {
        read_len = tuya_ring_buff_read(ctrl->ringbuf, ctrl->frame_buf, ctrl->frame_size);
        if (read_len < ctrl->frame_size) {
            return;
        }
    }

    switch (ctrl->type) {
#ifdef AUDIO_OUTPUT_DEVICE_DAC
    case TAL_AUDIO_OUTPUT_DAC:
        ret = tal_audio_output_dac_write(ctrl, ctrl->frame_buf, ctrl->frame_size);
        if (ret == OPRT_OS_ADAPTER_DAC_BUSY) {
            tkl_system_sleep(ctrl->frame_feed_retry_timeout);
            tal_audio_output_dac_write(ctrl, ctrl->frame_buf, ctrl->frame_size);
        }
        break;
#endif
#ifdef AUDIO_OUTPUT_DEVICE_I2S
    case TAL_AUDIO_OUTPUT_I2S:
        ret = tal_audio_output_i2s_write(ctrl, ctrl->frame_buf, ctrl->frame_size);
        if (ret == OPRT_OS_ADAPTER_DAC_BUSY) {
            tkl_system_sleep(ctrl->frame_feed_retry_timeout);
            tal_audio_output_i2s_write(ctrl, ctrl->frame_buf, ctrl->frame_size);
        }
        break;
#endif
    default:
        break;
    }
}

/**
 * @brief Continue writing data from a pending write request into the ring buffer
 * @param[in] ctrl output control context
 * @return none
 * @note Completes the request and posts done_sem when all data is written.
 */
STATIC VOID_T __ao_continue_write_user_data(TAL_AO_CTRL_T *ctrl)
{
    TAL_AO_WRITE_REQ_T *wr;
    UINT32_T written;

    if (ctrl->pending_write == NULL) {
        return;
    }

    wr = ctrl->pending_write;

    written = tuya_ring_buff_write(ctrl->ringbuf,
                                   wr->buf + wr->offset,
                                   wr->len - wr->offset);

    wr->offset += written;
    if (wr->offset >= wr->len) {
        wr->result = OPRT_OK;
        tkl_semaphore_post(wr->done_sem);
        ctrl->pending_write = NULL;
    }
}

/**
 * @brief Start audio output in caller context
 * @param[in] ctrl output control context
 * @return OPRT_OK on success, error code on failure
 */
STATIC OPERATE_RET __ao_start(TAL_AO_CTRL_T *ctrl)
{
    OPERATE_RET ret = OPRT_NOT_SUPPORTED;

    switch (ctrl->type) {
#ifdef AUDIO_OUTPUT_DEVICE_DAC
    case TAL_AUDIO_OUTPUT_DAC:
        ret = tal_audio_output_dac_start(ctrl);
        break;
#endif
#ifdef AUDIO_OUTPUT_DEVICE_I2S
    case TAL_AUDIO_OUTPUT_I2S:
        ret = tal_audio_output_i2s_start(ctrl);
        break;
#endif
    default:
        break;
    }

    if (ret == OPRT_OK) {
        ctrl->started = TRUE;
    }

    return ret;
}

/**
 * @brief Stop audio output in caller context
 * @param[in] ctrl output control context
 * @return OPRT_OK on success, error code on failure
 * @note Aborts any pending write, resets the ring buffer, and stops the DAC.
 */
STATIC OPERATE_RET __ao_stop(TAL_AO_CTRL_T *ctrl)
{
    OPERATE_RET ret = OPRT_NOT_SUPPORTED;

    __ao_cancel_write_user_data(ctrl, OPRT_COM_ERROR);
    ctrl->started = FALSE;

    tuya_ring_buff_reset(ctrl->ringbuf);

    switch (ctrl->type) {
#ifdef AUDIO_OUTPUT_DEVICE_DAC
    case TAL_AUDIO_OUTPUT_DAC:
        ret = tal_audio_output_dac_stop(ctrl);
        break;
#endif
#ifdef AUDIO_OUTPUT_DEVICE_I2S
    case TAL_AUDIO_OUTPUT_I2S:
        ret = tal_audio_output_i2s_stop(ctrl);
        break;
#endif
    default:
        break;
    }

    return ret;
}

/**
 * @brief Handle WRITE message in worker context
 * @param[in] ctrl output control context
 * @param[in] msg queue message carrying a TAL_AO_WRITE_REQ_T payload
 * @return none
 * @note Writes as much as possible to ring buffer. If not all data fits,
 *       registers as pending_write to be continued on FRAME_DONE events.
 */
STATIC VOID_T __ao_handle_write(TAL_AO_CTRL_T *ctrl, TAL_AO_MSG_T *msg)
{
    TAL_AO_WRITE_REQ_T *wr = (TAL_AO_WRITE_REQ_T *)msg->payload;
    UINT32_T written;

    if (!ctrl->started) {
        wr->result = OPRT_COM_ERROR;
        tkl_semaphore_post(wr->done_sem);
        return;
    }

    written = tuya_ring_buff_write(ctrl->ringbuf,
                                   wr->buf + wr->offset,
                                   wr->len - wr->offset);

    wr->offset += written;
    if (wr->offset >= wr->len) {
        wr->result = OPRT_OK;
        tkl_semaphore_post(wr->done_sem);
    } else {
        ctrl->pending_write = wr;
    }
}

/**
 * @brief Handle FRAME_DONE message in worker context
 * @param[in] ctrl output control context
 * @return none
 * @note Feeds one frame to the DAC, then continues any pending write.
 */
STATIC VOID_T __ao_handle_frame_done(TAL_AO_CTRL_T *ctrl)
{
    if (!ctrl->started) {
        return;
    }

    __ao_feed_frame_to_device(ctrl);
    __ao_continue_write_user_data(ctrl);
}

/**
 * @brief Set output volume in caller context
 * @param[in] ctrl output control context
 * @param[in] volume volume level in range 0 to 100
 * @return OPRT_OK on success, error code on failure
 */
STATIC OPERATE_RET __ao_set_volume(TAL_AO_CTRL_T *ctrl, UINT8_T volume)
{
    OPERATE_RET ret = OPRT_NOT_SUPPORTED;

    switch (ctrl->type) {
#ifdef AUDIO_OUTPUT_DEVICE_DAC
    case TAL_AUDIO_OUTPUT_DAC:
        ret = tal_audio_output_dac_set_volume(ctrl, volume);
        break;
#endif
#ifdef AUDIO_OUTPUT_DEVICE_I2S
    case TAL_AUDIO_OUTPUT_I2S:
        ret = tal_audio_output_i2s_set_volume(ctrl, volume);
        break;
#endif
    default:
        break;
    }

    return ret;
}

/**
 * @brief Handle DEINIT message in worker context
 * @param[in] ctrl output control context
 * @return none
 * @note Aborts pending write, stops and deinitializes the DAC so no further
 *       IRQ callbacks can fire.
 */
STATIC VOID_T __ao_device_deinit(TAL_AO_CTRL_T *ctrl)
{
    __ao_cancel_write_user_data(ctrl, OPRT_COM_ERROR);
    ctrl->started = FALSE;

    switch (ctrl->type) {
#ifdef AUDIO_OUTPUT_DEVICE_DAC
    case TAL_AUDIO_OUTPUT_DAC:
        tal_audio_output_dac_stop(ctrl);
        tal_audio_output_dac_deinit(ctrl);
        break;
#endif
#ifdef AUDIO_OUTPUT_DEVICE_I2S
    case TAL_AUDIO_OUTPUT_I2S:
        tal_audio_output_i2s_stop(ctrl);
        tal_audio_output_i2s_deinit(ctrl);
        break;
#endif
    default:
        break;
    }
}

/**
 * @brief Output worker thread entry — unified event loop
 * @param[in] arg output control context
 * @return none
 * @note Fetches messages from msg_queue and dispatches streaming events.
 *       Handles WRITE, FRAME_DONE, and DEINIT.
 */
STATIC VOID_T __ao_output_worker(VOID_T *arg)
{
    OPERATE_RET ret = OPRT_OK;
    TAL_AO_CTRL_T *ctrl = (TAL_AO_CTRL_T *)arg;
    TAL_AO_MSG_T msg;
    TKL_THREAD_HANDLE self_thread = NULL;

    if (ctrl == NULL) {
        return;
    }

    self_thread = ctrl->worker_thread;

    ret = tkl_queue_create_init(&ctrl->msg_queue, sizeof(TAL_AO_MSG_T), TAL_AO_MSG_QUEUE_SIZE);
    if (ret != OPRT_OK) {
        goto __worker_exit;
    }

    ret = tkl_semaphore_create_init(&ctrl->write_done_sem, 0, 1);
    if (ret != OPRT_OK) {
        goto __worker_exit;
    }

    ret = tuya_ring_buff_create(TAL_AO_RINGBUF_SIZE, AO_RINGBUF_TYPE, &ctrl->ringbuf);
    if (ret != OPRT_OK) {
        goto __worker_exit;
    }

    ctrl->frame_buf = (UINT8_T *)AO_MALLOC(ctrl->frame_size);
    if (ctrl->frame_buf == NULL) {
        goto __worker_exit;
    }

    ctrl->running = TRUE;

    while (1) {
        if (tkl_queue_fetch(ctrl->msg_queue, &msg, ctrl->queue_timeout) != OPRT_OK) {
            continue;
        }

        if (msg.type == TAL_AO_MSG_WRITE) {
            __ao_handle_write(ctrl, &msg);
        } else if (msg.type == TAL_AO_MSG_FRAME_DONE) {
            __ao_handle_frame_done(ctrl);
        } else if (msg.type == TAL_AO_MSG_DEINIT) {
            break;
        }
    }

__worker_exit:
    ctrl->running = FALSE;

    if (ctrl->frame_buf != NULL) {
        AO_FREE(ctrl->frame_buf);
        ctrl->frame_buf = NULL;
    }

    if (ctrl->ringbuf != NULL) {
        tuya_ring_buff_free(ctrl->ringbuf);
        ctrl->ringbuf = NULL;
    }

    if (ctrl->write_done_sem != NULL) {
        tkl_semaphore_release(ctrl->write_done_sem);
        ctrl->write_done_sem = NULL;
    }

    if (ctrl->msg_queue != NULL) {
        tkl_queue_free(ctrl->msg_queue);
        ctrl->msg_queue = NULL;
    }

    tkl_semaphore_post(ctrl->thread_exit_sem);

    if (self_thread != NULL) {
        tkl_thread_release(self_thread);
    }
}

/**
 * @brief Initialize TAL audio output
 * @param[in] config output configuration
 * @return output handle on success, NULL on failure
 * @note The returned handle owns the worker thread and device resources used by
 *       the output stream.
 */
TAL_AUDIO_OUTPUT_HANDLE tal_audio_output_init(TAL_AUDIO_OUTPUT_CFG_T *config)
{
    OPERATE_RET ret = OPRT_OK;
    TAL_AO_CTRL_T *ctrl = NULL;

    if (config == NULL) {
        PR_ERR("audio output config is NULL");
        return NULL;
    }

    ret = __ao_parameter_check(config);
    if (ret != OPRT_OK) {
        return NULL;
    }

    ctrl = (TAL_AO_CTRL_T *)AO_MALLOC(sizeof(TAL_AO_CTRL_T));
    if (ctrl == NULL) {
        PR_ERR("audio output malloc ctrl failed");
        return NULL;
    }

    memset(ctrl, 0, sizeof(TAL_AO_CTRL_T));
    ctrl->type = config->type;

    switch (config->type) {
#ifdef AUDIO_OUTPUT_DEVICE_DAC
        case TAL_AUDIO_OUTPUT_DAC:
            ctrl->frame_size = DAC_FRAME_SIZE(config->sample_rate, config->sample_bits, config->frame_time_ms,
                                              config->dev.dac_config.spk_num);
            memcpy(&ctrl->dev, &config->dev, sizeof(TAL_AUDIO_DAC_OUTPUT_CHAN_T));
            ret = tal_audio_output_dac_init(ctrl, config);
            ctrl->queue_timeout = config->frame_time_ms + config->frame_time_ms / 2;
            ctrl->frame_feed_retry_timeout = config->frame_time_ms / 2;
            break;
#endif

#ifdef AUDIO_OUTPUT_DEVICE_I2S
        case TAL_AUDIO_OUTPUT_I2S:
            ctrl->frame_size = DAC_FRAME_SIZE(config->sample_rate, config->sample_bits, config->frame_time_ms,
                                              __ao_i2s_channel_count(config->dev.i2s_config.data_format));
            memcpy(&ctrl->dev, &config->dev, sizeof(TAL_AUDIO_OUTPUT_I2S_T));
            ret = tal_audio_output_i2s_init(ctrl, config);
            ctrl->queue_timeout = config->frame_time_ms + config->frame_time_ms / 2;
            ctrl->frame_feed_retry_timeout = config->frame_time_ms / 2;
            break;
#endif

        default:
            ret = OPRT_NOT_SUPPORTED;
            break;
    }

    if (ret != OPRT_OK || ctrl->frame_size == 0) {
        PR_ERR("init output %d error", config->type);
        goto __ao_init_error;
    }

    ret = tkl_semaphore_create_init(&ctrl->thread_exit_sem, 0, 1);
    if (ret != OPRT_OK) {
        goto __ao_init_error;
    }

#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    ret = tkl_thread_create_in_psram(&ctrl->worker_thread, "tal_ao", TAL_AO_WORKER_STACK_SIZE,
                            TAL_AO_WORKER_PRIORITY, __ao_output_worker, ctrl);
#else
    ret = tkl_thread_create(&ctrl->worker_thread, "tal_ao", TAL_AO_WORKER_STACK_SIZE,
                            TAL_AO_WORKER_PRIORITY, __ao_output_worker, ctrl);
#endif
    if (ret != OPRT_OK) {
        goto __ao_init_error;
    }

    return (TAL_AUDIO_OUTPUT_HANDLE)ctrl;

__ao_init_error:
    if (ctrl != NULL) {
        tal_audio_output_deinit(ctrl);
    }

    PR_ERR("tal audio output init error exit");
    return NULL;
}

/**
 * @brief Deinitialize TAL audio output
 * @param[in] handle output handle
 * @return OPRT_OK on success, error code on failure
 * @note Sends DEINIT to the worker which stops/deinits the DAC, then waits
 *       for the thread to exit before freeing all resources associated with
 *       the handle.
 */
OPERATE_RET tal_audio_output_deinit(TAL_AUDIO_OUTPUT_HANDLE handle)
{
    TAL_AO_CTRL_T *ctrl = (TAL_AO_CTRL_T *)handle;
    TAL_AO_MSG_T msg;

    if (ctrl == NULL) {
        return OPRT_INVALID_PARM;
    }

    __ao_device_deinit(ctrl);

    if ((ctrl->running == TRUE) && (ctrl->msg_queue != NULL)) {
        memset(&msg, 0, sizeof(msg));
        msg.type = TAL_AO_MSG_DEINIT;
        tkl_queue_post(ctrl->msg_queue, &msg, TKL_QUEUE_WAIT_FROEVER);
    }

    if ((ctrl->worker_thread != NULL) && (ctrl->thread_exit_sem != NULL)) {
        tkl_semaphore_wait(ctrl->thread_exit_sem, TKL_SEM_WAIT_FOREVER);
        ctrl->worker_thread = NULL;
    }

    if (ctrl->thread_exit_sem != NULL) {
        tkl_semaphore_release(ctrl->thread_exit_sem);
        ctrl->thread_exit_sem = NULL;
    }

    memset(ctrl, 0, sizeof(TAL_AO_CTRL_T));
    AO_FREE(ctrl);

    return OPRT_OK;
}

/**
 * @brief Start audio output playback
 * @param[in] handle output handle
 * @return OPRT_OK on success, error code on failure
 * @note Executes the start flow directly in caller context. Playback must be
 *       started before calling `tal_audio_output_write()`.
 */
OPERATE_RET tal_audio_output_start(TAL_AUDIO_OUTPUT_HANDLE handle)
{
    TAL_AO_CTRL_T *ctrl = (TAL_AO_CTRL_T *)handle;
    OPERATE_RET ret;

    if ((ctrl == NULL) || (ctrl->running == FALSE)) {
        PR_ERR("audio output start failed, parameter error");
        return OPRT_INVALID_PARM;
    }

    ret = __ao_start(ctrl);

    return ret;
}

/**
 * @brief Stop audio output playback
 * @param[in] handle output handle
 * @return OPRT_OK on success, error code on failure
 * @note Executes the stop flow directly in caller context, aborts any pending
 *       write request, resets the ring buffer, and stops the DAC.
 */
OPERATE_RET tal_audio_output_stop(TAL_AUDIO_OUTPUT_HANDLE handle)
{
    TAL_AO_CTRL_T *ctrl = (TAL_AO_CTRL_T *)handle;
    OPERATE_RET ret;

    if ((ctrl == NULL) || (ctrl->running == FALSE)) {
        return OPRT_INVALID_PARM;
    }

    ret = __ao_stop(ctrl);

    return ret;
}

/**
 * @brief Set output volume
 * @param[in] handle output handle
 * @param[in] volume volume level in range 0 to 100
 * @return OPRT_OK on success, error code on failure
 * @note Executes the volume update directly in caller context.
 */
OPERATE_RET tal_audio_output_set_volume(TAL_AUDIO_OUTPUT_HANDLE handle, UINT8_T volume)
{
    TAL_AO_CTRL_T *ctrl = (TAL_AO_CTRL_T *)handle;
    OPERATE_RET ret;

    if ((ctrl == NULL) || (ctrl->running == FALSE)) {
        return OPRT_INVALID_PARM;
    }

    if (volume > 100) {
        PR_ERR("audio output error, volume %d out of range", volume);
        return OPRT_INVALID_PARM;
    }

    ret = __ao_set_volume(ctrl, volume);

    return ret;
}

/**
 * @brief Write PCM data for playback
 * @param[in] handle output handle
 * @param[in] buf user PCM buffer
 * @param[in] len buffer length in bytes
 * @return OPRT_OK on success, error code on failure
 * @note Posts a WRITE request to the worker and blocks until all data has been
 *       written into the internal ring buffer. May be unblocked early with an error
 *       if stop or deinit is called concurrently.
 */
STATIC TAL_AO_WRITE_REQ_T wr;
OPERATE_RET tal_audio_output_write(TAL_AUDIO_OUTPUT_HANDLE handle, UINT8_T *buf, UINT32_T len)
{
    TAL_AO_CTRL_T *ctrl = (TAL_AO_CTRL_T *)handle;
    TAL_AO_MSG_T msg;
    OPERATE_RET ret;

    if ((ctrl == NULL) || (buf == NULL) || (len == 0U)) {
        return OPRT_INVALID_PARM;
    }

    if ((ctrl->running == FALSE) || (ctrl->started == FALSE) ||
        (ctrl->msg_queue == NULL) || (ctrl->write_done_sem == NULL)) {
        return OPRT_COM_ERROR;
    }

    wr.buf = buf;
    wr.len = len;
    wr.offset = 0;
    wr.result = OPRT_COM_ERROR;
    wr.done_sem = ctrl->write_done_sem;

    memset(&msg, 0, sizeof(msg));
    msg.type = TAL_AO_MSG_WRITE;
    msg.payload = &wr;

    ret = tkl_queue_post(ctrl->msg_queue, &msg, TKL_QUEUE_WAIT_FROEVER);
    if (ret != OPRT_OK) {
        return ret;
    }

    tkl_semaphore_wait(ctrl->write_done_sem, TKL_SEM_WAIT_FOREVER);
    return wr.result;
}
