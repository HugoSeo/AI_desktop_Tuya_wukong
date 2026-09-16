/**
 * @file svc_ai_player.c
 * @brief 
 * @version 0.1
 * @date 2025-09-19
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
#include "base_event.h"
#include "mix_method.h"
#include "tal_queue.h"
#include "tal_system.h"
#include "tal_memory.h"
#include "tal_thread.h"
#include "svc_ai_player.h"
#include "ai_player.h"
#include "./mixer/mixer_fixed.h"
#include "svc_ai_player_ctx.h"
#if defined(AI_PLAYER_LITE) && (AI_PLAYER_LITE == 1)
#include "svc_ai_player_lite.h"
#endif

STATIC CHAR_T *s_player_cmd_str[PLAYER_CMD_MAX] = {
    "START",
    "STOP",
    "PAUSE",
    "RESUME",
    "EXIT",
    "DEINIT"
};

STATIC CHAR_T *s_player_mode_str[AI_PLAYER_MODE_MAX] = {
    "FG",
    "BG"
};

AI_PLAYER_CTX_T s_ai_player_ctx = {0};

#if defined(AI_PLAYER_DEBUG_STATS) && (AI_PLAYER_DEBUG_STATS == 1)
/* 卡顿诊断统计: 内存累加, 每 AP_STAT_WINDOW_MS 聚合输出一行, 避免逐帧打日志本身造成卡顿.
 * 判读: audio < win 且 sink zero/max 大 → 网络饿; dec/rs max 大且 wr blkmax≈0 → CPU 饿;
 *       wr blkmax 大 → 下游 ring 满, 供给充足(健康). */
#define AP_STAT_WINDOW_MS (2000)

typedef struct {
    UINT_T sink_calls;   // datasink 读次数
    UINT_T sink_bytes;   // datasink 读到的字节
    UINT_T sink_zero;    // 读到 0 字节的次数(供给饥饿)
    UINT_T sink_max_ms;  // 单次读最大耗时
    UINT_T dec_calls;    // 解码次数
    UINT_T dec_bytes;    // 解码产出 PCM 字节
    UINT_T dec_max_ms;   // 单次解码最大耗时
    UINT_T rs_max_ms;    // 单次重采样最大耗时
    UINT_T wr_bytes;     // 写给 consumer 的 PCM 字节
    UINT_T wr_max_ms;    // 单次写最大阻塞时长(下游背压)
} AP_STAT_T;

STATIC AP_STAT_T s_ap_stat[AI_PLAYER_MODE_MAX] = {0};
STATIC UINT_T s_ap_stat_loops = 0;
STATIC SYS_TIME_T s_ap_stat_win_start = 0;
STATIC BOOL_T s_ap_stat_en = FALSE;   /* 运行时门: 默认关(内存态), UI 诊断页动态开启 */

#define AP_STAT_TS(var)  SYS_TIME_T var = s_ap_stat_en ? tal_system_get_millisecond() : 0
#define AP_STAT_ADD(mode, field, v) do { \
    if (s_ap_stat_en) { \
        s_ap_stat[mode].field += (v); \
    } \
} while(0)
#define AP_STAT_DUR_MAX(mode, field, ts) do { \
    if (!s_ap_stat_en || (ts) == 0) { \
        /* ts==0: 开关关闭期间取的起点(在途样本), 开启瞬间结算会得到
         * now-0 的假峰值, 跳过 */ \
        break; \
    } \
    UINT_T __dt = (UINT_T)(tal_system_get_millisecond() - (ts)); \
    if (__dt > s_ap_stat[mode].field) { \
        s_ap_stat[mode].field = __dt; \
    } \
} while(0)
#define AP_STAT_WR_BLOCK(handle, ts) do { \
    if (handle) { \
        AP_STAT_DUR_MAX(((AI_PLAYER_T *)(handle))->mode, wr_max_ms, ts); \
    } \
} while(0)
#define AP_STAT_WR_BYTES(handle, len) do { \
    if (handle) { \
        AP_STAT_ADD(((AI_PLAYER_T *)(handle))->mode, wr_bytes, len); \
    } \
} while(0)

/* start 单路时只清该路的累计; 仅当无其他在播路时才重开全局窗口,
 * 避免 mix 模式下一路 start 截断另一路正在累计的窗口 */
STATIC VOID __ap_stat_reset_mode(UINT_T mode)
{
    if (mode < AI_PLAYER_MODE_MAX) {
        memset(&s_ap_stat[mode], 0, SIZEOF(AP_STAT_T));
    }
    if (!s_ai_player_ctx.is_playing) {
        s_ap_stat_loops = 0;
        s_ap_stat_win_start = 0;
    }
}

STATIC VOID __ap_stat_print(UINT_T mode, UINT_T span)
{
    AP_STAT_T *st = &s_ap_stat[mode];
    if ((st->sink_calls == 0) && (st->wr_bytes == 0)) {
        return;
    }

    UINT_T bytes_per_ms = (s_ai_player_ctx.cfg.sample_rate / 1000) *
                          (s_ai_player_ctx.cfg.sample_bits / 8) * s_ai_player_ctx.cfg.channel;
    if (bytes_per_ms == 0) {
        bytes_per_ms = 32; // 16k/16bit/mono
    }

    PR_NOTICE("[AP-STAT %s] loop=%u sink(r=%u B=%u zero=%u max=%ums) dec(n=%u B=%u max=%ums) rs_max=%ums wr(B=%u audio=%ums blkmax=%ums) win=%ums",
        s_player_mode_str[mode], s_ap_stat_loops,
        st->sink_calls, st->sink_bytes, st->sink_zero, st->sink_max_ms,
        st->dec_calls, st->dec_bytes, st->dec_max_ms,
        st->rs_max_ms,
        st->wr_bytes, st->wr_bytes / bytes_per_ms, st->wr_max_ms,
        span);
}

STATIC VOID __ap_stat_flush(BOOL_T force)
{
    SYS_TIME_T now = tal_system_get_millisecond();
    UINT_T i = 0;

    if (!s_ap_stat_en) {
        return;
    }

    if (s_ap_stat_win_start == 0) {
        s_ap_stat_win_start = now;
        return;
    }

    UINT_T span = (UINT_T)(now - s_ap_stat_win_start);
    if (!force && (span < AP_STAT_WINDOW_MS)) {
        return;
    }

    for (i = 0; i < AI_PLAYER_MODE_MAX; i++) {
        __ap_stat_print(i, span);
    }

    memset(s_ap_stat, 0, SIZEOF(s_ap_stat));
    s_ap_stat_loops = 0;
    s_ap_stat_win_start = now;
}

/* mix 模式下 stop 单路时只结算该路的尾部统计:
 * 不动 win_start/loops, 另一路正在累计的窗口不被截断 */
STATIC VOID __ap_stat_flush_mode(UINT_T mode)
{
    if (s_ap_stat_win_start == 0 || mode >= AI_PLAYER_MODE_MAX) {
        return;
    }
    __ap_stat_print(mode, (UINT_T)(tal_system_get_millisecond() - s_ap_stat_win_start));
    memset(&s_ap_stat[mode], 0, SIZEOF(AP_STAT_T));
}

#define AP_STAT_LOOP() do { \
    if (s_ap_stat_en) { \
        s_ap_stat_loops++; \
        __ap_stat_flush(FALSE); \
    } \
} while(0)

/* 运行时开关(内存态, 不持久化): 开启时重开统计窗口, 避免打印关闭期间的陈旧累计 */
VOID tuya_ai_player_debug_stats_set(BOOL_T on)
{
    if (on && !s_ap_stat_en) {
        memset(s_ap_stat, 0, SIZEOF(s_ap_stat));
        s_ap_stat_loops = 0;
        s_ap_stat_win_start = 0;
    }
    s_ap_stat_en = on;
    PR_NOTICE("[AP-STAT] runtime switch: %s", on ? "on" : "off");
}

BOOL_T tuya_ai_player_debug_stats_get(VOID)
{
    return s_ap_stat_en;
}
#else
#define AP_STAT_TS(var)
#define AP_STAT_ADD(mode, field, v)      do { } while(0)
#define AP_STAT_DUR_MAX(mode, field, ts) do { } while(0)
#define AP_STAT_WR_BLOCK(handle, ts)     do { } while(0)
#define AP_STAT_WR_BYTES(handle, len)    do { } while(0)
#define AP_STAT_LOOP()                   do { } while(0)
#define __ap_stat_reset_mode(mode)       do { } while(0)
#define __ap_stat_flush(force)           do { } while(0)
#define __ap_stat_flush_mode(mode)       do { } while(0)
#endif

STATIC VOID __switch_player_mode(VOID)
{
    AI_PLAYER_T *fg_player = s_ai_player_ctx.player[AI_PLAYER_MODE_FOREGROUND];
    AI_PLAYER_T *bg_player = s_ai_player_ctx.player[AI_PLAYER_MODE_BACKGROUND];
    AI_PLAYER_T *active_player = NULL;

    if(fg_player != NULL && fg_player->state == AI_PLAYER_PLAYING) {
        active_player = fg_player;
    } else if(bg_player != NULL && bg_player->state == AI_PLAYER_PLAYING) {
        active_player = bg_player;
    }

    if(active_player) {
        if(s_ai_player_ctx.active_player != active_player) {
            if(!s_ai_player_ctx.active_player && s_ai_player_ctx.decoder_mode && s_ai_player_ctx.consumer.start) {
                s_ai_player_ctx.consumer.start(NULL);
            }
            s_ai_player_ctx.active_player = active_player;
            s_ai_player_ctx.is_playing = TRUE;
        }
    } else {
        if(s_ai_player_ctx.active_player && s_ai_player_ctx.decoder_mode && s_ai_player_ctx.consumer.stop) {
            s_ai_player_ctx.consumer.stop(NULL);
        }
        s_ai_player_ctx.active_player = NULL;
        s_ai_player_ctx.is_playing = FALSE;
    }

    if(s_ai_player_ctx.is_playing) {
        s_ai_player_ctx.msg_timeout = PLAYER_BUSY_TIMEOUT_MS;
    } else {
        s_ai_player_ctx.msg_timeout = PLAYER_IDLE_TIMEOUT_MS;
    }
}

STATIC OPERATE_RET __cmd_player_start(AI_PLAYER_T *player, AI_PLAYER_MSG_T *msg)
{
    OPERATE_RET rt = OPRT_OK;

    PR_DEBUG("start player %s value=%s", s_player_mode_str[player->mode], msg->param.cmd_start.value ? msg->param.cmd_start.value : "null");

    TUYA_CALL_ERR_RETURN(ai_player_datasink_start(player->sink, msg->param.cmd_start.src, msg->param.cmd_start.value));
    TUYA_CALL_ERR_RETURN(ai_player_decoder_start(player->decoder, msg->param.cmd_start.codec));
    __ap_stat_reset_mode(player->mode);
    player->state = AI_PLAYER_PLAYING;
    player->offset = 0;
    player->has_pending_output = FALSE;
    if(msg->param.cmd_start.value) {
        Free(msg->param.cmd_start.value);
    }

    if(!s_ai_player_ctx.decoder_mode) {
        if(s_ai_player_ctx.consumer.start) {
            s_ai_player_ctx.consumer.start(player);
        }
    }

    AI_PLAYER_EVT_T evt = {
        .handle = player,
        .state = player->state
    };
    ty_publish_event(EVENT_AI_PLAYER_STATE, &evt);
    return OPRT_OK;
}

STATIC OPERATE_RET __cmd_player_stop(AI_PLAYER_T *player, AI_PLAYER_STOP_REASON_E reason)
{
    PR_DEBUG("stop player %s, reason %d", s_player_mode_str[player->mode], reason);

    __ap_stat_flush_mode(player->mode); // 只结算停止这一路的尾部统计, 不截断另一路的窗口
    ai_player_datasink_stop(player->sink);
    ai_player_decoder_stop(player->decoder);
    player->state = AI_PLAYER_STOPPED;
    player->offset = 0;
    player->has_pending_output = FALSE;
    if(player->playlist && player->playlist_cb) {
        player->playlist_cb(player->playlist, player->state);
    }

    if(!s_ai_player_ctx.decoder_mode) {
        if(s_ai_player_ctx.consumer.stop) {
            s_ai_player_ctx.consumer.stop(player);
        }
    }

    AI_PLAYER_EVT_T evt = {
        .handle = player,
        .state = player->state,
        .reason = reason
    };
    ty_publish_event(EVENT_AI_PLAYER_STATE, &evt);
    return OPRT_OK;
}

STATIC OPERATE_RET __cmd_player_pause(AI_PLAYER_T *player)
{
    player->state = AI_PLAYER_PAUSED;

    AI_PLAYER_EVT_T evt = {
        .handle = player,
        .state = player->state
    };
    ty_publish_event(EVENT_AI_PLAYER_STATE, &evt);
    return OPRT_OK;
}

STATIC OPERATE_RET __cmd_player_resume(AI_PLAYER_T *player)
{
    player->state = AI_PLAYER_PLAYING;

    AI_PLAYER_EVT_T evt = {
        .handle = player,
        .state = player->state
    };
    ty_publish_event(EVENT_AI_PLAYER_STATE, &evt);
    return OPRT_OK;
}

STATIC OPERATE_RET __cmd_player_exit(AI_PLAYER_T *player)
{
    ai_player_datasink_deinit(player->sink);
    ai_player_decoder_deinit(player->decoder);
    s_ai_player_ctx.player[player->mode] = NULL;

    Free(player->framebuf);
    Free(player->decode_buf);
    Free(player);
    return OPRT_OK;
}

STATIC OPERATE_RET __cmd_player_service_deinit(VOID)
{
    UINT_T i = 0;

    for(i = 0; i < AI_PLAYER_MODE_MAX; i++) {
        if(s_ai_player_ctx.player[i]) {
            __cmd_player_exit(s_ai_player_ctx.player[i]);
        }
    }

    tal_queue_free(s_ai_player_ctx.queue);
    ai_player_resample_deinit();

    if(s_ai_player_ctx.mixer_buf) {
        Free(s_ai_player_ctx.mixer_buf);
        s_ai_player_ctx.mixer_buf = NULL;
    }

    if(s_ai_player_ctx.consumer.close) {
        s_ai_player_ctx.consumer.close();
    }

    memset(&s_ai_player_ctx, 0, SIZEOF(AI_PLAYER_CTX_T));
    return OPRT_OK;
}

#if defined(AUDIO_SUBSYS_MULTI_BASE_SPEEXDSP) && (AUDIO_SUBSYS_MULTI_BASE_SPEEXDSP == 1)
#include "speexdsp/biquad.h"
// #include "speexdsp/drc_limiter.h"
static EQobj *eqhandle = NULL;
// static void *drchandle = NULL;
#endif

STATIC OPERATE_RET __player_consumer_write_wrap(AI_PLAYER_HANDLE handle, CONST VOID *buf, UINT_T len)
{
#if defined(AUDIO_SUBSYS_MULTI_BASE_SPEEXDSP) && (AUDIO_SUBSYS_MULTI_BASE_SPEEXDSP == 1)
    if(s_ai_player_ctx.decoder_mode) {
        if (eqhandle == NULL) {
            eqhandle = EQ_create();
        }
        if (eqhandle) {
            // SYS_TIME_T start = tal_system_get_millisecond();
            EQ_process(eqhandle, (short *)buf, len/2);
            // limiter_run(drchandle, (short *)buf, len/2);
            // PR_DEBUG("eq process spk data chunk size = %d\r\n", chunk_size);
            // SYS_TIME_T end = tal_system_get_millisecond();
            // SYS_TIME_T delta = end - start;
            // PR_DEBUG("eq process = : start: %llu, end: %llu, delta: %llu(ms), out_bytes=%d", start, end, delta, len);
        }
    }
#endif

    OPERATE_RET rt = OPRT_OK;
    AP_STAT_TS(wr_ts);
    rt = s_ai_player_ctx.consumer.write(handle, buf, len);
    AP_STAT_WR_BLOCK(handle, wr_ts); // 背压时长无条件记录
    if (OPRT_OK == rt) {
        AP_STAT_WR_BYTES(handle, len); // 仅成功写出计入, 避免 audio= 虚高掩盖欠载
    }
    return rt;
}

STATIC OPERATE_RET __handle_player_streaming_source(AI_PLAYER_T *player)
{
    OPERATE_RET rt = OPRT_OK;
    BOOL_T is_eof = FALSE;

    player->decode_size = 0;

    // 1. Read data from datasink (skip if decoder has pending output)
    if (!player->has_pending_output) {
        UINT_T out_len = 0;
        AP_STAT_TS(sink_ts);
        rt = ai_player_datasink_read(player->sink, player->framebuf + player->offset, AI_PLAYER_FRAMEBUF_SIZE - player->offset, &out_len);
        AP_STAT_ADD(player->mode, sink_calls, 1);
        AP_STAT_ADD(player->mode, sink_bytes, out_len);
        AP_STAT_ADD(player->mode, sink_zero, (out_len == 0) ? 1 : 0);
        AP_STAT_DUR_MAX(player->mode, sink_max_ms, sink_ts);
        if(OPRT_OK == rt) {
            player->offset += out_len;
        } else if(OPRT_NOT_FOUND == rt) {
            is_eof = TRUE;
        } else {
            PR_ERR("ai player %s datasink read error: %d", s_player_mode_str[player->mode], rt);
            return rt;
        }
    }

    if(0 == player->offset && !player->has_pending_output) {
        if(is_eof) {
            PR_NOTICE("ai player %s eof", s_player_mode_str[player->mode]);

            __cmd_player_stop(player, AI_PLAYER_STOP_EOF);
            __switch_player_mode();
        }
        tal_system_sleep(10);
        return OPRT_OK;
    }

    if(!s_ai_player_ctx.decoder_mode) {
        player->decode_size = (player->offset > AI_PLAYER_DECODEBUF_SIZE) ? AI_PLAYER_DECODEBUF_SIZE : player->offset;
        memcpy(player->decode_buf, player->framebuf, player->decode_size);
        player->offset -= player->decode_size;
        return OPRT_OK;
    }

    // 2. Decode data
    DECODER_OUTPUT_T output = {0};
    memset(&output, 0, SIZEOF(DECODER_OUTPUT_T));
    AP_STAT_TS(dec_ts);
    rt = ai_player_decoder_process(player->decoder, player->framebuf, player->offset, player->decode_buf, AI_PLAYER_DECODEBUF_SIZE, &output);
    AP_STAT_ADD(player->mode, dec_calls, 1);
    AP_STAT_ADD(player->mode, dec_bytes, output.used_size);
    AP_STAT_DUR_MAX(player->mode, dec_max_ms, dec_ts);

    // Update player's pending output flag based on decoder return value
    player->has_pending_output = (rt == OPRT_BUFFER_NOT_ENOUGH);
    // If decoder indicates buffer not enough, it means there is still pending data to be processed
    rt = (OPRT_OK == rt || player->has_pending_output) ? OPRT_OK : rt;

    if(rt > 0) { // buf is not completely consumed
        if((rt == player->offset) && is_eof) {
            player->offset = 0; // all data consumed and eof
        } else {
            memmove(player->framebuf, player->framebuf + (player->offset - rt), rt);
            player->offset = rt;
        }
    } else if (rt == 0) {
        player->offset = 0;
    } else {
        player->offset = 0;
        return OPRT_OK;
    }

    /* 1) sample==0: id3 tag / 等头还没解析完
     * 2) used_size==0: 头解析成功但本帧仅消费 header, 还没产出 PCM (例如 WAV 首帧 RIFF/JUNK/FLLR 占满 buffer).
     *    后续的 volume/resample 会把 used_size=0 当作非法参数返回 -2, 进而触发整段播放 stop, 必须早退. */
    if(output.sample_rate == 0 || output.used_size == 0) {
        return OPRT_OK;
    }

#if defined(AI_PLAYER_SUPPORT_DIGITAL_VOLUME) && (AI_PLAYER_SUPPORT_DIGITAL_VOLUME == 1)
    // 3. Adjust volume
    if(player->volume != PLAYER_MAX_VOLUME) {
        ai_player_volume_process(player->decode_buf, output.used_size, player->volume, PLAYER_MAX_VOLUME, output.sample_bits);
    }
#endif

    // 4. Resample data if needed
    player->decode_size = output.used_size;
    if(output.channel == s_ai_player_ctx.cfg.channel && output.sample_rate == s_ai_player_ctx.cfg.sample_rate && output.sample_bits == s_ai_player_ctx.cfg.sample_bits) {
        // No need to resample
    } else {
#if defined(AI_PLAYER_SUPPORT_RESAMPLE) && (AI_PLAYER_SUPPORT_RESAMPLE == 1)
        AP_STAT_TS(rs_ts);
        rt = ai_player_resample_process(player->decode_buf, &output, player->decode_buf, (INT_T *)&player->decode_size);
        AP_STAT_DUR_MAX(player->mode, rs_max_ms, rs_ts);
        if(OPRT_OK != rt) {
            PR_ERR("ai player %s resample error: %d", s_player_mode_str[player->mode], rt);
            return rt;
        }
#else
        PR_ERR("ai player %s resample not support!", s_player_mode_str[player->mode]);
        return OPRT_NOT_SUPPORTED;
#endif
    }

    return OPRT_OK;
}

STATIC OPERATE_RET __handle_player_streaming_mix(AI_PLAYER_T *fg_player, AI_PLAYER_T *bg_player)
{
    UINT_T samples = 0;

    if(fg_player && bg_player) {
        if(fg_player->decode_size >= bg_player->decode_size) {
            samples = bg_player->decode_size/2;
            ai_player_mixer_process(bg_player->decode_buf, fg_player->decode_buf, samples);
            if(s_ai_player_ctx.consumer.write && samples) {
                __player_consumer_write_wrap(fg_player, fg_player->decode_buf, samples*2);
            }
            s_ai_player_ctx.mixer_offset = fg_player->decode_size - bg_player->decode_size;
            if(s_ai_player_ctx.mixer_offset) {
                memmove(s_ai_player_ctx.mixer_buf, fg_player->decode_buf + bg_player->decode_size, s_ai_player_ctx.mixer_offset);
                s_ai_player_ctx.mixer_flag = 1;
            } else {
                s_ai_player_ctx.mixer_flag = 0;
            }
        } else {
            samples = fg_player->decode_size/2;
            ai_player_mixer_process(bg_player->decode_buf, fg_player->decode_buf, samples);
            if(s_ai_player_ctx.consumer.write && samples) {
                __player_consumer_write_wrap(fg_player, fg_player->decode_buf, samples*2);
            }
            s_ai_player_ctx.mixer_offset = bg_player->decode_size - fg_player->decode_size;
            if(s_ai_player_ctx.mixer_offset) {
                memmove(s_ai_player_ctx.mixer_buf, bg_player->decode_buf + fg_player->decode_size, s_ai_player_ctx.mixer_offset);
                s_ai_player_ctx.mixer_flag = 2;
            } else {
                s_ai_player_ctx.mixer_flag = 0;
            }
        }
    } else if(fg_player) {
        if(fg_player->decode_size >= s_ai_player_ctx.mixer_offset) {
            samples = s_ai_player_ctx.mixer_offset/2;
            ai_player_mixer_process(s_ai_player_ctx.mixer_buf, fg_player->decode_buf, samples);
            if(s_ai_player_ctx.consumer.write && samples) {
                __player_consumer_write_wrap(fg_player, fg_player->decode_buf, samples*2);
            }
            s_ai_player_ctx.mixer_offset = fg_player->decode_size - s_ai_player_ctx.mixer_offset;
            if(s_ai_player_ctx.mixer_offset) {
                memmove(s_ai_player_ctx.mixer_buf, fg_player->decode_buf + samples*2, s_ai_player_ctx.mixer_offset);
                s_ai_player_ctx.mixer_flag = 1;
            } else {
                s_ai_player_ctx.mixer_flag = 0;
            }
        } else {
            samples = fg_player->decode_size/2;
            ai_player_mixer_process(s_ai_player_ctx.mixer_buf, fg_player->decode_buf, samples);
            if(s_ai_player_ctx.consumer.write && samples) {
                __player_consumer_write_wrap(fg_player, fg_player->decode_buf, samples*2);
            }
            s_ai_player_ctx.mixer_offset = s_ai_player_ctx.mixer_offset - fg_player->decode_size;
            if(s_ai_player_ctx.mixer_offset) {
                memmove(s_ai_player_ctx.mixer_buf, s_ai_player_ctx.mixer_buf + samples*2, s_ai_player_ctx.mixer_offset);
                s_ai_player_ctx.mixer_flag = 2;
            } else {
                s_ai_player_ctx.mixer_flag = 0;
            }
        }
    } else if(bg_player) {
        if(bg_player->decode_size >= s_ai_player_ctx.mixer_offset) {
            samples = s_ai_player_ctx.mixer_offset/2;
            ai_player_mixer_process(s_ai_player_ctx.mixer_buf, bg_player->decode_buf, samples);
            if(s_ai_player_ctx.consumer.write && samples) {
                __player_consumer_write_wrap(bg_player, bg_player->decode_buf, samples*2);
            }
            s_ai_player_ctx.mixer_offset = bg_player->decode_size - s_ai_player_ctx.mixer_offset;
            if(s_ai_player_ctx.mixer_offset) {
                memmove(s_ai_player_ctx.mixer_buf, bg_player->decode_buf + samples*2, s_ai_player_ctx.mixer_offset);
                s_ai_player_ctx.mixer_flag = 2;
            } else {
                s_ai_player_ctx.mixer_flag = 0;
            }
        } else {
            samples = bg_player->decode_size/2;
            ai_player_mixer_process(s_ai_player_ctx.mixer_buf, bg_player->decode_buf, samples);
            if(s_ai_player_ctx.consumer.write && samples) {
                __player_consumer_write_wrap(bg_player, bg_player->decode_buf, samples*2);
            }
            s_ai_player_ctx.mixer_offset = s_ai_player_ctx.mixer_offset - bg_player->decode_size;
            if(s_ai_player_ctx.mixer_offset) {
                memmove(s_ai_player_ctx.mixer_buf, s_ai_player_ctx.mixer_buf + samples*2, s_ai_player_ctx.mixer_offset);
                s_ai_player_ctx.mixer_flag = 1;
            } else {
                s_ai_player_ctx.mixer_flag = 0;
            }
        }
    }

    return OPRT_OK;
}

STATIC OPERATE_RET __handle_player_streaming(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    AI_PLAYER_T *active_player = s_ai_player_ctx.active_player;

    AP_STAT_LOOP();

#if defined(AI_PLAYER_SUPPORT_MIX_MODE) && (AI_PLAYER_SUPPORT_MIX_MODE == 1)
    // Handle mixing if needed
    AI_PLAYER_T *bg_player = s_ai_player_ctx.player[AI_PLAYER_MODE_BACKGROUND];
    if(s_ai_player_ctx.mixer_mode && (active_player->mode == AI_PLAYER_MODE_FOREGROUND)) {
        if(bg_player && (bg_player->state == AI_PLAYER_PLAYING)) {
            OPERATE_RET bg_rt = OPRT_OK;
            if(s_ai_player_ctx.mixer_flag == 0) {
                bg_rt = __handle_player_streaming_source(bg_player);
                rt = __handle_player_streaming_source(active_player);
                if(!s_ai_player_ctx.decoder_mode) {
                    if(s_ai_player_ctx.consumer.write) {
                        if(bg_player->decode_size) {
                            __player_consumer_write_wrap(bg_player, bg_player->decode_buf, bg_player->decode_size);
                            bg_player->decode_size = 0;
                        }
                        if(active_player->decode_size) {
                            __player_consumer_write_wrap(active_player, active_player->decode_buf, active_player->decode_size);
                            active_player->decode_size = 0;
                        }
                    }
                } else {
                    rt = __handle_player_streaming_mix(active_player, bg_player);
                }
            } else if(s_ai_player_ctx.mixer_flag == 1) {
                bg_rt = __handle_player_streaming_source(bg_player);
                if(!s_ai_player_ctx.decoder_mode) {
                    if((OPRT_OK == bg_rt) && bg_player->decode_size && s_ai_player_ctx.consumer.write) {
                        __player_consumer_write_wrap(bg_player, bg_player->decode_buf, bg_player->decode_size);
                        bg_player->decode_size = 0;
                    }
                } else {
                    rt = __handle_player_streaming_mix(NULL, bg_player);
                }
            } else {
                rt = __handle_player_streaming_source(active_player);
                if(!s_ai_player_ctx.decoder_mode) {
                    if((OPRT_OK == rt) && active_player->decode_size && s_ai_player_ctx.consumer.write) {
                        __player_consumer_write_wrap(active_player, active_player->decode_buf, active_player->decode_size);
                        active_player->decode_size = 0;
                    }
                } else {
                    rt = __handle_player_streaming_mix(active_player, NULL);
                }
            }

            if(OPRT_OK != bg_rt) {
                PR_ERR("ai player BG source fatal in mix, stop bg: %d", bg_rt);
                __cmd_player_stop(bg_player, AI_PLAYER_STOP_ERROR);
                __switch_player_mode();
            }

            return OPRT_OK; // mixed
        }
    } else if(s_ai_player_ctx.mixer_offset) {
        s_ai_player_ctx.mixer_flag = 0;
        s_ai_player_ctx.mixer_offset = 0;
    }
#endif

    // No mixing
    rt = __handle_player_streaming_source(active_player);
    if((OPRT_OK == rt) && active_player->decode_size && s_ai_player_ctx.consumer.write) {
        __player_consumer_write_wrap(active_player, active_player->decode_buf, active_player->decode_size);
    }
    active_player->decode_size = 0;

    return rt;
}

STATIC VOID __ai_player_thread_cb(PVOID_T args)
{
    OPERATE_RET rt = OPRT_OK;
    AI_PLAYER_MSG_T msg = {0};
    s_ai_player_ctx.msg_timeout = PLAYER_IDLE_TIMEOUT_MS;

    while(tal_thread_get_state(s_ai_player_ctx.thread) == THREAD_STATE_RUNNING) {
        if (tal_queue_fetch(s_ai_player_ctx.queue, &msg, s_ai_player_ctx.msg_timeout) != OPRT_OK) {
            if(s_ai_player_ctx.is_playing) {
                rt = __handle_player_streaming();
                if(OPRT_OK != rt) {
                    __cmd_player_stop(s_ai_player_ctx.active_player, AI_PLAYER_STOP_ERROR);
                    __switch_player_mode();
                }
            }
            continue;
        }

        PR_DEBUG("ai player %s cmd %s", s_player_mode_str[msg.mode], s_player_cmd_str[msg.cmd]);

        AI_PLAYER_T *player = s_ai_player_ctx.player[msg.mode];
        if (!player && (msg.cmd != PLAYER_CMD_SERVICE_DEINIT)) {
            continue;
        }

        switch (msg.cmd) {
            case PLAYER_CMD_START:
                rt = __cmd_player_start(player, &msg);
                break;
            case PLAYER_CMD_STOP:
                rt = __cmd_player_stop(player, AI_PLAYER_STOP_USER);
                break;
            case PLAYER_CMD_PAUSE:
                rt = __cmd_player_pause(player);
                break;
            case PLAYER_CMD_RESUME:
                rt = __cmd_player_resume(player);
                break;
            case PLAYER_CMD_EXIT:
                rt = __cmd_player_exit(player);
                break;
            case PLAYER_CMD_SERVICE_DEINIT:
                tal_thread_delete(s_ai_player_ctx.thread);
                break;
            default:
                break;
        }

        if((OPRT_OK != rt) && player) {
            PR_ERR("ai player cmd error: %d", rt);
            __cmd_player_stop(player, AI_PLAYER_STOP_ERROR);
        }

        __switch_player_mode();
    }

    __cmd_player_service_deinit();
}

STATIC BOOL_T __is_player_valid(AI_PLAYER_T *player)
{
    if (!player) {
        PR_ERR("player is null");
        return FALSE;
    }

    if (player->mode >= AI_PLAYER_MODE_MAX || s_ai_player_ctx.player[player->mode] != player) {
        PR_ERR("invalid player mode: %d", player->mode);
        return FALSE;
    }

    return TRUE;
}

/**
 * @brief Initialize the AI player service.
 *
 * This function must be called once before creating or using any AI player instance.
 * It sets up global resources such as:
 * - Audio configuration (sample rate, bit depth, channels)
 * - Internal tasks and message queues
 * - Audio driver or output backends
 *
 * @param[in] cfg  Pointer to the player configuration (must not be NULL).
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_player_service_init(AI_PLAYER_CFG_T *cfg)
{
    OPERATE_RET rt = OPRT_OK;

    memcpy(&s_ai_player_ctx.cfg, cfg, SIZEOF(AI_PLAYER_CFG_T));
    memcpy(&s_ai_player_ctx.consumer, &g_consumer_speaker, SIZEOF(AI_PLAYER_CONSUMER_T));
    s_ai_player_ctx.decoder_mode = TRUE;
    s_ai_player_ctx.volume = PLAYER_MAX_VOLUME;
    s_ai_player_ctx.mute = FALSE;

#if defined(AI_PLAYER_LITE) && (AI_PLAYER_LITE == 1)
    s_ai_player_ctx.decoder_mode = FALSE;
    g_consumer_speaker.open(cfg->sample_rate, cfg->sample_bits, cfg->channel);
    TUYA_CALL_ERR_RETURN(lite_service_init(cfg));
#else
    TUYA_CALL_ERR_RETURN(tal_queue_create_init(&s_ai_player_ctx.queue, SIZEOF(AI_PLAYER_MSG_T), PLAYER_QUEUE_DEPTH));

    THREAD_CFG_T thread_cfg = {0};
    thread_cfg.stackDepth = AI_PLAYER_STACK_SIZE;
    /* PRIO_1 时会被相机/视频管线抢到欠载(解码 22ms→60ms, 供给率<1 持续 underrun);
     * 本线程写满输出 ring 即阻塞在信号量上自限流, 提到 PRIO_0 不会饿死低优先级线程 */
    thread_cfg.priority   = THREAD_PRIO_0;
    thread_cfg.thrdname   = "ai_player";
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    thread_cfg.psram_mode = 1;
#endif
    TUYA_CALL_ERR_RETURN(tal_thread_create_and_start(&s_ai_player_ctx.thread, NULL, NULL, __ai_player_thread_cb, NULL, &thread_cfg));

    g_consumer_speaker.open(cfg->sample_rate, cfg->sample_bits, cfg->channel);
    ai_player_resample_init(cfg->sample_rate, cfg->sample_bits, cfg->channel);
#endif

    return rt;
}

/**
 * @brief Deinitialize the AI player service.
 *
 * This function releases all resources allocated by
 * tuya_ai_player_service_init(). After calling this function,
 * no player instance can be created or used until
 * tuya_ai_player_service_init() is called again.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_player_service_deinit(VOID)
{
    OPERATE_RET rt = OPRT_OK;

#if defined(AI_PLAYER_LITE) && (AI_PLAYER_LITE == 1)
    UINT_T i = 0;
    for (i = 0; i < AI_PLAYER_MODE_MAX; i++) {
        if (s_ai_player_ctx.player[i]) {
            lite_player_destroy(s_ai_player_ctx.player[i]);
        }
    }
    lite_service_deinit();
    memset(&s_ai_player_ctx, 0, SIZEOF(AI_PLAYER_CTX_T));
#else
    AI_PLAYER_MSG_T msg = {0};
    msg.mode = AI_PLAYER_MODE_FOREGROUND;
    msg.cmd  = PLAYER_CMD_SERVICE_DEINIT;
    TUYA_CALL_ERR_RETURN(tal_queue_post(s_ai_player_ctx.queue, &msg, 0));
#endif
    return OPRT_OK;
}

/**
 * @brief Create an audio player instance.
 *
 * @param[in]  mode    Player working mode.
 * @param[out] handle  Pointer to the player handle returned on success.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_player_create(AI_PLAYER_MODE_E mode, AI_PLAYER_HANDLE *handle)
{
    OPERATE_RET rt = OPRT_OK;

    if (!handle || (mode >= AI_PLAYER_MODE_MAX) || (s_ai_player_ctx.player[mode] != NULL)) {
        return OPRT_INVALID_PARM;
    }

    AI_PLAYER_T *player = Malloc(SIZEOF(AI_PLAYER_T));
    if (!player) {
        return OPRT_MALLOC_FAILED;
    }
    memset(player, 0, SIZEOF(AI_PLAYER_T));

    player->state = AI_PLAYER_STOPPED;
    player->mode = mode;
    player->mute = FALSE;
    player->volume = PLAYER_MAX_VOLUME;
    player->has_pending_output = FALSE;
#if defined(AI_PLAYER_LITE) && (AI_PLAYER_LITE == 1)
    TUYA_CALL_ERR_RETURN(lite_player_create(player));
#else
    player->framebuf = Malloc(AI_PLAYER_FRAMEBUF_SIZE);
    if (!player->framebuf) {
        Free(player);
        return OPRT_MALLOC_FAILED;
    }

    player->decode_buf = Malloc(AI_PLAYER_DECODEBUF_SIZE);
    if (!player->decode_buf) {
        Free(player->framebuf);
        Free(player);
        return OPRT_MALLOC_FAILED;
    }
    ai_player_datasink_init(&player->sink);
    ai_player_decoder_init(&player->decoder);
#endif
    s_ai_player_ctx.player[mode] = player;

    *handle = (AI_PLAYER_HANDLE)player;

    return OPRT_OK;
}

/**
 * @brief Destroy an audio player instance and release all resources.
 *
 * @param[in] handle Player handle to be destroyed.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_player_destroy(AI_PLAYER_HANDLE handle)
{
    OPERATE_RET rt = OPRT_OK;
    AI_PLAYER_T *player = (AI_PLAYER_T *)handle;

    PR_DEBUG("destroy player %d", player->mode);
    if (!__is_player_valid(player)) {
        return OPRT_INVALID_PARM;
    }

#if defined(AI_PLAYER_LITE) && (AI_PLAYER_LITE == 1)
    return lite_player_destroy(player);
#else
    AI_PLAYER_MSG_T msg = {0};
    msg.mode = player->mode;
    msg.cmd  = PLAYER_CMD_EXIT;
    TUYA_CALL_ERR_RETURN(tal_queue_post(s_ai_player_ctx.queue, &msg, 0));
    return OPRT_OK;
#endif
}

/**
 * @brief Start playback from a specific data source.
 *
 * @param[in] handle Player handle.
 * @param[in] src    Data source type (memory, file, or URL).
 * @param[in] value  Source value:
 *                   - For memory: reserved or NULL (use feed API instead).
 *                   - For file:   file path string.
 *                   - For URL:    URL string.
 * @param[in] codec  Audio codec type of the source data.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_player_start(AI_PLAYER_HANDLE handle, AI_PLAYER_SRC_E src, CHAR_T *value, AI_AUDIO_CODEC_E codec)
{
    OPERATE_RET rt = OPRT_OK;
    AI_PLAYER_T *player = (AI_PLAYER_T *)handle;

    if (!__is_player_valid(player) || (player->state != AI_PLAYER_STOPPED)) {
        PR_ERR("player invalid or not stopped: %d", player->state);
        return OPRT_INVALID_PARM;
    }

#if defined(AI_PLAYER_LITE) && (AI_PLAYER_LITE == 1)
    if (src != AI_PLAYER_SRC_MEM) {
        PR_ERR("lite player only supports AI_PLAYER_SRC_MEM");
        return OPRT_NOT_SUPPORTED;
    }
    return lite_player_start(player, codec);
#else
    if((src != AI_PLAYER_SRC_MEM) && (value == NULL || strlen(value) == 0)) {
        PR_ERR("invalid source %d, value: %s", src, value ? value : "null");
        return OPRT_INVALID_PARM;
    }

    AI_PLAYER_MSG_T msg = {0};
    msg.mode = player->mode;
    msg.cmd  = PLAYER_CMD_START;
    msg.param.cmd_start.src = src;
    msg.param.cmd_start.codec = codec;
    msg.param.cmd_start.value = value ? mm_strdup(value) : NULL;
    PR_DEBUG("start player %d %d", src, codec);
    rt = tal_queue_post(s_ai_player_ctx.queue, &msg, PLAYER_CMD_POST_TIMEOUT_MS);
    if (OPRT_OK != rt) {
        PR_ERR("post start cmd failed: %d", rt);
        if (msg.param.cmd_start.value) {
            Free(msg.param.cmd_start.value);
        }
        return rt;
    }
    if(src == AI_PLAYER_SRC_MEM) {
        UINT_T timeout = 0;
        while(player->state != AI_PLAYER_PLAYING) {
            tal_system_sleep(10);
            if(timeout++ > 1000) { // wait max 10s
                PR_ERR("start player timeout: %d", timeout);
                return OPRT_TIMEOUT;
            }
        }
    }
    return OPRT_OK;
#endif
}

/**
 * @brief Feed raw audio data to the player (used in memory mode).
 *
 * This function pushes audio data into the player for playback when using 
 * memory-based input. The data buffer is not copied internally, so the caller 
 * should ensure its validity until the function returns.
 *
 * Special case:
 * - If @p data == NULL and @p len == 0, it indicates the end of the stream.
 *
 * @param[in] handle Player handle.
 * @param[in] data   Pointer to audio data buffer.
 * @param[in] len    Length of the data buffer in bytes.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_player_feed(AI_PLAYER_HANDLE handle, UINT8_T *data, UINT_T len)
{
    AI_PLAYER_T *player = (AI_PLAYER_T *)handle;

    if (!__is_player_valid(player) || (player->state == AI_PLAYER_STOPPED)) {
        return OPRT_INVALID_PARM;
    }

#if defined(AI_PLAYER_LITE) && (AI_PLAYER_LITE == 1)
    return lite_player_feed(player, data, len);
#else
    return ai_player_datasink_feed(player->sink, data, len);
#endif
}

/**
 * @brief Stop playback immediately.
 *
 * @param[in] handle Player handle.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_player_stop(AI_PLAYER_HANDLE handle)
{
    OPERATE_RET rt = OPRT_OK;
    AI_PLAYER_T *player = (AI_PLAYER_T *)handle;

    if (!__is_player_valid(player)) {
        return OPRT_INVALID_PARM;
    }

    if(player->state == AI_PLAYER_STOPPED) {
        PR_ERR("player already stopped: %d", player->state);
        return OPRT_OK;
    }

#if defined(AI_PLAYER_LITE) && (AI_PLAYER_LITE == 1)
    return lite_player_stop(player, AI_PLAYER_STOP_USER);
#else
    AI_PLAYER_MSG_T msg = {0};
    msg.mode = player->mode;
    msg.cmd  = PLAYER_CMD_STOP;
    PR_DEBUG("stop player %d", player->mode);
    TUYA_CALL_ERR_RETURN(tal_queue_post(s_ai_player_ctx.queue, &msg, PLAYER_CMD_POST_TIMEOUT_MS));
    UINT_T stop_timeout = 0;
    while(player->state != AI_PLAYER_STOPPED) {
        tal_system_sleep(10);
        if(stop_timeout++ > 200) { // 最多等 2s，FG 卡住时不再死等、避免挂死调用线程(按键)
            PR_ERR("stop player timeout, mode=%d state=%d", player->mode, player->state);
            return OPRT_TIMEOUT;
        }
    }
    return OPRT_OK;
#endif
}

/**
 * @brief Pause playback.
 *
 * @param[in] handle Player handle.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_player_pause(AI_PLAYER_HANDLE handle)
{
    OPERATE_RET rt = OPRT_OK;
    AI_PLAYER_T *player = (AI_PLAYER_T *)handle;

    PR_DEBUG("pause player %d", player->state);
    if (!__is_player_valid(player) || (player->state != AI_PLAYER_PLAYING)) {
        return OPRT_INVALID_PARM;
    }

#if defined(AI_PLAYER_LITE) && (AI_PLAYER_LITE == 1)
#else
    AI_PLAYER_MSG_T msg = {0};
    msg.mode = player->mode;
    msg.cmd  = PLAYER_CMD_PAUSE;
    TUYA_CALL_ERR_RETURN(tal_queue_post(s_ai_player_ctx.queue, &msg, 0));
#endif
    return OPRT_OK;
}

/**
 * @brief Resume playback after being paused.
 *
 * @param[in] handle Player handle.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_player_resume(AI_PLAYER_HANDLE handle)
{
    OPERATE_RET rt = OPRT_OK;
    AI_PLAYER_T *player = (AI_PLAYER_T *)handle;

    PR_DEBUG("resume player %d", player->state);
    if (!__is_player_valid(player) || (player->state != AI_PLAYER_PAUSED)) {
        return OPRT_INVALID_PARM;
    }

#if defined(AI_PLAYER_LITE) && (AI_PLAYER_LITE == 1)
#else
    AI_PLAYER_MSG_T msg = {0};
    msg.mode = player->mode;
    msg.cmd  = PLAYER_CMD_RESUME;
    TUYA_CALL_ERR_RETURN(tal_queue_post(s_ai_player_ctx.queue, &msg, 0));
#endif
    return OPRT_OK;
}

/**
 * @brief Get the current playback state of the player
 *
 * @param[in] handle  Handle of the player instance
 *
 * @return Current player state (see @ref AI_PLAYER_STATE_T)
 */
AI_PLAYER_STATE_T tuya_ai_player_get_state(AI_PLAYER_HANDLE handle)
{
    AI_PLAYER_T *player = (AI_PLAYER_T *)handle;

    if (!__is_player_valid(player)) {
        return AI_PLAYER_STOPPED;
    }

    return player->state;
}

/**
 * @brief Get the current playback progress (offset and total length).
 *
 * @param[in]  handle Player handle.
 * @param[out] offset Current playback offset in bytes.
 * @param[out] length Total length of the audio source in bytes (0 if unknown).
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_player_get_progress(AI_PLAYER_HANDLE handle, UINT_T *offset, UINT_T *length)
{
    OPERATE_RET rt = OPRT_OK;
    AI_PLAYER_T *player = (AI_PLAYER_T *)handle;

    if (!__is_player_valid(player) || (player->state == AI_PLAYER_STOPPED)) {
        return OPRT_INVALID_PARM;
    }

#if defined(AI_PLAYER_LITE) && (AI_PLAYER_LITE == 1)
    return OPRT_NOT_SUPPORTED;
#else
    return ai_player_datasink_get_progress(player->sink, offset, length);
#endif
}

/**
 * @brief Get the current audio codec of the player
 *
 * @param[in] handle  Handle of the player instance
 *
 * @return Current audio codec (see @ref AI_AUDIO_CODEC_E)
 */
AI_AUDIO_CODEC_E tuya_ai_player_get_codec(AI_PLAYER_HANDLE handle)
{
    AI_PLAYER_T *player = (AI_PLAYER_T *)handle;

    if (!__is_player_valid(player)) {
        return AI_AUDIO_CODEC_MP3;
    }

    return ai_player_decoder_get_codec(player->decoder);
}

/**
 * @brief Set the consumer for the audio player (default is tal_audio).
 *
 * This function binds a user-implemented consumer (AI_PLAYER_CONSUMER_T)
 * to the player service. The player service will invoke the consumer’s
 * callbacks to handle decoded audio output.
 *
 * @param[in] consumer  Pointer to user-implemented consumer interface
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_player_set_consumer(AI_PLAYER_CONSUMER_T *consumer)
{
    if(s_ai_player_ctx.consumer.close) {
        s_ai_player_ctx.consumer.close();
    }

    if(!consumer) {
        memcpy(&s_ai_player_ctx.consumer, &g_consumer_speaker, SIZEOF(AI_PLAYER_CONSUMER_T));
    } else {
        memcpy(&s_ai_player_ctx.consumer, consumer, SIZEOF(AI_PLAYER_CONSUMER_T));
    }

    if(s_ai_player_ctx.consumer.open) {
        s_ai_player_ctx.consumer.open(s_ai_player_ctx.cfg.sample_rate, s_ai_player_ctx.cfg.sample_bits, s_ai_player_ctx.cfg.channel);
    }

    return OPRT_OK;
}

/**
 * @brief Set playback volume.
 *
 * This function adjusts the playback volume of the AI player.
 * - If @p handle is NULL, it adjusts the global analog (hardware) volume.
 * - If @p handle is not NULL, it adjusts the digital volume for the specified player instance.
 *
 * The valid range of @p volume is 0–100, where a higher value indicates a higher volume.
 *
 * @param[in] handle Player handle. NULL for global analog volume control.
 * @param[in] volume Volume level (0–100, higher means louder).
 *
 * @return OPRT_OK on success. Other error codes are defined in tuya_error_code.h.
 */
OPERATE_RET tuya_ai_player_set_volume(AI_PLAYER_HANDLE handle, INT32_T volume)
{
    if(!handle) {
        s_ai_player_ctx.volume = volume;

        if(s_ai_player_ctx.consumer.set_volume) {
            s_ai_player_ctx.consumer.set_volume(s_ai_player_ctx.volume);
        }
    } else {
        AI_PLAYER_T *player = (AI_PLAYER_T *)handle;
        player->volume = volume;
    }

    return OPRT_OK;
}

/**
 * @brief Get the current playback volume.
 *
 * @param[in] handle Player handle. Null indicates global.
 * @param[out] volume Pointer to store the current volume level.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_player_get_volume(AI_PLAYER_HANDLE handle, INT32_T *volume)
{
    if(!handle) {
        *volume = s_ai_player_ctx.volume;
    } else {
        AI_PLAYER_T *player = (AI_PLAYER_T *)handle;
        *volume = player->volume;
    }

    return OPRT_OK;
}

/**
 * @brief Enable or disable mute.
 *
 * @param[in] handle Player handle. Null indicates global.
 * @param[in] mute   TRUE to mute, FALSE to unmute.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_player_set_mute(AI_PLAYER_HANDLE handle, BOOL_T mute)
{
    if(!handle) {
        s_ai_player_ctx.mute = mute;

        if(s_ai_player_ctx.consumer.set_volume) {
            if(mute) {
                s_ai_player_ctx.consumer.set_volume(0);
            } else {
                s_ai_player_ctx.consumer.set_volume(s_ai_player_ctx.volume);
            }
        }
    } else {
        AI_PLAYER_T *player = (AI_PLAYER_T *)handle;
        player->mute = mute;
    }

    return OPRT_OK;
}

/**
 * @brief Get the mute status.
 *
 * @param[in] handle Player handle. Null indicates global.
 * @param[out] mute   Pointer to store mute status (TRUE if muted).
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
 */
OPERATE_RET tuya_ai_player_get_mute(AI_PLAYER_HANDLE handle, BOOL_T *mute)
{
    if(!handle) {
        *mute = s_ai_player_ctx.mute;
    } else {
        AI_PLAYER_T *player = (AI_PLAYER_T *)handle;
        *mute = player->mute;
    }

    return OPRT_OK;
}

/**
 * @brief Set the player mixing mode
 *
 * Configures whether the player operates in preemptive mode or mixing mode
 * when handling foreground and background playback.
 *
 * In **preemptive mode** (default), foreground playback takes priority:
 * - When foreground audio starts, background playback is automatically paused.
 * - When foreground playback ends, background playback resumes.
 *
 * In **mixing mode**, both foreground and background audio can be played
 * simultaneously:
 * - The background volume is automatically reduced (e.g., to 50%) while
 *   foreground audio is active.
 * - Once foreground playback ends, the background volume returns to normal.
 *
 * @param[in] enable  TRUE to enable mixing mode, FALSE to use preemptive mode.
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h.
 *
 * @note This setting affects the behavior of both foreground and background
 *       players globally. It should be configured before playback starts.
 */
OPERATE_RET tuya_ai_player_set_mix_mode(BOOL_T enable)
{
    if(enable && !s_ai_player_ctx.mixer_buf) {
        s_ai_player_ctx.mixer_buf = Malloc(AI_PLAYER_DECODEBUF_SIZE);
        if(!s_ai_player_ctx.mixer_buf) {
            return OPRT_MALLOC_FAILED;
        }
    }
    s_ai_player_ctx.mixer_offset = 0;
    s_ai_player_ctx.mixer_flag = 0;
    s_ai_player_ctx.mixer_mode = enable;
    return OPRT_OK;
}

/**
 * @brief Enable or disable the decoder for PCM output.
 *
 * This function controls whether the AI player decodes the input audio stream.
 * When enabled (default), the player decodes the input data into PCM format before output.
 * When disabled, the player bypasses decoding and outputs the raw audio data directly.
 *
 * @param[in] enable  TRUE to enable decoding (output PCM), FALSE to disable decoding (output raw data).
 *
 * @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h.
 */
OPERATE_RET tuya_ai_player_set_decoder_mode(BOOL_T enable)
{
    s_ai_player_ctx.decoder_mode = enable;
    return OPRT_OK;
}
