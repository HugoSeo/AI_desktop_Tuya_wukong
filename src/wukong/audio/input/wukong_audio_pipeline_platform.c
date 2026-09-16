/**
 * @file wukong_audio_pipeline_platform.c
 * @brief 平台机制（ENABLE_WUKONG_AUDIO_PIPELINE=n，仅 T5 单麦）的 pipeline 同名接口实现。
 *
 *        vendor bk_voice 采集；app 前端（AEC/VAD/KWS 喂数）经 tkl_ai_set_vad_aec_algorithm
 *        注册进驱动、在驱动 voice task 内运行；回调交付的已是 AFE 处理后 mono 帧，直接经
 *        output_cb 入 recorder 队列——无 ring buffer / process task / LR 拆分，即老 tkl
 *        链路的数据流。
 *
 *        与 wukong_audio_pipeline.c 同名符号二选一，由 app 顶层 local.mk 按
 *        CONFIG_ENABLE_WUKONG_AUDIO_PIPELINE 选源；本文件内宏守卫作纵深兜底
 *        （与 tal_audio 的 platform_mode/ 同一模式）。
 */
#include "tuya_iot_config.h"

#ifdef USER_SW_VER
#include "tuya_app_config.h"   /* app-level Kconfig macros (ENABLE_WUKONG_AUDIO_PIPELINE) */
#endif

/* platform mode is the ENABLE_WUKONG_AUDIO_PIPELINE=n branch */
#if !defined(ENABLE_WUKONG_AUDIO_PIPELINE) || (ENABLE_WUKONG_AUDIO_PIPELINE == 0)

#include "wukong_audio_pipeline.h"
#include "wukong_audio_input_frame.h"
#include "wukong_audio_frontend.h"
#include "wukong_audio_aec_vad.h"
#include "tal_log.h"
#include "tal_audio_input.h"
#include "tkl_audio.h"   //! tkl_ai_set_vad_aec_algorithm: 把 app 前端注册进 vendor AFE
#if defined(ENABLE_TY_AVI_MEDIA) && (ENABLE_TY_AVI_MEDIA == 1)
#include "ty_avi_recorder.h"   //! AVI recording's independent, VAD-unrelated mic tap
#endif

STATIC TAL_AUDIO_INPUT_HANDLE s_adc_handle = NULL;
STATIC volatile WUKONG_AUDIO_PIPELINE_OUTPUT_CB s_output_cb = NULL;

//! 驱动同步上下文回调：帧已是 AFE 后 20ms mono（与老链路一致），直接交付
//! （output_cb 内部只做无阻塞入队）
STATIC VOID_T __audio_input_direct_cb(TAL_AUDIO_FRAME_T *frame, VOID_T *args)
{
    //! snapshot：deinit 可能从其它线程并发清掉 s_output_cb，不能读到一半
    WUKONG_AUDIO_PIPELINE_OUTPUT_CB output_cb = s_output_cb;

    (void)args;
    if (output_cb == NULL || frame == NULL || frame->buf == NULL) {
        return;
    }
    //! AVI 录制的音频拾取点：故意放在 __audio_frame_put 的 vad_flag 门控之前——
    //! 那道门控只在"检测到人声/已唤醒"时才放行给 AI 链路，正常拍摄时大部分时间不会
    //! 满足，会导致录出来的视频没有声音。这里独立于该门控、每帧必发，不占用/不影响
    //! AI 唤醒词与语音上传那条路径（两者读的是同一份已处理好的 mono 帧，互不干扰）。
#if defined(ENABLE_TY_AVI_MEDIA) && (ENABLE_TY_AVI_MEDIA == 1)
    if (ty_avi_recorder_is_running()) {
        ty_avi_recorder_audio_feed((const uint8_t *)frame->buf, (uint32_t)frame->len);
    }
#endif
    output_cb(frame->buf, frame->len);
}

OPERATE_RET wukong_audio_pipeline_init(WUKONG_AUDIO_PIPELINE_CFG_T *cfg)
{
    TUYA_CHECK_NULL_RETURN(cfg, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(cfg->output_cb, OPRT_INVALID_PARM);

    OPERATE_RET rt = OPRT_OK;

    s_output_cb = cfg->output_cb;

    TAL_AUDIO_INPUT_CFG_T audio_config = {0};
    audio_config.type = TAL_AUDIO_INPUT_ADC;
    audio_config.sample_bits = cfg->sample_bits;
    audio_config.sample_rate = cfg->sample_rate;
    audio_config.frame_time_ms = AUDIO_INPUT_CAPTURE_FRAME_MS;   //! 平台后端忽略此参数
    audio_config.audio_input_cb = __audio_input_direct_cb;

    s_adc_handle = tal_audio_input_init(&audio_config);
    if (s_adc_handle == NULL) {
        rt = OPRT_COM_ERROR;
        goto __error;
    }
    //! 调用顺序与参考实现(legacy input_board)一致: init -> start -> 前端注册 -> 算法注册
    TUYA_CALL_ERR_GOTO(tal_audio_input_start(s_adc_handle), __error);

#if defined(TUYA_MODULE_T5) && (TUYA_MODULE_T5 == 1)
    //! SPRS 与平台机制编译期互斥（见 wukong_audio_input_frame.h #error），此路径恒为 tuya ops
    wukong_audio_frontend_register(&g_tuya_frontend_ops);
    TUYA_CALL_ERR_GOTO(wukong_audio_frontend_init(cfg->vad_active_ms, cfg->vad_off_ms,
                               AUDIO_INPUT_PROCESS_SAMPLES * AUDIO_INPUT_BYTES_PER_SAMPLE), __error);
    //! 与老链路一致：app 前端注册进 vendor 驱动，AEC/VAD/KWS 喂数在驱动内运行
    tkl_ai_set_vad_aec_algorithm(wukong_audio_frontend_process);
#endif

    return OPRT_OK;
__error:
    wukong_audio_pipeline_deinit();
    return rt;
}

OPERATE_RET wukong_audio_pipeline_deinit(VOID)
{
    if (s_adc_handle == NULL && s_output_cb == NULL) {
        return OPRT_OK;
    }

    //! 先停止交付再拆链路（回调侧有 snapshot 保护）
    s_output_cb = NULL;

    if (s_adc_handle != NULL) {
        tal_audio_input_stop(s_adc_handle);
        tal_audio_input_deinit(s_adc_handle);
        s_adc_handle = NULL;
    }

#if defined(TUYA_MODULE_T5) && (TUYA_MODULE_T5 == 1)
    //! 驱动已 uninit 不再回调：先注销算法指针，再释放前端资源
    tkl_ai_set_vad_aec_algorithm(NULL);
    wukong_audio_frontend_deinit();
#endif

    return OPRT_OK;
}

OPERATE_RET wukong_audio_pipeline_set_vol(UINT8_T volume)
{
    TUYA_CHECK_NULL_RETURN(s_adc_handle, OPRT_RESOURCE_NOT_READY);
    return tal_audio_input_set_volume(s_adc_handle, volume);
}

#endif /* !ENABLE_WUKONG_AUDIO_PIPELINE (platform mode) */
