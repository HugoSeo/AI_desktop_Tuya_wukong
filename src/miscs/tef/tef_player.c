/**
 * @file tef_player.c
 * @brief TEF 表情动画播放器 — 解码 + 脏块直推 GRAM（不经 LVGL）。
 *
 * 通路：解码线程按 fps 节拍 → 流式拉取解压(deflate 32KB 环形字典 /
 * heatshrink 增量,段明文从不整块落地,TEF v3 P1) → 脏块位图 → 脏 tile
 * 查调色板写入影子帧缓存(PSRAM) → 每帧整屏 tal_display_flush 推送。
 *
 * 为什么按"整行带"而不是逐脏块子窗口：T5 上 SPI2/3 实际走 QSPI 驱动，
 * tkl_qspi_send 对 ≤256 字节的发送走命令 FIFO 路径（首字节被拆作命令字节、
 * 数据按 32bit 字打包），像素小包经它必现花方块；整行带 = 宽 128 × tile 高
 * ≥ 2KB，恒走 DMA 大包路径（旧 UI 整带刷验证过的通路），且天然 4 字节对齐、
 * 在影子缓存中行连续（免拷贝直接以带首地址做 DMA 源）。
 *
 * 双屏（EYES 上下两块 panel 摞成 128x256 逻辑屏）：
 *  - 素材画布高 == 单 panel 高（128x128 单眼素材）→ 同帧镜像推到所有 panel；
 *  - 素材画布高 == 逻辑屏高（128x256 双眼素材）→ 按行带路由到对应 panel。
 * tile 高 8/16/32/64 均整除 128，脏块不会跨 panel。
 *
 * EYES 通常关闭 UI 框架(ENABLE_TUYA_DISPLAY only)，屏幕归本播放器独占；
 * 若 UI 框架开启，板 UI 不建任何会失效重绘的 LVGL 对象，LVGL 只在启动时
 * flush 一次全黑帧——播放线程起播前延时避开这次 flush，且每次循环回绕的
 * 第 0 帧是全脏关键帧，可自愈任何意外覆盖。
 *
 * 字节交换：8bit SPI 面板(st7735s)期望 MSB-first，由 local.mk 定义
 * TEF_RGB565_BYTE_SWAP=1，调色板加载时一次交换完成，热路径零开销。
 *
 * @copyright Copyright (c) tuya.inc 2026
 */
#include <string.h>
#include <stdlib.h>

#include "tuya_cloud_types.h"
#include "tal_log.h"
#include "tal_thread.h"
#include "tal_mutex.h"
#include "tal_semaphore.h"
#include "tal_system.h"
#include "tkl_memory.h"
#include "tal_display_service.h"
#include "tuya_display_hw.h"

#include "tef_format.h"
#include "vendor/heatshrink_decoder.h"
#include "vendor/miniz_tinfl.h"

#include "tef_player.h"

/* heatshrink 流无参数头，编解码两端须约定一致（同 C 参考实现 hs_util.c） */
#define TEF_HS_WINDOW    11
#define TEF_HS_LOOKAHEAD 4

#define TEF_MAX_PANELS   2
#define TEF_DEFAULT_FPS  20
/* 播放统计日志（[TEF-STAT] 每 ~2s 一行:帧间隔/解压/刷屏耗时聚合），置 0 关闭 */
#ifndef TEF_PLAYER_STATS
#define TEF_PLAYER_STATS 0
#endif
#define TEF_STAT_PERIOD_MS 2000
#define TEF_IDLE_POLL_MS 50
/* 起播延时：等 LVGL 完成启动后的唯一一次全黑整帧 flush（~1 个 30ms tick） */
#define TEF_START_DELAY_MS 300

typedef struct {
    TY_DISPLAY_HANDLE handle;
    uint16_t y_off;              /* 该 panel 在逻辑屏中的起始行 */
    uint16_t rows;
} tef_panel_t;

typedef struct {
    const uint8_t *data;         /* .tef 常量数组（flash 常驻） */
    uint32_t       len;
    tef_header     hdr;
    const tef_seg *segs;         /* 指向 data 内的段表 */
    uint16_t       pal[256];     /* RAM 副本（按需已做字节交换） */
    int ntx, nty, ntiles, bmb;
    uint32_t frame_ms;
    /* 工作缓冲（PSRAM，随素材按需增长）——P1 流式解码后不再有整段明文槽：
     * deflate 只需 32KB 环形字典，段明文从不整块落地（TEF v3 P1） */
    uint8_t  *dict;   uint32_t dict_cap;    /* deflate 流式环形输出字典 */
    uint32_t dict_size;                     /* 环大小 = 1<<hdr.window_bits；cap 只增
                                             * 不减，取模必须用本值而非 dict_cap */
    uint8_t  *bmp;    uint32_t bmp_cap;     /* 当前帧脏块位图 */
    uint8_t  *unp;    uint32_t unp_cap;     /* 一帧脏像素的 8bit 索引 */
    uint16_t *shadow; uint32_t shadow_cap;  /* 全画布 RGB565 影子帧缓存 */
    uint16_t *dst;    uint32_t dst_stride;  /* 渲染目标：常规=shadow/行距 canvas_w；
                                             * play_once 指向外部画布（行距为外部值） */
    BOOL_T jpeg_warned;
} tef_ctx_t;

/* 拉取式解压流：把压缩段当字节水管，解一点取一点（TEF v3 §4.1）。
 * deflate：tinfl 增量解到环形字典（out_buf 即 LZ 匹配窗口，须为 2 的幂且
 * ≥ 编码窗口 1<<window_bits，环大小取自素材头），消费游标紧跟生产游标，
 * 段明文从不整块落地；
 * heatshrink：解码器自带窗口，sink/poll 直接产出到目标。
 * 约束：s_tinfl 为常驻单例，同一时刻只允许一条活动流（播放与 bench 不并发）。 */
typedef struct {
    uint8_t comp;
    const uint8_t *src;          /* 压缩段（flash，零拷贝） */
    size_t src_len, src_pos;
    /* deflate */
    tinfl_decompressor *tinfl;   /* 由 stream_open 注入：单例或 tef_dec 私有 */
    uint8_t *dict;               /* = ctx->dict */
    uint32_t dict_size;          /* = ctx->dict_size（2 的幂，环取模用） */
    uint32_t dict_pos;           /* 生产游标 */
    uint32_t avail_off, avail;   /* 已生产未消费区间 */
    int eof;
    /* heatshrink */
    heatshrink_decoder *hsd;
} tef_stream_t;

#if TEF_PLAYER_STATS
/* 聚合统计（播放线程内累积，flush_band 记录刷屏耗时） */
static struct {
    uint32_t frames;
    uint32_t itv_sum, itv_max;      /* 实际帧间隔 ms */
    uint32_t decomp_max;            /* 单次段解压 ms */
    uint32_t flush_max, flush_sum;  /* 单帧写屏 ms */
    uint32_t rows_sum;              /* 推送行数 */
    uint32_t win_start;
    uint32_t last_ts;               /* 上一帧完成时刻（算实际帧间隔） */
} s_stat;
#endif

static BOOL_T        s_inited = FALSE;
static THREAD_HANDLE s_thread = NULL;
static MUTEX_HANDLE  s_lock = NULL;
static SEM_HANDLE    s_flush_sem = NULL;

static tef_panel_t s_panel[TEF_MAX_PANELS];
static uint8_t     s_panel_cnt = 0;

/* 切换请求：play() 写入，解码线程在帧边界消费 */
static const uint8_t *s_req_data = NULL;
static uint32_t       s_req_len = 0;
static volatile uint32_t s_req_seq = 0;

/* flush 描述符须在驱动异步发送期间常驻；双 panel 并行在途各占一个 */
static ty_frame_buffer_t s_desc[TEF_MAX_PANELS];

static void flush_done_cb(UINT8_T *frame)
{
    (void)frame;
    tal_semaphore_post(s_flush_sem);
}

/* ------------------------------------------------------------------------ */
/* 流式解压（TEF v3 P1：段明文从不整块落地）                                  */
/* ------------------------------------------------------------------------ */
/* tinfl_decompressor 约 10KB——绝不能上线程栈(tinfl_decompress_mem_to_mem
 * 就是把它放栈上,在嵌入式线程栈上必然溢出),常驻 PSRAM 单例。 */
static tinfl_decompressor *s_tinfl = NULL;

static int stream_open(tef_ctx_t *c, tef_stream_t *s, const tef_seg *sg,
                       tinfl_decompressor *tf)
{
    memset(s, 0, sizeof(*s));
    s->comp = sg->comp;
    s->src = c->data + sg->offset;
    s->src_len = sg->comp_len;
    if (s->comp == TEF_COMP_HEATSHRINK) {
        s->hsd = heatshrink_decoder_alloc(256, TEF_HS_WINDOW, TEF_HS_LOOKAHEAD);
        if (s->hsd == NULL) {
            return -1;
        }
    } else {
        if (tf == NULL || c->dict == NULL) {
            return -1;
        }
        tinfl_init(tf);
        s->tinfl = tf;
        s->dict = c->dict;
        s->dict_size = c->dict_size;
    }
    return 0;
}

static void stream_close(tef_stream_t *s)
{
    if (s->hsd != NULL) {
        heatshrink_decoder_free(s->hsd);
        s->hsd = NULL;
    }
}

/* deflate：向环形字典生产至少 1 字节可消费数据。输入一次性全量递给 tinfl
 * (flags=0，RAW deflate 且输入完整)；输出口 = 字典写游标到环尾的连续区，
 * LZ 匹配窗口按环大小(dict_size，2 的幂)取模。 */
static int stream_produce_deflate(tef_stream_t *s)
{
    while (s->avail == 0) {
        size_t in_len, out_len;
        tinfl_status st;

        if (s->eof) {
            return -1;                     /* 读越段尾 = 素材截断/损坏 */
        }
        in_len = s->src_len - s->src_pos;
        out_len = s->dict_size - s->dict_pos;
        st = tinfl_decompress(s->tinfl,
                              (const mz_uint8 *)(s->src + s->src_pos), &in_len,
                              (mz_uint8 *)s->dict,
                              (mz_uint8 *)(s->dict + s->dict_pos), &out_len,
                              0);
        s->src_pos += in_len;
        s->avail_off = s->dict_pos;
        s->avail = (uint32_t)out_len;
        s->dict_pos = (s->dict_pos + (uint32_t)out_len) & (s->dict_size - 1);
        if (st == TINFL_STATUS_DONE) {
            s->eof = 1;
        } else if (st != TINFL_STATUS_HAS_MORE_OUTPUT) {
            return -1;                     /* 输入已全量给出仍缺 = 损坏 */
        }
    }
    return 0;
}

/* 从流拉取恰好 n 字节；返回 0 成功 / -1 截断或损坏 */
static int stream_pull(tef_stream_t *s, uint8_t *out, size_t n)
{
    size_t got = 0;

    if (s->comp != TEF_COMP_HEATSHRINK) {
        while (got < n) {
            size_t take;

            if (s->avail == 0 && stream_produce_deflate(s) < 0) {
                return -1;
            }
            take = n - got;
            if (take > s->avail) {
                take = s->avail;
            }
            memcpy(out + got, s->dict + s->avail_off, take);
            s->avail_off += (uint32_t)take;
            s->avail -= (uint32_t)take;
            got += take;
        }
        return 0;
    }

    /* heatshrink：解码器自带窗口，poll 直接产出；无进展哨兵防死循环 */
    {
        int stall = 0;

        while (got < n) {
            size_t polled = 0, sunk = 0;
            HSD_poll_res pr = heatshrink_decoder_poll(s->hsd, out + got, n - got, &polled);

            if (pr < 0) {
                return -1;
            }
            got += polled;
            if (got >= n) {
                break;
            }
            if (pr != HSDR_POLL_MORE) {
                if (s->src_pos < s->src_len) {
                    if (heatshrink_decoder_sink(s->hsd, (uint8_t *)s->src + s->src_pos,
                                                s->src_len - s->src_pos, &sunk) < 0) {
                        return -1;
                    }
                    s->src_pos += sunk;
                } else {
                    HSD_finish_res fr = heatshrink_decoder_finish(s->hsd);
                    if (fr == HSDR_FINISH_DONE) {
                        return -1;         /* 流已尽仍缺数据 = 截断 */
                    }
                }
            }
            stall = (polled == 0 && sunk == 0) ? stall + 1 : 0;
            if (stall > 4) {
                return -1;
            }
        }
        return 0;
    }
}

/* ------------------------------------------------------------------------ */
/* 位流展开（与 tef_codec.c unpackbits 一致）                                 */
/* ------------------------------------------------------------------------ */
static void unpack_indices(const uint8_t *in, int bits, uint8_t *out, uint32_t npx)
{
    uint32_t o = 0, i = 0;

    if (bits == 8) {
        memcpy(out, in, npx);
        return;
    }
    if (bits == 4) {
        for (i = 0; o < npx; i++) {
            out[o++] = in[i] >> 4;
            if (o < npx) {
                out[o++] = in[i] & 0xF;
            }
        }
        return;
    }
    for (i = 0; o < npx; i++) {
        for (int j = 7; j >= 0 && o < npx; j--) {
            out[o++] = (in[i] >> j) & 1;
        }
    }
}

/* ------------------------------------------------------------------------ */
/* 缓冲管理                                                                  */
/* ------------------------------------------------------------------------ */
static OPERATE_RET buf_reserve(void **buf, uint32_t *cap, uint32_t need)
{
    if (need <= *cap) {
        return OPRT_OK;
    }
    if (*buf != NULL) {
        tkl_system_psram_free(*buf);
    }
    *buf = tkl_system_psram_malloc(need);
    if (*buf == NULL) {
        *cap = 0;
        return OPRT_MALLOC_FAILED;
    }
    *cap = need;
    return OPRT_OK;
}

/* ------------------------------------------------------------------------ */
/* 素材装载                                                                  */
/* ------------------------------------------------------------------------ */
/* ext_dst=TRUE：渲染到外部画布（调用方在 load 成功后自行设 c->dst/c->dst_stride），
 * 不校验画布与显示的尺寸匹配（几何由调用方负责）、不分配 shadow。 */
static OPERATE_RET ctx_load(tef_ctx_t *c, const uint8_t *data, uint32_t len, BOOL_T ext_dst)
{
    tef_header h;
    uint32_t i;

    c->data = NULL;
    if (data == NULL || len < sizeof(tef_header)) {
        return OPRT_INVALID_PARM;
    }
    memcpy(&h, data, sizeof(h));
    if (memcmp(h.magic, TEF_MAGIC, 4) != 0) {
        TAL_PR_ERR("tef: bad magic");
        return OPRT_INVALID_PARM;
    }
    if (h.version != TEF_VERSION) {
        TAL_PR_ERR("tef: unsupported version %u (only v3; re-encode with tef_encoder)",
                   h.version);
        return OPRT_INVALID_PARM;
    }
    /* mode/flush_policy 对"流式 + 整屏 flush"路径无分支意义（comp 按段自带；
     * EYES 无 TE 固定 immediate）；window_bits 决定 deflate 环形字典大小，
     * 在下方按需校验/分配 */
    TAL_PR_DEBUG("tef: v3 mode=%u win=%u flush=%u max_seg=%u",
                 h.mode, h.window_bits, h.flush_policy, h.max_seg_decomp);
    if (h.flags & 0x1) {                       /* bit0: RGB888 输出，本通路仅 565 */
        TAL_PR_ERR("tef: rgb888 asset not supported");
        return OPRT_NOT_SUPPORTED;
    }
    if (h.ncolors > 256 ||
        (h.index_bits != 1 && h.index_bits != 4 && h.index_bits != 8) ||
        h.tile_w == 0 || h.tile_h == 0 || h.seg_count == 0) {
        TAL_PR_ERR("tef: bad header fields");
        return OPRT_INVALID_PARM;
    }
    if (!ext_dst &&
        (h.canvas_w != tuya_display_hw_get_width() ||
         (h.canvas_h != s_panel[0].rows && h.canvas_h != tuya_display_hw_get_height()))) {
        TAL_PR_ERR("tef: canvas %ux%u mismatch display", h.canvas_w, h.canvas_h);
        return OPRT_NOT_SUPPORTED;
    }
    if (h.seg_table_off + (uint32_t)h.seg_count * sizeof(tef_seg) > len ||
        h.palette_off + (uint32_t)h.ncolors * 2 > len) {
        TAL_PR_ERR("tef: truncated file");
        return OPRT_INVALID_PARM;
    }

    c->hdr = h;
    c->segs = (const tef_seg *)(data + h.seg_table_off);
    c->ntx = (h.canvas_w + h.tile_w - 1) / h.tile_w;
    c->nty = (h.canvas_h + h.tile_h - 1) / h.tile_h;
    c->ntiles = c->ntx * c->nty;
    c->bmb = (c->ntiles + 7) / 8;
    c->frame_ms = 1000 / ((h.fps > 0) ? h.fps : TEF_DEFAULT_FPS);
    if (c->frame_ms == 0) {
        c->frame_ms = 1;
    }
    c->jpeg_warned = FALSE;

    /* 调色板拷到 RAM；SPI 8bit 面板需整体字节交换，在此一次做完，
     * 解码热路径查表即得最终字节序 */
    memcpy(c->pal, data + h.palette_off, (size_t)h.ncolors * 2);
#if defined(TEF_RGB565_BYTE_SWAP) && TEF_RGB565_BYTE_SWAP
    for (i = 0; i < h.ncolors; i++) {
        c->pal[i] = (uint16_t)((c->pal[i] >> 8) | (c->pal[i] << 8));
    }
#endif

    /* P1 流式解码：段明文不再整块落地。deflate 段只需 1<<window_bits 环形
     * 字典（tinfl 环要求 2 的幂，编码端保证回溯距离不超窗口）；
     * (v3 头的 max_seg_decomp 供无流式解码器的整段解压实现使用，本实现忽略) */
    {
        int need_dict = 0;

        for (i = 0; i < h.seg_count; i++) {
            tef_seg sg;
            memcpy(&sg, &c->segs[i], sizeof(sg));
            if (sg.method == TEF_SEG_PALETTE && sg.comp == TEF_COMP_DEFLATE) {
                need_dict = 1;
                break;
            }
        }
        if (need_dict) {
            if (h.window_bits < 9 || h.window_bits > 15) {
                TAL_PR_ERR("tef: bad deflate window_bits %u (9..15)", h.window_bits);
                return OPRT_INVALID_PARM;
            }
            c->dict_size = 1u << h.window_bits;
        }
        if ((need_dict &&
             buf_reserve((void **)&c->dict, &c->dict_cap, c->dict_size) != OPRT_OK) ||
            buf_reserve((void **)&c->bmp, &c->bmp_cap, (uint32_t)c->bmb) != OPRT_OK ||
            buf_reserve((void **)&c->unp, &c->unp_cap,
                        (uint32_t)h.canvas_w * h.canvas_h) != OPRT_OK ||
            (!ext_dst &&
             buf_reserve((void **)&c->shadow, &c->shadow_cap,
                         (uint32_t)h.canvas_w * h.canvas_h * 2) != OPRT_OK)) {
            TAL_PR_ERR("tef: psram alloc failed");
            return OPRT_MALLOC_FAILED;
        }
        if (!ext_dst) {
            /* 新素材从关键帧起步，影子缓存清零即为首帧前的确定底色 */
            memset(c->shadow, 0, (uint32_t)h.canvas_w * h.canvas_h * 2);
            c->dst = c->shadow;              /* buf_reserve 可能换过地址，每次重指 */
            c->dst_stride = h.canvas_w;
        }
    }

    c->data = data;
    c->len = len;
    TAL_PR_NOTICE("tef: load %ux%u %u frames fps=%u tile=%ux%u colors=%u segs=%u",
                  h.canvas_w, h.canvas_h, h.frame_count, h.fps,
                  h.tile_w, h.tile_h, h.ncolors, h.seg_count);
    return OPRT_OK;
}

/* ------------------------------------------------------------------------ */
/* 刷屏                                                                      */
/* ------------------------------------------------------------------------ */
/* 提交一个 panel 的整行带（不等待完成），返回是否成功入队 */
static int band_submit(const tef_panel_t *p, uint8_t idx, uint16_t y_local,
                       uint16_t rows, uint16_t w, uint16_t *pixels)
{
    ty_frame_buffer_t *d = &s_desc[idx];

    memset(d, 0, sizeof(*d));
    d->type = TYPE_PSRAM;
    d->fmt = TY_PIXEL_FMT_RGB565;
    d->x_start = 0;
    d->y_start = y_local;                  /* panel 内坐标；GRAM 偏移由驱动加 */
    d->width = w;
    d->height = rows;
    d->len = (uint32_t)w * rows * 2;
    d->frame = (uint8_t *)pixels;
    d->free_cb = flush_done_cb;
    return (tal_display_flush(p->handle, d) == OPRT_OK) ? 1 : 0;
}

/* 整行带推送。SPI2/SPI3 是独立 QSPI 控制器 + 独立 DMA 通道：镜像素材以同一
 * 影子缓冲并行喂两路（只读无冲突），双眼素材按 panel 切片并行——先全部提交
 * 再统一等完成，把单帧写屏窗口压到最小（写窗口越长，与面板自扫描拍出的
 * 撕裂/拖影越明显）。描述符与影子缓冲在途期间被驱动持有，等齐 free_cb 再返回。 */
static void flush_band(const tef_ctx_t *c, uint16_t y, uint16_t rows)
{
    const tef_header *h = &c->hdr;
    int pending = 0;
    uint8_t i;
#if TEF_PLAYER_STATS
    uint32_t t0 = (uint32_t)tal_system_get_millisecond();
#endif

    if (h->canvas_h <= s_panel[0].rows) {
        /* 单眼素材：镜像到所有 panel（两只眼睛播同一动画） */
        for (i = 0; i < s_panel_cnt; i++) {
            pending += band_submit(&s_panel[i], i, y, rows, h->canvas_w,
                                   c->shadow + (uint32_t)y * h->canvas_w);
        }
    } else {
        for (i = 0; i < s_panel_cnt; i++) {
            uint16_t p0 = s_panel[i].y_off;
            uint16_t p1 = (uint16_t)(p0 + s_panel[i].rows);
            uint16_t ov0 = (y > p0) ? y : p0;
            uint16_t ov1 = ((uint16_t)(y + rows) < p1) ? (uint16_t)(y + rows) : p1;

            if (ov0 >= ov1) {
                continue;
            }
            pending += band_submit(&s_panel[i], i, (uint16_t)(ov0 - p0),
                                   (uint16_t)(ov1 - ov0), h->canvas_w,
                                   c->shadow + (uint32_t)ov0 * h->canvas_w);
        }
    }
    while (pending-- > 0) {
        tal_semaphore_wait(s_flush_sem, SEM_WAIT_FOREVER);
    }
#if TEF_PLAYER_STATS
    {
        uint32_t dt = (uint32_t)tal_system_get_millisecond() - t0;
        if (dt > s_stat.flush_max) {
            s_stat.flush_max = dt;
        }
        s_stat.flush_sum += dt;
        s_stat.rows_sum += rows;
    }
#endif
}

/* ------------------------------------------------------------------------ */
/* 单帧流式渲染：从流拉取位图与打包索引，脏 tile 解到影子帧缓存               */
/* ------------------------------------------------------------------------ */
#define TILE_DIRTY(bmp, ti) ((bmp)[(ti) >> 3] & (1 << ((ti) & 7)))

static int render_frame_stream(tef_ctx_t *c, tef_stream_t *s)
{
    const tef_header *h = &c->hdr;
    uint8_t chunk[512];
    uint32_t dpx = 0, pkb, kk = 0, px_done = 0, done_bytes = 0;
    int ti, tx, ty;

    /* 1. 脏块位图 */
    if (stream_pull(s, c->bmp, (size_t)c->bmb) < 0) {
        return -1;
    }
    for (ti = 0; ti < c->ntiles; ti++) {
        if (TILE_DIRTY(c->bmp, ti)) {
            int x0 = (ti % c->ntx) * h->tile_w, y0 = (ti / c->ntx) * h->tile_h;
            int x1 = (x0 + h->tile_w < h->canvas_w) ? x0 + h->tile_w : h->canvas_w;
            int y1 = (y0 + h->tile_h < h->canvas_h) ? y0 + h->tile_h : h->canvas_h;
            dpx += (uint32_t)(x1 - x0) * (y1 - y0);
        }
    }
    pkb = (h->index_bits == 8) ? dpx
        : (h->index_bits == 4) ? (dpx + 1) / 2
                               : (dpx + 7) / 8;

    /* 2. 打包索引分块拉取 + 解包（块按字节切：4/1bit 每字节含整数像素，
     *    任意字节边界切块相位都不碎裂） */
    while (done_bytes < pkb) {
        uint32_t m = pkb - done_bytes;
        uint32_t px;

        if (m > sizeof(chunk)) {
            m = sizeof(chunk);
        }
        if (stream_pull(s, chunk, m) < 0) {
            return -1;
        }
        px = (h->index_bits == 8) ? m
           : (h->index_bits == 4) ? m * 2
                                  : m * 8;
        if (px > dpx - px_done) {
            px = dpx - px_done;
        }
        unpack_indices(chunk, h->index_bits, c->unp + px_done, px);
        px_done += px;
        done_bytes += m;
    }

    /* 3. 脏 tile 解到影子帧缓存（kk 消费顺序 = 流内 tile 行主序） */
    for (ty = 0; ty < c->nty; ty++) {
        int y0 = ty * h->tile_h;
        int y1 = (y0 + h->tile_h < h->canvas_h) ? y0 + h->tile_h : h->canvas_h;
        int band_h = y1 - y0;

        for (tx = 0; tx < c->ntx; tx++) {
            int x0, tw, yy, xx;

            if (!TILE_DIRTY(c->bmp, ty * c->ntx + tx)) {
                continue;
            }
            x0 = tx * h->tile_w;
            tw = ((x0 + h->tile_w < h->canvas_w) ? x0 + h->tile_w : h->canvas_w) - x0;
            /* 流内像素按 tile 依次排列（tile 内行主序） */
            for (yy = 0; yy < band_h; yy++) {
                uint16_t *dst = c->dst + (uint32_t)(y0 + yy) * c->dst_stride + x0;
                for (xx = 0; xx < tw; xx++) {
                    dst[xx] = c->pal[c->unp[kk++]];
                }
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* 播放线程                                                                  */
/* ------------------------------------------------------------------------ */
static void player_entry(void *arg)
{
    static tef_ctx_t ctx;      /* 单实例；static 避免占线程栈 */
    uint32_t seq_seen = 0;
    uint32_t next_ms = 0;

    (void)arg;
    tal_system_sleep(TEF_START_DELAY_MS);

    for (;;) {
        uint16_t s;

        if (s_req_seq != seq_seen) {
            const uint8_t *data;
            uint32_t len;

            tal_mutex_lock(s_lock);
            data = s_req_data;
            len = s_req_len;
            seq_seen = s_req_seq;
            tal_mutex_unlock(s_lock);

            if (ctx_load(&ctx, data, len, FALSE) != OPRT_OK) {
                ctx.data = NULL;
            }
            next_ms = tal_system_get_millisecond();
        }
        if (ctx.data == NULL) {
            tal_system_sleep(TEF_IDLE_POLL_MS);
            continue;
        }

        /* 播一整遍（所有段），帧边界检查切换请求；播完回绕（帧 0 是关键帧） */
        for (s = 0; s < ctx.hdr.seg_count && s_req_seq == seq_seen; s++) {
            tef_seg sg;

            memcpy(&sg, &ctx.segs[s], sizeof(sg));
            if (sg.method == TEF_SEG_JPEG) {
                /* 设备端不解 JPEG 段（素材生成已禁用）；保持画面并按时长推进 */
                if (!ctx.jpeg_warned) {
                    TAL_PR_WARN("tef: jpeg segment skipped (unsupported on device)");
                    ctx.jpeg_warned = TRUE;
                }
                next_ms += ctx.frame_ms * sg.frame_count;
                continue;
            }

            /* P1 流式：按段开流，逐帧拉取解码——段明文不落地，解压开销
             * 均摊进每帧（无段边界大解压，双槽预取机制随之退役） */
            if (sg.offset + sg.comp_len > ctx.len) {
                TAL_PR_ERR("tef: seg %u out of range", s);
                ctx.data = NULL;
                break;
            }
            tef_stream_t stm;
            if (stream_open(&ctx, &stm, &sg, s_tinfl) < 0) {
                TAL_PR_ERR("tef: seg %u stream open failed", s);
                ctx.data = NULL;
                break;
            }
            uint16_t f;
            for (f = 0; f < sg.frame_count && s_req_seq == seq_seen; f++) {
#if TEF_PLAYER_STATS
                uint32_t d0 = (uint32_t)tal_system_get_millisecond();
#endif
                if (render_frame_stream(&ctx, &stm) < 0) {
                    TAL_PR_ERR("tef: seg %u frame %u truncated/corrupt", s, f);
                    ctx.data = NULL;
                    break;
                }
#if TEF_PLAYER_STATS
                {
                    uint32_t dd = (uint32_t)tal_system_get_millisecond() - d0;
                    if (dd > s_stat.decomp_max) {
                        s_stat.decomp_max = dd;   /* 单帧解码(含流式解压)峰值 */
                    }
                }
#endif
                /* 每帧整屏推送（影子缓存全帧维护，随时可整体上屏）：传输时长
                 * 恒定，撕裂缝位置/形态稳定可预期，优于随脏区大小跳变的局部带；
                 * 64KB 整帧 @48MHz 双 panel 并行约 5.5ms，远小于帧预算。
                 * 空帧（无脏块）也照推，保持节拍与观感一致。 */
                flush_band(&ctx, 0, ctx.hdr.canvas_h);

                next_ms += ctx.frame_ms;
                {
                    uint32_t now = tal_system_get_millisecond();
                    int32_t delay = (int32_t)(next_ms - now);
#if TEF_PLAYER_STATS
                    if (s_stat.last_ts != 0) {
                        uint32_t itv = now - s_stat.last_ts;
                        s_stat.itv_sum += itv;
                        if (itv > s_stat.itv_max) {
                            s_stat.itv_max = itv;
                        }
                        s_stat.frames++;
                    }
                    s_stat.last_ts = now;
                    if (s_stat.win_start == 0) {
                        s_stat.win_start = now;
                    }
                    if (now - s_stat.win_start >= TEF_STAT_PERIOD_MS && s_stat.frames > 0) {
                        TAL_PR_NOTICE("[TEF-STAT] frames=%u itv(avg/max)=%u/%ums "
                                      "flush(avg/max)=%u/%ums dec_max=%ums rows_avg=%u",
                                      s_stat.frames,
                                      s_stat.itv_sum / s_stat.frames, s_stat.itv_max,
                                      s_stat.flush_sum / s_stat.frames, s_stat.flush_max,
                                      s_stat.decomp_max,
                                      s_stat.rows_sum / s_stat.frames);
                        s_stat.frames = 0;
                        s_stat.itv_sum = s_stat.itv_max = 0;
                        s_stat.flush_sum = s_stat.flush_max = 0;
                        s_stat.decomp_max = 0;
                        s_stat.rows_sum = 0;
                        s_stat.win_start = now;
                    }
#endif
                    if (delay > 0) {
                        tal_system_sleep((uint32_t)delay);
                    } else {
                        next_ms = now;   /* 落后就重整节拍，不追帧 */
                    }
                }
            }
            stream_close(&stm);          /* 正常段尾/切素材/错误统一关流 */
            if (ctx.data == NULL) {
                break;
            }
        }
    }
}

/* ------------------------------------------------------------------------ */
/* 对外接口                                                                  */
/* ------------------------------------------------------------------------ */
OPERATE_RET tef_player_init(void)
{
    uint8_t i, n;

    if (s_inited) {
        return OPRT_OK;
    }

    n = tuya_display_hw_get_lcd_count();
    if (n == 0) {
        return OPRT_COM_ERROR;
    }
    if (n > TEF_MAX_PANELS) {
        n = TEF_MAX_PANELS;
    }
    for (i = 0; i < n; i++) {
        if (tuya_display_hw_get_lcd_region(i, &s_panel[i].handle,
                                           &s_panel[i].y_off, &s_panel[i].rows) != OPRT_OK ||
            s_panel[i].handle == NULL) {
            return OPRT_COM_ERROR;
        }
    }
    s_panel_cnt = n;

    s_tinfl = (tinfl_decompressor *)tkl_system_psram_malloc(sizeof(tinfl_decompressor));
    if (s_tinfl == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    OPERATE_RET rt = tal_mutex_create_init(&s_lock);
    if (rt != OPRT_OK) {
        return rt;
    }
    rt = tal_semaphore_create_init(&s_flush_sem, 0, TEF_MAX_PANELS);
    if (rt != OPRT_OK) {
        return rt;
    }

    THREAD_CFG_T cfg = {
        .stackDepth = 8192,
        .priority = THREAD_PRIO_1,
        .thrdname = "tef_player",
    };
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM == 1)
    cfg.psram_mode = 1;
#endif
    rt = tal_thread_create_and_start(&s_thread, NULL, NULL, player_entry, NULL, &cfg);
    if (rt != OPRT_OK) {
        return rt;
    }
    s_inited = TRUE;
    return OPRT_OK;
}

OPERATE_RET tef_player_play(const uint8_t *data, uint32_t len)
{
    if (!s_inited || data == NULL || len == 0) {
        return OPRT_INVALID_PARM;
    }
    tal_mutex_lock(s_lock);
    s_req_data = data;
    s_req_len = len;
    s_req_seq++;
    tal_mutex_unlock(s_lock);
    return OPRT_OK;
}

OPERATE_RET tef_player_play_once(const uint8_t *data, uint32_t len,
                                 uint16_t *dst, uint16_t dst_w, uint16_t dst_h,
                                 uint16_t stride_px,
                                 void (*commit_cb)(void *ud), void *ud)
{
    static tef_ctx_t s_once_ctx;        /* 独立于播放/bench ctx；static 免占调用者线程栈 */
    tef_ctx_t *c = &s_once_ctx;
    OPERATE_RET ret = OPRT_OK;
    uint32_t next_ms;
    uint16_t s, f;

    if (data == NULL || len == 0 || dst == NULL || commit_cb == NULL ||
        dst_w == 0 || dst_h == 0 || stride_px < dst_w) {
        return OPRT_INVALID_PARM;
    }
    /* s_tinfl 单例约束：本函数与播放线程/bench 不得并发（boot 阶段单独使用） */
    if (s_tinfl == NULL) {              /* 允许在 tef_player_init 前单独使用 */
        s_tinfl = (tinfl_decompressor *)tkl_system_psram_malloc(sizeof(tinfl_decompressor));
        if (s_tinfl == NULL) {
            return OPRT_MALLOC_FAILED;
        }
    }
    if (ctx_load(c, data, len, TRUE) != OPRT_OK) {
        ret = OPRT_COM_ERROR;
        goto cleanup;
    }
    if (c->hdr.canvas_w > dst_w || c->hdr.canvas_h > dst_h) {
        TAL_PR_ERR("tef: canvas %ux%u exceeds dst %ux%u",
                   c->hdr.canvas_w, c->hdr.canvas_h, dst_w, dst_h);
        ret = OPRT_NOT_SUPPORTED;
        goto cleanup;
    }
    /* 画布在目标窗口内居中 */
    c->dst = dst + (uint32_t)((dst_h - c->hdr.canvas_h) / 2) * stride_px +
             (dst_w - c->hdr.canvas_w) / 2;
    c->dst_stride = stride_px;

    next_ms = tal_system_get_millisecond();
    for (s = 0; s < c->hdr.seg_count; s++) {
        tef_seg sg;
        tef_stream_t stm;

        memcpy(&sg, &c->segs[s], sizeof(sg));
        if (sg.method == TEF_SEG_JPEG) {
            /* 设备端不解 JPEG 段（素材生成已禁用）；保持画面并按时长推进 */
            TAL_PR_WARN("tef: jpeg segment skipped (unsupported on device)");
            next_ms += c->frame_ms * sg.frame_count;
            continue;
        }
        if (sg.offset + sg.comp_len > c->len) {
            TAL_PR_ERR("tef: seg %u out of range", s);
            ret = OPRT_COM_ERROR;
            goto cleanup;
        }
        if (stream_open(c, &stm, &sg, s_tinfl) < 0) {
            TAL_PR_ERR("tef: seg %u stream open failed", s);
            ret = OPRT_COM_ERROR;
            goto cleanup;
        }
        for (f = 0; f < sg.frame_count; f++) {
            if (render_frame_stream(c, &stm) < 0) {
                TAL_PR_ERR("tef: seg %u frame %u truncated/corrupt", s, f);
                stream_close(&stm);
                ret = OPRT_COM_ERROR;
                goto cleanup;
            }
            commit_cb(ud);
            next_ms += c->frame_ms;
            {
                uint32_t now = tal_system_get_millisecond();
                int32_t delay = (int32_t)(next_ms - now);

                if (delay > 0) {
                    tal_system_sleep((uint32_t)delay);
                } else {
                    next_ms = now;       /* 落后就重整节拍，不追帧 */
                }
            }
        }
        stream_close(&stm);
    }

cleanup:
    /* 一次性场景：工作缓冲用完即还（错误路径同样回收；末帧像素留在外部画布） */
    if (c->dict)   { tkl_system_psram_free(c->dict); }
    if (c->bmp)    { tkl_system_psram_free(c->bmp); }
    if (c->unp)    { tkl_system_psram_free(c->unp); }
    if (c->shadow) { tkl_system_psram_free(c->shadow); }
    memset(c, 0, sizeof(*c));
    return ret;
}

/* ------------------------------------------------------------------------ */
/* 逐帧步进解码器（lv_tef 等外部画布场景）。每实例私有 tinfl(~11KB PSRAM)，
 * 多实例可并存；但所有 tef_dec_* 调用须在同一线程串行（LVGL timer 即满足），
 * 与播放线程/play_once/bench 无共享状态。                                    */
struct tef_dec {
    tef_ctx_t ctx;
    tinfl_decompressor *tinfl;   /* 私有实例，勿用 s_tinfl */
    tef_stream_t stm;
    BOOL_T stm_open;
    uint16_t seg;                /* 当前段游标 */
    uint16_t frame;              /* 段内已解帧数 */
};

void tef_dec_close(tef_dec_t *d)
{
    if (d == NULL) {
        return;
    }
    if (d->stm_open) {
        stream_close(&d->stm);
    }
    if (d->tinfl)    { tkl_system_psram_free(d->tinfl); }
    if (d->ctx.dict) { tkl_system_psram_free(d->ctx.dict); }
    if (d->ctx.bmp)  { tkl_system_psram_free(d->ctx.bmp); }
    if (d->ctx.unp)  { tkl_system_psram_free(d->ctx.unp); }
    /* ext_dst 模式无 shadow；画布归调用方 */
    tkl_system_psram_free(d);
}

OPERATE_RET tef_dec_open(tef_dec_t **out, const uint8_t *data, uint32_t len)
{
    tef_dec_t *d;
    OPERATE_RET rt;

    if (out == NULL) {
        return OPRT_INVALID_PARM;
    }
    *out = NULL;
    d = (tef_dec_t *)tkl_system_psram_malloc(sizeof(tef_dec_t));
    if (d == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    memset(d, 0, sizeof(*d));
    rt = ctx_load(&d->ctx, data, len, TRUE);
    if (rt != OPRT_OK) {
        goto err;
    }
    d->tinfl = (tinfl_decompressor *)tkl_system_psram_malloc(sizeof(tinfl_decompressor));
    if (d->tinfl == NULL) {
        rt = OPRT_MALLOC_FAILED;
        goto err;
    }
    *out = d;
    return OPRT_OK;
err:
    tef_dec_close(d);            /* ctx_load 半途分配的工作缓冲一并回收 */
    return rt;
}

uint16_t tef_dec_width(const tef_dec_t *d)    { return (d != NULL) ? d->ctx.hdr.canvas_w : 0; }
uint16_t tef_dec_height(const tef_dec_t *d)   { return (d != NULL) ? d->ctx.hdr.canvas_h : 0; }
uint32_t tef_dec_frame_ms(const tef_dec_t *d) { return (d != NULL) ? d->ctx.frame_ms : 0; }

OPERATE_RET tef_dec_set_dst(tef_dec_t *d, uint16_t *dst, uint16_t stride_px)
{
    if (d == NULL || dst == NULL || stride_px < d->ctx.hdr.canvas_w) {
        return OPRT_INVALID_PARM;
    }
    d->ctx.dst = dst;
    d->ctx.dst_stride = stride_px;
    return OPRT_OK;
}

OPERATE_RET tef_dec_next(tef_dec_t *d)
{
    uint32_t hops = 0;

    if (d == NULL || d->ctx.data == NULL || d->ctx.dst == NULL) {
        return OPRT_INVALID_PARM;
    }
    for (;;) {
        tef_seg sg;

        if (d->seg >= d->ctx.hdr.seg_count) {       /* 回卷：帧 0 是全脏关键帧 */
            d->seg = 0;
        }
        memcpy(&sg, &d->ctx.segs[d->seg], sizeof(sg));
        if (sg.method == TEF_SEG_JPEG || sg.frame_count == 0) {
            /* 设备端不解 JPEG（素材端已禁用）：跳段保持画面、不补时。
             * hops 防全 JPEG/空段素材死循环 */
            if (sg.method == TEF_SEG_JPEG && !d->ctx.jpeg_warned) {
                TAL_PR_WARN("tef: jpeg segment skipped (unsupported on device)");
                d->ctx.jpeg_warned = TRUE;
            }
            d->seg++;
            if (++hops > d->ctx.hdr.seg_count) {
                TAL_PR_ERR("tef: no decodable segment");
                return OPRT_COM_ERROR;
            }
            continue;
        }
        if (!d->stm_open) {
            if (sg.offset + sg.comp_len > d->ctx.len) {
                TAL_PR_ERR("tef: seg %u out of range", d->seg);
                return OPRT_COM_ERROR;
            }
            if (stream_open(&d->ctx, &d->stm, &sg, d->tinfl) < 0) {
                TAL_PR_ERR("tef: seg %u stream open failed", d->seg);
                return OPRT_COM_ERROR;
            }
            d->stm_open = TRUE;
            d->frame = 0;
        }
        if (render_frame_stream(&d->ctx, &d->stm) < 0) {
            TAL_PR_ERR("tef: seg %u frame %u truncated/corrupt", d->seg, d->frame);
            stream_close(&d->stm);
            d->stm_open = FALSE;
            return OPRT_COM_ERROR;
        }
        d->frame++;
        if (d->frame >= sg.frame_count) {           /* 段尾关流，下次 next 开下一段 */
            stream_close(&d->stm);
            d->stm_open = FALSE;
            d->seg++;
        }
        return OPRT_OK;
    }
}

/* ------------------------------------------------------------------------ */
/* 基准测试钩子（TEF_BENCH=1 编译）：复用上方真实解码路径（流式拉取 + 帧渲染
 * 到影子缓存），不刷屏；独立 ctx，跑完释放全部工作缓冲。                     */
/* ------------------------------------------------------------------------ */
#if defined(TEF_BENCH) && (TEF_BENCH == 1)
#include "bench/tef_bench.h"

OPERATE_RET tef_bench_decode(const uint8_t *data, uint32_t len, int loops,
                             tef_bench_stat_t *st)
{
    static tef_ctx_t bctx;              /* 独立于播放 ctx；static 免占栈 */
    OPERATE_RET ret = OPRT_OK;
    int l;
    uint16_t s, f;
    uint32_t t0, f0, d;

    memset(st, 0, sizeof(*st));
    if (s_tinfl == NULL) {              /* 允许在 tef_player_init 前单独跑 */
        s_tinfl = (tinfl_decompressor *)tkl_system_psram_malloc(sizeof(tinfl_decompressor));
        if (s_tinfl == NULL) {
            return OPRT_MALLOC_FAILED;
        }
    }
    if (ctx_load(&bctx, data, len, FALSE) != OPRT_OK) {
        return OPRT_COM_ERROR;
    }
    st->w = bctx.hdr.canvas_w;
    st->h = bctx.hdr.canvas_h;
    st->fps = bctx.hdr.fps;
    st->frames_per_loop = bctx.hdr.frame_count;
    /* 常驻工作内存（精确）：影子帧缓存 + 流式字典 + 索引/位图缓冲 + tinfl + 调色板 */
    st->mem_bytes = bctx.shadow_cap + bctx.dict_cap + bctx.unp_cap + bctx.bmp_cap +
                    sizeof(tinfl_decompressor) + sizeof(bctx.pal);

    t0 = (uint32_t)tal_system_get_millisecond();
    for (l = 0; l < loops; l++) {
        for (s = 0; s < bctx.hdr.seg_count; s++) {
            tef_seg sg;
            tef_stream_t stm;

            memcpy(&sg, &bctx.segs[s], sizeof(sg));
            if (sg.method != TEF_SEG_PALETTE) {
                continue;
            }
            if (sg.offset + sg.comp_len > bctx.len ||
                stream_open(&bctx, &stm, &sg, s_tinfl) < 0) {
                ret = OPRT_COM_ERROR;
                goto cleanup;
            }
            f0 = (uint32_t)tal_system_get_millisecond();
            for (f = 0; f < sg.frame_count; f++) {
                if (render_frame_stream(&bctx, &stm) < 0) {
                    stream_close(&stm);
                    ret = OPRT_COM_ERROR;
                    goto cleanup;
                }
                st->frames++;
                d = (uint32_t)tal_system_get_millisecond() - f0;
                if (d > st->max_frame_ms) {
                    st->max_frame_ms = d;
                }
                f0 = (uint32_t)tal_system_get_millisecond();
            }
            stream_close(&stm);
        }
    }
    st->total_ms = (uint32_t)tal_system_get_millisecond() - t0;

cleanup:
    /* 统一出口释放基准 ctx 的工作缓冲——错误路径同样回收（play ctx 不受影响） */
    if (bctx.dict)   { tkl_system_psram_free(bctx.dict); }
    if (bctx.bmp)    { tkl_system_psram_free(bctx.bmp); }
    if (bctx.unp)    { tkl_system_psram_free(bctx.unp); }
    if (bctx.shadow) { tkl_system_psram_free(bctx.shadow); }
    memset(&bctx, 0, sizeof(bctx));
    return ret;
}
#endif /* TEF_BENCH */
