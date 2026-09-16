/**
* Copyright (C) by Tuya Inc                                                  
* All rights reserved                                                        
*
* @file audio_dump.c
* @brief audio dump mic/ref/aec
* @version 1.0
* @author linch
* @date 2025-06-02
*
*/

#include "tuya_device_cfg.h"
#include "tuya_cloud_types.h"
#include "audio_dump.h"
#include "tal_memory.h"
#include "tal_uart.h"
#include "tal_log.h"
#include "tal_thread.h"
#include "tal_mutex.h"
#include "tal_time_service.h"   /* tal_time_get_local_time_custom — SD 文件名时间戳 */
#include "audio_analysis.h"
#include "wukong_audio_input.h"
#include "wukong_audio_player.h"
#include "tkl_fs.h"
#include "tal_workq_service.h"   /* WORKQ_SYSTEM — SDCARD 双缓冲后台落盘 */
#include "tal_system.h"          /* tal_system_sleep — finalize 等待在途落盘 */
#include <stdio.h>   /* snprintf */
#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
#include "tuya_ai_monitor.h"
#include "tal_workq_service.h"
#endif

#define AUDIO_DUMP_BUF          1024*1024

typedef struct {
    uint8_t  *data;
    uint32_t  datalen;
} audio_dump_t;

STATIC audio_dump_t audio_dump[AUDIO_DUMP_MAX];
STATIC BOOL_T audio_dump_flag = FALSE;
STATIC MUTEX_HANDLE __s_dump_mutex = NULL;

/* State tracking for GET operations */
STATIC INT_T __s_current_volume = 50;     // Default volume
STATIC INT_T __s_current_micgain = 70;    // Default mic gain

#define AUDIO_DUMP_SD_DIR_MAX   96
#define AUDIO_DUMP_SD_WRITE_RETRY   5     /* tkl_fwrite 无进展时最多退避重试次数 */
#define AUDIO_DUMP_SD_WRITE_BACKOFF 10    /* 每次重试前退避 ms */

STATIC AUDIO_DUMP_CHANNEL_E __s_channel = AUDIO_DUMP_CH_OFF;
STATIC CHAR_T __s_sd_dir[AUDIO_DUMP_SD_DIR_MAX] = {0};
#if ENABLE_EXT_RAM
STATIC CONST CHAR_T *__s_sd_names[AUDIO_DUMP_MAX] = { "mic", "ref", "aec", "kws", "vad" };

/* ---- SDCARD 双缓冲（ping-pong）自动滚动落盘通路 ----
 * 与传统单缓冲 audio_dump[] / UART / LAN-realtime 通路并列隔离。
 * 仅在 SDCARD 通道会话期间分配（5 路 x 2 块 x AUDIO_DUMP_BLOCK_SIZE = 1.6MB）。 */
typedef struct {
    uint8_t  *buf[2];          /* ping-pong 两块，各 AUDIO_DUMP_BLOCK_SIZE */
    uint32_t  wlen[2];         /* 各块已写字节 */
    uint8_t   active;          /* 当前写入块 0/1 */
    volatile uint8_t busy[2];  /* 该块是否正在后台落盘 */
    uint32_t  flushlen[2];     /* 后台落盘冻结的待写长度 */
} audio_dump_sd_t;

STATIC audio_dump_sd_t __s_sd[AUDIO_DUMP_MAX];
STATIC TUYA_FILE       __s_sd_fp[AUDIO_DUMP_MAX] = {NULL};  /* 每路 session 文件句柄：会话期间常开 */
STATIC BOOL_T          __s_sd_on = FALSE;        /* SD 旋转通路激活标志（独立于 __s_dump_mode） */
STATIC CHAR_T          __s_sd_stamp[20] = {0};   /* 会话级文件名时间戳前缀 */
STATIC volatile INT_T  __s_sd_overrun = 0;       /* 备用块未落完导致的丢帧计数 */

/* 前向声明：audio_dump_write 在该函数定义之前调用它 */
STATIC VOID __audio_dump_sd_schedule_flush(INT_T type, uint8_t idx);
#endif

#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
typedef enum {
    AUDIO_DUMP_MODE_BUFFER = 0,
    AUDIO_DUMP_MODE_REALTIME,
} AUDIO_DUMP_MODE_E;

typedef struct {
    AUDIO_DUMP_MODE_E mode;
    UINT32_T batch_count;
    UINT32_T frame_size;
} AUDIO_DUMP_REALTIME_CFG_T;

STATIC AUDIO_DUMP_MODE_E __s_dump_mode = AUDIO_DUMP_MODE_BUFFER;
STATIC UINT32_T __s_realtime_batch_count = 50;
STATIC UINT32_T __s_realtime_frame_size = 640;
STATIC CHAR_T *__s_realtime_buf[AUDIO_DUMP_MAX] = {NULL};
STATIC UINT32_T __s_realtime_idx = 0;
STATIC UINT32_T __s_realtime_written[AUDIO_DUMP_MAX] = {0};
STATIC BOOL_T __s_realtime_first_send_flags[AUDIO_DUMP_MAX] = {TRUE, TRUE, TRUE, TRUE, TRUE};
STATIC volatile INT_T __s_realtime_pending_count = 0;
#define REALTIME_PENDING_MAX 10

typedef struct {
    INT_T type;
    CHAR_T *data;
    UINT32_T size;
    AI_STREAM_TYPE stream_type;
} AUDIO_REALTIME_WORKQ_DATA_T;

STATIC OPERATE_RET __audio_dump_realtime_init(CONST AUDIO_DUMP_REALTIME_CFG_T *cfg);
STATIC VOID __audio_dump_realtime_deinit(VOID);
STATIC VOID __audio_dump_set_mode(AUDIO_DUMP_MODE_E mode);

STATIC VOID_T __audio_realtime_workq_cb(VOID_T *data)
{
    AUDIO_REALTIME_WORKQ_DATA_T *workq_data = (AUDIO_REALTIME_WORKQ_DATA_T *)data;
    if (workq_data == NULL || workq_data->data == NULL) {
        return;
    }
    
    switch (workq_data->type) {
    case AUDIO_DUMP_MIC:
        tuya_ai_monitor_broadcast_audio_mic(workq_data->stream_type, workq_data->data, workq_data->size);
        break;
    case AUDIO_DUMP_REF:
        tuya_ai_monitor_broadcast_audio_ref(workq_data->stream_type, workq_data->data, workq_data->size);
        break;
    case AUDIO_DUMP_AEC:
        tuya_ai_monitor_broadcast_audio_aec(workq_data->stream_type, workq_data->data, workq_data->size);
        break;
    case AUDIO_DUMP_KWS:
        tuya_ai_monitor_broadcast_audio_kws(workq_data->stream_type, workq_data->data, workq_data->size);
        break;
    case AUDIO_DUMP_VAD:
        tuya_ai_monitor_broadcast_audio_vad(workq_data->stream_type, workq_data->data, workq_data->size);
        break;
    default:
        break;
    }
    
    tal_free(workq_data->data);
    tal_free(workq_data);
    
    if (__s_dump_mutex != NULL) {
        tal_mutex_lock(__s_dump_mutex);
        __s_realtime_pending_count--;
        tal_mutex_unlock(__s_dump_mutex);
    }
}

STATIC VOID_T __audio_realtime_send(INT_T type, UINT32_T batch_size, AI_STREAM_TYPE stream_type)
{
    if (__s_realtime_buf[type] == NULL) {
        return;
    }
    
    if (__s_realtime_pending_count >= REALTIME_PENDING_MAX) {
        TAL_PR_WARN("realtime workq full, drop type %d", type);
        return;
    }
    
    AUDIO_REALTIME_WORKQ_DATA_T *workq_data = (AUDIO_REALTIME_WORKQ_DATA_T *)tal_malloc(sizeof(AUDIO_REALTIME_WORKQ_DATA_T));
    if (workq_data == NULL) {
        return;
    }
    
    workq_data->type = type;
    workq_data->size = batch_size;
    workq_data->stream_type = stream_type;
    workq_data->data = (CHAR_T *)tal_malloc(batch_size);
    if (workq_data->data == NULL) {
        tal_free(workq_data);
        return;
    }
    
    memcpy(workq_data->data, __s_realtime_buf[type], batch_size);
    
    if (tal_workq_schedule(WORKQ_SYSTEM, __audio_realtime_workq_cb, workq_data) != OPRT_OK) {
        tal_free(workq_data->data);
        tal_free(workq_data);
    } else {
        __s_realtime_pending_count++;
    }
}
#endif

VOID audio_dump_init(VOID);
VOID audio_dump_write(INT_T type, uint8_t *data, uint16_t datalen)
{
#if ENABLE_AUDIO_ANALYSIS
    STATIC INT_T init = 0;
    if (!init) {
        audio_dump_init();
        init = 1;
    }

    if (!audio_dump_flag) {
        return;
    }

    if (type >= AUDIO_DUMP_MAX) {
        return;
    }
    
    if (__s_dump_mutex == NULL) {
        return;
    }

#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
    if (__s_dump_mode == AUDIO_DUMP_MODE_REALTIME && __s_realtime_buf[type] != NULL) {
        tal_mutex_lock(__s_dump_mutex);
        if (__s_dump_mode != AUDIO_DUMP_MODE_REALTIME || __s_realtime_buf[type] == NULL) {
            tal_mutex_unlock(__s_dump_mutex);
            return;
        }
        
        UINT32_T buf_capacity = __s_realtime_batch_count * __s_realtime_frame_size;
        if (__s_realtime_written[type] + datalen <= buf_capacity) {
            memcpy(__s_realtime_buf[type] + __s_realtime_written[type], data, datalen);
            __s_realtime_written[type] += datalen;
        }
        
        if (type == AUDIO_DUMP_AEC) {
            __s_realtime_idx++;
            if (__s_realtime_idx >= __s_realtime_batch_count) {
                for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
                    if (__s_realtime_buf[i] != NULL && __s_realtime_written[i] > 0) {
                        AI_STREAM_TYPE st = __s_realtime_first_send_flags[i] ? AI_STREAM_START : AI_STREAM_ING;
                        __audio_realtime_send(i, __s_realtime_written[i], st);
                        __s_realtime_first_send_flags[i] = FALSE;
                        __s_realtime_written[i] = 0;
                    }
                }
                __s_realtime_idx = 0;
            }
        }
        tal_mutex_unlock(__s_dump_mutex);
        return;
    }
#endif

#if ENABLE_EXT_RAM
    if (__s_sd_on) {
        tal_mutex_lock(__s_dump_mutex);
        /* 复查 audio_dump_flag：顶部那次是无锁读，finalize 会持锁置其为 FALSE 作为停写屏障。
         * 不复查的话，本线程可能在 finalize 之后仍写 active 块，与 finalize 的无锁终落盘读竞态。 */
        if (!__s_sd_on || !audio_dump_flag || __s_sd[type].buf[__s_sd[type].active] == NULL) {
            tal_mutex_unlock(__s_dump_mutex);
            return;
        }
        audio_dump_sd_t *c = &__s_sd[type];

        if (c->wlen[c->active] + datalen > AUDIO_DUMP_BLOCK_SIZE) {
            uint8_t other = c->active ^ 1u;
            if (c->busy[other]) {
                /* 备用块还在落盘（10s 周期 vs <1s 落盘，正常不会发生）：丢这一帧 */
                __s_sd_overrun++;
                tal_mutex_unlock(__s_dump_mutex);
                return;
            }
            c->flushlen[c->active] = c->wlen[c->active];
            c->busy[c->active]     = 1;
            __audio_dump_sd_schedule_flush(type, c->active);  /* 持锁调度 */
            c->active          = other;
            c->wlen[c->active] = 0;
        }
        memcpy(c->buf[c->active] + c->wlen[c->active], data, datalen);
        c->wlen[c->active] += datalen;
        tal_mutex_unlock(__s_dump_mutex);
        return;
    }
#endif

    if (audio_dump[type].datalen + datalen > AUDIO_DUMP_BUF) {
        return;
    }
#if ENABLE_EXT_RAM
    if (audio_dump[type].data != NULL) {
        memcpy(audio_dump[type].data + audio_dump[type].datalen, data, datalen);
        audio_dump[type].datalen += datalen;
    }
#else
    if (__s_dump_type != AUDIO_DUMP_MAX) {
        audio_dump_with_uart(type, data, datalen);
    }
#endif

#endif // ENABLE_AUDIO_ANALYSIS
}

VOID audio_dump_enable(VOID)
{
    if (__s_dump_mutex == NULL) {
        return;
    }
    
    tal_mutex_lock(__s_dump_mutex);
    
#if ENABLE_EXT_RAM
    for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
        if (audio_dump[i].data == NULL) {
            audio_dump[i].data = tal_psram_malloc(AUDIO_DUMP_BUF);
            if (audio_dump[i].data == NULL) {
                TAL_PR_ERR("audio_dump_enable: malloc failed for type %d", i);
            }
        }
        audio_dump[i].datalen = 0;
    }
#endif
    
    audio_dump_flag = TRUE;
    tal_mutex_unlock(__s_dump_mutex);
    TAL_PR_DEBUG("audio_dump enabled");
}

VOID audio_dump_stop(VOID)
{
    if (__s_dump_mutex == NULL) {
        return;
    }
    
    tal_mutex_lock(__s_dump_mutex);
    audio_dump_flag = FALSE;
    tal_mutex_unlock(__s_dump_mutex);
    TAL_PR_DEBUG("audio_dump stopped (data preserved for dump)");
}

#if ENABLE_EXT_RAM
/* ====================================================================
 *  SDCARD 双缓冲自动滚动落盘通路
 * ==================================================================== */

/* 打开某路 session 文件（append）。失败返回 NULL。dir/stamp 须已就绪。 */
STATIC TUYA_FILE __audio_dump_sd_open(INT_T type)
{
    CHAR_T path[AUDIO_DUMP_SD_DIR_MAX + 32];
    if (__s_sd_dir[0] == '\0' || __s_sd_stamp[0] == '\0') {
        TAL_PR_ERR("audio_dump sd: dir/stamp not set");
        return NULL;
    }
    snprintf(path, sizeof(path), "%s%s_%s.pcm", __s_sd_dir, __s_sd_stamp, __s_sd_names[type]);
    TUYA_FILE fp = tkl_fopen(path, "ab");
    if (fp == NULL) {
        TAL_PR_ERR("audio_dump sd: open %s failed", path);
    }
    return fp;
}

/* 释放/关闭全部 SD 资源。调用方须持 __s_dump_mutex。
 * UAF 防护：后台 flush cb 读 buf[idx]/写 __s_sd_fp[type] 是不持锁的；finalize 的有限等待
 * 在 workq 压满（或 cb 排在 finalize 之后的单队列场景）时可能超时而 busy 仍为 1。此时
 * 绝不能释放该块/关闭该句柄——否则在途 cb 会读已释放内存。故对仍 busy 的块跳过释放、
 * 对仍有在途块的通道跳过 close（宁可在该病态场景下短暂泄漏，也不 UAF）。cb 跑完会清 busy，
 * 但本会话不再回收该泄漏块；属极少见路径，打 WARN 以便发现。 */
STATIC VOID __audio_dump_sd_free_all(VOID)
{
    for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
        BOOL_T ch_busy = (__s_sd[i].busy[0] || __s_sd[i].busy[1]);
        if (__s_sd_fp[i] != NULL && !ch_busy) {
            tkl_fclose(__s_sd_fp[i]);
            __s_sd_fp[i] = NULL;
        }
        for (INT_T b = 0; b < 2; b++) {
            if (__s_sd[i].busy[b]) {
                TAL_PR_WARN("audio_dump sd: ch%d blk%d still in-flight at teardown, leak to avoid UAF", i, b);
                continue;   /* 跳过：在途 cb 仍引用该块/句柄 */
            }
            if (__s_sd[i].buf[b] != NULL) {
                tal_psram_free(__s_sd[i].buf[b]);
                __s_sd[i].buf[b] = NULL;
            }
            __s_sd[i].wlen[b]     = 0;
            __s_sd[i].flushlen[b] = 0;
        }
        if (!ch_busy) {
            __s_sd[i].active = 0;
        }
    }
}

STATIC OPERATE_RET __audio_dump_sd_init(VOID)
{
    if (__s_dump_mutex == NULL) {
        return OPRT_NOT_FOUND;
    }

    /* 回收上一会话病态 teardown 残留的在途块（free_all 为避免 UAF 会保留仍 busy 的
     * buf/句柄）。先【无锁】等待残留 busy 清零——清 busy 的 flush cb 也要拿锁，持锁等会死锁。
     * 此刻 __s_sd_on/audio_dump_flag 均为 FALSE，不会有新写入产生新的 busy，等待是收敛的。
     * 正常无残留时首次检查即通过，无额外延迟。 */
    for (INT_T waited = 0; waited < 3000; waited += 20) {
        BOOL_T any_busy = FALSE;
        tal_mutex_lock(__s_dump_mutex);
        for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
            if (__s_sd[i].busy[0] || __s_sd[i].busy[1]) { any_busy = TRUE; break; }
        }
        tal_mutex_unlock(__s_dump_mutex);
        if (!any_busy) {
            break;
        }
        tal_system_sleep(20);
    }

    tal_mutex_lock(__s_dump_mutex);
    /* busy 已清 → free_all 把上一会话残留的 buf/句柄全部回收；仍 busy 则其会保留。 */
    __audio_dump_sd_free_all();
    /* 若极端情况下在途块始终未清零：绝不覆盖其指针/句柄（否则错位/污染新会话+双泄漏），
     * 放弃本次启动，UI 维持 OFF，用户稍后重试即可（届时 cb 必已跑完）。 */
    for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
        if (__s_sd[i].busy[0] || __s_sd[i].busy[1]) {
            TAL_PR_ERR("audio_dump sd: prev session flush still in-flight, init aborted");
            tal_mutex_unlock(__s_dump_mutex);
            return OPRT_COM_ERROR;
        }
    }
    for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
        __s_sd[i].active = 0;
        for (INT_T b = 0; b < 2; b++) {
            __s_sd[i].wlen[b]     = 0;
            __s_sd[i].busy[b]     = 0;
            __s_sd[i].flushlen[b] = 0;
            __s_sd[i].buf[b] = (uint8_t *)tal_psram_malloc(AUDIO_DUMP_BLOCK_SIZE);
            if (__s_sd[i].buf[b] == NULL) {
                UINT32_T need_total = (UINT32_T)AUDIO_DUMP_MAX * 2 * AUDIO_DUMP_BLOCK_SIZE;
                UINT32_T got_total  = ((UINT32_T)i * 2 + b) * AUDIO_DUMP_BLOCK_SIZE;
                TAL_PR_ERR("audio_dump sd: psram malloc failed at ch %d blk %d "
                           "(block=%u, got %u/%u bytes = %u/%u blocks)",
                           i, b, (unsigned)AUDIO_DUMP_BLOCK_SIZE,
                           (unsigned)got_total, (unsigned)need_total,
                           (unsigned)(i * 2 + b), (unsigned)(AUDIO_DUMP_MAX * 2));
                __audio_dump_sd_free_all();
                tal_mutex_unlock(__s_dump_mutex);
                return OPRT_MALLOC_FAILED;
            }
        }
    }
    /* 各路 session 文件 open 一次，会话期间常开（落盘只 fwrite+fsync，finalize 时 close） */
    for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
        __s_sd_fp[i] = __audio_dump_sd_open(i);
        if (__s_sd_fp[i] == NULL) {
            __audio_dump_sd_free_all();
            tal_mutex_unlock(__s_dump_mutex);
            return OPRT_COM_ERROR;
        }
    }
    __s_sd_overrun  = 0;
    __s_sd_on       = TRUE;
    audio_dump_flag = TRUE;
    tal_mutex_unlock(__s_dump_mutex);
    TAL_PR_DEBUG("audio_dump sd: init ok (%d ch x 2 x %d bytes)", AUDIO_DUMP_MAX, AUDIO_DUMP_BLOCK_SIZE);
    return OPRT_OK;
}

STATIC VOID __audio_dump_sd_deinit(VOID)
{
    if (__s_dump_mutex == NULL) {
        return;
    }
    tal_mutex_lock(__s_dump_mutex);
    __s_sd_on       = FALSE;
    audio_dump_flag = FALSE;
    __audio_dump_sd_free_all();
    __s_sd_stamp[0] = '\0';
    tal_mutex_unlock(__s_dump_mutex);
    TAL_PR_DEBUG("audio_dump sd: deinit (overrun=%d)", __s_sd_overrun);
}

/* 把 data[0..len) append 写入某通道的常开 session 文件，写完 fsync（不 close）。
 * 句柄在会话期间常开；SDIO 持续无进展（卡复位致句柄失效）时 close+reopen 兜底一次。 */
STATIC VOID __audio_dump_sd_write_file(INT_T type, CONST uint8_t *data, uint32_t len)
{
    if (len == 0 || data == NULL) {
        return;
    }
    if (__s_sd_fp[type] == NULL) {
        __s_sd_fp[type] = __audio_dump_sd_open(type);   /* 句柄缺失（如曾 reopen 失败）：补开 */
        if (__s_sd_fp[type] == NULL) {
            return;
        }
    }

    /* SDIO 偶发短写/复位（见 sdio_host_driver 的 RESET SDCARD）：循环写剩余字节，
     * 只要还有进展就继续；单次零进展才计一次重试。注意 tkl_fwrite 可能返回 < 请求量
     *（部分写），下次从已写偏移继续，文件位置已随之前进。
     * 连续无进展超过上限：close+reopen 句柄（卡复位后旧句柄可能失效）再试一轮；
     * 仍不行则放弃本段（句柄置 NULL，下次落盘重开）。 */
    uint32_t off      = 0;
    INT_T    stall    = 0;
    BOOL_T   reopened = FALSE;
    while (off < len) {
        INT_T wr = tkl_fwrite((VOID *)(data + off), (INT_T)(len - off), __s_sd_fp[type]);
        if (wr > 0) {
            off += (uint32_t)wr;
            stall = 0;
            continue;
        }
        if (++stall <= AUDIO_DUMP_SD_WRITE_RETRY) {
            TAL_PR_WARN("audio_dump sd: short write ch%d %u/%u (ret=%d), retry %d/%d",
                        type, (unsigned)off, (unsigned)len, wr, stall, AUDIO_DUMP_SD_WRITE_RETRY);
            tal_system_sleep(AUDIO_DUMP_SD_WRITE_BACKOFF);
            continue;
        }
        if (!reopened) {
            TAL_PR_WARN("audio_dump sd: ch%d stalled %u/%u, reopen handle", type, (unsigned)off, (unsigned)len);
            tkl_fclose(__s_sd_fp[type]);
            __s_sd_fp[type] = __audio_dump_sd_open(type);
            if (__s_sd_fp[type] == NULL) {
                return;
            }
            reopened = TRUE;
            stall    = 0;
            continue;
        }
        TAL_PR_ERR("audio_dump sd: write ch%d stalled %u/%u, give up segment", type, (unsigned)off, (unsigned)len);
        tkl_fclose(__s_sd_fp[type]);
        __s_sd_fp[type] = NULL;       /* 下次落盘 __audio_dump_sd_write_file 会重开 */
        return;
    }
    tkl_fsync(tkl_fileno(__s_sd_fp[type]));
}

/* 纯落盘：读已冻结的非活跃块 [0, flushlen)，不持锁——音频线程在另一块写，区间不重叠。
 * 不清状态；状态清理由各调用点按自己的持锁情况负责。 */
STATIC VOID __audio_dump_sd_flush_block(INT_T type, uint8_t idx)
{
    __audio_dump_sd_write_file(type, __s_sd[type].buf[idx], __s_sd[type].flushlen[idx]);
}

/* WORKQ_SYSTEM 回调：不持锁进入，落盘后自行加锁清状态。arg = (type<<1)|idx。 */
STATIC VOID __audio_dump_sd_flush_cb(VOID *arg)
{
    uintptr_t v = (uintptr_t)arg;
    INT_T   type = (INT_T)(v >> 1);
    uint8_t idx  = (uint8_t)(v & 1u);
    if (type < 0 || type >= AUDIO_DUMP_MAX) {
        return;
    }
    __audio_dump_sd_flush_block(type, idx);
    if (__s_dump_mutex != NULL) {
        tal_mutex_lock(__s_dump_mutex);
        __s_sd[type].wlen[idx]     = 0;
        __s_sd[type].flushlen[idx] = 0;
        __s_sd[type].busy[idx]     = 0;
        tal_mutex_unlock(__s_dump_mutex);
    }
}

/* 由音频线程在持锁状态调用。调度失败时就地落盘（调用方已持锁，故不再加锁）。 */
STATIC VOID __audio_dump_sd_schedule_flush(INT_T type, uint8_t idx)
{
    uintptr_t v = (uintptr_t)(((unsigned)type << 1) | (idx & 1u));
    if (tal_workq_schedule(WORKQ_SYSTEM, __audio_dump_sd_flush_cb, (VOID *)v) != OPRT_OK) {
        TAL_PR_WARN("audio_dump sd: schedule failed, flush inline (ch %d)", type);
        __audio_dump_sd_flush_block(type, idx);   /* 调用方已持锁 */
        __s_sd[type].wlen[idx]     = 0;
        __s_sd[type].flushlen[idx] = 0;
        __s_sd[type].busy[idx]     = 0;
    }
}

/* 生成会话级文件名时间戳前缀；墙钟未同步时退化为自增序号。 */
STATIC VOID __audio_dump_sd_make_stamp(VOID)
{
    POSIX_TM_S tm;
    STATIC UINT32_T __s_sd_seq = 0;
    memset(&tm, 0, sizeof(tm));
    if (tal_time_get_local_time_custom(0, &tm) == OPRT_OK) {
        snprintf(__s_sd_stamp, sizeof(__s_sd_stamp), "%02d%02d_%02d%02d%02d",
                 tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    } else {
        snprintf(__s_sd_stamp, sizeof(__s_sd_stamp), "%04u", (unsigned)__s_sd_seq);
        __s_sd_seq = (__s_sd_seq + 1) % 10000;
    }
}

/* UI 关闭时收尾：停写 → 等在途后台落盘完成 → 把各路活跃块剩余数据落盘。 */
STATIC VOID __audio_dump_sd_finalize(VOID)
{
    if (__s_dump_mutex == NULL) {
        return;
    }
    /* 1) 停止音频线程继续写入本通路 */
    tal_mutex_lock(__s_dump_mutex);
    audio_dump_flag = FALSE;
    tal_mutex_unlock(__s_dump_mutex);

    /* 2) 等待在途后台落盘完成（10s 周期 vs <1s 落盘，通常立即就绪）。最多等 ~3s。 */
    for (INT_T waited = 0; waited < 3000; waited += 20) {
        BOOL_T any_busy = FALSE;
        tal_mutex_lock(__s_dump_mutex);
        for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
            if (__s_sd[i].busy[0] || __s_sd[i].busy[1]) { any_busy = TRUE; break; }
        }
        tal_mutex_unlock(__s_dump_mutex);
        if (!any_busy) break;
        tal_system_sleep(20);
    }

    /* 3) 把每路当前活跃块剩余数据 append 落盘（此时音频线程已停写，可安全读 active 块）。 */
    for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
        uint8_t a = __s_sd[i].active;
        if (__s_sd[i].buf[a] != NULL && __s_sd[i].wlen[a] > 0) {
            __s_sd[i].flushlen[a] = __s_sd[i].wlen[a];
            __audio_dump_sd_flush_block(i, a);   /* 纯落盘，不加锁 */
            __s_sd[i].wlen[a] = 0;
        }
    }
}
#endif

VOID audio_dump_disable(VOID)
{
    if (__s_dump_mutex == NULL) {
        return;
    }
    
    tal_mutex_lock(__s_dump_mutex);
    audio_dump_flag = FALSE;
    
#if ENABLE_EXT_RAM
    for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
        if (audio_dump[i].data != NULL) {
            tal_psram_free(audio_dump[i].data);
            audio_dump[i].data = NULL;
        }
        audio_dump[i].datalen = 0;
    }
#endif
    
    tal_mutex_unlock(__s_dump_mutex);
    TAL_PR_DEBUG("audio_dump disabled");
}

uint32_t audio_dump_channel_caps(void)
{
    uint32_t caps = (1u << AUDIO_DUMP_CH_OFF);   /* OFF 恒可用 */
#if ENABLE_AUDIO_ANALYSIS
#if ENABLE_EXT_RAM
    caps |= (1u << AUDIO_DUMP_CH_UART);
    caps |= (1u << AUDIO_DUMP_CH_SDCARD);
#endif
#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
    caps |= (1u << AUDIO_DUMP_CH_LAN);
#endif
#endif
    return caps;
}

AUDIO_DUMP_CHANNEL_E audio_dump_get_channel(void)
{
    return __s_channel;
}

OPERATE_RET audio_dump_set_channel(AUDIO_DUMP_CHANNEL_E ch, const char *sd_dir)
{
    if ((audio_dump_channel_caps() & (1u << ch)) == 0) {
        return OPRT_NOT_SUPPORTED;
    }
    if (ch == AUDIO_DUMP_CH_SDCARD && (sd_dir == NULL || sd_dir[0] == '\0')) {
        return OPRT_INVALID_PARM;
    }
    if (__s_dump_mutex == NULL) {
        audio_dump_init();   /* 懒初始化：保证 mutex/缓存就绪 */
    }
    if (__s_dump_mutex == NULL) {
        return OPRT_NOT_FOUND;
    }

    /* --- 旧通道收尾 --- */
    if (__s_channel == AUDIO_DUMP_CH_SDCARD) {
#if ENABLE_EXT_RAM
        __audio_dump_sd_finalize();   /* 停写 + 等在途 + 终落盘（内部自管锁） */
        __audio_dump_sd_deinit();     /* 释放双缓冲、清状态 */
#endif
    }
#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
    else if (__s_channel == AUDIO_DUMP_CH_LAN) {
        __audio_dump_set_mode(AUDIO_DUMP_MODE_BUFFER);
        __audio_dump_realtime_deinit();
    }
#endif
    audio_dump_disable();            /* 统一释放缓存、清 flag */
    __s_channel = AUDIO_DUMP_CH_OFF;
    __s_sd_dir[0] = '\0';

    /* --- 新通道启动 --- */
    switch (ch) {
    case AUDIO_DUMP_CH_OFF:
        break;
    case AUDIO_DUMP_CH_UART:
        audio_dump_enable();
        break;
#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
    case AUDIO_DUMP_CH_LAN: {
        AUDIO_DUMP_REALTIME_CFG_T cfg = {0};
        cfg.mode = AUDIO_DUMP_MODE_REALTIME;
        cfg.batch_count = 50;
        cfg.frame_size = 640;
        audio_dump_enable();
        if (OPRT_OK != __audio_dump_realtime_init(&cfg)) {
            audio_dump_disable();
            return OPRT_COM_ERROR;
        }
        break;
    }
#endif
    case AUDIO_DUMP_CH_SDCARD:
        strncpy(__s_sd_dir, sd_dir, sizeof(__s_sd_dir) - 1);
        __s_sd_dir[sizeof(__s_sd_dir) - 1] = '\0';
        __audio_dump_sd_make_stamp();          /* 会话级时间戳，整段录音共用 */
        if (__audio_dump_sd_init() != OPRT_OK) {   /* 分配双缓冲，开启旋转通路 */
            __s_sd_dir[0] = '\0';
            return OPRT_MALLOC_FAILED;
        }
        break;
    default:
        return OPRT_NOT_SUPPORTED;
    }
    __s_channel = ch;
    TAL_PR_DEBUG("audio_dump channel -> %d", ch);
    return OPRT_OK;
}

VOID audio_dump_reset(VOID)
{
    audio_dump[0].datalen = 0;
    audio_dump[1].datalen = 0;
    audio_dump[2].datalen = 0;

#ifndef ENABLE_EXT_RAM
    // __s_dump_type = AUDIO_DUMP_MAX;
#endif    
}

VOID audio_dump_with_uart(INT_T type)
{
    TAL_PR_DEBUG("audio_dump type[%d] len %d\r\n", type, audio_dump[type].datalen);
#if ENABLE_EXT_RAM
    tal_uart_write(TUYA_UART_NUM_0, audio_dump[type].data , audio_dump[type].datalen);
    audio_dump[type].datalen = 0;
#else
    // __s_dump_type = type;
#endif
}

VOID audio_dump_with_net(INT_T type)
{
    TAL_PR_DEBUG("audio_dump type[%d] len %d\r\n", type, audio_dump[type].datalen);

#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
    audio_dump_t *dump = &audio_dump[type];
    if (audio_dump && dump->data && dump->datalen > 0) {
        TAL_PR_DEBUG("net dump audio type %d, len %d", type, dump->datalen);
        // dump pcm data interval is 100ms
        for (int i = 0; i < dump->datalen; i += 3200) {
            TAL_PR_DEBUG("net dump audio data offset %d, len %d", i, MIN(3200, dump->datalen - i));
            AI_STREAM_TYPE stype = AI_STREAM_ONE;
            if (i == 0 && i + 3200 >= dump->datalen) {
                stype = AI_STREAM_ONE;
            } else if (i == 0) {
                stype = AI_STREAM_START;
            } else if (i + 3200 >= dump->datalen) {
                stype = AI_STREAM_END;
            } else {
                stype = AI_STREAM_ING;
            }

            if (type == 0) {
                tuya_ai_monitor_broadcast_audio_mic(stype, dump->data + i, MIN(3200, dump->datalen - i));
            } else if (type == 1) {
                tuya_ai_monitor_broadcast_audio_ref(stype, dump->data + i, MIN(3200, dump->datalen - i));
            } else if (type == 2) {
                tuya_ai_monitor_broadcast_audio_aec(stype, dump->data + i, MIN(3200, dump->datalen - i));
            } else if (type == 3) {
                tuya_ai_monitor_broadcast_audio_kws(stype, dump->data + i, MIN(3200, dump->datalen - i));
            } else if (type == 4) {
                tuya_ai_monitor_broadcast_audio_vad(stype, dump->data + i, MIN(3200, dump->datalen - i));
            }
        }
        dump->datalen = 0; // reset dump data length
    }
#else
    TAL_PR_ERR("ENABLE_APP_AI_MONITOR is not enabled, can not net dump audio");
#endif
}

VOID audio_play_bgm(INT_T type, INT_T freq)
{
    if (0 == type) {
        AUDIO_ANALYSIS_PARAMS_T params = {0};
        AUDIO_ANALYSIS_DEFAULT_PARAMS_GET_RANG(&params);
        audio_analysis_play(AUDIO_ANALYSIS_TYPE_RANG, &params);
    } else if (1 == type) { /*单频*/
        AUDIO_ANALYSIS_PARAMS_T params = {0};
        AUDIO_ANALYSIS_DEFAULT_PARAMS_GET_SINGLE(&params);
        if (freq > 0) {
            params.freq = freq;
        }                
        audio_analysis_play(AUDIO_ANALYSIS_TYPE_SINGLE, &params);
    } else if (2 == type) { /*白噪声*/
        AUDIO_ANALYSIS_PARAMS_T params = {0};
        AUDIO_ANALYSIS_DEFAULT_PARAMS_GET_SWEEP(&params);
        audio_analysis_play(AUDIO_ANALYSIS_TYPE_SWEEP, &params);
    } else if (3 == type) { /*调频*/                
        AUDIO_ANALYSIS_PARAMS_T params = {0};
        INT_T amp = freq;
        if (amp < 0) {
            params.amp = amp;
        }
        AUDIO_ANALYSIS_DEFAULT_PARAMS_GET_SWEEPSPECIAL(&params);
        audio_analysis_play(AUDIO_ANALYSIS_TYPE_SWEEPSPECIAL, &params);
    } else if (4 == type) { /*最小信号*/
        AUDIO_ANALYSIS_PARAMS_T params = {0};
        AUDIO_ANALYSIS_DEFAULT_PARAMS_GET_MINSIN(&params);
        if (freq > 0) {
            params.freq = freq;
        }                
        audio_analysis_play(AUDIO_ANALYSIS_TYPE_MINSIN, &params);
    } else {
        TAL_PR_ERR("unknown audio test data %d", type);
    }
}

VOID audio_set_volume(INT_T volume)
{
    if (volume < 0) {
        volume = 0;
    } else if (volume > 100) {
        volume = 100;
    }

    __s_current_volume = volume;
    wukong_audio_player_set_vol((UINT8_T)volume);
}

VOID audio_set_micgain(INT_T micgain)
{
    if (micgain < 0) {
        micgain = 0;
    } else if (micgain > 100) {
        micgain = 100;
    }

    __s_current_micgain = micgain;
    wukong_audio_input_set_vol((UINT8_T)micgain);
}

// VOID audio_ctrl_alg(INT_T argc, CHAR_T *argv[])
// {
//     if (0 == strcmp(argv[2], "set")) {
//         // ! ao alg set <para> <value>
//         if (argc != 5) {
//             TAL_PR_DEBUG("audio alg set cmd error\r\n");
//             return;
//         }
//         INT_T i;
//         for (i = 0; i < sizeof(audio_alg_para_map) / sizeof(aa_alg_para_map_t); i++) {
//             if (0 == strcmp(argv[3], audio_alg_para_map[i].name)) {
//                 break;
//             }
//         }
//         if (i >= sizeof(audio_alg_para_map) / sizeof(aa_alg_para_map_t)) {
//             TAL_PR_DEBUG("audio alg set para %s not found\r\n", argv[3]);
//             return;
//         }
//         uint32_t type = audio_alg_para_map[i].para;
//         uint32_t value = atoi(argv[4]);
//         _audio_test_event(AUDIO_TEST_EVENT_SET_ALG_PARA, type, value);
//     } else if (0 == strcmp(argv[2], "get")) {
//         // ! ao alg get <para>
//         if (argc != 4 && argc != 5) {
//             return;
//         }
//         INT_T i;
//         for (i = 0; i < sizeof(audio_alg_para_map) / sizeof(aa_alg_para_map_t); i++) {
//             if (0 == strcmp(argv[3], audio_alg_para_map[i].name)) {
//                 break;
//             }
//         }
//         if (i >= sizeof(audio_alg_para_map) / sizeof(aa_alg_para_map_t)) {
//             TAL_PR_DEBUG("audio alg get para %s not found\r\n", argv[3]);
//             return;
//         }
//         uint32_t type = audio_alg_para_map[i].para;
//         uint32_t value = 0;
//         if (argc == 5) {
//             value = atoi(argv[4]);
//         }
//         _audio_test_event(AUDIO_TEST_EVENT_GET_ALG_PARA, type, value);
//     } else if (0 == strcmp(argv[2], "dump")) {
//         // ! ao alg dump
//         _audio_test_event(AUDIO_TEST_EVENT_DUMP_ALG_PARA, 0, 0);
//     } else {
//         TAL_PR_DEBUG("audio alg cmd error\r\n");
//     }
// }

//！ ao start
//！ ao stop
//！ ao reset
//！ ao dump 0
//！ ao dump 1
//！ ao dump 2
//！ ao netdump 0
//！ ao netdump 1
//！ ao netdump 2
//！ ao bg 0
//！ ao bg 1 (ao bg 1 1000)
//！ ao bg 2
//！ ao volume 50
// ! ao micgain 70(default)
// ! ao alg set <para> [<para2>] <value>
// ! ao alg get <para> [<para2>]
// ! ao alg dump
// ! ao echo <info>
// ! ao realtime start [batch] [framesize]
// ! ao realtime stop
// ! ao filedump start <dir>
// ! ao filedump stop
VOID audio_dump_exec(INT_T argc, CHAR_T *argv[])
{
    if (0 == strcmp(argv[1], "start")) {
        audio_dump_enable();
        TAL_PR_DEBUG("audio_dump start\r\n");
    } else if (0 == strcmp(argv[1], "stop")) {
        audio_dump_stop();
        TAL_PR_DEBUG("audio_dump stop\r\n");
    } else if (0 == strcmp(argv[1], "dump")) {
        audio_dump_with_uart(atoi(argv[2]));
        TAL_PR_DEBUG("audio_dump, %d\r\n", atoi(argv[2]));
    } else if (0 == strcmp(argv[1], "netdump")) {
        audio_dump_with_net(atoi(argv[2]));
        TAL_PR_DEBUG("audio_dump, %d\r\n", atoi(argv[2]));
    } else if (0 == strcmp(argv[1], "reset")) {
        audio_dump_reset();
        TAL_PR_DEBUG("audio_dump reset\r\n");
    } else if (0 == strcmp(argv[1], "bg")) {
        INT_T freq = 0;
        if (argc > 3) {
            freq = atoi(argv[3]);
        }
        audio_play_bgm(atoi(argv[2]), freq);
        TAL_PR_DEBUG("audio_dump play bgm %d\r\n", atoi(argv[2]));
    } else if (0 == strcmp(argv[1], "volume")) {
        audio_set_volume(atoi(argv[2]));
        TAL_PR_DEBUG("audio_dump set volume %d\r\n", atoi(argv[2]));
    } else if (0 == strcmp(argv[1], "micgain")) {
        audio_set_micgain(atoi(argv[2]));
        TAL_PR_DEBUG("audio_dump set micgain %d\r\n", atoi(argv[2]));
    } else if (0 == strcmp(argv[1], "alg")) {
        // audio_ctrl_alg(argc, argv);
    } else if (0 == strcmp(argv[1], "echo")) {
        TAL_PR_DEBUG("echo %s", argv[2]);
#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
    } else if (0 == strcmp(argv[1], "realtime")) {
        if (argc < 3) {
            TAL_PR_DEBUG("usage: ao realtime start|stop [batch] [framesize]\r\n");
            return;
        }
        if (0 == strcmp(argv[2], "start")) {
            AUDIO_DUMP_REALTIME_CFG_T cfg = {0};
            cfg.mode = AUDIO_DUMP_MODE_REALTIME;
            cfg.batch_count = (argc > 3) ? atoi(argv[3]) : 50;
            cfg.frame_size = (argc > 4) ? atoi(argv[4]) : 640;
            audio_dump_enable();
            if (OPRT_OK == __audio_dump_realtime_init(&cfg)) {
                TAL_PR_DEBUG("audio realtime start: batch=%d, frame=%d\r\n", cfg.batch_count, cfg.frame_size);
            } else {
                TAL_PR_ERR("audio realtime init failed\r\n");
            }
        } else if (0 == strcmp(argv[2], "stop")) {
            __audio_dump_set_mode(AUDIO_DUMP_MODE_BUFFER);
            __audio_dump_realtime_deinit();
            TAL_PR_DEBUG("audio realtime stop\r\n");
        } else {
            TAL_PR_DEBUG("usage: ao realtime start|stop\r\n");
        }
#endif
    } else if (0 == strcmp(argv[1], "filedump")) {
        OPERATE_RET rt;

        if (argc < 3) {
            TAL_PR_DEBUG("usage: ao filedump start <dir> | stop\r\n");
            return;
        }

        if (0 == strcmp(argv[2], "start")) {
            if (argc < 4) {
                TAL_PR_DEBUG("usage: ao filedump start <dir>\r\n");
                return;
            }
            rt = audio_dump_set_channel(AUDIO_DUMP_CH_SDCARD, argv[3]);
            TAL_PR_DEBUG("filedump start: %d\r\n", rt);
        } else if (0 == strcmp(argv[2], "stop")) {
            rt = audio_dump_set_channel(AUDIO_DUMP_CH_OFF, NULL);
            TAL_PR_DEBUG("filedump stop: %d\r\n", rt);
        } else {
            TAL_PR_DEBUG("usage: ao filedump start <dir> | stop\r\n");
        }
    } else {
        TAL_PR_DEBUG("audio_dump cmd error\r\n");
    }
}


INT_T strsplit(CHAR_T* input, INT_T *argc, CHAR_T *argv[])
{
    CONST CHAR_T delimiter[] = " ";
    CHAR_T *token = NULL;

    *argc = 0;
    // Get the first token
    token = strtok(input, delimiter);
    argv[(*argc)++] = token;
    TAL_PR_DEBUG("token %s\r\n", token);
    // Iterate over tokens
    while (token != NULL) {
        // Get the next token
        token = strtok(NULL, delimiter);
        if (token) {
            argv[(*argc)++] = token;
        }
        if (*argc >= 10) {
            return OPRT_INDEX_OUT_OF_BOUND;
        }
    }

    return OPRT_OK;
}

VOID __audio_dump_task(VOID *params)
{
    INT_T     rt;

    TAL_UART_CFG_T cfg = {0};
    cfg.base_cfg.baudrate = 460800;
    cfg.base_cfg.databits = TUYA_UART_DATA_LEN_8BIT;
    cfg.base_cfg.stopbits = TUYA_UART_STOP_LEN_1BIT;
    cfg.base_cfg.parity = TUYA_UART_PARITY_TYPE_NONE;
    cfg.rx_buffer_size = 256;
    cfg.open_mode = O_BLOCK;
    rt = tal_uart_init(TUYA_UART_NUM_0, &cfg);

    uint8_t ch;
    uint8_t buffer[255];
    uint8_t index = 0;
    INT_T     argc;
    CHAR_T   *argv[10];

    for (;;) {
        tal_uart_read(TUYA_UART_NUM_0, (UINT8_T*)&ch, 1);
        if (ch != '\r' && ch != '\n') {
            buffer[index++] = ch;
            continue;
        }
        buffer[index] = '\0';
        //! if '\r\n' is end of CHAR_T, '\n' is need discard， so check index
        if (index && OPRT_OK == strsplit(buffer, &argc, argv)) {
            TAL_PR_DEBUG("dump command:%s %s", argv[0], argv[1]);
            //! parse cmd
            if (0 == strcmp(argv[0], "ao")) {
                audio_dump_exec(argc, argv);
            }
        }
        index = 0;
    }
}

#include "tkl_thread.h"

STATIC TKL_THREAD_HANDLE audio_dump_handle = NULL;

#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
STATIC OPERATE_RET __audio_dump_alg_ctrl_cb(UINT8_T op, UINT8_T cmd,
                                             UINT16_T param1, UINT16_T param2,
                                             UINT16_T *out_val1, UINT16_T *out_val2,
                                             UINT16_T *out_val3, UINT16_T *out_val4);
#endif

VOID audio_dump_init(VOID)
{
    INT_T i = 0;
    for (i = 0; i < AUDIO_DUMP_MAX; i++) {
        audio_dump[i].datalen = 0;
        audio_dump[i].data = NULL;
    }
    
    if (__s_dump_mutex == NULL) {
        tal_mutex_create_init(&__s_dump_mutex);
    }

#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
    tuya_ai_monitor_register_alg_ctrl_cb(AI_ALG_CTRL_MODULE_AUDIO_DUMP, __audio_dump_alg_ctrl_cb);
#endif

    THREAD_CFG_T thread_param = {0};
    thread_param.stackDepth = 1024*4;
    thread_param.priority = THREAD_PRIO_1;
    thread_param.thrdname = "audio_dump";
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    thread_param.psram_mode = 1;
#endif
    tal_thread_create_and_start(&audio_dump_handle, NULL, NULL, __audio_dump_task, NULL, &thread_param);
}

#if defined(ENABLE_APP_AI_MONITOR) && (ENABLE_APP_AI_MONITOR == 1)
STATIC OPERATE_RET __audio_dump_realtime_init(CONST AUDIO_DUMP_REALTIME_CFG_T *cfg)
{
    if (cfg == NULL) {
        return OPRT_INVALID_PARM;
    }
    
    if (__s_dump_mutex == NULL) {
        return OPRT_NOT_FOUND;
    }
    
    tal_mutex_lock(__s_dump_mutex);
    
    __s_realtime_batch_count = cfg->batch_count;
    __s_realtime_frame_size = cfg->frame_size;
    __s_realtime_idx = 0;
    
    UINT32_T buf_size = cfg->batch_count * cfg->frame_size;
    for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
        __s_realtime_written[i] = 0;
        __s_realtime_first_send_flags[i] = TRUE;
        if (__s_realtime_buf[i] == NULL) {
            __s_realtime_buf[i] = (CHAR_T *)tal_malloc(buf_size);
            if (__s_realtime_buf[i] == NULL) {
                tal_mutex_unlock(__s_dump_mutex);
                __audio_dump_realtime_deinit();
                return OPRT_MALLOC_FAILED;
            }
        }
    }
    
    __s_dump_mode = cfg->mode;
    tal_mutex_unlock(__s_dump_mutex);
    
    TAL_PR_DEBUG("audio_dump realtime init: mode=%d, batch=%d, frame=%d", 
                 cfg->mode, cfg->batch_count, cfg->frame_size);
    return OPRT_OK;
}

STATIC VOID __audio_dump_realtime_deinit(VOID)
{
    if (__s_dump_mutex == NULL) {
        return;
    }
    
    tal_mutex_lock(__s_dump_mutex);
    
    if (__s_dump_mode == AUDIO_DUMP_MODE_REALTIME) {
        for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
            if (__s_realtime_buf[i] != NULL && __s_realtime_written[i] > 0) {
                AI_STREAM_TYPE stype = __s_realtime_first_send_flags[i] ? AI_STREAM_ONE : AI_STREAM_END;
                __audio_realtime_send(i, __s_realtime_written[i], stype);
            }
        }
    }
    
    __s_dump_mode = AUDIO_DUMP_MODE_BUFFER;
    __s_realtime_idx = 0;
    
    for (INT_T i = 0; i < AUDIO_DUMP_MAX; i++) {
        __s_realtime_written[i] = 0;
        __s_realtime_first_send_flags[i] = TRUE;
        if (__s_realtime_buf[i] != NULL) {
            tal_free(__s_realtime_buf[i]);
            __s_realtime_buf[i] = NULL;
        }
    }
    
    tal_mutex_unlock(__s_dump_mutex);
}

STATIC VOID __audio_dump_set_mode(AUDIO_DUMP_MODE_E mode)
{
    if (__s_dump_mutex == NULL) {
        return;
    }
    tal_mutex_lock(__s_dump_mutex);
    __s_dump_mode = mode;
    __s_realtime_idx = 0;
    tal_mutex_unlock(__s_dump_mutex);
    TAL_PR_DEBUG("audio_dump set mode: %d", mode);
}

STATIC OPERATE_RET __audio_dump_alg_ctrl_cb(UINT8_T op, UINT8_T cmd,
                                             UINT16_T param1, UINT16_T param2,
                                             UINT16_T *out_val1, UINT16_T *out_val2,
                                             UINT16_T *out_val3, UINT16_T *out_val4)
{
    OPERATE_RET rt = OPRT_OK;

    TAL_PR_DEBUG("audio_dump alg_ctrl: op=%d, cmd=0x%02X, param1=%d, param2=%d",
                 op, cmd, param1, param2);

    if (op == AI_ALG_CTRL_OP_SET) {
        switch (cmd) {
        case AUDIO_DUMP_CMD_ENABLE:
            if (param1) {
                audio_dump_enable();
            } else {
                audio_dump_stop();
            }
            break;

        case AUDIO_DUMP_CMD_RESET:
            audio_dump_reset();
            break;

        case AUDIO_DUMP_CMD_DUMP_NET:
            if (param1 < AUDIO_DUMP_MAX) {
                audio_dump_with_net((INT_T)param1);
            } else {
                rt = OPRT_INVALID_PARM;
            }
            break;

        case AUDIO_DUMP_CMD_REALTIME:
            if (param1 == 0 && param2 == 0) {
                __audio_dump_set_mode(AUDIO_DUMP_MODE_BUFFER);
                __audio_dump_realtime_deinit();
            } else {
                AUDIO_DUMP_REALTIME_CFG_T cfg = {0};
                cfg.mode = AUDIO_DUMP_MODE_REALTIME;
                cfg.batch_count = (param1 > 0) ? param1 : 50;
                cfg.frame_size = (param2 > 0) ? param2 : 640;
                audio_dump_enable();
                rt = __audio_dump_realtime_init(&cfg);
            }
            break;

        case AUDIO_DUMP_CMD_PLAY_BGM:
            audio_play_bgm((INT_T)param1, (INT_T)param2);
            break;

        case AUDIO_DUMP_CMD_VOLUME:
            audio_set_volume((INT_T)param1);
            break;

        case AUDIO_DUMP_CMD_MICGAIN:
            audio_set_micgain((INT_T)param1);
            break;

        default:
            rt = OPRT_NOT_SUPPORTED;
            break;
        }
    } else if (op == AI_ALG_CTRL_OP_GET) {
        switch (cmd) {
        case AUDIO_DUMP_CMD_ENABLE:
            if (out_val1) *out_val1 = audio_dump_flag ? 1 : 0;
            break;

        case AUDIO_DUMP_CMD_REALTIME:
            if (out_val1) *out_val1 = (UINT16_T)__s_dump_mode;
            break;

        case AUDIO_DUMP_CMD_VOLUME:
            if (out_val1) *out_val1 = (UINT16_T)__s_current_volume;
            break;

        case AUDIO_DUMP_CMD_MICGAIN:
            if (out_val1) *out_val1 = (UINT16_T)__s_current_micgain;
            break;

        case AUDIO_DUMP_CMD_GET_STATUS:
            if (out_val1) *out_val1 = audio_dump_flag ? 1 : 0;
            if (out_val2) *out_val2 = (UINT16_T)__s_dump_mode;
            break;

        case AUDIO_DUMP_CMD_GET_ALL:
            if (out_val1) *out_val1 = audio_dump_flag ? 1 : 0;
            if (out_val2) *out_val2 = (UINT16_T)__s_dump_mode;
            if (out_val3) *out_val3 = (UINT16_T)__s_current_volume;
            if (out_val4) *out_val4 = (UINT16_T)__s_current_micgain;
            break;

        default:
            rt = OPRT_NOT_SUPPORTED;
            break;
        }
    } else {
        rt = OPRT_NOT_SUPPORTED;
    }

    return rt;
}
#endif
