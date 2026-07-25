
#include "wukong_audio_input.h"
#include "wukong_audio_frontend.h"
#include "wukong_audio_aec_vad.h"
#if defined(USING_IFLY_DUAL_MIC) && (USING_IFLY_DUAL_MIC == 1)
#include "ifly_audio_aec_vad.h"
#endif
#include "tuya_device_cfg.h"
#include "tuya_ringbuf.h"
#include "base_event.h"
#include "tal_mutex.h"
#include "tal_queue.h"
#include "tal_log.h"
#include "tal_thread.h"
#include "stop_watch.h"
#include "tal_memory.h"

#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM==1)
#define MEM_MALLOC tal_psram_malloc
#define MEM_FREE   tal_free
#else
#define MEM_MALLOC tal_malloc
#define MEM_FREE   tal_free
#endif
#include "audio_dump.h"

typedef struct {
    BOOL_T                       enable;
    
    //! wakeup
    BOOL_T                       wakeup_flag;
    
    //! vad
    WUKONG_AUDIO_VAD_MODE_E      vad_mode;
    WUKONG_AUDIO_VAD_FLAG_E      vad_flag;
    UINT16_T                     vad_size;
    THREAD_HANDLE                vad_task;
    TUYA_RINGBUFF_T              ringbuf;
    MUTEX_HANDLE                 mutex;

    UINT16_T                     slice_size;
    WUKONG_AUDIO_OUTPUT          output_cb;
    VOID                         *user_data;
} AUDIO_RECODER_T;

STATIC AUDIO_RECODER_T *recorder = NULL;

STATIC OPERATE_RET __audio_slice_check_and_send(BOOL_T *more_data)
{
    *more_data = FALSE;
    if (recorder->vad_flag == WUKONG_AUDIO_VAD_START) {
        UINT8_T *cache_data = NULL;
        UINT32_T read_len = 0;

        tal_mutex_lock(recorder->mutex);
        UINT32_T len = tuya_ring_buff_used_size_get(recorder->ringbuf);
        // if cache is large than slice size, read all and send
        if (len >= recorder->slice_size) {
            // read slice from cache and send
            cache_data = (UINT8_T*)MEM_MALLOC(recorder->slice_size);
            if (cache_data) {
                read_len = tuya_ring_buff_read(recorder->ringbuf, cache_data, recorder->slice_size);
                *more_data = TRUE;
            }
        }
        tal_mutex_unlock(recorder->mutex);

        if (cache_data && read_len > 0) {
            recorder->output_cb(cache_data, read_len);
#if defined(TUYA_MODULE_T5) && (TUYA_MODULE_T5 == 1)
            audio_dump_write(AUDIO_DUMP_VAD, cache_data, read_len);
#endif
            MEM_FREE(cache_data);
        }
    }
    return OPRT_OK;
}

STATIC INT_T __audio_frame_put(TKL_AUDIO_FRAME_INFO_T *pframe)
{
    TUYA_CHECK_NULL_RETURN(recorder, OPRT_RESOURCE_NOT_READY);
    TUYA_CHECK_NULL_RETURN(pframe, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(pframe->pbuf, OPRT_COM_ERROR);
    INT_T rt = 0;

    //! mic disabled?
    if (!recorder->enable)
        return 0;
    
    if (recorder->vad_mode == WUKONG_AUDIO_VAD_MANUAL){
        //! in manial mode, if has vad flag, send audio data to cache
        //! why we need cache the data? it is because the audio data is from the CP1, by IPC sync message
        //! in IPC sync operation, we cannot do anything which can cause block
        if (recorder->vad_flag == WUKONG_AUDIO_VAD_START) { 
            tal_mutex_lock(recorder->mutex);
            rt = tuya_ring_buff_write(recorder->ringbuf, pframe->pbuf, (UINT16_T)pframe->buf_size);
            tal_mutex_unlock(recorder->mutex);            
        } else {
            //! no vad flag, ignore
        }
    } else {
        //! in auto mode, cache the data to ringbuffer
        tal_mutex_lock(recorder->mutex);
        UINT32_T len = tuya_ring_buff_used_size_get(recorder->ringbuf);
        if (len >= recorder->vad_size -1) {
            //! if ringbuffer is full, drop pframe->buf_size
            UINT8_T *cache_data = (UINT8_T*)MEM_MALLOC(pframe->buf_size);
            tuya_ring_buff_read(recorder->ringbuf, cache_data, pframe->buf_size);
            MEM_FREE(cache_data);
        }
        rt = tuya_ring_buff_write(recorder->ringbuf, pframe->pbuf, (UINT16_T)pframe->buf_size);
        if (rt != pframe->buf_size) {
            TAL_PR_DEBUG("wukong audio input -> overflow %d:%d", pframe->buf_size, rt);
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


VOID __audio_recorder_destroy()
{
    if (recorder) {
        if (recorder->mutex) {
            tal_mutex_release(recorder->mutex);
        }

        if (recorder->ringbuf) {
            tuya_ring_buff_free(recorder->ringbuf);
        }

        tal_free(recorder);
        recorder = NULL;
    }
}

STATIC VOID_T __record_task()
{
    NewStopWatch(sw);
    SYS_TIME_T elapsed = 0;
    BOOL_T more_data = FALSE;

    while(tal_thread_get_state(recorder->vad_task) == THREAD_STATE_RUNNING) {
        //! mic disable or not wakeup, dont need to send vad stat change
        if (!recorder->enable || !recorder->wakeup_flag) {
            // goto nextloop;
            tal_system_sleep(10);
        } else {
            __audio_slice_check_and_send(&more_data);

            //! manual mode dont need send vad stat change
            if (recorder->vad_mode == WUKONG_AUDIO_VAD_AUTO) {
                WUKONG_AUDIO_VAD_FLAG_E stat = wukong_audio_frontend_vad_get_flag();
                if (stat != recorder->vad_flag) {
                    TAL_PR_DEBUG("wukong audio input -> wakup flag is %d, auto vad set from %d to %d!", recorder->wakeup_flag, recorder->vad_flag, stat);
                    recorder ->vad_flag = stat;
                    __update_vad_flag(recorder ->vad_flag);
                }
            }
        }
    }
    
    // release resource
    __audio_recorder_destroy();
}

STATIC AUDIO_RECODER_T *__audio_recorder_create(WUKONG_AUDIO_INPUT_CFG_T *cfg)
{
    // TUYA_CHECK_NULL_RETURN(cfg, NULL);
    // TUYA_CHECK_NULL_RETURN(cfg->output_cb, NULL);
    if (recorder)
        return recorder;

    OPERATE_RET rt = OPRT_OK;
    TUYA_CHECK_NULL_RETURN(recorder = tal_calloc(1, sizeof(AUDIO_RECODER_T)), NULL);
    memset(recorder, 0, sizeof(AUDIO_RECODER_T));
 
    recorder->user_data      = cfg->board.user_data;
    recorder->output_cb      = cfg->board.output_cb;
    recorder->wakeup_flag    = FALSE;
    recorder->vad_task       = NULL;
    recorder->vad_mode       = cfg->board.vad_mode;
    UINT32_T audio_1ms_size  = cfg->board.sample_rate * cfg->board.sample_bits * cfg->board.channel / 8 / 1000;
    recorder->vad_size       = (cfg->board.vad_active_ms + 400) * audio_1ms_size + 1;   // add 400ms offset
    recorder->slice_size     = cfg->board.slice_ms * audio_1ms_size;
    UINT32_T rb_size = recorder->vad_size;
#if defined(TUYA_MODULE_T5) && (TUYA_MODULE_T5==1)
    TUYA_CALL_ERR_GOTO(tuya_ring_buff_create(rb_size, OVERFLOW_PSRAM_STOP_TYPE, &recorder->ringbuf), __error);
#else
    TUYA_CALL_ERR_GOTO(tuya_ring_buff_create(rb_size, OVERFLOW_STOP_TYPE, &recorder->ringbuf), __error);
#endif
    TUYA_CALL_ERR_GOTO(tal_mutex_create_init(&recorder->mutex), __error);
    TAL_PR_DEBUG("recorder vad mode %d", cfg->board.vad_mode);
    TAL_PR_DEBUG("recorder total ms %d, slice ms %d, vad active %d ms, vad off timeout %d", rb_size, cfg->board.slice_ms, cfg->board.vad_active_ms, cfg->board.vad_off_ms);
    return recorder;

__error:
    __audio_recorder_destroy();
    return NULL;
}

OPERATE_RET wukong_audio_board_input_start(VOID)
{
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

    return rt;
}

OPERATE_RET wukong_audio_board_input_deinit(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    TUYA_CHECK_NULL_RETURN(recorder, OPRT_OK);

    // stop mic,speaker,audio AFE(AEC,NS,VAD)
    TUYA_CALL_ERR_LOG(tkl_ai_uninit());

    // stop input
    TUYA_CALL_ERR_LOG(wukong_audio_board_input_stop());

    // release thread
    tal_thread_delete(recorder->vad_task);

    return rt;
}

OPERATE_RET wukong_audio_board_input_init(WUKONG_AUDIO_INPUT_CFG_T *cfg)
{
    TUYA_CHECK_NULL_RETURN(cfg, OPRT_INVALID_PARM);

    OPERATE_RET rt = OPRT_OK;
    TUYA_CHECK_NULL_RETURN(recorder = __audio_recorder_create(cfg), OPRT_MALLOC_FAILED);

    // init mic & speaker & AFE(AEC,ND,VAD)
    TKL_AUDIO_CONFIG_T audio_config;
    memset(&audio_config, 0, sizeof(TKL_AUDIO_CONFIG_T));

    audio_config.enable                 = 1;      //! enable vendor AFE(AEC,ND,VAD)
    audio_config.ai_chn                 = 0;
    audio_config.sample                 = cfg->board.sample_rate;
    audio_config.spk_sample             = cfg->board.sample_rate;
    audio_config.datebits               = cfg->board.sample_bits;
    audio_config.codectype              = TKL_CODEC_AUDIO_PCM;
    audio_config.spk_gpio               = cfg->board.spk_io;
    audio_config.spk_gpio_polarity      = cfg->board.spk_io_level;
    audio_config.put_cb                 = __audio_frame_put;
#if defined(USING_IFLY_DUAL_MIC) && (USING_IFLY_DUAL_MIC == 1)
    audio_config.card                   = TKL_AUDIO_TYPE_DUAL;
#else
    audio_config.card                   = TKL_AUDIO_TYPE_BOARD;
#endif
    audio_config.channel                = cfg->board.channel;

    TUYA_CALL_ERR_GOTO(tkl_ai_init(&audio_config, 0), __error);
    TUYA_CALL_ERR_GOTO(tkl_ai_start(0, 0), __error);
    TAL_PR_DEBUG("sample %d, databits %d, channel %d", cfg->board.sample_rate, cfg->board.sample_bits, cfg->board.channel);
    TAL_PR_DEBUG("spk io %d, spk io level %d", cfg->board.spk_io, cfg->board.spk_io_level);

#if defined(TUYA_MODULE_T5) && (TUYA_MODULE_T5 == 1)
#if defined(USING_IFLY_DUAL_MIC) && (USING_IFLY_DUAL_MIC == 1)
    wukong_audio_frontend_register(&g_ifly_frontend_ops);
#else
    wukong_audio_frontend_register(&g_tuya_frontend_ops);
#endif
    wukong_audio_frontend_init(cfg->board.vad_active_ms, cfg->board.vad_off_ms, cfg->board.sample_rate / 1000 * 2 * 20);
    tkl_ai_set_vad_aec_algorithm(wukong_audio_frontend_process);
#endif

    if (!recorder->vad_task) {
        THREAD_CFG_T thrd_cfg = {
            .priority = THREAD_PRIO_1,
            .stackDepth = INPUT_BOARD_STACK_SIZE,
            .thrdname = "record_task",
            #ifdef ENABLE_EXT_RAM
            .psram_mode = 1,
            #endif            
        };
        tal_thread_create_and_start(&recorder->vad_task, NULL, NULL, __record_task, NULL, &thrd_cfg);
    }

    // start audio process
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

OPERATE_RET wukong_audio_board_input_reset()
{
    TUYA_CHECK_NULL_RETURN(recorder, OPRT_RESOURCE_NOT_READY);
    TAL_PR_NOTICE("wukong audio input -> reset ringbuf!");
    
    tal_mutex_lock(recorder->mutex);
    tuya_ring_buff_reset(recorder->ringbuf);
    tal_mutex_unlock(recorder->mutex);
    // recorder->vad_flag = WUKONG_AUDIO_VAD_STOP;
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
}; 