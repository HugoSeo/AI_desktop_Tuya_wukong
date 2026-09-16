/*
 * TEF (Tuya Emotion Format) — on-disk format.
 *
 * File layout:
 *   [tef_header: 38 bytes (v3)]
 *   [global palette: ncolors * u16]       RGB565, at palette_off
 *   [segment payloads ...]
 *   [segment table: seg_count * tef_seg]  at seg_table_off
 *
 * Segment (contiguous run of frames), one of:
 *   PALETTE (method 0): a run of palette frames as ONE entropy stream
 *     (heatshrink/deflate). Decompressed blob, per frame in order:
 *        [dirty bitmap: ceil(n_tiles/8) bytes]   (bit set => tile changed)
 *        [packed indices of dirty tiles]         (byte-aligned per frame,
 *                                                 index_bits = 1/4/8)
 *     => bitmap carried INSIDE the compressed stream.
 *   JPEG (method 1): one frame; payload = [u16 x][u16 y][u16 w][u16 h][JPEG].
 *
 * tile_w/tile_h chosen per file by the encoder (adaptive).
 * Frame 0 fully dirty (keyframe). replace semantics: payload = NEW pixels of
 * dirty regions; unchanged regions retained by display (GRAM or self-cache).
 */
#ifndef TEF_FORMAT_H
#define TEF_FORMAT_H
#include <stdint.h>

#define TEF_MAGIC   "TEF1"
#define TEF_VERSION 3u
#define TEF_SEG_PALETTE 0
#define TEF_SEG_JPEG    1

/* v3: 编码模式(帕累托前沿两端点,见 TEF v3 设计) */
#define TEF_MODE_ROM_PRIORITY 0    /* deflate/win15: ROM 最小,解码 RAM 较大 */
#define TEF_MODE_RAM_PRIORITY 1    /* heatshrink 小窗: ROM 较大,解码 RAM ~KB 级 */

/* v3: 刷屏策略建议(板级集成参考;EYES 无 TE 引脚固定用 immediate+整屏) */
#define TEF_FLUSH_IMMEDIATE     0
#define TEF_FLUSH_TE_GATED      1
#define TEF_FLUSH_DOUBLE_BUFFER 2
#define TEF_COMP_HEATSHRINK 0
#define TEF_COMP_DEFLATE    1

#pragma pack(push, 1)
/* v3 头（38 bytes）——本代码库仅支持 v3；旧 v1 素材用 tools/tef_encoder.py
 * 重新编码即可（GIF 源在手时直接 gen_assets.py 一键重生成）。 */
typedef struct {                 /* 38 bytes */
    char     magic[4];           /* "TEF1" */
    uint16_t version;            /* = 3 */
    uint16_t canvas_w;
    uint16_t canvas_h;
    uint8_t  tile_w;             /* adaptive */
    uint8_t  tile_h;
    uint16_t frame_count;
    uint16_t fps;
    uint16_t ncolors;            /* <=256; 0 if JPEG-only */
    uint8_t  index_bits;         /* 1 / 4 / 8 */
    uint8_t  flags;              /* bit0: RGB888 output */
    uint16_t seg_count;
    uint8_t  mode;               /* TEF_MODE_* */
    uint8_t  window_bits;        /* 压缩窗口位数。deflate: 9~15(默认 14)，解码端
                                  * 环形字典 = 1<<window_bits; heatshrink: 11/13... */
    uint8_t  flush_policy;       /* TEF_FLUSH_*（板级集成建议） */
    uint8_t  _rsvd;
    uint32_t max_seg_decomp;     /* 全文件最大段解压后字节数（供整段解压类
                                  * 实现精确分配；本流式播放器忽略） */
    uint32_t palette_off;
    uint32_t seg_table_off;
} tef_header;

typedef struct {                 /* 16 bytes */
    uint8_t  method;             /* TEF_SEG_* */
    uint8_t  comp;               /* palette seg: TEF_COMP_* */
    uint16_t frame_start;
    uint16_t frame_count;
    uint16_t _r;
    uint32_t offset;
    uint32_t comp_len;
} tef_seg;
#pragma pack(pop)

#endif
