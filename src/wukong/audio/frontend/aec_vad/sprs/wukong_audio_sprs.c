/**
 * @file wukong_audio_sprs.c
 * @brief SPRS ESNR dual-mic frontend (init/deinit implemented; process/vad/kws are stubs for later tasks)
 * @copyright Copyright (c) Tuya Inc.
 */
#include "wukong_audio_sprs.h"
#include "esnr.h"
#include "audio_dump.h"
#include "tal_memory.h"
#include "tal_system.h"
#include "tal_log.h"
#include "tuya_app_config.h"
#include <string.h>

/* SPRS 固定帧：512 样点 = 32ms @16kHz；mono 输出帧字节数 */
#define SPRS_FRAME_SIZE        512
#define SPRS_MONO_FRAME_BYTES  (SPRS_FRAME_SIZE * (INT_T)sizeof(INT16_T))   /* 1024 */

/* SPRS 算法 SRAM。example 称 80KB，同系列旧库需 365KB —— 以 T5 实测为准，可配。
 * 先给足余量；若 esnr_init 失败按日志上调。 */
#ifndef SPRS_SRAM_SIZE
#define SPRS_SRAM_SIZE         (128 * 1024)
#endif

/* SPRS 调参宏来自应用侧生成头 tuya_app_config.h（USING_SPRS 选中时定义，无 CONFIG_ 前缀）：
 * SPRS_AGC_TARGET_LEVEL(int)、SPRS_ENABLE_AEC(bool，n 时不定义)。未定义时给保守默认。 */
#ifndef SPRS_AGC_TARGET_LEVEL
#define SPRS_AGC_TARGET_LEVEL 5
#endif
#ifndef SPRS_ENABLE_AEC
#define SPRS_ENABLE_AEC 0
#endif

STATIC VOID                   *s_sprs_handle = NULL;
STATIC UINT8_T                *s_sprs_sram   = NULL;
STATIC UINT8_T                *s_res_psram   = NULL;
STATIC INT16_T                *s_kws_buf     = NULL;   /* 通道0 AEC-only, 供 KWS */
STATIC UINT32_T                s_frame_size  = 0;      /* frontend 传入的字节帧长 */
STATIC volatile WUKONG_AUDIO_VAD_FLAG_E s_vad_flag = WUKONG_AUDIO_VAD_STOP;

/* esnr_process 大缓冲：不放栈（in 3072B + out 2048B ≈5KB，处理线程栈默认仅 2560B） */
STATIC INT16_T s_sprs_in[SPRS_FRAME_SIZE * 3];   /* [MIC1|MIC2|REF] planar, 1536 */
STATIC INT16_T s_sprs_out[SPRS_FRAME_SIZE * 2];  /* [AEC-only|AEC+NS] planar, 1024 */

STATIC OPERATE_RET __sprs_deinit(VOID);   /* 前置声明，init 失败回滚用 */

STATIC OPERATE_RET __sprs_init(UINT32_T min_speech_ms, UINT32_T max_interval_ms, UINT32_T frame_size)
{
    (void)min_speech_ms;   /* SPRS VAD 无 min/max 参数，忽略（保留签名兼容 ops 表） */
    (void)max_interval_ms;

    /* frame_size 由 board 传入，单位字节；SPRS 处理帧 = 512 样点 mono = 1024B */
    if (frame_size != (UINT32_T)SPRS_MONO_FRAME_BYTES) {
        TAL_PR_ERR("sprs init: frame_size %u != %d (512 samples mono)", frame_size, SPRS_MONO_FRAME_BYTES);
        return OPRT_INVALID_PARM;
    }
    s_frame_size = frame_size;

    /* 1) SRAM */
    if (s_sprs_sram == NULL) {
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
        s_sprs_sram = (UINT8_T *)tal_psram_malloc(SPRS_SRAM_SIZE);
#else
        s_sprs_sram = (UINT8_T *)tal_malloc(SPRS_SRAM_SIZE);
#endif
        if (s_sprs_sram == NULL) { TAL_PR_ERR("sprs: alloc sram %d failed", SPRS_SRAM_SIZE); goto __err; }
    }

    /* 2) 资源数组：engine_get_res 取内嵌资源，拷入 PSRAM 加速 */
    const unsigned char *res_addr = NULL;
    unsigned int res_size = 0;
    engine_get_res(&res_addr, &res_size);
    if (res_addr == NULL || res_size == 0) { TAL_PR_ERR("sprs: engine_get_res invalid"); goto __err; }
    if (s_res_psram == NULL) {
        /* 与 s_sprs_sram 同款分支：无 PSRAM 时退化为普通堆，仅失去加速，功能不变。
         * tal_psram_* 声明受 ENABLE_EXT_RAM 宏保护，未开该宏时不能引用。 */
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
        s_res_psram = (UINT8_T *)tal_psram_malloc(res_size);
#else
        s_res_psram = (UINT8_T *)tal_malloc(res_size);
#endif
        if (s_res_psram == NULL) { TAL_PR_ERR("sprs: alloc res_psram %u failed", res_size); goto __err; }
        memcpy(s_res_psram, res_addr, res_size);
    }

    /* 3) KWS 缓冲（通道0 AEC-only） */
    if (s_kws_buf == NULL) {
        s_kws_buf = (INT16_T *)tal_malloc(SPRS_MONO_FRAME_BYTES);
        if (s_kws_buf == NULL) { TAL_PR_ERR("sprs: alloc kws buf failed"); goto __err; }
    }

    /* 4) esnr_init：全字段，enable_2mic/enable_vad 必须显式=1 */
    if (s_sprs_handle == NULL) {
        ESNRConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.sram_addr        = (char *)s_sprs_sram;
        cfg.sram_size        = SPRS_SRAM_SIZE;
        cfg.work_mode        = WORK_MODE_TEL;
        cfg.res_aes_addr     = s_res_psram;
        cfg.res_aes_size     = (int)res_size;
        cfg.agc_target_level = SPRS_AGC_TARGET_LEVEL;
        cfg.enable_aec       = SPRS_ENABLE_AEC;
        cfg.enable_vad       = 1;
        cfg.enable_2mic      = 1;

        int ret = esnr_init(&s_sprs_handle, &cfg);
        if (ret != 0 || s_sprs_handle == NULL) { TAL_PR_ERR("sprs: esnr_init ret=%d", ret); goto __err; }

        const char *ver = NULL;
        if (esnr_get_version(s_sprs_handle, &ver) == 0 && ver) {
            TAL_PR_NOTICE("sprs: esnr version=%s, sram=%d, res=%u, agc=%d, aec=%d",
                          ver, SPRS_SRAM_SIZE, res_size, SPRS_AGC_TARGET_LEVEL, SPRS_ENABLE_AEC);
        }
    }
    s_vad_flag = WUKONG_AUDIO_VAD_STOP;
    return OPRT_OK;

__err:
    __sprs_deinit();
    return OPRT_COM_ERROR;
}

STATIC OPERATE_RET __sprs_deinit(VOID)
{
    if (s_sprs_handle) { esnr_uninit(s_sprs_handle); s_sprs_handle = NULL; }
    if (s_sprs_sram) {
        /* s_sprs_sram 的分配方式与 init 中一致：ENABLE_EXT_RAM 下走 PSRAM 分配器，
         * 本仓库 tal_free 默认不路由 PSRAM（见 wukong_audio_input_board.c 顶部
         * MEM_MALLOC/MEM_FREE 约定），故这里必须与分配器成对使用 tal_psram_free。 */
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
        tal_psram_free(s_sprs_sram);
#else
        tal_free(s_sprs_sram);
#endif
        s_sprs_sram = NULL;
    }
    if (s_res_psram) {
        /* 释放路由必须与 init 中分配分支完全对称（tal_psram_* 受 ENABLE_EXT_RAM 宏保护）。 */
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
        tal_psram_free(s_res_psram);
#else
        tal_free(s_res_psram);
#endif
        s_res_psram = NULL;
    }
    if (s_kws_buf) { tal_free(s_kws_buf); s_kws_buf = NULL; }
    s_frame_size = 0;
    s_vad_flag = WUKONG_AUDIO_VAD_STOP;
    return OPRT_OK;
}

STATIC OPERATE_RET __sprs_process(INT16_T *mic, INT16_T *ref, INT16_T *out)
{
    TUYA_CHECK_NULL_RETURN(mic, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(ref, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(out, OPRT_INVALID_PARM);
    TUYA_CHECK_NULL_RETURN(s_sprs_handle, OPRT_RESOURCE_NOT_READY);

    /* 重排为通道拼接 planar: [MIC1(512)|MIC2(512)|REF(512)]
     * mic 是 DMIC LR 逐样点交织: mic[2i]=MIC1(L), mic[2i+1]=MIC2(R) */
    for (INT32_T i = 0; i < SPRS_FRAME_SIZE; i++) {
        s_sprs_in[0 * SPRS_FRAME_SIZE + i] = mic[i * 2];
        s_sprs_in[1 * SPRS_FRAME_SIZE + i] = mic[i * 2 + 1];
        s_sprs_in[2 * SPRS_FRAME_SIZE + i] = ref[i];
    }

    int vadout[2] = {0};
    UINT32_T t0 = tal_system_get_millisecond();
    int ret = esnr_process(s_sprs_handle, s_sprs_in, s_sprs_out, vadout);
    UINT32_T t1 = tal_system_get_millisecond();
    if (ret != 0) { TAL_PR_ERR("sprs: esnr_process ret=%d", ret); return OPRT_COM_ERROR; }

    /* 输出通道拼接: out[0:512]=通道0 AEC-only(→KWS), out[512:1024]=通道1 AEC+NS(→上行) */
    memcpy(s_kws_buf, s_sprs_out, SPRS_MONO_FRAME_BYTES);                       /* 通道0 → KWS */
    memcpy(out, s_sprs_out + SPRS_FRAME_SIZE, SPRS_MONO_FRAME_BYTES);           /* 通道1 → 上行主输出 */

    /* VAD 取引擎 vadout[0] */
    if (vadout[0]) {
        if (s_vad_flag != WUKONG_AUDIO_VAD_START) { s_vad_flag = WUKONG_AUDIO_VAD_START; TAL_PR_DEBUG("sprs vad start"); }
    } else {
        if (s_vad_flag != WUKONG_AUDIO_VAD_STOP)  { s_vad_flag = WUKONG_AUDIO_VAD_STOP;  TAL_PR_DEBUG("sprs vad stop"); }
    }

    /* dump 四路（datalen uint16_t，各路 ≤2048B）：MIC=DMIC LR 交织，REF，AEC=通道1，KWS=通道0 */
    audio_dump_write(AUDIO_DUMP_MIC, (uint8_t *)mic, (uint16_t)(SPRS_FRAME_SIZE * 2 * sizeof(INT16_T)));
    audio_dump_write(AUDIO_DUMP_REF, (uint8_t *)ref, (uint16_t)SPRS_MONO_FRAME_BYTES);
    audio_dump_write(AUDIO_DUMP_AEC, (uint8_t *)out, (uint16_t)SPRS_MONO_FRAME_BYTES);
    audio_dump_write(AUDIO_DUMP_KWS, (uint8_t *)s_kws_buf, (uint16_t)SPRS_MONO_FRAME_BYTES);

    STATIC INT_T cnt = 0;
    if ((cnt++ % 500) == 0) TAL_PR_DEBUG("sprs process %ums, vad=%d, cnt=%d", t1 - t0, s_vad_flag, cnt);
    return OPRT_OK;
}

STATIC OPERATE_RET __sprs_get_kws_output(INT16_T **data, UINT32_T *len)
{
    if (s_kws_buf == NULL) return OPRT_RESOURCE_NOT_READY;
    *data = s_kws_buf;                 /* 通道0 AEC-only */
    *len  = SPRS_MONO_FRAME_BYTES;     /* 1024B */
    return OPRT_OK;
}

/* 新库无运行时 VAD 控制接口：start/stop 仅复位内部 flag，set_threshold no-op */
STATIC OPERATE_RET __sprs_vad_start(VOID) { s_vad_flag = WUKONG_AUDIO_VAD_STOP; return OPRT_OK; }
STATIC OPERATE_RET __sprs_vad_stop(VOID)  { s_vad_flag = WUKONG_AUDIO_VAD_STOP; return OPRT_OK; }
STATIC OPERATE_RET __sprs_vad_set_threshold(WUKONG_AUDIO_VAD_THRESHOLD_E l) { (void)l; return OPRT_OK; }
STATIC INT_T       __sprs_vad_get_flag(VOID) { return (INT_T)s_vad_flag; }

WUKONG_AUDIO_FRONTEND_OPS_T g_sprs_frontend_ops = {
    .init              = __sprs_init,
    .deinit            = __sprs_deinit,
    .process           = __sprs_process,
    .vad_start         = __sprs_vad_start,
    .vad_stop          = __sprs_vad_stop,
    .vad_set_threshold = __sprs_vad_set_threshold,
    .vad_get_flag      = __sprs_vad_get_flag,
    .get_kws_output    = __sprs_get_kws_output,
};
