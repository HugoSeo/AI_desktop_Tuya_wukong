#include "wukong_audio_pipeline.h"
#include "wukong_audio_input_frame.h"
#include "wukong_audio_preprocess.h"
#include "wukong_audio_frontend.h"
#include "wukong_audio_aec_vad.h"
#include "tuya_device_cfg.h"
#include "tuya_ringbuf.h"
#include "tal_semaphore.h"
#include "tal_log.h"
#include "tal_thread.h"
#include "stop_watch.h"
#include "tal_memory.h"
#include "tal_audio_input.h"
#if defined(ENABLE_TY_AVI_MEDIA) && (ENABLE_TY_AVI_MEDIA == 1)
#include "ty_avi_recorder.h"   //! AVI recording's independent, VAD-unrelated mic tap
#endif
#if defined(USING_SPRS_AUDIO_FRONTEND)
#include "wukong_audio_sprs.h"
#endif
#include <string.h>

typedef struct {
    UINT32_T seq_no;
    UINT32_T len;
    SYS_TIME_T time_stamp;
    UINT8_T data[AUDIO_INPUT_FRAME_SIZE];
} WUKONG_AUDIO_PIPELINE_FRAME_T;

typedef struct {
    TAL_AUDIO_INPUT_HANDLE adc_handle;
    TUYA_RINGBUFF_T adc_rb;

    THREAD_HANDLE task;
    volatile BOOL_T running;
    SEM_HANDLE    exit_sem;     //!< process task 退出确认信号量（deinit 等其 post 后再释放资源）

    UINT8_T capture_channels;
    UINT32_T frame_ms;

    UINT32_T capture_frame_size;
    UINT32_T output_frame_size;

#if defined(WUKONG_AUDIO_USING_DUAL_MIC)
    //! dual-mic capture members, grouped together rather than scattered across the struct
    TAL_AUDIO_INPUT_HANDLE dmic_handle;
    TUYA_RINGBUFF_T dmic_rb;
    //! process_task 的 DMIC 侧对齐配对工作帧（从 dmic_rb pop 到此，移出栈）
    WUKONG_AUDIO_PIPELINE_FRAME_T dmic_frame;
#endif

    //! 算法工作 buffer，按真实需求定尺寸（不必等于 LR 采集帧）：
    //! mic = MIC_NUM 路(tuya 1 / SPRS 2)，ref/output = mono；均 PROCESS_SAMPLES/通道。
    INT16_T mic_buf[AUDIO_INPUT_PROCESS_SAMPLES * AUDIO_INPUT_MIC_NUM];
    INT16_T ref_buf[AUDIO_INPUT_PROCESS_SAMPLES];
    INT16_T output_buf[AUDIO_INPUT_PROCESS_SAMPLES];
#if defined(WUKONG_AUDIO_USING_DUAL_MIC)
    //! 仅双麦 DMIC 输入做 HPF 去 DC；单麦 ADC 路径保持原始行为。
    WUKONG_AUDIO_HPF_T mic_hpf[AUDIO_INPUT_MIC_NUM];
#if WUKONG_AUDIO_HPF_FILTER_REF
    WUKONG_AUDIO_HPF_T ref_hpf;
#endif
#endif

    //! process_task 从 rb pop 帧的工作缓冲（单双麦共用；单线程独占，移出栈避免 ~2KB 栈占用）
    WUKONG_AUDIO_PIPELINE_FRAME_T adc_frame;

    //! 处理后 mono 帧交付回调（init 时必填校验，运行期直接调用）
    WUKONG_AUDIO_PIPELINE_OUTPUT_CB output_cb;
} WUKONG_AUDIO_PIPELINE_T;

//! heap/PSRAM-allocated at init (struct holds several KB of buffers, too large to keep
//! statically resident); allocated in wukong_audio_board_input_init, freed in
//! wukong_audio_board_input_deinit after the process task confirms exit
STATIC WUKONG_AUDIO_PIPELINE_T *s_pipeline = NULL;

STATIC UINT32_T __pipeline_push_frame(TUYA_RINGBUFF_T rb, TAL_AUDIO_FRAME_T *frame)
{
    WUKONG_AUDIO_PIPELINE_FRAME_T rb_frame;

    if (rb == NULL || frame == NULL || frame->buf == NULL || frame->len == 0U ||
        frame->len > AUDIO_INPUT_FRAME_SIZE) {
        return 0;
    }

    if (tuya_ring_buff_free_size_get(rb) < sizeof(WUKONG_AUDIO_PIPELINE_FRAME_T)) {
        return 0;
    }

    memset(&rb_frame, 0, sizeof(WUKONG_AUDIO_PIPELINE_FRAME_T));
    rb_frame.seq_no = frame->seq_no;
    rb_frame.len = frame->len;
    rb_frame.time_stamp = frame->time_stamp;
    memcpy(rb_frame.data, frame->buf, frame->len);

    return tuya_ring_buff_write(rb, (UINT8_T *)&rb_frame, sizeof(WUKONG_AUDIO_PIPELINE_FRAME_T));
}

STATIC BOOL_T __pipeline_pop_frame(TUYA_RINGBUFF_T rb, WUKONG_AUDIO_PIPELINE_FRAME_T *frame)
{
    if (rb == NULL || frame == NULL ||
        tuya_ring_buff_used_size_get(rb) < sizeof(WUKONG_AUDIO_PIPELINE_FRAME_T)) {
        return FALSE;
    }

    tuya_ring_buff_read(rb, (UINT8_T *)frame, sizeof(WUKONG_AUDIO_PIPELINE_FRAME_T));
    return TRUE;
}

STATIC VOID_T __audio_input_raw_cb(TAL_AUDIO_FRAME_T *frame, VOID_T *args)
{
    TUYA_RINGBUFF_T rb = (TUYA_RINGBUFF_T)args;

    if (s_pipeline == NULL || !s_pipeline->running) {
        return;
    }

    __pipeline_push_frame(rb, frame);
}

#if defined(WUKONG_AUDIO_USING_DUAL_MIC)
/* ---------------------------------------------------------------------------
 * Dual-mic (SPRS) ADC/DMIC 时间戳同步 + 2 帧累积 helpers。
 * 两路独立采集流(ADC ref / DMIC 双麦)带各自 time_stamp 到达独立 ring buffer；
 * 在 SPRS_SYNC_THRESHOLD_MS 内配对，累积 AUDIO_INPUT_FRAMES_PER_PROC 个采集帧
 * 成一个处理帧送 wukong_audio_frontend_process。运行态由 process_task 独占的
 * DUAL_MIC_SYNC_CTX_T(栈上)持有；帧/缓冲在 s_pipeline。
 * --------------------------------------------------------------------------- */
typedef enum {
    DUAL_MIC_SYNCING,       //!< 尚未对齐(上电/漂移后)，丢较早帧直到时间戳收敛
    DUAL_MIC_STREAMING,     //!< 已对齐，稳定累积
} DUAL_MIC_SYNC_STATE_E;

//! STREAMING 下允许的连续 drift 帧数：单次 drift 不丢数据(保连续)，连续超此值才回 SYNCING
#define DUAL_MIC_STREAM_DRIFT_MAX   3U
#define DUAL_MIC_RESYNC_DROP_ADC    0x01U
#define DUAL_MIC_RESYNC_DROP_DMIC   0x02U
#define DUAL_MIC_RESYNC_WARN        0x04U

typedef struct {
    DUAL_MIC_SYNC_STATE_E state;
    BOOL_T     has_adc_frame;          //!< 当前是否已持有一帧待配对 ADC 数据
    BOOL_T     has_dmic_frame;         //!< 同上，DMIC
    UINT32_T   accumulated_pair_count; //!< 已累积的同步帧对数(满 FRAMES_PER_PROC 触发处理)
    UINT32_T   streaming_drift_count;  //!< STREAMING 下连续 drift 帧数(对齐即清零)
    SYS_TIME_T sync_start_ms;          //!< 距上次成功出帧(或 init)的计时起点，用于收敛超时判断
    BOOL_T     sync_timeout_warned;    //!< 是否已报过收敛超时
} DUAL_MIC_SYNC_CTX_T;

//! 回同步入口：按 flags 丢弃持有帧、清半帧累积/连续 drift，并按需做收敛超时告警。
//! 不重置 sync_start_ms；计时表示距上次成功出帧/init 的时长，跨 drift/坏帧累积。
STATIC VOID_T __dual_mic_resync(DUAL_MIC_SYNC_CTX_T *ctx, UINT32_T flags)
{
    BOOL_T need_warn = flags & DUAL_MIC_RESYNC_WARN;
    BOOL_T sync_timeout = (tal_system_get_millisecond() - ctx->sync_start_ms) > SPRS_SYNC_CONVERGE_MAX_MS;

    if (flags & DUAL_MIC_RESYNC_DROP_ADC) {
        ctx->has_adc_frame = FALSE;
    }
    if (flags & DUAL_MIC_RESYNC_DROP_DMIC) {
        ctx->has_dmic_frame = FALSE;
    }

    ctx->state = DUAL_MIC_SYNCING;
    ctx->accumulated_pair_count = 0;
    ctx->streaming_drift_count = 0;

    if (need_warn && !ctx->sync_timeout_warned && sync_timeout) {
        TAL_PR_ERR("dual-mic: sync not converged in %ums, algo result unreliable", SPRS_SYNC_CONVERGE_MAX_MS);
        ctx->sync_timeout_warned = TRUE;
    }
}

//! 取一对"时间戳对齐且帧长合法"的 ADC/DMIC 帧到 s_pipeline->adc_frame/dmic_frame。
//! 对齐成功返回 TRUE；缺帧/坏帧/未对齐返回 FALSE(缺帧时内部 sleep 一小段等采集)。
STATIC BOOL_T __dual_mic_try_get_synced_pair(DUAL_MIC_SYNC_CTX_T *ctx)
{
    if (s_pipeline->adc_rb == NULL || s_pipeline->dmic_rb == NULL) {
        tal_system_sleep(AUDIO_INPUT_CAPTURE_FRAME_MS);
        return FALSE;
    }
    if (!ctx->has_adc_frame)  ctx->has_adc_frame  = __pipeline_pop_frame(s_pipeline->adc_rb,  &s_pipeline->adc_frame);
    if (!ctx->has_dmic_frame) ctx->has_dmic_frame = __pipeline_pop_frame(s_pipeline->dmic_rb, &s_pipeline->dmic_frame);
    if (!ctx->has_adc_frame || !ctx->has_dmic_frame) {
        tal_system_sleep(AUDIO_INPUT_CAPTURE_FRAME_MS / 2);   //!< 缺帧，等下一批采集
        return FALSE;
    }

    //! 帧长校验：短帧(驱动异常)按固定 CAPTURE_SAMPLES 读会越界/错位——两路都丢并重同步
    if (s_pipeline->adc_frame.len != AUDIO_INPUT_FRAME_SIZE ||
        s_pipeline->dmic_frame.len != AUDIO_INPUT_FRAME_SIZE) {
        TAL_PR_WARN("dual-mic: bad frame len adc=%u dmic=%u, drop & resync",
                    s_pipeline->adc_frame.len, s_pipeline->dmic_frame.len);
        __dual_mic_resync(ctx, DUAL_MIC_RESYNC_DROP_ADC | DUAL_MIC_RESYNC_DROP_DMIC | DUAL_MIC_RESYNC_WARN);
        return FALSE;
    }

    INT64_T d = (INT64_T)s_pipeline->adc_frame.time_stamp - (INT64_T)s_pipeline->dmic_frame.time_stamp;
    if ((d < 0 ? -d : d) > (INT64_T)SPRS_SYNC_THRESHOLD_MS) {
        if (ctx->state == DUAL_MIC_SYNCING) {
            //! SYNCING：严格找对齐——丢较早那路(保留较新的继续配下一帧)，不出数据；计时跨 drift 累积
            __dual_mic_resync(ctx, (d > 0 ? DUAL_MIC_RESYNC_DROP_DMIC : DUAL_MIC_RESYNC_DROP_ADC) | DUAL_MIC_RESYNC_WARN);
            return FALSE;
        }
        //! STREAMING：保连续——单次 drift 不丢数据(当前帧对照常处理)，仅连续 drift 超限才回 SYNCING
        ctx->streaming_drift_count++;
        TAL_PR_WARN("dual-mic: drift %lldms (%u/%u)", d, ctx->streaming_drift_count, DUAL_MIC_STREAM_DRIFT_MAX);
        if (ctx->streaming_drift_count < DUAL_MIC_STREAM_DRIFT_MAX) {
            return TRUE;
        }
        TAL_PR_WARN("dual-mic: drift exceeded %u frames, re-sync", DUAL_MIC_STREAM_DRIFT_MAX);
        __dual_mic_resync(ctx, DUAL_MIC_RESYNC_DROP_ADC | DUAL_MIC_RESYNC_DROP_DMIC | DUAL_MIC_RESYNC_WARN);
        return FALSE;
    }

    ctx->streaming_drift_count = 0;    //!< 对齐：清连续 drift 计数
    ctx->state = DUAL_MIC_STREAMING;
    return TRUE;
}

//! 把当前已对齐的一对帧累积进 mic_buf(DMIC LR 原样交织)/ref_buf(ADC R)；
//! 满 FRAMES_PER_PROC 送算法处理并出片。
STATIC VOID_T __dual_mic_accumulate_pair(DUAL_MIC_SYNC_CTX_T *ctx)
{
    UINT32_T off = ctx->accumulated_pair_count * AUDIO_INPUT_CAPTURE_SAMPLES;
    const INT16_T *adc_pcm  = (const INT16_T *)s_pipeline->adc_frame.data;   //!< ADC LR 交织
    const INT16_T *dmic_pcm = (const INT16_T *)s_pipeline->dmic_frame.data;  //!< DMIC LR = MIC1/MIC2

    memcpy(&s_pipeline->mic_buf[off * AUDIO_INPUT_MIC_NUM], dmic_pcm,
           AUDIO_INPUT_CAPTURE_SAMPLES * AUDIO_INPUT_MIC_NUM * sizeof(INT16_T));

    for (UINT32_T i = 0; i < AUDIO_INPUT_CAPTURE_SAMPLES; i++) {
        s_pipeline->ref_buf[off + i] = adc_pcm[i * AUDIO_INPUT_CAPTURE_CHAN_NUM + AUDIO_INPUT_REF_CH_INDEX];   //!< REF = ADC R
    }
    ctx->has_adc_frame = ctx->has_dmic_frame = FALSE;

    if (++ctx->accumulated_pair_count >= AUDIO_INPUT_FRAMES_PER_PROC) {
        ctx->accumulated_pair_count = 0;
        wukong_audio_hpf_process_interleaved(s_pipeline->mic_hpf, s_pipeline->mic_buf,
                                             AUDIO_INPUT_PROCESS_SAMPLES, AUDIO_INPUT_MIC_NUM);
#if WUKONG_AUDIO_HPF_FILTER_REF
        wukong_audio_hpf_process_mono(&s_pipeline->ref_hpf, s_pipeline->ref_buf, AUDIO_INPUT_PROCESS_SAMPLES);
#endif
        if (wukong_audio_frontend_process(s_pipeline->mic_buf, s_pipeline->ref_buf, s_pipeline->output_buf) == OPRT_OK) {
            //! AVI 录制拾音点：独立于 AI 唤醒词/VAD 门控，见 wukong_audio_pipeline_platform.c 同名注释
#if defined(ENABLE_TY_AVI_MEDIA) && (ENABLE_TY_AVI_MEDIA == 1)
            if (ty_avi_recorder_is_running()) {
                ty_avi_recorder_audio_feed((const uint8_t *)s_pipeline->output_buf,
                                           AUDIO_INPUT_PROCESS_SAMPLES * AUDIO_INPUT_BYTES_PER_SAMPLE);
            }
#endif
            s_pipeline->output_cb((UINT8_T *)s_pipeline->output_buf, AUDIO_INPUT_PROCESS_SAMPLES * AUDIO_INPUT_BYTES_PER_SAMPLE);
            ctx->sync_start_ms = tal_system_get_millisecond();   //!< 本轮收敛完成，计时归零
            ctx->sync_timeout_warned = FALSE;                    //!< 允许下一轮再告警
        }
    }
}
#else
//! mono 模拟麦路径：ADC LR 中 L=mic、R=ref；保持原始行为，不做 HPF/DC 处理。
STATIC BOOL_T __mono_mic_try_get_frame(VOID)
{
    if (s_pipeline->adc_rb == NULL ||
        s_pipeline->capture_frame_size == 0 || s_pipeline->output_frame_size == 0) {
        tal_system_sleep(s_pipeline->frame_ms);
        return FALSE;
    }

    if (!__pipeline_pop_frame(s_pipeline->adc_rb, &s_pipeline->adc_frame)) {
        tal_system_sleep(s_pipeline->frame_ms / 2);
        return FALSE;
    }

    if (s_pipeline->adc_frame.len > s_pipeline->capture_frame_size ||
        s_pipeline->adc_frame.len > AUDIO_INPUT_FRAME_SIZE) {
        TAL_PR_ERR("wukong audio input -> raw frame too large %u", s_pipeline->adc_frame.len);
        return FALSE;
    }

    return TRUE;
}

STATIC VOID_T __mono_mic_process_frame(VOID)
{
    UINT32_T samples = (s_pipeline->adc_frame.len / sizeof(INT16_T)) / s_pipeline->capture_channels;
    INT16_T *raw_pcm = (INT16_T *)s_pipeline->adc_frame.data;

    for (UINT32_T i = 0; i < samples; i++) {
        s_pipeline->mic_buf[i] = raw_pcm[i * s_pipeline->capture_channels + AUDIO_INPUT_MIC_CH_INDEX];
        s_pipeline->ref_buf[i] = raw_pcm[i * s_pipeline->capture_channels + AUDIO_INPUT_REF_CH_INDEX];
    }

    if (wukong_audio_frontend_process(s_pipeline->mic_buf,
                                      s_pipeline->ref_buf,
                                      s_pipeline->output_buf) == OPRT_OK) {
        //! AVI 录制拾音点：独立于 AI 唤醒词/VAD 门控，见 wukong_audio_pipeline_platform.c 同名注释
#if defined(ENABLE_TY_AVI_MEDIA) && (ENABLE_TY_AVI_MEDIA == 1)
        if (ty_avi_recorder_is_running()) {
            ty_avi_recorder_audio_feed((const uint8_t *)s_pipeline->output_buf, samples * sizeof(INT16_T));
        }
#endif
        s_pipeline->output_cb((UINT8_T *)s_pipeline->output_buf, samples * sizeof(INT16_T));
    }
}
#endif /* WUKONG_AUDIO_USING_DUAL_MIC */

STATIC VOID_T __audio_input_process_task(PVOID_T args)
{
    (void)args;

#if defined(WUKONG_AUDIO_USING_DUAL_MIC)
    /*
     * SPRS dual-mic path: two independent capture streams (ADC ref channel,
     * DMIC dual-mic channel) arrive on separate ring buffers with their own
     * time_stamp. We pair one ADC frame with one DMIC frame, verify they are
     * within SPRS_SYNC_THRESHOLD_MS of each other, and only once aligned do we
     * accumulate AUDIO_INPUT_FRAMES_PER_PROC (2) pairs of 256-sample capture
     * frames into one 512-sample process frame for wukong_audio_frontend_process.
     */
    DUAL_MIC_SYNC_CTX_T sync_ctx = {0};
    sync_ctx.sync_start_ms = tal_system_get_millisecond();

    while (s_pipeline != NULL && s_pipeline->running) {
        if (!__dual_mic_try_get_synced_pair(&sync_ctx)) {
            continue;
        }
        __dual_mic_accumulate_pair(&sync_ctx);
    }
#else
    while (s_pipeline != NULL && s_pipeline->running) {
        if (!__mono_mic_try_get_frame()) {
            continue;
        }
        __mono_mic_process_frame();
    }
#endif

    //! 通知 deinit：本线程已退出循环，不再触碰 s_pipeline/adc_rb/recorder，可安全释放
    if (s_pipeline != NULL && s_pipeline->exit_sem) {
        tal_semaphore_post(s_pipeline->exit_sem);
    }
}

OPERATE_RET wukong_audio_pipeline_init(WUKONG_AUDIO_PIPELINE_CFG_T *cfg)
{
    TUYA_CHECK_NULL_RETURN(cfg, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(cfg->output_cb, OPRT_INVALID_PARM);

    OPERATE_RET rt = OPRT_OK;

    //! allocate the pipeline (holds several KB of buffers, so it is heap/PSRAM backed
    //! rather than a static instance); must happen before any rb/handle/thread is
    //! created below, and before capture can start
    s_pipeline = (WUKONG_AUDIO_PIPELINE_T *)MEM_MALLOC(sizeof(WUKONG_AUDIO_PIPELINE_T));
    if (s_pipeline == NULL) {
        TAL_PR_ERR("alloc pipeline failed");
        return OPRT_MALLOC_FAILED;
    }
    memset(s_pipeline, 0, sizeof(WUKONG_AUDIO_PIPELINE_T));
    s_pipeline->output_cb = cfg->output_cb;

    UINT32_T sample_rate = cfg->sample_rate;
    UINT8_T sample_bits = cfg->sample_bits;
    UINT8_T capture_channels = AUDIO_INPUT_CAPTURE_CHAN_NUM;
    UINT8_T output_channels = cfg->channel;
    UINT32_T frame_ms = AUDIO_INPUT_CAPTURE_FRAME_MS;
    UINT32_T capture_frame_size = (sample_rate * sample_bits * capture_channels * frame_ms) / 8 / 1000;
    UINT32_T output_frame_size = (sample_rate * sample_bits * output_channels * frame_ms) / 8 / 1000;

    s_pipeline->capture_channels = capture_channels;
    s_pipeline->frame_ms = frame_ms;
    s_pipeline->capture_frame_size = capture_frame_size;
    s_pipeline->output_frame_size = output_frame_size;
#if defined(WUKONG_AUDIO_USING_DUAL_MIC)
    for (UINT32_T i = 0; i < AUDIO_INPUT_MIC_NUM; i++) {
        wukong_audio_hpf_init(&s_pipeline->mic_hpf[i], (float)sample_rate,
                              (float)WUKONG_AUDIO_HPF_CUTOFF_HZ);
    }
#if WUKONG_AUDIO_HPF_FILTER_REF
    wukong_audio_hpf_init(&s_pipeline->ref_hpf, (float)sample_rate,
                          (float)WUKONG_AUDIO_HPF_CUTOFF_HZ);
#endif
#endif

    if (capture_frame_size == 0 || capture_frame_size > AUDIO_INPUT_FRAME_SIZE ||
        output_frame_size == 0 || output_frame_size > AUDIO_INPUT_FRAME_SIZE) {
        TAL_PR_ERR("invalid tal audio frame size raw=%u output=%u",
                   capture_frame_size, output_frame_size);
        rt = OPRT_INVALID_PARM;
        goto __error;
    }

    TUYA_CALL_ERR_GOTO(tuya_ring_buff_create(AUDIO_INPUT_RB_SIZE, OVERFLOW_STOP_TYPE, &s_pipeline->adc_rb), __error);

    TAL_AUDIO_INPUT_CFG_T audio_config = {0};
    audio_config.type = TAL_AUDIO_INPUT_ADC;
    audio_config.dev.ai_adc_conf.port = TUYA_AUDIO_ADC_PORT_0;
    audio_config.dev.ai_adc_conf.chan = TUYA_AUDIO_ADC_CHANNEL_LR;
    audio_config.sample_bits = sample_bits;
    audio_config.sample_rate = sample_rate;
    audio_config.frame_time_ms = frame_ms;
    audio_config.audio_input_cb = __audio_input_raw_cb;
    audio_config.args = s_pipeline->adc_rb;

    s_pipeline->adc_handle = tal_audio_input_init(&audio_config);
    if (s_pipeline->adc_handle == NULL) {
        rt = OPRT_COM_ERROR;
        goto __error;
    }
    TAL_PR_DEBUG("sample %d, databits %d, channel %d", cfg->sample_rate, cfg->sample_bits, cfg->channel);

#if defined(WUKONG_AUDIO_USING_DUAL_MIC)
    TUYA_CALL_ERR_GOTO(tuya_ring_buff_create(AUDIO_INPUT_RB_SIZE, OVERFLOW_STOP_TYPE, &s_pipeline->dmic_rb), __error);
    audio_config.type = TAL_AUDIO_INPUT_DMIC;
    memset(&audio_config.dev, 0, sizeof(audio_config.dev));
    audio_config.dev.ai_dmic_conf.port = TUYA_AUDIO_DMIC_PORT_0;
    audio_config.dev.ai_dmic_conf.chan = TUYA_AUDIO_DMIC_CHANNEL_LR;
    audio_config.args = s_pipeline->dmic_rb;
    s_pipeline->dmic_handle = tal_audio_input_init(&audio_config);
    if (s_pipeline->dmic_handle == NULL) {
        rt = OPRT_COM_ERROR;
        goto __error;
    }
#endif

#if defined(TUYA_MODULE_T5) && (TUYA_MODULE_T5 == 1)
    //! backend ops differ (distinct symbols) so registration stays guarded; the init
    //! frame_size is the mono "process output frame" byte count shared by both
    //! (SPRS 512 samples->1024B, tuya 320 samples->640B == its output_frame_size)
#if defined(USING_SPRS_AUDIO_FRONTEND)
    wukong_audio_frontend_register(&g_sprs_frontend_ops);
#else
    wukong_audio_frontend_register(&g_tuya_frontend_ops);
#endif
    TUYA_CALL_ERR_GOTO(wukong_audio_frontend_init(cfg->vad_active_ms, cfg->vad_off_ms,
                               AUDIO_INPUT_PROCESS_SAMPLES * AUDIO_INPUT_BYTES_PER_SAMPLE), __error);
#endif

    if (!s_pipeline->task) {
        THREAD_CFG_T thrd_cfg = {
            .priority = THREAD_PRIO_1,
            .stackDepth = INPUT_BOARD_STACK_SIZE,
            .thrdname = "audio_proc",
            #if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
            .psram_mode = 1,
            #endif
        };
        TUYA_CALL_ERR_GOTO(tal_semaphore_create_init(&s_pipeline->exit_sem, 0, 1), __error);
        s_pipeline->running = TRUE;
        TUYA_CALL_ERR_GOTO(tal_thread_create_and_start(&s_pipeline->task, NULL, NULL, __audio_input_process_task, NULL, &thrd_cfg), __error);
    }

#if defined(WUKONG_AUDIO_USING_DUAL_MIC)
    TUYA_CALL_ERR_GOTO(tal_audio_input_start(s_pipeline->dmic_handle), __error);
#endif
    TUYA_CALL_ERR_GOTO(tal_audio_input_start(s_pipeline->adc_handle), __error);

    return OPRT_OK;
__error:
    wukong_audio_pipeline_deinit();
    return rt;
}

OPERATE_RET wukong_audio_pipeline_deinit(VOID)
{
    if (s_pipeline == NULL) {
        return OPRT_OK;
    }

    //! mirror init start order (DMIC first, then ADC): stop DMIC before ADC
#if defined(WUKONG_AUDIO_USING_DUAL_MIC)
    if (s_pipeline->dmic_handle != NULL) {
        tal_audio_input_stop(s_pipeline->dmic_handle);
    }
#endif
    if (s_pipeline->adc_handle != NULL) {
        tal_audio_input_stop(s_pipeline->adc_handle);
    }

    s_pipeline->running = FALSE;
    //! 与 vad_task 同款同步：等 process task 确认退出（不再触碰 adc_rb/s_pipeline/recorder）后再释放，
    //! 避免 tal_thread_delete 异步返回后线程仍读取即将被 free 的资源（UAF）
    if (s_pipeline->task != NULL) {
        if (s_pipeline->exit_sem) {
            tal_semaphore_wait(s_pipeline->exit_sem, SEM_WAIT_FOREVER);
        }
        tal_thread_delete(s_pipeline->task);
        s_pipeline->task = NULL;
    }

    //! process task 已退出、不再调 frontend_process，安全释放前端后端资源
    //! (SPRS: esnr SRAM / res_psram / KWS buffer；tuya: speex/rnn)，支持 deinit/reinit 不泄漏
#if defined(TUYA_MODULE_T5) && (TUYA_MODULE_T5 == 1)
    wukong_audio_frontend_deinit();
#endif

    if (s_pipeline->adc_handle != NULL) {
        tal_audio_input_deinit(s_pipeline->adc_handle);
        s_pipeline->adc_handle = NULL;
    }

    if (s_pipeline->adc_rb != NULL) {
        tuya_ring_buff_free(s_pipeline->adc_rb);
        s_pipeline->adc_rb = NULL;
    }

#if defined(WUKONG_AUDIO_USING_DUAL_MIC)
    if (s_pipeline->dmic_handle != NULL) {
        tal_audio_input_deinit(s_pipeline->dmic_handle);
        s_pipeline->dmic_handle = NULL;
    }
    if (s_pipeline->dmic_rb != NULL) {
        tuya_ring_buff_free(s_pipeline->dmic_rb);
        s_pipeline->dmic_rb = NULL;
    }
#endif

    if (s_pipeline->exit_sem) {
        tal_semaphore_release(s_pipeline->exit_sem);
        s_pipeline->exit_sem = NULL;
    }

    //! process task 已确认退出（上方 exit_sem 等待），无人再触碰 s_pipeline，可安全释放
    MEM_FREE(s_pipeline);
    s_pipeline = NULL;

    return OPRT_OK;
}

OPERATE_RET wukong_audio_pipeline_set_vol(UINT8_T volume)
{
    TUYA_CHECK_NULL_RETURN(s_pipeline, OPRT_RESOURCE_NOT_READY);
    TUYA_CHECK_NULL_RETURN(s_pipeline->adc_handle, OPRT_RESOURCE_NOT_READY);
    return tal_audio_input_set_volume(s_pipeline->adc_handle, volume);
}
