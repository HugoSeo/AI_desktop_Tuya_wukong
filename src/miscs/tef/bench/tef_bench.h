/**
 * @file tef_bench.h
 * @brief TEF vs GIF 基准对比（CPU / 内存 / flash）。仅随 TEF_BENCH=1 编译：
 *        make app APP_NAME=tuyaos_demo_wukong_ai TEF_BENCH=1
 *        同一素材分别用 TEF 解码与 gifdec（旧 lv_gif 同源解码器）各跑 N 圈，
 *        启动时输出 [TEF-BENCH] 报告。
 */
#ifndef __TEF_BENCH_H__
#define __TEF_BENCH_H__

#include <stdint.h>
#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t frames;          /* 实际解码帧数（loops × 每圈帧数） */
    uint32_t total_ms;        /* 解码总耗时（不含刷屏） */
    uint32_t max_frame_ms;    /* 单帧最大耗时（含该帧的段解压） */
    uint32_t mem_bytes;       /* 常驻工作内存（精确字节） */
    uint16_t fps;
    uint16_t frames_per_loop;
    uint16_t w, h;
} tef_bench_stat_t;

/* TEF 解码基准（实现于 tef_player.c，复用真实解码路径，不刷屏）。
 * 须在 tef_player_init() 之后调用。 */
OPERATE_RET tef_bench_decode(const uint8_t *tef, uint32_t len, int loops,
                             tef_bench_stat_t *st);

/* 完整对比：TEF 与 GIF 各跑 loops 圈，打印 [TEF-BENCH] 报告 */
void tef_bench_run(const char *name, const uint8_t *tef, uint32_t tef_len,
                   const uint8_t *gif, uint32_t gif_len, int loops);

#ifdef __cplusplus
}
#endif

#endif /* __TEF_BENCH_H__ */
