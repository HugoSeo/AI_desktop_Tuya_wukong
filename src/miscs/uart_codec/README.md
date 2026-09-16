# uart_codec — 离线语音模组 UART 对接组件

## 概述

本组件是**涂鸦离线语音对接协议**的模组侧实现：涂鸦模组（T5 等）通过 UART（1M 波特率、8N1）外挂离线语音芯片，芯片负责拾音、声学前端（AEC/降噪）与**离线唤醒**，模组负责云端 AI 对话、TTS/音乐播放与业务逻辑。当前内置两种芯片适配（`TDL_COMM_AUDIO_MODULE_E`）：

| 芯片 | 厂商 | 下行播放格式（默认） | 备注 |
|---|---|---|---|
| GX8006 | 国芯微 | PCM | 上电需拉 BOOT 引脚 |
| CI1302 | 启英泰伦 | MP3 | `tool/CI1302/` 内含参考固件与离线烧录工具 |

同协议的其他型号（如启英泰伦同系列芯片）可按 CI1302 的路径对接。协议帧格式为 `55aa + 版本 + 命令字 + 长度 + 数据 + 校验和`（大端），完整帧格式与全部命令定义见涂鸦开发者平台《涂鸦离线语音对接协议》（文档中心即将上架）。

### 分层结构

| 层 | 位置 | 职责 |
|---|---|---|
| tdl 业务层 | `src/tdl_comm_audio.c`、`include/tdl_comm_audio.h` | 对应用的全部接口：初始化/上电时序、SPK 流式播放（start/data/stop）、音量与 MIC 增益、离线语音参数（拾音/静默/唤醒/VAD/mute 电平）、6 类回调注册、录音播音产测、芯片固件版本与 OTA TP 查询、自学习命令词 |
| tdd 驱动层 | `src/tdd_comm_audio.c` | 协议命令编排（0x92 子命令的编解码与应答处理）、按芯片型号区分的上电/BOOT 时序、SPK 下行流控 |
| gfw_core 帧框架 | `src/gfw_core/` | `55aa` 帧封装/校验、命令队列与响应超时（3s）、收发线程（`gfw_core`，PRIO_1/4KB）、自动波特率探测（`TY_AUDIO_BAUD_LIST`，当前仅 `{1000000}`） |
| gfw_mcu_ota | `src/gfw_mcu_ota/` | 语音芯片固件**云端 OTA**（借涂鸦 MCU OTA 通道，命令字 `0x0a`/`0x0b`），独立 TP 号 |
| tool | `tool/CI1302/` | CI1302 参考固件 bin、`PACK_UPDATE_TOOL` 离线烧录工具与烧录步骤文档 |

### 已实现的协议子命令（命令字 0x92）

`0x01` 版本查询 ·`0x02` MIC 配置 ·`0x03` SPK 配置 ·`0x04` 唤醒引脚 ·`0x05` MIC 数据上行 ·`0x06` SPK 播放 ·`0x07` 离线语音参数配置 ·`0x08` 唤醒事件 ·`0x09` 休眠事件 ·`0x0A` 状态查询 ·`0x0B` 录音播音测试 ·`0x0C` 音频回路测试 ·`0x0D` GPIO 测试 ·`0x12` MCU 事件上报 ·`0x17` 带格式 SPK 播放 ·`0x19` 自学习（仅 CI 系列）·`0x1A` 扩展唤醒事件

应用侧完整参考实现见 wukong app 仓库：`src/wukong/audio/input/wukong_audio_input_uart.c`（输入与回调编排）、`output/wukong_audio_output_uart.c`（播放）、`frontend/kws/uart/uart.c`（唤醒事件对接）；接入步骤见该仓库 `docs/howto/connect-uart-voice-chip.md`。

## 设计要点与坑

1. **初始化是异步握手，成功判据是收到芯片的版本上报**（日志 `[HL] init success`）。三种等待姿势按需选：`tdl_comm_audio_init(cfg, timeout)` 阻塞等、`init_no_wait()` + `wait_init(max_delay_ms)` 分步等、`init_flag_get()` 轮询。init 可重入，重复调用不会重复建线程。
2. **trigger_mode（会话状态机）由应用维护，组件不管**：唤醒后开拾音、静默后关拾音这类状态流转，全靠应用在 `wake_up_cb`/`mic_status_cb`/`silence_timeout_cb` 里调 `voice_cfg_set_mic()`/`set_silent()` 驱动。组件早期版本内置过该逻辑，已明确剔除——不要期望组件自动收敛会话状态。
3. **芯片复位会触发 `reset_cb`，应用必须在里面恢复业务参数**：芯片每次上电（含 OTA 重启、异常复位）主动上报版本，组件以此判定"芯片复位过"并回调。组件内部只自动重设 `mic_time` 和 `silence_timeout` 两项，其余运行态（拾音开关、VAD、音量等）要应用自己重新下发。
4. **SPK 下行受流控 IO 约束**：`spk_flow_io` 由芯片侧拉高表示"可以发"、拉低表示"不能发"（默认高有效，可配）。播放必须按 `spk_write_stream_start() → data() → stop()` 的流式三段走；下行格式按芯片能力选（8006=PCM、1302=MP3，指定格式单发走 0x17）。
5. **静默超时：定时器在组件、动作在应用**：`silence_timeout_start()` 启动组件内定时器，超时后仅回调 `silence_timeout_cb`，关拾音/进静默由应用决定。超时时长 `set_silence_timeout()` 建议范围 30–180s（默认 30），最大拾音时间 `set_mic_time()` 建议范围 10–60s（默认 30）——**这两项组件不做越界校验**（直接透传给芯片，且形参为有符号 8 位 `CHAR_T`，最大只能表达 127），调用方自行保证取值合理；音量/MIC 增益越界则被**截断到边界**而非返回错误。
6. **低功耗**：各模块均有 `deinit`，配合 `power_io` 断电实现整链路下电；重新上电用 `tdl_comm_audio_power_on(boot_io, power_io)`——`boot_io` 仅 GX8006 需要（其他型号忽略该参数）。
7. **芯片固件升级两条路**：云端 OTA（`gfw_mcu_ota_init()` 注册涂鸦 MCU OTA 通道，TP 号由 `tdl_comm_audio_ota_tp_get()` 提供，8006 已实测）；或产线/调试期用 `tool/` 下的离线烧录工具直刷。
8. **唤醒事件带扩展信息**：`0x08`/`0x1A` 唤醒上报可携带唤醒词 semantic_id、能量与置信度（`TDD_COMM_AUDIO_WAKEUP_INFO_T`），自定义唤醒策略（如按置信度过滤）从这里取数。

## 改动指南

- **新增芯片型号**：`TDL_COMM_AUDIO_MODULE_E` 加枚举 → `tdd_comm_audio.c` 里按型号分支处理上电时序与默认格式差异 → 应用侧板级映射跟上（wukong app 在 `src/boards/tuya_board_config.h` 与 `src/miscs/audio_player/Kconfig` 的 `UART Codec` 菜单做 vendor 选择）。协议兼容的型号通常只需要这三处。
- **新增/扩展协议子命令**：`include/gfw_core/gfw_core_cmd.h` 的 `GFW_0x92_SUBCMD_E` 加枚举 → `tdd_comm_audio.c` 实现编解码与应答/事件分发 → 需要暴露给应用的在 `tdl_comm_audio.h/.c` 加接口。帧收发、校验、命令队列复用 `gfw_core`，不要绕开它自己读写 UART。
- **调整波特率**：改 `include/gfw_core/gfw_core.h` 的 `TY_AUDIO_BAUD_LIST`（自动探测按列表逐个尝试）。
- **芯片固件 OTA 行为**：云通道逻辑在 `src/gfw_mcu_ota/gfw_mcu_ota.c`；离线烧录看 `tool/` 下对应芯片目录内的说明文档。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
