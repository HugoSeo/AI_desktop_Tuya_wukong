# 资源统计

本文回答两类问题：**跑起来用了哪些线程**（优先级 / 栈大小 / 什么条件下创建），以及**固件静态体积怎么统计**（DATA/BSS/ROM 按组件分解）。最后收口运行时观测手段。

> **基准**：master 分支 2026-08-14 代码（1.1.0），线程清单经该版本构建 ELF 校验。线程的栈大小/优先级取代码与 Kconfig 默认值；线程是否存在取决于板型与配置开关，表中"创建条件"列为准。改动线程配置的 MR 请同步更新本表（见文末维护说明）。

## 一、线程优先级语义

`THREAD_PRIO_x` 枚举定义在 SDK `components/tal_system/include/tal_thread.h`，**数字越小优先级越高**（映射到 FreeRTOS 数值恰好相反，容易记反）：

| 枚举 | FreeRTOS 值 | 语义 |
|---|---|---|
| `THREAD_PRIO_0` | 5 | 最高（音频输出、按键、显示刷新等实时性敏感线程） |
| `THREAD_PRIO_1` | 4 | 高（音频前端处理、UI 主循环） |
| `THREAD_PRIO_2` | 3 | 中 |
| `THREAD_PRIO_3` | 2 | 中低 |
| `THREAD_PRIO_5`/`_6` | 0 | 最低 |

优先级配错的真实代价可参考[播放卡顿排查](troubleshooting/audio-playback-stutter.md)中的案例：播放线程曾在 `THREAD_PRIO_1` 被相机预览抢占导致持续卡顿，提升到 `THREAD_PRIO_0` 后修复。

## 二、线程清单

说明：

- 「栈大小」为字节；标注 `Kconfig:` 的项可经 `make app_menuconfig` 调整。
- `ENABLE_EXT_RAM` 开启时（T5 默认），大部分应用线程设置 `psram_mode=1`，栈落在 PSRAM、不占 SRAM。
- **SDK/components 层线程不在本表**（`sys_timer`、`wq_system`/`wq_highpri`、`health_monitor`、`mqtt_thread`、`ai_client`/`ai_agent_input`/`ai_agent_output` 等由 TuyaOS SDK 创建，属 SDK 范畴；它们会一并出现在下文第四节的栈水线日志里）。

### 2.1 核心常驻（默认 T5AI_BOARD 板载音频配置）

| 线程名 | 优先级 | 栈大小 | 职责 | 来源 |
|---|---|---|---|---|
| `tuya_app_main` | PRIO_2 | 4096 | 应用启动主流程 | `src/tuya_app_main.c` |
| `ai_toy_state` | PRIO_5 | 3072 | AI 玩具状态机 | `src/tuya_ai_toy.c` |
| `wk_provider` | PRIO_3 | 4096 | AI provider 事件分发 | `src/wukong/provider/wukong_ai_provider.c` |
| `record_task` | PRIO_1 | Kconfig: `INPUT_BOARD_STACK_SIZE`（Kconfig 默认 2560；T5AI_BOARD / DESKTOP / EVB / EYES / QEMU 预置 40960，EVB_PRO 等其余板型仍为 2560） | 录音切片交付/上传 | `src/wukong/audio/input/wukong_audio_input_board.c` |
| `wukong_kws` | PRIO_1 | 6144 | 唤醒词检测（板载 KWS） | `src/wukong/audio/frontend/kws/wukong_kws.c` |
| `ai_player` | PRIO_0 | Kconfig: `AI_PLAYER_STACK_SIZE`（OPUS 系解码器开启时默认 12288，否则 6144） | 音频播放管线（LITE 模式无此线程） | `src/miscs/audio_player/src/svc_ai_player.c` |
| `key_handle` | PRIO_0 | 4096 | 按键扫描与事件分发 | `src/drivers/app_tuya_key/src/tuya_key.c` |

> 默认 T5AI_BOARD 走**平台机制**采集（`ENABLE_WUKONG_AUDIO_PIPELINE=n`）：音频前端（AEC/VAD）在 vendor 驱动的 voice task 内运行，应用侧没有独立的前端处理线程；`audio_proc` 仅在 wukong pipeline 机制下创建，见 2.4。

### 2.2 显示 / UI（相应开关开启的板型）

| 线程名 | 优先级 | 栈大小 | 创建条件 | 来源 |
|---|---|---|---|---|
| `spi_task` / `rgb_task` / `display_qspi_task` | PRIO_0 | 2048 / 4096 / 4096 | `ENABLE_TUYA_DISPLAY`，按屏接口三选一 | `src/drivers/app_tuya_display/tal_display/` |
| `tp_task` | PRIO_1 | 4096 | 触摸屏板型 | `src/drivers/app_tuya_tp/tal_tp/src/tal_tp_service.c` |
| `ui_loop` | PRIO_1 | 12288 | `ENABLE_TUYA_UI`，LVGL 主循环 | `src/ui/port/ui_port.c` |
| `boot_splash` | PRIO_0 | 8192 | `ENABLE_TUYA_UI`，开机动画（播完退出） | `src/boards/common/boot_splash/tuya_boot_splash.c` |
| `tef_player` | PRIO_1 | 8192 | 板级调用 `tef_player_init()`（EYES 表情常驻播放） | `src/miscs/tef/tef_player.c` |
| `rec_sd_writer` | PRIO_2 | 4096 | wukong 页面集录音功能，录音期间按需 | `src/ui/services/ui_svc_recording.c` |
| `rec_transcribe` | PRIO_2 | 20480 | 同上，转写轮询期间按需 | `src/ui/services/ui_svc_transcribe.c` |
| `ty_avi_rec` | PRIO_1 | 8192 | `ENABLE_TY_AVI_MEDIA`，本地 AVI 录像期间按需 | `src/miscs/tuya_avi_service/recorder/ty_video_recorder_ctlr.c` |
| `ty_avi_play` | PRIO_2 | 8192 | `ENABLE_TY_AVI_MEDIA`，本地 AVI 播放期间按需 | `src/miscs/tuya_avi_service/src/ty_avi_player.c` |
| `avi_cleanup` | PRIO_2 | 4096 | 仅播放收尾/停止任务因 `WORKQ_SYSTEM` 队列已满而投递失败时短暂创建，清理后自退出 | `src/ui/services/ui_svc_video_playback.c` |
| `avi_rec_stop` | PRIO_2 | 4096 | 仅已运行录像的停止任务因 `WORKQ_SYSTEM` 队列已满而投递失败时短暂创建，清理后自退出 | `src/ui/services/ui_svc_video.c` |

### 2.3 板型外设

| 线程名 | 优先级 | 栈大小 | 创建条件 | 来源 |
|---|---|---|---|---|
| `imu_task` | PRIO_5 | 3072 | DESKTOP 板 | `src/drivers/app_tuya_imu/src/tuya_imu.c` |
| `motion_heart` + `motion_ctrl` | PRIO_5 | 3072 ×2 | DESKTOP 板运动控制 | `src/miscs/motion/tuya_motion_ctrl.c` |
| `gesture_monitor` | PRIO_1 | 2048 | ROBOT 板手势传感器 | `src/miscs/gesture/app_gesture.c` |
| `action_task` | PRIO_2 | 4096 | ROBOT 板舵机动作队列 | `src/miscs/servo_ctrl/servo_ctrl.c` |
| `pn532_poll` | PRIO_3 | 2048 | NFC 轮询期间按需 | `src/drivers/app_tuya_nfc/src/tuya_pn532_hsu.c` |
| `conv` | PRIO_2 | 2048 | `ENABLE_BATTERY`，电量采样 | `src/miscs/battery/tuya_ai_battery.c` |
| `gfw_core` | PRIO_1 | 4096 | UART 外接语音芯片模式（`USING_UART_AUDIO_INPUT`） | `src/miscs/uart_codec/src/gfw_core/gfw_core.c` |

### 2.4 功能开关

| 线程名 | 优先级 | 栈大小 | 创建条件 | 来源 |
|---|---|---|---|---|
| `audio_proc` | PRIO_1 | Kconfig: `INPUT_BOARD_STACK_SIZE`（同 `record_task`） | `ENABLE_WUKONG_AUDIO_PIPELINE=y`（wukong pipeline 采集机制，SPRS 双麦前端必须），音频前端处理（AEC/VAD） | `src/wukong/audio/input/wukong_audio_pipeline.c` |
| `pb_auto_next` | PRIO_3 | 8192 | `ENABLE_TOOLKITS_PLAYBACK`，自动切歌 | `src/wukong/audio/wukong_playback_ctrl.c` |
| `wk_wechat` | PRIO_1 | 6144 | `ENABLE_CHAN_WECHAT`，微信消息轮询 | `src/wukong/channel/im_wechat/im_wechat_channel.c` |
| `cube_engine` | PRIO_2 | 12288（UART 透传）/ 30720（板载 OPUS 编码） | `ENABLE_PROVIDER_CUBE` | `src/wukong/provider/cube/wukong_provider_cube.c` |
| `xiaozhi_ws_recv` | PRIO_2 | 4096 | `ENABLE_PROVIDER_CUBE`，WebSocket 接收 | `src/wukong/provider/cube/xiaozhi/xiaozhi_protocol_websocket.c` |
| `jd_client` | PRIO_1 | 8192 | `ENABLE_PROVIDER_JD` | `src/wukong/provider/jd/joyinside/joyinside_client.c` |
| `jd_input` | PRIO_1 | 40960 | `ENABLE_PROVIDER_JD`，音频上行 | `src/wukong/provider/jd/wukong_provider_jd.c` |
| `tuya_ipc_thread` | PRIO_2 | 8192 | `ENABLE_AI_MODE_P2P`，P2P 服务 | `src/miscs/p2p/tuya_p2p_app.c` |
| `tuya_tmm_control` | PRIO_1 | 8192 | P2P 通话期间按需 | `src/miscs/p2p/tuya_tmm_control/src/tuya_tmm_control.c` |
| `tuya_tmm_stream` | PRIO_1 | 10240 | 同上 | `src/miscs/p2p/tuya_tmm_stream/src/tuya_tmm_stream.c` |
| `tuya_tmm_manager` | PRIO_1 | 10240 | 同上（P2P 通话主管理线程） | `src/miscs/p2p/tuya_tmm_manager/src/tuya_tmm_manager.c` |
| `tmm_audio_send` | PRIO_1 | 4096 | 同上 | `src/miscs/p2p/tuya_tmm_manager/src/tuya_tmm_manager.c` |

### 2.5 调试 / 产测（正常固件默认不创建）

| 线程名 | 优先级 | 栈大小 | 触发条件 | 来源 |
|---|---|---|---|---|
| `audio_dump` | PRIO_1 | 4096 | 首次音频抓取会话时懒初始化（[声学测试](troubleshooting/acoustic-test.md)工具链） | `src/miscs/audio_analysis/audio_dump.c` |
| `mf_thread` / `mf_factory_thread` / `record` / `ds` | PRIO_3 | 4096 ×4 | 产测（MF test）模式 | `src/miscs/mftest/tuya_ai_toy_mf_test.c` |
| `codec_bench` | PRIO_1 | 25600 | `CODEC_BENCH_TEST` 基准模式 | `src/miscs/codec_bench/codec_bench_encoder.c` |
| `taudio_*` ×3 | PRIO_2 | 8192 ×3 | tal_audio 测试命令 | `src/drivers/app_tuya_audio/tal_audio/src/test/test_audio_onboard.c` |
| `test_task` | PRIO_2 | 2048 | `SERVO_ACTION_TEST` | `src/miscs/servo_ctrl/servo_ctrl.c` |
| `ubuntu_key` | PRIO_3 | 2048 | PC 仿真板（`WUKONG_BOARD_UBUNTU`） | `src/tuya_ai_toy_key.c` |

## 三、静态存储统计（DATA/BSS/ROM）

仓库根目录的 `tuyaos_statistics_ai.sh` 基于 `nm` 符号表，把固件的静态占用**按组件目录分解**。用法（需先完成一次构建）：

```bash
cd apps/tuyaos_demo_wukong_ai
./tuyaos_statistics_ai.sh output/<版本号>/debug/bk7258/app.elf
./tuyaos_statistics_ai.sh output/<版本号>/debug/bk7258_ap/app.elf   # T5 双核，两个 elf 各跑一次
```

输出为按组件分组的表格，每行一个统计对象：

```
rom-----ro------ram-----data----bss-----weak----components
...     ...     ...     ...     ...     ...     tal_system
...     ...     ...     ...     ...     ...     audio_player
...     ...     ...     ...     ...     ...     wukong
```

- **分组规则**：`components/` 下各 SDK 组件、`src/miscs/` 与 `src/drivers/` 下各模块目录、`wukong`（应用主体）、`vendor`（平台）各成一行；末尾 `no-path` 行是无源码路径的符号（汇编/预编译库），`all` 行是全量合计。
- **列含义**：`rom` = 代码+常量+已初始化数据（flash 占用）；`ro` = 只读常量；`ram` = data+bss（静态 RAM 占用）；`data`/`bss` = 已初始化/未初始化变量；`weak` = 弱符号。
- **符号类型对照**（`nm` 输出第三列，脚本按此分类）：

| 符号 | 含义 | 归属 |
|---|---|---|
| `t` / `T` | 静态 / 全局函数（代码段） | ROM |
| `r` / `R` | 静态 / 全局常量 | ROM（只读） |
| `d` / `D` | 静态 / 全局已初始化变量 | RAM + ROM（初值） |
| `b` / `B` | 静态 / 全局未初始化变量 | RAM（BSS） |

注意：统计的是**静态**占用；线程栈（PSRAM/堆分配）、堆运行时分配不在其中。若本机 `nm` 解析交叉 elf 异常，改用工具链的 `arm-none-eabi-nm`（路径见[崩溃日志定位](troubleshooting/crash-analysis.md)的工具链一节）。

## 四、运行时观测

- **线程栈水线（最重要）**：SDK 健康监测周期性（约每 10 分钟）在 DEBUG 日志输出一次全部 `tal_thread` 线程的栈水线，来源 `tal_thread_dump_watermark()`（`components/tal_system/src/os/tal_thread.c`，由 `svc_devos` 健康监测调度），格式：

  ```text
  [tal_thread.c:336] thread[ai_player       ] stack[12288] free[10228]
  [tal_thread.c:336] thread[ai_toy_state    ] stack[ 3072] free[ 1716]
  ```

  `stack` 为配置栈大小，`free` 为 FreeRTOS 栈高水位——**运行以来的历史最小剩余**，不是当前瞬时值。`free` 持续逼近 0 的线程即将溢出，优先加栈；反之长期富余很大的线程可考虑缩栈。这份日志同时覆盖 SDK 线程与应用线程，也是校对第二节表格实际生效值的最直接手段。
- **剩余堆**：日志中的 `mem left:<N>`（`tal_system_get_free_heap_size()`），按键触发等事件时打印。
- **播放链路耗时/流量**：`[AP-STAT]` 诊断，见[播放卡顿排查](troubleshooting/audio-playback-stutter.md)。
- **TEF 动图解码开销**：`TEF_BENCH=1` 基准报告，见 [TEF 动图](howto/use-tef-animation.md)。
- 栈已经溢出时的定位方法见[崩溃日志定位](troubleshooting/crash-analysis.md)的栈溢出模式。

## 维护说明

- 新增/修改线程（优先级、栈、创建条件）的 MR，随同一 MR 更新第二节表格对应行；表中"来源"列路径受 `scripts/docs_check.py` 存在性校验。
- 发版归档 changelog 时顺手核对一遍本表与基准声明。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
