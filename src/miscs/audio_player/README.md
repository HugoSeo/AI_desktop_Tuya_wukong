# svc_ai_player（AI 播放器）

## 概述

svc_ai_player 是 AI 设备的音频播放服务组件（独立仓库，在 wukong app 中以子模块挂载于 `src/miscs/audio_player/`）。职责：从数据源取数 → 解码 → 重采样/混音/数字音量 → 交给 consumer 输出，对上层提供 `tuya_ai_player_*` API（声明在 `include/svc_ai_player.h`；wukong app 经 `src/wukong/audio/wukong_audio_player.c` 封装使用）。

- **数据源（datasink）三种**：`mem`（内存环形缓冲，TTS 流式）、`url`（HTTP/HTTPS 流，音乐）、`file`（本地文件），实现在 `src/datasink/`。
- **解码器**：MP3/WAV 恒编入；OPUS 系（raw/OGG/VBR）由 Kconfig 可选。
- **双路模式**：FG（前台，TTS/提示音）与 BG（后台，音乐）两个播放实例，可混音（`AI_PLAYER_SUPPORT_MIX_MODE`）。
- **输出**：consumer 由使用方注册；`AI_PLAYER_SUPPORT_DEFAULT_CONSUMER`（内置 tal_audio 喇叭输出）默认关——wukong app 自带 consumer。

全部编译配置已收敛到 Kconfig 的 `Audio Configuration → Audio Player` 菜单（wukong app 内 `make app_menuconfig` 可见）；`include/svc_ai_player.h` 中各宏恒有定义；bool 项未配置时，app 构建（定义 `USER_SW_VER`）视为关，独立/组件构建走 `#ifndef` 兜底默认值（功能全开），见头文件 `AI_PLAYER_CFG_UNSET` 说明。

## 设计要点与坑

- **单线程管线（刻意取舍）**：下载不是独立线程，`datasink_url` 的读取就在播放线程里 `http_read_content`（`AI_PLAYER_HTTP_YIELD_MS`=10ms 让出），下载/解码/重采样/混音全部串行。这是资源受限下的刻意设计，不做后台下载重构；URL 通路也不加预缓冲。
- **播放线程优先级必须是 `THREAD_PRIO_0`**（`svc_ai_player.c` 线程创建处）：该线程在输出环满时自阻塞、不会饿死他人；历史教训是 PRIO_1 时被摄像头预览抢占，解码单帧 22→60ms、供给率跌到 92%，持续 underrun 卡顿。
- **mem sink 蓄水（TTS 防碎卡）**：起播及每次断流后，`datasink_mem` 的 read 先返回 0 字节（播放线程走既有 sleep 空转），攒够 `AI_PLAYER_MEM_PREBUF_BYTES`（默认 2048）或 EOF 或 `AI_PLAYER_MEM_PREBUF_TIMEOUT_MS`（默认 500ms）超时才放闸，把网络抖动的高频碎卡合并成一次停顿。水位**须显著小于** `AI_PLAYER_RINGBUF_SIZE`（默认 16384），否则永远攒不满只能靠超时兜底；蓄水完成打印 `mem sink prebuf done: N bytes in M ms`，空超时放行不打印（防刷屏）。
- **LITE 模式（`AI_PLAYER_LITE`）是另一条形态**：同步内存源直通，无播放线程、无 datasink 层，OPUS 系解码器全部禁用；蓄水、AP-STAT、混音/重采样/数字音量等配置对 LITE 一律不生效（Kconfig 已做 `!AI_PLAYER_LITE` 联动，`svc_ai_player.h` 里另有 #undef 防呆）。
- **AP-STAT 诊断两级门**：`AI_PLAYER_DEBUG_STATS=y` 只**编入**统计代码，打印由运行时开关控制——`tuya_ai_player_debug_stats_set(TRUE)`（内存态、默认关、重启失效；wukong app 诊断页有动态开关行，`AI_PLAYER_DEBUG_STATS_AVAILABLE` 供 UI 判断是否编入）。开启后播放期间每 2s 输出一行 `[AP-STAT FG/BG]` 聚合统计（sink 读取/解码/重采样/写出的次数、字节与最大耗时），用于区分「网络饿」与「CPU 饿」。排查播放卡顿的完整判读方法见 wukong app 的 `docs/troubleshooting/audio-playback-stutter.md`。
- **underrun 无日志**：输出环放空时下游补零静音不打印任何日志，"没有报错"不代表链路健康，要看 AP-STAT 的写出时长是否追上实际时间。

## 改动指南

- **调蓄水/缓冲/解码器/诊断开关**：一律走 Kconfig（wukong app 内 `make app_menuconfig` → `Audio Configuration` → `Audio Player`），改后必须 `make app_config` 重新生成配置头。不要在源码里手写 `#define` 覆盖。
- **新增解码器**：在 `src/decoder/` 参照现有 `decoder_*.c` 实现并注册；若引入可选依赖库，Kconfig 加开关并在使用方 `local.mk` 里条件编入。
- **新增数据源**：在 `src/datasink/` 参照 `datasink_file.c`（最简）实现 `ai_player_datasink.h` 的接口集。
- **排查播放问题**：确认固件带 `AI_PLAYER_DEBUG_STATS=y`，再打开运行时开关（诊断页或 `tuya_ai_player_debug_stats_set`）看 AP-STAT，按 wukong app `docs/troubleshooting/audio-playback-stutter.md` 的口诀判读；不要一上来就改缓冲大小。
- **本组件是被 wukong 外层指针引用的子模块**：改动需在本仓库提交并推送后，同步 bump wukong app 的子模块指针；MR 用普通 merge（勿 squash）。
