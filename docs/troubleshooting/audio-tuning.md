# 语音算法调试指南

本文排查**语音上行链路的算法类问题**：回声消除不干净、打断（双讲）失灵、噪声抑制不足或过度、VAD 漏检/误触发。硬件声学结构（麦克风/喇叭布局、削波、谐波失真）的测试验收见[硬件声学结构测试](acoustic-test.md)；唤醒词/KWS 灵敏度见[更换唤醒词](../howto/customize-wakeword.md)；播放侧（音乐/TTS）卡顿见[播放卡顿排查](audio-playback-stutter.md)。

**适用范围**：板载麦克风模式（`CONFIG_USING_BOARD_AUDIO_INPUT=y`）且 T5 模组。UART 外接语音芯片模式下 3A 算法在外接芯片内完成，不适用本文。当前无串口在线调参能力，调试方式为**改源码/Kconfig 参数后重编**，配合抓取音频数据离线分析。

## 现象速查

| 你遇到… | 看章节 |
|---|---|
| 播放 TTS/音乐时设备自己被回声触发、识别串音 | [3. Tuya 前端：残留回声抑制](#3-tuya-前端speex-aecaesns--rnn-vad) |
| 播放中喊唤醒词/说话打断不了（双讲失灵） | [3. Tuya 前端：残留回声抑制](#3-tuya-前端speex-aecaesns--rnn-vad) |
| 环境噪声大、识别率低，或降噪过度切掉人声 | [3. Tuya 前端：降噪](#3-tuya-前端speex-aecaesns--rnn-vad) |
| 说了话没反应（VAD 漏检）/ 静音被当成说话（误触发）| [4. VAD 调试](#4-vad灵敏度与断句) |
| 双麦板（SPRS 前端）效果不佳 | [5. SPRS 双麦前端](#5-sprs-双麦前端) |

## 1. 先认清当前用的是哪个前端

音频前端（AEC/NS/VAD）是编译期二选一，Kconfig choice 在 `make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai` → `Audio Configuration` → `Audio Input` → `Audio frontend engine`（定义于 `src/miscs/audio_player/Kconfig`）：

| 选项 | 引擎 | 麦克风形态 | 实现 |
|---|---|---|---|
| `USING_TUYA_AUDIO_FRONTEND`（默认） | Speex AEC + AES + NS，RNN VAD | 单麦 + REF 回采 | `src/wukong/audio/frontend/aec_vad/tuya/wukong_audio_aec_vad.c` |
| `USING_SPRS_AUDIO_FRONTEND` | SPRS ESNR（AEC/NR/VAD 一体） | 双麦（DMIC LR）+ REF（ADC） | `src/wukong/audio/frontend/aec_vad/sprs/wukong_audio_sprs.c` |
| `USING_3RD_AUDIO_FRONTEND` | 占位项，未实现 | — | — |

两个前端都实现 `WUKONG_AUDIO_FRONTEND_OPS_T` 回调集，由 `src/wukong/audio/frontend/wukong_audio_frontend.c` 统一分发；注册发生在 `src/wukong/audio/input/wukong_audio_input_board.c` 的管线初始化里（按上述宏二选一）。历史接口 `wukong_aec_vad_*`（头文件 `src/wukong/audio/frontend/aec_vad/tuya/wukong_audio_aec_vad.h`）仍可用，是对分发器的薄封装。

切换前端后必须重跑 `make app_config APP_NAME=tuyaos_demo_wukong_ai` 再 `make app`。

## 2. 抓数据：dump 通道与工具

用涂鸦官方串口工具 [tyuTool](https://github.com/tuya/tyutool/tree/master)（debug ser / ser_auto 模式）发送控制命令并抓取各通道 PCM（16k/16bit）。命令表、测试音（`bg 0/1/…`）、自动化测试见[硬件声学结构测试](acoustic-test.md)——两篇共用同一套命令，**通道定义以下表为准**（对应 `src/miscs/audio_analysis/audio_dump.h` 的 `AUDIO_DUMP_*` 宏）：

| 命令 | 通道 | 内容 | 写入点 |
|---|---|---|---|
| `dump 0` | MIC | 麦克风原始数据 | 前端 `process` 内（AEC 处理前） |
| `dump 1` | REF | 喇叭回采参考信号 | 同上 |
| `dump 2` | AEC | 回声消除后的输出 | 同上 |
| `dump 3` | KWS | 送入唤醒词引擎的数据 | `src/wukong/audio/frontend/kws/wukong_kws.c` |
| `dump 4` | VAD | VAD 判定后**实际上传云端**的语音切片 | `src/wukong/audio/input/wukong_audio_input_board.c` |

典型流程：`reset` → `start` → 播放测试音或真人说话 → `stop` → `dump 0`/`dump 1`/`dump 2` 依次抓取，导入 Audition / ocenaudio 对比 AEC 前后波形与频谱。对比 `dump 2` 与 `dump 4` 可检查 VAD 切音边界是否吃掉了首尾字。

需要**长时段、5 路同步、真实使用场景**的抓取（如边放音乐边对话查 AEC 残留），改用 SD 卡抓流通路：设备 UI 一键开关、整段落盘、PC 端脚本转 WAV，见[音频上行 5 路抓流](audio-stream-capture.md)。

**调参前先固定环境**，避免变量干扰判断：麦克风型号与 `micgain`、喇叭型号与 `volume`、产品外壳/音腔结构，三者固定后再评估算法。

## 3. Tuya 前端（Speex AEC/AES/NS + RNN VAD）

参数在 `src/wukong/audio/frontend/aec_vad/tuya/wukong_audio_aec_vad.c` 的 `__speex_rnn_init()` 里硬编码（第 38–39 行），改值重编生效：

**残留回声抑制**：`speex_aes_set_param(handle, 5)`

- 值**越大**：残留回声压得越干净，但双讲时近端人声也容易被压掉，**打断变差**。
- 值**越小**：近端语音保留多、打断好，但残留回声增多。
- 调法：播放 TTS 时残留回声可闻/误触发识别 → 适当调大；播放中喊话打断不灵 → 适当调小。每次只动这一个值，用 `dump 2` 对比。

**降噪**：`speex_ns_set_param(handle, 8, 10)`

- `level1` = 降噪力度，越大越强；`level2` = 底噪水平，越小可抑制的噪声越多。
- 高信噪比环境：大 `level1`、小 `level2`；低信噪比环境：小 `level1`、大 `level2`（避免把人声一起切掉）。

算法调参之外，减小喇叭音量、拉开麦克风与喇叭间距等结构手段往往更有效——结构性问题先按[硬件声学结构测试](acoustic-test.md)验收。

## 4. VAD（灵敏度与断句）

RNN VAD 的三个可调点：

**灵敏度档位**：`wukong_vad_set_threshold(level)`，运行时可调，档位映射为 RNN 阈值：

| 档位 | 阈值 | 效果 |
|---|---|---|
| `WUKONG_AUDIO_VAD_HIGH` | -40 dB | 不易误触发 |
| `WUKONG_AUDIO_VAD_MID`（默认） | -50 dB | — |
| `WUKONG_AUDIO_VAD_LOW` | -60 dB | 更易触发 |

**断句参数**：初始化入参在 `src/tuya_ai_toy.c` 的 `audio_cfg.board` 里（`vad_active_ms = 500`、`vad_off_ms = 1000`），经 `wukong_audio_frontend_init()` 传给前端：

- `vad_active_ms` → 最短有效语音时长（ms）：过小易把咳嗽/敲击声误报为语音。
- `vad_off_ms` → 最大静音间隔（ms）：说话中停顿超过该值即判一句话结束；说长句常被截断就调大它。

**调试建议**：

- 漏检（说了话没反应）：档位改 `WUKONG_AUDIO_VAD_LOW`，或调大 `vad_off_ms`。
- 误触发（静音/噪声被判成语音）：档位改 `WUKONG_AUDIO_VAD_HIGH`，或调大 `vad_active_ms`。
- 日志判据：VAD 起止时串口打印 `[vad start]` / `[vad stop]`（`wukong_audio_aec_vad.c`），结合 `dump 4` 数据核对切音边界。

## 5. SPRS 双麦前端

SPRS ESNR 前端（`USING_SPRS_AUDIO_FRONTEND=y`）为双麦一体化算法（AEC/NR/VAD 内置，32ms/512 样点帧），可调项都在 Kconfig（同菜单内，改后 `make app_config` 重编）：

- `SPRS_AGC_TARGET_LEVEL`（0–9，默认 5，0=关）：AGC 目标电平。
- `SPRS_ENABLE_AEC`（默认 n）：SPRS 额外 AEC，回声消除效果不足时开启并实测（增加 CPU 消耗）。

初始化成功时串口打印 `sprs esnr version …`（含 SRAM 配额与上面两个参数的生效值，见 `wukong_audio_sprs.c`），先确认参数确实生效再评估效果。双麦板效果不佳时，优先用[声学测试](acoustic-test.md)的「麦克一致性」项排除硬件问题（相关系数应 > 0.7），再回来调算法参数。

## 常见问题

**改了参数没效果**
Kconfig 项（前端选择、SPRS 参数）改后少跑了 `make app_config APP_NAME=tuyaos_demo_wukong_ai`；源码硬编码参数（speex 两组）只需重新 `make app`。确认方法：看 `include/tuya_app_config.h` 里对应宏的值。

**UART 外接语音芯片模式下想调 3A**
`CONFIG_USING_UART_AUDIO_INPUT=y` 时本文的前端代码不参与编译，3A 在外接芯片内完成，需按芯片厂商文档调试。

**dump 出来的数据全是静音/长度为 0**
先发 `start` 再制造声音、最后 `stop`，顺序错了缓存里没有数据；另确认板载麦克风模式（UART 模式下这些通道无数据）。

**残留回声和打断永远调不到都满意**
`speex_aes_set_param` 本质是二者的折中旋钮，算法上限受硬件影响很大——回采信号削波、麦克风离喇叭过近时，先解决结构问题（见[声学测试](acoustic-test.md)「削波失真」），再谈参数。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
