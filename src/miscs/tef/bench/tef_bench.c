/**
 * @file tef_bench.c
 * @brief TEF vs GIF 基准编排与报告。
 *
 * 度量口径（两侧对齐，均为"解码 + 合成整帧"，不含刷屏——刷屏两种方案已同为
 * 每帧整屏 flush，成本相同）：
 *  - CPU：同一素材各解 loops 圈，总耗时/帧数 = 平均每帧 µs；再折算相对素材
 *    fps 的 CPU 占用百分比。GIF 侧用与旧 lv_gif 同源的 gifdec（LZW 解码 +
 *    disposal 合成 + RGB565 canvas 渲染），TEF 侧走 tef_player 真实解码路径。
 *  - 内存：TEF = 影子帧缓存 + 流式字典(32KB) + 索引/位图缓冲 + tinfl + 调色板
 *    （精确字节，PSRAM 常驻）；GIF = gifdec 全部动态分配的峰值（单块 gd_GIF+canvas+
 *    frame + LZW 表 realloc 峰值，分配器逐字节统计；lv_gif 时代另有 LVGL
 *    本体开销，此处不计，故 GIF 侧为下限）。
 *  - flash：两种格式的素材字节数（代码体积差异见 map 文件：miscs/tef 全部
 *    目标码 ~15KB vs LVGL 整库，未计入本报告）。
 */
#if defined(TEF_BENCH) && (TEF_BENCH == 1)

#include <string.h>
#include "tuya_cloud_types.h"
#include "tal_log.h"
#include "tal_system.h"
#include "tef_bench.h"
#include "gifdec_bench.h"

static uint32_t now_ms(void)
{
    return (uint32_t)tal_system_get_millisecond();
}

void tef_bench_run(const char *name, const uint8_t *tef, uint32_t tef_len,
                   const uint8_t *gif, uint32_t gif_len, int loops)
{
    tef_bench_stat_t ts;

    TAL_PR_NOTICE("[TEF-BENCH] ===== asset '%s', %d loops =====", name, loops);

    /* ---- TEF ---- */
    if (tef_bench_decode(tef, tef_len, loops, &ts) != OPRT_OK) {
        TAL_PR_ERR("[TEF-BENCH] tef decode failed");
        return;
    }

    /* ---- GIF（与旧 lv_gif 同源解码器） ---- */
    gdb_alloc_stat_reset();
    gdb_GIF *g = gdb_open_gif_data(gif);
    if (g == NULL) {
        TAL_PR_ERR("[TEF-BENCH] gif open failed");
        return;
    }
    uint32_t target = (uint32_t)ts.frames_per_loop * (uint32_t)loops;
    uint32_t gframes = 0, gmax = 0;
    uint32_t t0 = now_ms();
    while (gframes < target) {
        uint32_t f0 = now_ms();
        int r = gdb_get_frame(g);
        if (r < 0) {
            TAL_PR_ERR("[TEF-BENCH] gif decode error at frame %u", gframes);
            break;
        }
        if (r == 0) {
            continue;                       /* 尾部已回绕，进入下一圈 */
        }
        gdb_render_frame(g, g->canvas);     /* 与 lv_gif 相同：渲染进 canvas */
        gframes++;
        uint32_t d = now_ms() - f0;
        if (d > gmax) {
            gmax = d;
        }
    }
    uint32_t gtotal = now_ms() - t0;
    uint32_t gmem = gdb_alloc_stat_peak();
    gdb_close_gif(g);

    /* ---- 报告 ---- */
    uint32_t period_ms = (ts.fps > 0) ? (1000u / ts.fps) : 50u;
    /* 平均每帧 µs；CPU 占用 = avg_us / (period_ms*1000) * 100%，
     * ×10 定点保留一位小数 => pct10 = avg_us / period_ms */
    uint32_t t_us = (ts.frames > 0) ? (ts.total_ms * 1000u / ts.frames) : 0;
    uint32_t g_us = (gframes > 0) ? (gtotal * 1000u / gframes) : 0;
    uint32_t t_pct10 = t_us / period_ms;
    uint32_t g_pct10 = g_us / period_ms;

    TAL_PR_NOTICE("[TEF-BENCH] canvas %ux%u, %u frames/loop, fps=%u (period %ums)",
                  ts.w, ts.h, ts.frames_per_loop, ts.fps, period_ms);
    TAL_PR_NOTICE("[TEF-BENCH] flash : tef=%uB  gif=%uB  (tef/gif = %u%%)",
                  tef_len, gif_len, gif_len ? tef_len * 100u / gif_len : 0);
    TAL_PR_NOTICE("[TEF-BENCH] mem   : tef=%uB (PSRAM 常驻: 影子+流式字典+索引+tinfl+调色板)",
                  ts.mem_bytes);
    TAL_PR_NOTICE("[TEF-BENCH] mem   : gif=%uB (峰值: gd_GIF+canvas+frame+LZW 表; 不含 LVGL 本体)",
                  gmem);
    TAL_PR_NOTICE("[TEF-BENCH] cpu   : tef %u frames, %ums total, avg=%uus/frame, max=%ums (含段解压)",
                  ts.frames, ts.total_ms, t_us, ts.max_frame_ms);
    TAL_PR_NOTICE("[TEF-BENCH] cpu   : gif %u frames, %ums total, avg=%uus/frame, max=%ums",
                  gframes, gtotal, g_us, gmax);
    TAL_PR_NOTICE("[TEF-BENCH] cpu@fps: tef=%u.%u%%  gif=%u.%u%%  (相对 %ums 帧节拍)",
                  t_pct10 / 10, t_pct10 % 10, g_pct10 / 10, g_pct10 % 10, period_ms);
    TAL_PR_NOTICE("[TEF-BENCH] ===== end =====");
}

#endif /* TEF_BENCH */
