# 对接 UART 离线语音芯片（GX8006 / CI1302）

## 目标

把离线语音芯片（国芯微 GX8006、启英泰伦 CI1302 等）通过 UART 外挂到 wukong 板型上：芯片负责拾音、声学前端与**离线唤醒**，涂鸦模组负责云端 AI 对话与播放。适用于模组不接麦克风/喇叭、或希望唤醒词由外置芯片承载的产品形态。

```
离线语音芯片(MCU)  ── UART 1M/8N1，涂鸦离线语音协议 ──  涂鸦模组(T5 等)
  拾音 + AEC/降噪 + 离线唤醒                      云端对话 + TTS/音乐下发播放
```

协议实现封装在组件 `src/miscs/uart_codec/`（分层与设计要点见其 README）；app 侧参考实现是 `src/wukong/audio/input/wukong_audio_input_uart.c`（输入与回调编排）、`src/wukong/audio/output/wukong_audio_output_uart.c`（播放）、`src/wukong/audio/frontend/kws/uart/uart.c`（唤醒对接）。协议本身（帧格式、全部命令）见涂鸦开发者平台《涂鸦离线语音对接协议》（文档中心即将上架）。

## 前置条件

- 已按[快速开始](../quickstart.md)跑通任意板子的编译烧录。
- **硬件接线**（引脚号都可在 menuconfig 里配）：
  | 信号 | 必要性 | 说明 |
  |---|---|---|
  | UART TX/RX | 必须 | 模组 UART0 或 UART2，波特率 1M、8N1，与芯片交叉相连 |
  | POWER IO | 必须 | 模组控制芯片供电，高电平工作 |
  | BOOT IO | 仅 GX8006 | 上电时序需要，CI1302 忽略 |
  | SPK 流控 IO | 建议 | 芯片拉高=模组可发播放数据（有效电平可配）；不接则大段播放可能溢出 |
- **芯片侧固件**：必须烧录**支持涂鸦离线语音协议**的固件（含唤醒词模型）。获取渠道：GX8006 见 [论坛帖](https://tuyaos.com/viewtopic.php?t=9147)，CI1302 见 [论坛帖](https://tuyaos.com/viewtopic.php?t=9148)；`src/miscs/uart_codec/tool/CI1302/` 内含参考固件与离线烧录工具（含烧录步骤文档）。唤醒词由芯片固件决定，换唤醒词=换芯片固件。

## 步骤

### 1. 切换音频输入/输出到 UART

`make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai` → `Audio Configuration`：

- `Audio input source` → **UART (external codec)**（`CONFIG_USING_UART_AUDIO_INPUT=y`）
- `Audio output target` → **UART (external codec)**（`CONFIG_USING_UART_AUDIO_OUTPUT=y`）

两项通常同时切换（芯片同时接管麦克风和喇叭）。切到 UART 模式后，板载 KWS（TUTUClear 等）相关代码不再编译，唤醒完全交给芯片。

### 2. 配置 UART Codec 子菜单

开启 UART 输入后出现 `UART Codec` 菜单：

| 配置项 | 说明 |
|---|---|
| `UART codec chip vendor` | GX8006 / CI1302 二选一；决定上电时序与默认下行播放格式（8006=PCM，1302=MP3） |
| `UART port number` | 与芯片相连的模组 UART 端口（0 或 2） |
| `Codec BOOT mode GPIO` | 仅 GX8006 生效 |
| `Codec power control GPIO` | 芯片供电控制引脚 |
| `Speaker flow-control GPIO` 及有效电平 | SPK 下行流控 |
| `UART codec upload audio format` | 上行音频格式：SPEEX（默认，CPU 占用低）/ OPUS（压缩率高） |

改完执行 `make app_config APP_NAME=tuyaos_demo_wukong_ai` 重新生成配置头，再编译烧录。

### 3.（可选）调整会话参数

最大拾音时间（10–60s）与静默超时（30–180s）等运行参数、唤醒后的拾音开关策略，都在 `wukong_audio_input_uart.c` 的回调编排里，按产品需要调整。芯片复位后参数恢复也在该文件的 reset 回调中处理。

## 验证方法

上电时序：模组先给芯片上电（组件内已按 POWER/BOOT 时序处理），随后串口日志按序出现：

| 日志 | 含义 |
|---|---|
| `[HL] init success` | 收到芯片版本上报，UART 链路与协议握手成功 |
| `[HL] init failed` | 超时未收到版本上报，见常见问题第 1 条 |
| `wake up cb` | 说唤醒词后芯片上报唤醒事件，app 已收到 |

对芯片说唤醒词 → 设备进入聆听并正常对话 → TTS/音乐从芯片侧喇叭播出，即全链路打通。

## 常见问题

**`[HL] init failed`，一直收不到芯片版本上报**
按序排查：① TX/RX 是否交叉接反、端口号是否与 menuconfig 一致；② 芯片固件是否为**含涂鸦离线语音协议**的版本（普通固件不会上报版本）；③ POWER/BOOT 引脚号与实际接线是否一致（8006 必须接 BOOT）；④ 波特率固定 1M，芯片固件波特率不一致会握手失败。

**说唤醒词没反应**
唤醒词由**芯片固件**决定，与模组侧配置无关；确认烧录的芯片固件唤醒词与你说的一致。模组侧 KWS 在 UART 模式下不编译，[定制唤醒词](customize-wakeword.md)一文的模型替换流程不适用于此模式（见该文常见问题）。

**播放无声或断续**
① SPK 流控 IO 未接或有效电平配反——芯片侧拉高才允许模组发数据；② 下行格式与芯片固件能力不匹配（vendor 选对即用默认格式，无需手改）；③ 音量确认 `tdl_comm_audio_spk_volume_set()` 有被调用（wukong 已接入系统音量）。

**芯片固件怎么升级**
两条路：走涂鸦云 OTA（芯片固件作为独立 TP 通道推送，组件 `gfw_mcu_ota` 已接好）；或产线/调试期用 `src/miscs/uart_codec/tool/` 下的离线烧录工具直刷。

**能接协议兼容的其他芯片型号吗**
可以。协议一致的型号（如启英泰伦同系列）按 CI1302 路径对接即可；行为有差异（上电时序、默认格式）时参照 `src/miscs/uart_codec/README.md`「改动指南·新增芯片型号」扩展。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
