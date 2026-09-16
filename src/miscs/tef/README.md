# miscs/tef — TEF 表情动画播放器框架（脏块直推 GRAM）

## 概述

TEF（Tuya Emotion Format，`TEF1`）是面向嵌入式小屏表情动画的存储格式：块级帧间差分 +
全局调色板（1/4/8bit 自适应）+ 多帧成段共享字典压缩（heatshrink / raw deflate），脏块位图
并入压缩流。本模块是它的设备端播放器：独立解码线程按素材 fps 节拍解出每帧脏 tile 写入
PSRAM 影子帧缓存，再**整屏一次**经 `tal_display_flush` 直推 SPI 屏 GRAM（双 panel
并行 DMA）——不依赖 LVGL，每帧恒定一次整帧写屏。差分只发生在解码侧（省 flash 与
CPU），刷屏侧用恒定整帧换取稳定的传输时序。

在 EYES 板上它取代了旧的 `lv_gif` 全帧刷新方案：素材从 GIF 预编码为 `.tef` 常量数组编入
固件（参考接入：`src/boards/T5AI_BOARD_EYES/ui/eyes_tef/`），业务侧表情经 `tuya_ai_display` 总线送达 `eyes_app.c`，后者调
`tef_player_play()` 切换动画。接口共三个：

- `tef_player_init()`（枚举 panel、起常驻解码线程）+ `tef_player_play(data, len)`
  （异步切素材，循环播放）——常驻表情播放器形态（EYES）；
- `tef_player_play_once(data, len, dst, dst_w, dst_h, stride, commit_cb, ud)`——
  一次性阻塞播放到调用者提供的 RGB565 画布（素材画布在窗口内居中，可小于窗口），
  每帧回调由调用者上屏，播完释放工作缓冲。不依赖 `tef_player_init()`，但与常驻
  播放器/bench 共享 tinfl 解压器单例，不得并发。UI 板的 boot splash
  （`src/boards/common/boot_splash/`）即用它在 LVGL 起来前播开机动画。

## 目录与工具链

```
miscs/tef/
├── tef_player.c/.h        设备端播放器（唯一需要编译进固件的框架代码）
├── tef_format.h           TEF1 格式定义（编解码两端共用）
├── vendor/                heatshrink 解码器 + miniz tinfl（vendored）
├── docs/TEF_技术规范.html  格式规范（原理：差分/调色板/成段压缩/两种刷屏模式）
└── tools/                 宿主机工具（Python3 + Pillow + numpy）
    ├── gen_assets.py      ★ 一键素材生成（见下）
    ├── tef_encoder.py     GIF -> .tef 编码器（gen_assets 内部调用，可单用/作库）
    ├── tef_decoder.py     .tef -> GIF/PNG 解码导出（无损校验用）
    └── tef_player.py      桌面窗口预览播放器（--info 可只看文件信息）
```

**一键生成素材**：在 `tools/` 下放一个 `gif/` 目录（文件名即素材名，如
`neutral.gif`），运行 `python3 gen_assets.py` 即输出全部 `tef_<name>.c` 与注册表
`tef_assets.[ch]` 到 `./out/`；也可显式指定输入/输出目录：

```bash
python3 src/miscs/tef/tools/gen_assets.py <gif_dir> <out_dir>
```

注册表把 `neutral`（若存在）固定排在第 0 项，播放侧未命中名字时回落表 0。

## 基准测试：TEF vs GIF（CPU / 内存 / flash）

`bench/` 内置与旧 `lv_gif` 同源的 gifdec（去 LVGL 依赖、分配器带峰值统计），
与 TEF 真实解码路径同素材各跑 N 圈：

```bash
make app APP_NAME=tuyaos_demo_wukong_ai TEF_BENCH=1   # 编入基准
# 烧录后启动串口输出 [TEF-BENCH] 报告：flash / mem / cpu(平均每帧µs + 相对帧节拍%)
```

度量口径见 `bench/tef_bench.c` 头注释。两侧解码结果已在宿主机逐像素比对一致
（同素材 gifdec 输出 == TEF 输出），对比公平。正常构建不编入 bench，零开销。

## 设计要点与坑

- **双屏路由**：EYES 为两块 128×128 panel 摞成 128×256 逻辑屏。素材画布高 ≤ 单 panel 高
  （128×128 单眼素材）→ 同帧镜像推到所有 panel；画布高 = 逻辑屏高（128×256 双眼素材）→
  按行带路由。tile 高 8/16/32/64 均整除 128，脏块不会跨 panel。
- **为什么整行带、不做逐脏块子窗口**：T5 上 SPI2/3 实际由 QSPI 驱动承载，
  `tkl_qspi_send` 对 ≤256 字节的发送走命令 FIFO 路径（首字节被拆作命令、数据按 32bit
  打包），像素小包经它必现花方块；整行带（128 宽 × tile 高 ≥ 2KB）恒走 DMA 大包路径
  （旧 UI 整带刷验证过），且 4 字节对齐、在影子缓存中行连续可零拷贝作 DMA 源。
- **每帧恒定整屏写 + 双 panel 并行**：面板以自身 ~60Hz 从 GRAM 自扫描，与写入无同步
  （无 TE 引脚），撕裂无法根除；整屏恒定传输让每帧写窗口时长一致，撕裂缝位置/形态
  稳定可预期，观感优于随脏区大小跳变的局部带（曾历经逐脏块 → 脏行带 → 包围带 →
  整屏的演进）。64KB 整帧 @48MHz 双 panel 并行约 5.5ms，远小于帧预算；SPI2/SPI3 是
  独立 QSPI 控制器 + 独立 DMA 通道，镜像素材同一缓冲并行喂两路、先全部提交再统一等完成。
- **字节交换零开销**：st7735s 等 8bit SPI 面板期望 MSB-first。由 local.mk 定义
  `TEF_RGB565_BYTE_SWAP=1`，调色板加载时一次交换完成，解码热路径查表即得最终字节序。
- **flush 同步**：`ty_frame_buffer_t` 描述符与被推送的影子缓存行带在驱动异步发送期间
  被持有，每次 flush 后等 `free_cb` 信号量才继续——描述符必须常驻（static），不能用栈变量。
- **解压器不上栈**：`tinfl_decompressor` 约 10KB，`tinfl_decompress_mem_to_mem` 会把它
  放栈上导致线程栈溢出（真机反复 crash）；本模块用 PSRAM 常驻单例直调 `tinfl_decompress`。
- **切换与循环**：`tef_player_play` 只写请求（指针 + 序号），解码线程在帧边界消费；素材
  循环回绕的第 0 帧是全脏关键帧，可自愈任何意外的 GRAM 覆盖。素材数据必须常驻（flash
  常量数组），播放器不拷贝。
- **JPEG 段不支持**：设备端不带 JPEG 解码，遇到 JPEG 段保持画面按时长跳过（告警一次）。
  素材生成侧已用 `jpeg_q=0` 禁用 JPEG 帧，正常不会出现。
- **时域整形（`--stabilize`，编码端前处理）**：AI 生成/抖动量化素材把帧间闪烁噪声
  烤进像素（变化像素多为 Δ≤24 的微小色差），块差分脏区被吹大数倍。编码前逐像素把
  `max(ΔR,ΔG,ΔB) ≤ 阈值` 的像素钉回（已稳定的）上一帧值，真动作 Δ 大不受影响。
  `auto` 按 Δ 分布自动选档（8/16/24/32）或跳过干净素材，并带结果保险：整形版
  没变小自动回退（渐变类素材 Δ 统计识别不了）。默认关闭；开启后对整形后帧仍逐像素
  无损，对原始输出为感知无损（误差 ≤ 阈值，钉回不跨帧累积）；淡入淡出类渐变内容
  会产生台阶感，不建议开启。仅编码端改动，格式与解码器零变化，已烧录设备直接受益。
- **内存（v3 P1 流式）**：工作缓冲全在 PSRAM——影子帧缓存（画布 W×H×2）+
  deflate 环形字典（`1<<window_bits`，编码默认 win=14 即 16KB；win=15 素材
  仍兼容，按头部动态分配）+ 一帧索引缓冲（W×H）+ tinfl ~11KB；段明文从不整块
  落地（拉取式流式解码）。win15→win14 flash 仅 +1.4%，解码字典省一半。**仅支持 v3 素材**
  （旧 v1 文件重新编码即可）；v3 头的 `max_seg_decomp` 供无流式解码器的整段
  解压实现使用，本播放器忽略。RAM 优先模式（heatshrink 小窗，~3KB 级）见
  TEF v3 设计方案 P2，格式位已预留。
- **与 UI 框架的关系**：EYES 默认 `ENABLE_TUYA_UI=n` + `ENABLE_TUYA_DISPLAY=y`，屏幕归
  播放器独占。若 UI 框架开启，板 UI 不得创建任何会失效重绘的 LVGL 对象（LVGL 只在启动时
  flush 一次全黑帧，起播延时 `TEF_START_DELAY_MS` 用于避开它）。
- **heatshrink 参数**：流内无参数头，窗口/前瞻（11/4）为编解码两端约定，改动必须同步
  `tef_encoder` 与本模块的 `TEF_HS_WINDOW/LOOKAHEAD`。

## 改动指南

- **增删表情 / 更新素材**：把命名为表情名的 GIF（须与云端 emoji 名一致，见
  `src/wukong/skills/skill_emotion.c`）放入板侧素材目录（EYES：
  `src/boards/T5AI_BOARD_EYES/ui/eyes_tef/gif/`），重跑一键生成：
  `python3 src/miscs/tef/tools/gen_assets.py <gif_dir> <素材输出目录>`。
- **改格式 / 解码逻辑**：格式定义在 `tef_format.h`（与编码器共用）；解码正确性可用宿主机
  比对验证——用 `tef_decoder.py`（Python 参考解码器）导出逐帧 RGB，与本模块解码结果
  逐像素比对（本次移植即以该方法验证 bit-exact）。
- **换屏/换板**：`tef_player_init` 从 `tuya_display_hw_get_lcd_region()` 取 panel 布局，
  无 EYES 硬编码；新板只需正确实现 `tuya_display_hw` 配置。非 byte-swap 面板去掉
  local.mk 里的 `TEF_RGB565_BYTE_SWAP` 定义即可。
- **编译接线**：miscs/tef 框架随 local.mk 的 `CONFIG_ENABLE_TUYA_DISPLAY` 块无条件编入
  （EYES 常驻表情、UI 板 boot splash 共用）；板级子块只负责自己的素材目录与
  `TEF_RGB565_BYTE_SWAP` 之类面板 flag（当前 `CONFIG_T5AI_BOARD_EYES` 子块：字节交换 +
  eyes_tef 素材 + eyes_app.c）。
  解压依赖 vendored 于 `vendor/`（heatshrink 解码器 + miniz tinfl，勿本地魔改，升级整体替换）。
