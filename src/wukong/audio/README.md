# 音频模块

## 概述

`audio/` 提供 Wukong AI 的全部音频能力：音频输入输出（板级麦克风或 UART 外置编解码芯片二选一，由 `CONFIG_USING_BOARD_AUDIO_INPUT`/`CONFIG_USING_UART_AUDIO_INPUT` 等编译宏决定；板级路径下还有「wukong pipeline / 平台机制」两种采集机制可选，见设计要点第 2 条）、前端 AEC/VAD 处理与关键词唤醒 KWS、TTS/音乐/提示音播放，以及本地播放列表控制。

数据流：`input/` 采集侧分两级——**采集 pipeline**（`wukong_audio_pipeline.c`：ADC/DMIC 采集 → ring buffer → process task → `frontend/` AEC+VAD，通过 ops 表解耦具体算法实现，处理后的 mono 帧经交付回调送出）与**recorder**（`wukong_audio_input_board.c`：切片组包、经 `output_cb` 交付业务，消费侧不感知上游麦克风拓扑）→ 处理结果自动喂给 [KWS](frontend/kws/README.md) 引擎判定是否唤醒 → 命中后发布 `EVENT_WUKONG_KWS_WAKEUP`，交由 `mode/` 处理会话状态切换；下行方向 `wukong_audio_player.c` 负责 TTS/音乐/提示音播放，`wukong_playback_ctrl.c` 在其上叠加本地播放列表 + 云端音乐列表分页的状态机。

接入方式：板级初始化调用 `wukong_audio_input_init()`/`wukong_audio_output_init()` 选择 board 或 uart 实现；新平台的 AEC/VAD 算法通过实现 `WUKONG_AUDIO_FRONTEND_OPS_T` 并调用 `wukong_audio_frontend_register()` 接入，不需要改前端调度器本身。

## 设计要点与坑

1. **输入侧是两级模型**：生产者在 pipeline 的交付上下文，消费在普通任务里。`__audio_frame_put()` 是 recorder 侧传给 pipeline 的交付回调——平台机制下由驱动 voice task 的 IPC 同步回调直接调用（帧从 CP1 经 IPC 同步消息送达），wukong pipeline 下由 process task 调用——两种情况下都不能阻塞、不能 `malloc`；它只把定长 mono 帧塞进一个 `tuya_queue`，真正组包、调用 `output_cb` 的工作在 record task 里做（`input/wukong_audio_input_board.c` 的 `__audio_slice_check_and_send()`），且组装用的 `slice_buf` 是任务私有缓冲区，加锁只覆盖出队操作，`output_cb` 调用特意放在锁外面执行。
2. **板级采集有两种机制，编译期二选一（`ENABLE_WUKONG_AUDIO_PIPELINE`，Kconfig 在 app 侧 `src/miscs/audio_player/Kconfig`）**：`=y` 走 **wukong pipeline**——tal_audio 新驱动路径，原始双声道（LR）采集进 ring buffer，AEC/前端算法在应用层 process task 里跑，SPRS 双麦前端必须走这条（目前仅适配 T5，`default y if USING_SPRS_AUDIO_FRONTEND`）；`=n`（默认）走**平台机制**——各芯片共用的 vendor tkl_audio 老路径，AFE 在驱动内部完成、直接交付处理后的 mono 帧（`wukong_audio_pipeline_platform.c` 以同名符号实现同一套 pipeline 接口，由 app 顶层 `local.mk` 按配置选源，切换自愈、会清掉旧目标文件）。两种机制对 recorder 及以上完全透明。
3. **双麦（SPRS）路径的三件事**：`USING_SPRS_AUDIO_FRONTEND` 打开时 `WUKONG_AUDIO_USING_DUAL_MIC=1`——①采集恒为 2 通道（ADC+DMIC 两路），`pipeline` 内做 ADC/DMIC **时间戳配对同步**（阈值 8ms、收敛超时 3s，超时打 `sync not converged` 错误日志并继续、算法结果不可靠）；②算法帧是 32ms（tuya 前端为 20ms），采集帧按 `AUDIO_INPUT_FRAMES_PER_PROC` 累积后才送前端；③SPRS 输出强制 mono，上行 `channel` 固定 1。
4. **DMIC 路做 HPF 去直流预处理**（`frontend/wukong_audio_preprocess.c`，一阶高通、截止 60Hz、可选滤 ref 路）：仅双麦 DMIC 输入启用，单麦 ADC 路径保持原始行为；调截止频率/开关看 `wukong_audio_preprocess.h` 顶部的 `WUKONG_AUDIO_HPF_*` 宏。
5. **两种 VAD 模式丢帧策略是反的**：手动 VAD 模式（`WUKONG_AUDIO_VAD_MANUAL`）下队列满会丢"最新帧"（避免打断正在说话时段的连续性）；自动/连续模式下队列满则丢"最旧帧"腾位置。排查断字/丢字问题先确认当前 `vad_mode`。
6. **PSRAM 分配必须配对释放**：`ENABLE_EXT_RAM` 打开时要用 `tal_psram_malloc`/`tal_psram_free`，因为 `tal_free` 是否路由到 PSRAM 由一个默认关闭的全局开关决定，混用会在普通堆上误释放 PSRAM 内存。KWS 的环形缓冲、输入侧的采集缓冲都遵循这条规则。
7. **KWS 环形缓冲实际容量约 1.6s**（`(单帧 640 字节 × 40) × 2` ≈ 51200 字节，16kHz/16bit/单声道下），VAD 结束后只保留最近 1s 数据、其余整帧丢弃；检测线程需要凑够 100ms 数据（或 VAD 结束强制）才 `post` 一次信号量触发检测。详见 [KWS 模块](frontend/kws/README.md)。
8. **`wukong_playback_ctrl.c`（audio/ 下最大的文件，约 1900 行）是独立于 `wukong_audio_player` 的播放列表/MQTT 音乐控制层**：`wukong_playback_ctrl_next/prev`、`wukong_playback_playlist_play_async` 等非阻塞版本会把导航调度到后台 worker 线程执行，避免 LVGL/UI 线程在刷新播放 URL 时被阻塞；而 `wukong_playback_playlist_next/prev`（无 `ctrl_` 前缀）仍是会阻塞调用者的同步版本。UI 回调场景应统一使用带 `ctrl_`/`_async` 的非阻塞版本。播放模式（顺序/列表循环/单曲循环/随机，`WK_PLAY_MODE_E`）保存在这一层的内存里、不做持久化。
9. **`wukong_audio_player.c` 是前台/后台双播放器架构，TTS 播放会自动压低音乐音量**：`__s_tone_player`（`AI_PLAYER_MODE_FOREGROUND`，播 TTS/提示音）与 `__s_music_player`（`AI_PLAYER_MODE_BACKGROUND`，播音乐）是两个独立的 `AI_PLAYER_HANDLE`，通过 `tuya_ai_player_set_mix_mode(TRUE)` 共享同一路音频输出实现混音。当 TTS（tone player）开始播放、且音乐播放器正在播放时，`__player_event()` 会把音乐音量自动降为当前音量的一半（`player_vol/2`，即压低 50%）；TTS 播放结束（`AI_PLAYER_STOPPED` 且已无其他播放中内容）后不做特殊恢复处理，仅在"仍有播放"时把音乐音量设回压低前的 `player_vol`。这层 ducking 逻辑在 `wukong_audio_player.c` 里，和第 8 条的 `wukong_playback_ctrl.c` 播放列表调度是两回事，改音量混合行为要看这里。支持的音频格式：MP3/WAV/Opus/Speex/OggOpus（`AI_AUDIO_CODEC_E`）。
10. **VAD 三档阈值** `WUKONG_AUDIO_VAD_HIGH/MID/LOW` 在 Speex+RNN VAD 实现里分别映射回调阈值 `-40/-50/-60`，数值越小越敏感；默认初始化时设为 `MID`。需要更灵敏的识别时调用 `wukong_audio_frontend_vad_set_threshold(WUKONG_AUDIO_VAD_LOW)`。
11. **旧 API 仍在，只是转调新接口**：`wukong_aec_vad_init/deinit/process`、`wukong_vad_start/stop/get_flag/set_threshold` 都保留为薄包装，内部转调 `wukong_audio_frontend_*` 系列。新代码不要再直接调用旧名字，两套名字同时存在容易改漏一处。
12. **共享音频输出按 owner 仲裁**（`output/wukong_audio_output.c`）：`WUKONG_AUDIO_OUTPUT_OWNER_E`（PLAYER/VIDEO/P2P）标识当前输出归属，旧接口 `wukong_audio_output_start/write/stop` 等价于 PLAYER owner 的薄包装。只有 P2P 允许抢占其他 owner（抢占先 stop 旧 backend，`write_lock` 作排空屏障等在途 write 返回后才 start 新 owner），其余请求在被占用时返回 `OPRT_RESOURCE_NOT_READY`；`wukong_audio_output_stop_owned` 对非当前 owner 幂等返回 OK。新增独立输出方要走 `*_owned` 系列并自定 owner，不要复用裸接口冒充 player。
13. **AVI 录像的拾音点在 VAD 门控之前**（`input/wukong_audio_pipeline*.c`，`ENABLE_TY_AVI_MEDIA` 打开才编入）：recorder 侧 `__audio_frame_put` 的 vad_flag 门控只在"检测到人声/已唤醒"时放行 AI 链路，录像若从门控后取音会大段无声；因此 `ty_avi_recorder_audio_feed()` 在门控前每帧必喂、独立于唤醒/VAD 状态，wukong pipeline 与平台机制两条采集路径都有同一个拾音点，与 AI 上行读同一份处理后 mono 帧、互不干扰。

## 改动指南

- 切换唤醒引擎、调整唤醒词库、新增自定义唤醒词：见 [KWS 模块](frontend/kws/README.md)。
- 接入新平台的 AEC/VAD 算法：实现 `frontend/wukong_audio_frontend.h` 里 `WUKONG_AUDIO_FRONTEND_OPS_T` 的全部回调，在板级初始化里调用 `wukong_audio_frontend_register()`；不要改 `frontend/wukong_audio_frontend.c` 的调度逻辑本身。示例：

  ```c
  STATIC WUKONG_AUDIO_FRONTEND_OPS_T g_platform_x_ops = {
      .init              = platform_x_init,
      .deinit            = platform_x_deinit,
      .process           = platform_x_process,
      .vad_start         = platform_x_vad_start,
      .vad_stop          = platform_x_vad_stop,
      .vad_set_threshold = platform_x_vad_set_threshold,
      .vad_get_flag      = platform_x_vad_get_flag,
  };

  /* 在板级初始化时注册 */
  wukong_audio_frontend_register(&g_platform_x_ops);
  ```

- 接入 UART 外置语音芯片（替代板载麦克风）：改 `input/wukong_audio_input_uart.c`、`output/wukong_audio_output_uart.c`，并切换 `CONFIG_USING_UART_AUDIO_INPUT`/`CONFIG_USING_UART_AUDIO_OUTPUT` 配置项。当前源码里已不再按芯片型号写死逻辑（UART 路径是通用实现），历史上验证过可用的外接语音芯片及获取唤醒词固件的入口（**以下链接未在本轮复核中重新访问，接入前请自行确认是否仍然有效**）：

  | 芯片型号 | 获取唤醒词/固件 |
  |---|---|
  | GX8006 | https://tuyaos.com/viewtopic.php?t=9147 |
  | CI1302 | https://tuyaos.com/viewtopic.php?t=9148 |

- 改本地播放列表、自动播放下一首、播放模式行为：改 `wukong_playback_ctrl.c`；先看 `wukong_playback_ctrl.h` 里每个函数头顶注明的"是否阻塞/是否调度到后台线程"，避免在 UI 回调里误用同步版本。
- 改 TTS/提示音播放本身（前后台混音、音量控制）：改 `wukong_audio_player.c`。

## 参考

### 事件契约：`EVENT_AUDIO_VAD`

音频输入模块在 VAD 状态变化时发布该事件（`audio/input/wukong_audio_input.h:24` 定义为 `"EVENT.VAD"`），板级（`wukong_audio_input_board.c`）与 UART（`wukong_audio_input_uart.c`）两种输入实现都会发布：

- 事件名：`EVENT_AUDIO_VAD`
- 数据：`WUKONG_AUDIO_VAD_FLAG_E`，取值 `WUKONG_AUDIO_VAD_START` / `WUKONG_AUDIO_VAD_STOP`

### 音频输入配置

`WUKONG_BOARD_AUDIO_INPUT_CFG_T` 里几个非自解释字段：

| 字段 | 含义 |
|---|---|
| `sample_rate` | 采样率，`UINT32_T`（不是枚举类型），常用 16000 |
| `vad_off_ms` | 语音活动补偿时间，单位 ms |
| `vad_active_ms` | 语音活动检测阈值，单位 ms（缓存大小 = `vad_active_ms + vad_off_ms`） |
| `slice_ms` | 音频切片时间，单位 ms |

典型板级输入配置示例：

```c
WUKONG_AUDIO_INPUT_CFG_T input_cfg = {0};
input_cfg.type = WUKONG_AUDIO_USING_BOARD;
input_cfg.board.sample_rate = 16000;
input_cfg.board.sample_bits = TUYA_AUDIO_SAMPLE_BITS_16;
input_cfg.board.channel = 1;
input_cfg.board.vad_mode = WUKONG_AUDIO_VAD_AUTO;
input_cfg.board.vad_off_ms = 500;
input_cfg.board.vad_active_ms = 300;
input_cfg.board.slice_ms = 20;
input_cfg.board.output_cb = my_audio_callback;

wukong_audio_input_init(&input_cfg);
wukong_audio_input_start();
```

典型运行时格式：16kHz / 16 位 / 单声道，帧大小 320 采样（20ms）。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
