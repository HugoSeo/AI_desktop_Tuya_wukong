# TEF v3 设计方案：ROM/RAM 双模式 + 自适应解码 + 防撕裂

> 目标：一套格式、一个解码器二进制，编码时选 **ROM 优先** 或 **RAM 优先**，解码时按文件头**自适应**分配缓冲与选路；同时给出屏幕撕裂的分层解决。
> 本方案的所有取舍数字均来自 128 眼睛包（12 表情 / 631 帧 / 128×128）与 Emote Pack 的实测。

---

## 1. 背景：一句话抓住整个取舍

前面所有实测收敛成一个因果链：

**ROM 与 RAM 的对立，本质由"压缩器窗口大小"决定；而"流式解码"把 RAM 从 SEG（成段大小）里解耦出来。**

| 压缩器 | 窗口 | 跨帧冗余捕捉 | ROM | 流式解码 RAM |
|---|---|---|---|---|
| deflate | 32 KB | 强（远距） | **小** | ~40 KB（32KB 窗 + 8.2KB tinfl 状态） |
| heatshrink | 2 KB | 弱（局部） | 大 | **~3 KB**（2KB 窗 + 一个 tile） |

眼睛包实测：deflate 整包 473 KB；heatshrink(win=11) 739 KB（+56%）。ROM 惩罚强内容依赖，另一套 Emote Pack 上高达 +238%——**编码时必须拿目标素材实测，不能拍脑袋**。

结论：不存在"既最省 ROM 又最省 RAM"的单点，只有一条帕累托前沿。v3 的做法是**把前沿上的两个端点做成可选模式**，让产品按板子约束（有无 PSRAM）在编码时二选一，解码器自动适配。

---

## 2. 两种编码模式

### 2.1 ROM 优先（默认，适合有 PSRAM / SRAM 中等的板子）

- 压缩器：**deflate**，`window_bits = 15`（32 KB）
- 分段：SEG 尽量大（流式后 RAM 与 SEG 无关，故只为压缩比服务；建议 SEG=30~60 或按整片自适应）
- 解码：**流式 deflate**，工作 RAM ~40 KB
- 眼睛包实测：**整包 473 KB / 40 KB RAM / 解码最快（~0.7ms 级）**

### 2.2 RAM 优先（适合裸 SRAM、无 PSRAM、RAM 是硬墙的板子）

- 压缩器：**heatshrink**，`window_bits` 可调（11=2KB → 3KB RAM；13=8KB → 9KB RAM）
- 分段：SEG 可保持中等（RAM 由窗口决定，不再受 SEG 约束；小 SEG 利于段内随机访问）
- 解码：**流式 heatshrink**，工作 RAM ~3 KB（逼近 EAF 的 4KB）
- 眼睛包实测：**整包 739 KB / 3 KB RAM / 解码 ~5.6× deflate（但每帧仅 0.03ms，绝对无感）**

### 2.3 编码器接口

```
tef_encode <in.gif> <out.tef> --mode rom            # ROM 优先
tef_encode <in.gif> <out.tef> --mode ram            # RAM 优先(默认 win=11)
tef_encode <in.gif> <out.tef> --target-ram 9        # 按目标 RAM(KB)反推 window_bits
tef_encode <in.gif> <out.tef> --mode auto           # 两种都编,报告 ROM/RAM 供决策
```

> 重要约束：一个产品的整套表情库**应统一用同一模式/窗口**。因为解码器要按"库内最大窗口"预留缓冲；若混用，等于按 deflate 的 40KB 预留，RAM 优先就白做了。

---

## 3. 格式变更（header v3）

保持 magic `"TEF1"`，`version` 升到 **3**。在现有 header 的保留位/尾部追加 4 个字段（都是 1~4 字节，向后兼容）：

```c
typedef struct __attribute__((packed)) {
    char     magic[4];        // "TEF1"
    uint16_t version;         // = 3
    uint16_t canvas_w, canvas_h;
    uint8_t  tile_w, tile_h;
    uint16_t frame_count;
    uint16_t fps;
    uint16_t ncolors;
    uint8_t  index_bits;
    uint8_t  flags;           // 含 alpha_mode 等(沿用)
    uint16_t seg_count;
    // ---- v3 新增 ----
    uint8_t  mode;            // 0=ROM_PRIORITY 1=RAM_PRIORITY
    uint8_t  window_bits;     // 压缩器窗口: deflate=15, hs=11/13...  解码器据此定缓冲
    uint8_t  flush_policy;    // 0=immediate 1=TE_gated 2=double_buffer
    uint8_t  _rsvd;
    uint32_t max_seg_decomp;  // 最大段解压后字节数(整段解压回退路径用;流式忽略)
    uint32_t palette_off, seg_table_off;
} tef_header_v3_t;
```

- 每段的 `comp` 字段（0=heatshrink / 1=deflate）**沿用现有**，不动。
- `window_bits`：解码器分配缓冲的唯一依据。ROM 模式写 15，RAM 模式写 11/13。
- `max_seg_decomp`：给"整段解压"回退路径（无流式解码器时）用；流式解码器只看 `window_bits`。
- v1/v2 老文件：解码器按 `mode=ROM, window=15, comp=deflate, 整段解压` 兜底，行为不变。

---

## 4. 自适应解码器（一个二进制，缓冲按文件头定）

### 4.1 核心：拉取式流水（pull-based streaming）

解码器不再"整段 inflate 后遍历"，而是把压缩流当作字节水管，解出一个 tile 就画一个 tile。

```c
// 统一的流读取接口: 底层是 deflate 或 heatshrink 的增量解码器
typedef struct {
    uint8_t  comp;            // 来自 header
    void    *dctx;            // tinfl_decompressor* 或 heatshrink_decoder*
    uint8_t *dict;            // deflate: 32KB 环形字典; hs: NULL(解码器自带窗口)
    const uint8_t *src;       // mmap flash 里的压缩段(零拷贝)
    size_t   src_len, src_pos;
} tef_stream_t;

// 从流里拉 n 字节到 out(跨解压边界自动续); 返回实际拉到的字节
size_t tef_pull(tef_stream_t *s, uint8_t *out, size_t n);
```

`decode_frame` 变成状态机，逻辑与现在一致，只是数据来源从"blob 指针算术"换成 `tef_pull()`：

```c
tef_pull(s, dirty_bitmap, bmb);              // 1. 脏块位图
int nd = popcount(dirty_bitmap, bmb);
if (alpha) tef_pull(s, partial_bitmap, ...); // 2. 部分位图(有 alpha 时)
for (each dirty tile t, 按 y 升序) {          // 3. 逐脏 tile
    tef_pull(s, tile_buf, tile_bytes);       //    只拉一个 tile
    (palette) lut_expand(tile_buf -> rgb);
    enqueue_flush(t.x, t.y, tile_buf);       //    进推屏队列(见 §5)
}
```

RAM 常驻 = 解码器窗口（由 `window_bits` 定）+ 一个 `tile_buf` + 当前帧 `dirty_bitmap`。整段 / 整帧从不落地。

### 4.2 自适应分配（"auto"的落点）

加载时读 header，一个函数算出精确工作集，供 App 静态分配或校验预算：

```c
size_t tef_decoder_working_ram(const tef_header_v3_t *h) {
    size_t win  = (h->comp == COMP_DEFLATE) ? (1u << h->window_bits) : 0;  // deflate 需环形字典
    size_t dctx = (h->comp == COMP_DEFLATE) ? sizeof(tinfl_decompressor) // ~8.2KB
                                            : heatshrink_decoder_sz(h->window_bits); // hs 自带窗口
    size_t tile = h->tile_w * h->tile_h * bytes_per_px(h);
    size_t bmap = tile_count(h) / 8;
    return win + dctx + tile + bmap + STAGING_SLACK;
}
```

- ROM 文件（deflate/win15）→ 返回 ~40 KB，走 tinfl + 32KB 环形字典路径。
- RAM 文件（hs/win11）→ 返回 ~3 KB，走 heatshrink 路径。
- **同一份解码器代码**，仅按 `comp`/`window_bits` 选路 + 定缓冲。这就是"解码时自适应"。

> 若设备要播放混合模式的多个文件：用 `max_i(tef_decoder_working_ram(header_i))` 预留（会退化到 40KB）。故仍推荐库内统一模式。

---

## 5. 屏幕撕裂方案（分层）

TEF 是**脏 tile 替换**，写屏是散布的小块（不像 EAF 整帧自上而下扫），因此比 EAF 更依赖同步。方案分三层，`flush_policy` 字段告诉板级集成走哪条。

### 5.1 解码-推屏解耦（流水线，尤其 RAM 模式必需）

RAM 模式 heatshrink 解码慢 5.6×，若"解码+推屏"都压在 TE 窗口内，时间会紧。做**提前一帧解码**：

```
显示线程:  [显示帧 N] ──TE──> [推帧 N 的脏 tile]
解码线程:  [解帧 N+1 到脏 tile 队列] ← 与显示并行,不在 TE 关键路径上
```

慢的解压挪出 TE 临界区；TE 时刻只做"把队列里的脏 tile blit 到 GRAM"这种纯拷贝。

### 5.2 有 GRAM 的屏（SPI/QSPI，如 GC9A01/ST7789）：TE 门控 + 追扫描线

- 等面板 **TE（消隐）脉冲**再开始 blit。
- **脏 tile 按 y 升序排序后一次性突发写**——即使 tile 散布，写入光标也跟着扫描方向自上而下推进，始终追在扫描线之后（写得比刷新快），扫到哪行哪行已是新帧 → 不撕裂。
- 脏区仅 5~12%，突发极快（128×128 满帧 32KB 都亚毫秒，脏区更快），远小于 ~16ms 刷新周期，竞速轻松赢。
- `flush_policy = TE_gated`。

### 5.3 无 GRAM 的屏（RGB 并口）：双缓冲 + 增量补偿

坑：TEF 只写脏 tile，而双缓冲的两块 buffer 各自只有一半历史。三选一：

- **B1 全帧拷贝双缓冲**：vsync 交换后 `memcpy(前→后)`，再在后缓冲叠加脏 tile。简单，代价是每帧一次全帧拷贝带宽。
- **B2 双写补偿**（推荐）：记录最近两帧的脏 tile 并集，把当前脏 tile 同时写进两块 buffer，保证交换后内容一致。省掉全帧拷贝，只多写少量 tile。
- **B3 单缓冲追扫描线**：无双缓冲，写入光标追在扫描线后（同 §5.2 思路，但目标是 RAM/PSRAM framebuffer 而非 GRAM）。最省 RAM，需写快于扫描。
- `flush_policy = double_buffer`。

### 5.4 底层同步（沿用 gfx 层，防的是"缓冲被 DMA 半途覆写"）

- 异步 flush + 完成回调（`flush_ready`）：一块 DMA 传输未完不复用其 buffer。
- 双缓冲时 `swap_act_buf` 传输完成后再切显示。
- 这层与 §5.2/5.3 正交：TE 防的是"扫描-写入撞车"，flush_ready 防的是"RAM 缓冲被破坏"。

---

## 6. 实施路线（分阶段，风险递增）

| 阶段 | 内容 | 收益 | 兼容性 |
|---|---|---|---|
| **P0** | 编码器加"按 RAM 预算自适应封顶 SEG" | 零解码改动，整段解压峰值 131KB→~50KB | 格式不变 |
| **P1** | 解码器改拉取式流水 + 流式 deflate（32KB 环形字典） | RAM 与 SEG 解耦；ROM 模式落到 40KB，现有文件字节兼容、免重编 | **格式不变**（comp 字段已支持） |
| **P2** | 加 header v3 字段 + 流式 heatshrink 路径 + `--mode ram/--target-ram` | RAM 优先模式（~3KB），编码时可选、解码时自适应 | version→3，老文件兜底 |
| **P3** | 撕裂方案：解码-推屏流水线 + TE 门控 + 双缓冲补偿 | 两种模式都跑得稳、无撕裂 | 板级集成 |
| **P4（可选）** | 全局 tile 去重（内容寻址 tile 字典 + 帧内 tile 索引） | 把跨帧冗余在格式层消掉，让 heatshrink 小窗也逼近 deflate 比值 → **ROM 与 RAM 前沿两点合并** | version→4 |

> P1 是性价比最高的一步（免重编、免格式改动就把 RAM 砍到 40KB）。P4 是长期最优方向：如果 tile 重复率高（表情类大概率如此），去重后 RAM 优先模式也能拿到接近 deflate 的 ROM，从根上化解对立。

---

## 附：眼睛包实测（12 表情 / 631 帧 / 128×128）

| 方案 | 整包 ROM | 解码工作 RAM（有 GRAM） | 解码 CPU | 帧随机访问 |
|---|---|---|---|---|
| **TEF deflate 流式（ROM 优先）** | **473 KB** | ~40 KB | 最快 | 段内顺序 |
| TEF deflate 整段解压（现状） | 473 KB | 74–131 KB | 最快 | 段内顺序 |
| GIF | 739 KB | ~52 KB（强制 MCU） | 最慢 | 差 |
| **TEF heatshrink 流式（RAM 优先）** | **739 KB** | **~3 KB** | 中（每帧仍 0.03ms） | 段内顺序 |
| EAF | 2616 KB | ~4 KB | 中 | 好（独立帧） |

帕累托前沿仅 { ROM 优先, RAM 优先 } 两点；GIF、TEF 整段解压、EAF 均被支配（EAF 唯一保留"帧随机跳段"这一功能优势）。
