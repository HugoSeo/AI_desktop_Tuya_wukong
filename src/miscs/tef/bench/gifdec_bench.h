/**
 * @file gifdec_bench.h
 * @brief 基准测试专用 GIF 解码器 —— 移植自 LVGL 8.3 gifdec（lecram/gifdec 派生），
 *        去 LVGL 依赖：仅保留 data 模式（无 lv_fs），LV_COLOR_DEPTH 固定按 16
 *        处理（canvas 3B/px = RGB565 + alpha，与旧 lv_gif 在本工程的实际配置一致），
 *        lv_mem_* 换成带峰值统计的本地分配器（精确度量 GIF 路径的内存占用）。
 *        仅随 TEF_BENCH=1 编译，不进正常固件。
 */
#ifndef __GIFDEC_BENCH_H__
#define __GIFDEC_BENCH_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gdb_Palette {
    int size;
    uint8_t colors[0x100 * 3];
} gdb_Palette;

typedef struct gdb_GCE {
    uint16_t delay;
    uint8_t tindex;
    uint8_t disposal;
    int input;
    int transparency;
} gdb_GCE;

typedef struct gdb_GIF {
    const char *data;            /* 仅 data 模式 */
    uint32_t f_rw_p;
    int32_t anim_start;
    uint16_t width, height;
    uint16_t depth;
    int32_t loop_count;
    gdb_GCE gce;
    gdb_Palette *palette;
    gdb_Palette lct, gct;
    uint16_t fx, fy, fw, fh;
    uint8_t bgindex;
    uint8_t *canvas, *frame;     /* canvas: 3B/px(RGB565+alpha), frame: 1B/px 索引 */
} gdb_GIF;

gdb_GIF *gdb_open_gif_data(const void *data);
int gdb_get_frame(gdb_GIF *gif);                       /* 1=有帧 0=尾(已回绕) -1=错 */
void gdb_render_frame(gdb_GIF *gif, uint8_t *buffer);
void gdb_close_gif(gdb_GIF *gif);

/* 分配统计（度量 GIF 路径内存）：cur=当前在用字节，peak=历史峰值 */
void gdb_alloc_stat_reset(void);
uint32_t gdb_alloc_stat_cur(void);
uint32_t gdb_alloc_stat_peak(void);

#ifdef __cplusplus
}
#endif

#endif /* __GIFDEC_BENCH_H__ */
