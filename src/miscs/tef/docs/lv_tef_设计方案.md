# lv_tef 设计方案 — LVGL 8 播放 TEF 动画

- 日期：2026-07-22
- 状态：设计已确认，待实现
- 前置阅读：`TEF_v3_设计方案.md`（格式与流式解码）、`docs/howto/use-tef-animation.md`（现有两条通路）

## 1. 背景与目标

TEF 现有两条播放通路都绕开 LVGL：

| 通路 | 场景 | 模型 |
|---|---|---|
| `tef_player_init/play` | EYES 无 UI 板 | 解码线程 + 脏块直推 GRAM |
| `tef_player_play_once` | boot splash | 阻塞式逐帧解到外部画布 |

目标：新增**通用 LVGL 控件 `lv_tef`**，任何 LVGL 页面可以像 `lv_gif` 一样即放即用地播放 TEF 动画（表情、待机动画等），吃到 TEF 相对 GIF 的 CPU（约 1/6）与 flash（约 0.7×）优势。

范围收敛（YAGNI，本期不做）：

- 只做**循环播放**：`set_src` 即播、删控件即停。不做 pause/resume/单次播放/播完事件。
- 素材仅支持 **flash 常量数组**（`LV_IMG_SRC_VARIABLE` 语义）；不做文件系统路径加载。
- 不走 `lv_img_decoder` 注册（LVGL 8 decoder 无动画语义，`lv_gif` 同样没走）。

## 2. 方案总览

照抄 `lv_gif` 的结构：**派生自 `lv_img` 的控件 + 自有 RGB565 画布 + `lv_timer` 按素材 fps 驱动逐帧解码 + invalidate**。

分两层实现：

1. **tef 侧**（`src/miscs/tef/`）：从 `tef_player.c` 现有内部件（`ctx_load` 外部画布模式、`stream_*` 拉取式解压、`render_frame_stream`）包出一个**逐帧步进解码器 API**（`tef_dec_*`），即把 `play_once` 的 for 循环拆成可重入状态机。
2. **UI 侧**（`src/ui/widgets/lv_tef.[ch]`）：`lv_tef` 控件，持有画布与解码器句柄，timer 回调里步进一帧。

## 3. tef 侧：逐帧解码器 API

声明加在 `tef_player.h`，实现加在 `tef_player.c`（内部复用其 static 函数，不拆新文件）：

```c
typedef struct tef_dec tef_dec_t;                     /* 不透明句柄 */

OPERATE_RET tef_dec_open(tef_dec_t **out, const uint8_t *data, uint32_t len);
uint16_t    tef_dec_width(const tef_dec_t *d);        /* 素材画布宽 */
uint16_t    tef_dec_height(const tef_dec_t *d);
uint32_t    tef_dec_frame_ms(const tef_dec_t *d);     /* 帧间隔（全文件统一 fps） */
OPERATE_RET tef_dec_set_dst(tef_dec_t *d, uint16_t *dst, uint16_t stride_px);
OPERATE_RET tef_dec_next(tef_dec_t *d);               /* 解下一帧到 dst；末帧后自动回卷循环 */
void        tef_dec_close(tef_dec_t *d);
```

### 3.1 结构与语义

`tef_dec_t`（PSRAM 动态分配）：

- `tef_ctx_t ctx` — 以 `ext_dst=TRUE` 走 `ctx_load`：跳过屏幕尺寸校验、不分配 shadow（LVGL 画布本身跨帧常驻，天然满足 TEF「未变化区域由显示端保持」的 replace 语义）。
- 段游标 `seg` / 段内帧游标 `frame` / 当前打开的 `tef_stream_t`（段内帧共享一条压缩流，跨 `next` 调用保持打开）。
- **私有 `tinfl_decompressor`**（PSRAM，约 11KB），见 3.2。

`tef_dec_next` 状态机：

1. 无活动流 → `stream_open` 当前段；JPEG 段照 `play_once` 语义跳过（保持画面，直接推进到下一段；节拍由控件 timer 统一，无需补时）。
2. `render_frame_stream` 解一帧到 `dst`；失败返回错误（流已关，句柄可安全 close）。
3. 段内帧播完 → `stream_close`，段游标 +1。
4. 全部段播完 → 回卷到段 0。帧 0 是关键帧（全脏），画布自然被整幅重绘，无需清屏。

`tef_dec_set_dst` 未调用前 `tef_dec_next` 返回 `OPRT_INVALID_PARM`。open 成功后素材数据指针须保持有效（flash 常量数组天然满足）。

### 3.2 tinfl 单例解耦（关键改造）

现状：`s_tinfl` 为进程级单例，约束「同一时刻只允许一条活动流」。压缩流**跨帧持续打开**（tinfl 增量状态贯穿整段），两个解码器交替解帧会互踩状态。

改法：`tef_stream_t` 增加 `tinfl_decompressor *tinfl` 字段，`stream_open` 由持有者注入：

- 现有三条通路（播放线程 / `play_once` / bench）照传 `s_tinfl` 单例——**行为零变化**，原有互斥约束继续成立。
- 每个 `tef_dec` 传自己的私有实例——多个 lv_tef 控件并存互不干扰。

heatshrink 的 `hsd` 本来就是流内私有分配，不需要动。

## 4. UI 侧：lv_tef 控件

`src/ui/widgets/lv_tef.[ch]`，骨架与 `lvgl/src/extra/libs/gif/lv_gif.c` 一致：

```c
extern const lv_obj_class_t lv_tef_class;   /* base = lv_img_class */

lv_obj_t *lv_tef_create(lv_obj_t *parent);
void      lv_tef_set_src(lv_obj_t *obj, const uint8_t *data, uint32_t len);
```

实例字段：`lv_img_t img` 基类、`lv_img_dsc_t imgdsc`、画布指针、`tef_dec_t *dec`、`lv_timer_t *timer`。

### 4.1 生命周期

- **constructor**：创建 timer（回调 = 步进一帧），先 pause。
- **`lv_tef_set_src`**：
  1. 有旧素材 → `lv_img_cache_invalidate_src` + `tef_dec_close` + 释放旧画布；
  2. `tef_dec_open` → 按素材 w×h 分配画布（`tkl_system_psram_malloc`，w×h×2 字节）→ `tef_dec_set_dst`；
  3. 立即解第 0 帧（画布从首帧起有效，避免闪黑）；
  4. `imgdsc` = { cf `LV_IMG_CF_TRUE_COLOR`, w, h, data=画布 } → `lv_img_set_src`；
  5. timer 周期设为 `tef_dec_frame_ms`，reset + resume。
  6. 任一步失败：清理已分配资源、`LV_LOG_WARN`，控件保持空 img（与 `lv_gif` 加载失败行为一致）。
- **timer 回调**：`tef_dec_next` → `lv_img_cache_invalidate_src(lv_img_get_src(obj))` + `lv_obj_invalidate(obj)`。解码失败 → pause timer + 日志，画面定格末帧。
- **destructor**：del timer → cache invalidate → `tef_dec_close` → 释放画布。画布指针只存在实例字段里，**无 static 缓存**（RULES §7.1 不适用）。

### 4.2 并发与字节序

- 解码全部发生在 UI 线程 timer 回调中，串行执行——多控件并存无并发问题；与 boot splash（UI 启动前）、EYES 直推播放器（无 UI 板）时序天然错开。
- LVGL 画布需 native RGB565。`TEF_RGB565_BYTE_SWAP` 只在 EYES local.mk 定义，UI 板不定义，调色板天然 native；SPI 面板字节交换由 UI flush 层（`CONFIG_UI_LCD_RGB565_BYTE_SWAP`）负责，两套开关职责不变。

## 5. 内存与编译

每控件 PSRAM 占用 ≈ 画布 `w×h×2` + 索引缓冲 `w×h` + deflate 环形字典 `1<<window_bits`（默认 win14 = 16KB）+ tinfl 约 11KB：

| 素材 | 画布 | 索引 | 合计约 |
|---|---|---|---|
| 128×128 | 32KB | 16KB | 75KB |
| 240×240 | 113KB | 56KB | 196KB |

同屏多控件按内存预算控制个数；素材尺寸不再要求匹配屏幕。

编译接线：

- 无新 Kconfig。`ENABLE_TUYA_UI` depends on `ENABLE_TUYA_DISPLAY`，`miscs/tef` 随后者必编；`src/ui/widgets/` 随 UI 全量编译。
- local.mk 确认 UI 侧可 include `src/miscs/tef/`（`tef_player.h`）。

## 6. 错误处理汇总

| 故障 | 行为 |
|---|---|
| 素材头非法 / 非 v3 / RGB888 | `tef_dec_open` 失败，控件空 img + WARN 日志 |
| PSRAM 分配失败 | 同上（open / set_src 画布两处） |
| 播放中段数据截断/损坏 | `tef_dec_next` 失败 → timer pause，画面定格 |
| JPEG 段 | 跳段保持画面（与 play_once 一致；素材生成端已禁用 JPEG） |

## 7. 验证

- 现有三条通路的改造是纯参数注入（`stream_open` 多传一个 tinfl 指针），无行为变化：编译 + EYES / boot splash 回归确认。
- lv_tef：带屏 UI 板任选页面临时挂一个 TEF 素材目测（素材 `tools/gen_assets.py` 从任意 GIF 生成）。编译与真机验证由用户执行。
- 文档：`docs/howto/use-tef-animation.md`（中英）增补 lv_tef 用法一节，随实现 MR 同步。
