/**
 * @file wukong_audio_input_frame.h
 * @brief input/ 私有配置头：帧参数宏与内存分配宏集中处（board / pipeline 共用）
 * @note 仅供 src/wukong/audio/input/ 内部使用，勿从其它模块 include
 */

#ifndef __WUKONG_AUDIO_INPUT_FRAME_H__
#define __WUKONG_AUDIO_INPUT_FRAME_H__

#include "tal_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM==1)
//! PSRAM allocations must be released with the PSRAM allocator; tal_free routes by a
//! global flag (default off) and would free PSRAM memory on the normal heap.
#define MEM_MALLOC tal_psram_malloc
#define MEM_FREE   tal_psram_free
#else
#define MEM_MALLOC tal_malloc
#define MEM_FREE   tal_free
#endif

//! frame length "three concepts": capture (ADC/DMIC raw callback), process (frontend
//! algorithm output) and frames-per-proc (how many capture frames accumulate into one
//! process frame). Single-mic (tuya) path keeps capture == process == 20ms, 1:1.
#define AUDIO_INPUT_SAMPLE_RATE        16000U
#define AUDIO_INPUT_SAMPLE_BITS        16U
#define AUDIO_INPUT_BYTES_PER_SAMPLE   (AUDIO_INPUT_SAMPLE_BITS / 8U)
#define AUDIO_INPUT_CAPTURE_CHAN_NUM   2U     //! 采集恒 LR 双通道
#define AUDIO_INPUT_MIC_CH_INDEX       0U
#define AUDIO_INPUT_REF_CH_INDEX       1U
#define AUDIO_INPUT_OUTPUT_CHAN_NUM    1U
#define AUDIO_INPUT_RB_SIZE            (128U * 1024U)
#define AUDIO_INPUT_FRAME_BYTES(sample_rate, bits, ch, ms) \
    (((sample_rate) / 1000U) * ((bits) / 8U) * (ch) * (ms))

#if defined(USING_SPRS_AUDIO_FRONTEND)   /* frame timing is SPRS-algorithm-specific */
//! dual-mic capture capability, currently derived from the SPRS backend macro; kept as
//! its own semantic macro so capture topology (dual-mic) is decoupled from the specific
//! backend (SPRS) mounted on top of it -- see the backend registration in
//! wukong_audio_pipeline.c, which stays gated on the SPRS macro directly
#define WUKONG_AUDIO_USING_DUAL_MIC   1
#define AUDIO_INPUT_CAPTURE_FRAME_MS   16U    /* (1) 采集帧 */
#define AUDIO_INPUT_PROCESS_FRAME_MS   32U    /* (2) SPRS 算法帧 */
#define AUDIO_INPUT_FRAMES_PER_PROC    2U     /* 2 个采集帧累积成 1 个处理帧 */
#define AUDIO_INPUT_MIC_NUM            2U     /* DMIC LR = MIC1 + MIC2 */
#define AUDIO_INPUT_CAPTURE_SAMPLES    256U   /* 16ms/通道，累积循环用 */
#define AUDIO_INPUT_PROCESS_SAMPLES    512U   /* 32ms/通道，buffer/frontend 用 */
#define SPRS_SYNC_THRESHOLD_MS       (AUDIO_INPUT_CAPTURE_FRAME_MS / 2U)   /* 8ms */
#define SPRS_SYNC_CONVERGE_MAX_MS    3000U                                  /* 收敛超时上限 */
#else
#define AUDIO_INPUT_CAPTURE_FRAME_MS   20U    /* 单麦: capture == process == 20ms */
#define AUDIO_INPUT_PROCESS_FRAME_MS   20U
#define AUDIO_INPUT_FRAMES_PER_PROC    1U
#define AUDIO_INPUT_MIC_NUM            1U     /* ADC L = mic (R = ref) */
#define AUDIO_INPUT_CAPTURE_SAMPLES    320U   /* 20ms/通道 */
#define AUDIO_INPUT_PROCESS_SAMPLES    320U
#endif

//! 采集帧字节：pipeline 存的 LR 原始帧（采集恒 2ch）。tuya 20ms->1280B、SPRS 16ms->1024B。
#define AUDIO_INPUT_FRAME_SIZE \
    AUDIO_INPUT_FRAME_BYTES(AUDIO_INPUT_SAMPLE_RATE, AUDIO_INPUT_SAMPLE_BITS, \
                            AUDIO_INPUT_CAPTURE_CHAN_NUM, AUDIO_INPUT_CAPTURE_FRAME_MS)

#ifdef __cplusplus
}
#endif

#endif /* __WUKONG_AUDIO_INPUT_FRAME_H__ */
