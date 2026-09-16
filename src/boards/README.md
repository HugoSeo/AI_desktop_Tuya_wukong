# 板级支持包 (BSP)

## 概述

`src/boards/` 是各硬件开发板的板级支持包（BSP）：每个板型目录下的 `tuya_device_board.c` 实现
GPIO / 外设（显示屏、摄像头、按键）初始化，`Kconfig` 提供该板型的引脚 / 屏参 / 电池等硬件默认值。
**UI 统一由 `src/ui/` 框架提供**（`ENABLE_TUYA_UI` 控制编译）：板子既可不放 `ui/` 目录、走框架默认的
完整 wukong 页面，也可以在自身 `ui/` 目录下实现自定义界面并通过 `ui_app_register_board_ui()` 接管。

板型选择三段式：`make app_config_choice APP_NAME=tuyaos_demo_wukong_ai` 从 `build/appconfig/<板型名>`
预置配置中选一个 → 生效为 `CONFIG_<BOARD>=y` → 该板型在 `src/boards/Kconfig` 中以
`default ... if <BOARD>` 声明的硬件默认值（引脚、屏参、电池等）随之落到 `include/tuya_app_config.h`。
新增/修改板型的硬件默认值都改这个 Kconfig 文件，而不是散落改各处业务代码里的常量。

## 板型目录

以下按板型列出关键硬件差异（原理图、引脚、特性）；平台/联网/编解码器等已在下方对比表中列出的字段不再重复。

- **T5AI_BOARD**（标准开发板）：[原理图](https://developer.tuya.com/cn/docs/iot-device-dev/T5-E1-IPEX-development-board?id=Ke9xehig1cabj)。音频触发 GPIO12、扬声器使能 GPIO28、LED GPIO56、配网未用。PID `badnnka7rzrm2idb`。摄像头 DVP 480×480@10fps MJPEG，触摸屏。UI：默认 wukong（无 `ui/` 目录）。

- **T5AI_BOARD_EVB**（白盒子评估板）：[原理图](https://developer.tuya.com/cn/docs/iot/T5AI-EVB-DATA-SHEET?id=Kelbkhytqktgm)。音频触发 GPIO4、扬声器使能 GPIO19、LED GPIO8、配网 GPIO12。电池：充电 GPIO21、容量 GPIO28。PID `e3jrgtmuqsljru1t`。UI：小智风格聊天界面（`ui/xiaozhi_app.c`）。

- **T5AI_BOARD_EVB_PRO**（评估板 Pro）：原理图同 EVB。音频触发 GPIO6、扬声器使能 GPIO26、LED GPIO25、配网 GPIO7。电池：充电 GPIO23、容量 GPIO22。摄像头 DVP（GC2145）。UI：小智风格聊天界面（`ui/xiaozhi_app.c`）。

- **T5AI_BOARD_EYES**（眼睛版）：[原理图](https://developer.tuya.com/cn/docs/iot-device-dev/T5-E1-IPEX-development-board?id=Ke9xehig1cabj)。双屏拼接 128×128、ST7735S，SPI1（`LCD_SPI_DEVICE_NUM=1`），逻辑分辨率 128×256。音频触发 GPIO12、扬声器使能 GPIO28，LED/配网未用。摄像头 DVP 480×480@10fps MJPEG。UI：眼睛表情应用（`ui/eyes_app.c`），TEF 播放器脏块直推 GRAM（框架 `src/miscs/tef/`，素材 `ui/eyes_tef/`），不经 LVGL——预置配置为 `ENABLE_TUYA_UI=n` + `ENABLE_TUYA_DISPLAY=y`。

- **T5AI_BOARD_ROBOT**（机器狗）：[原理图](./T5AI_BOARD_ROBOT/T5AI-DOG_V1.pdf)。320×172 横屏 ST7789P3。音频触发 GPIO5、扬声器使能 GPIO26、配网 GPIO4，LED 未用。电池：充电 GPIO21、容量 GPIO28。摄像头 UVC 640×480@15fps MJPEG。UI：机器人应用（`ui/robot/robot_app.c`）。

- **T5AI_BOARD_DESKTOP**（桌面版）：[基座原理图](./T5AI_BOARD_DESKTOP/doc/Baseboard.pdf) / [主体原理图](./T5AI_BOARD_DESKTOP/doc/T5-E1.pdf)。320×240 ST7789V2。音频触发 GPIO28、扬声器使能 GPIO26，LED/配网未用。电源管理：设备电源 GPIO4、电源/网络键 GPIO3（`DEVICE_POWER_CONTROL` 默认开启）。电池：充电 GPIO2、容量 GPIO12。摄像头 DVP。UI：默认 wukong（分辨率经 `ui_adaptive` 自适应 320×240）。

- **T2AI_BOARD**（T2 标准开发板）：320×480 ILI9488。音频触发 GPIO12、扬声器使能 GPIO28、LED GPIO56。不支持摄像头（camera Kconfig 未纳入该板）。音频编码：Opus（平台编解码）。UI：默认 wukong。

- **T3AI_BOARD**（T3 无屏语音方案）：无显示屏、不支持摄像头。音频编码：UART 外接 CI1302 语音芯片（Speex）。无 UI。

- **T3_V_PRO_BOARD**（T3 Pro 带屏）：320×480 ILI9488，不支持摄像头，UART 外接 CI1302（Speex）。UI：默认 wukong。

- **T1AI_BOARD**（T1 带屏方案）：320×480 ILI9488，不支持摄像头，UART 外接 CI1302（Speex）。UI：默认 wukong。

- **L511_Y6E_BOARD / L511_Y7PM_BOARD**（L511 蜂窝模组，无屏语音）：无显示屏、不支持摄像头；联网走蜂窝模组（`tuya_cellular_cloud.c`）；音频编码 UART 外接 CI1302（Speex）。Y6E：音频触发 GPIO25、LED GPIO16。Y7PM 硬件同 Y6E。无 UI。

- **LE270AI_BOARD**（LE270 蜂窝模组，无屏语音）：无显示屏、不支持摄像头，联网走蜂窝模组（`tuya_cellular_cloud.c`）。音频编码：UART 外接 GX8006 语音芯片（Opus）。音频触发 GPIO5、扬声器使能 GPIO28、LED GPIO56。无 UI。

- **RTL8720CF_VU2_BOARD**（RTL8720CF 带屏方案）：320×480 ILI9488，不支持摄像头，UART 外接 CI1302（Speex）。UI：默认 wukong。

- **Ubuntu**（模拟环境）：无显示屏/摄像头（均为模拟）。扬声器使能 GPIO26。音频编码：Speex（UART 编解码器，CI1302 芯片）。用于 PC 端开发调试，无硬件依赖。无 UI。

### 开发板对比表

| 开发板 | 平台 | 显示屏 | 摄像头 | 电池 | 联网 | 编码器 | UI | 使用场景 |
|--------|------|--------|--------|------|------|--------|----|----------|
| **T5AI_BOARD** | T5 | 320×480 ILI9488 | ✅ DVP | ❌ | Wi-Fi | Opus | 默认 wukong | 标准开发 |
| **T5AI_BOARD_EVB** | T5 | 240×240 ST7789 | ❌ | ✅ | Wi-Fi | Opus | 小智 | 评估测试 |
| **T5AI_BOARD_EVB_PRO** | T5 | 240×240 ST7789 | ✅ DVP | ✅ | Wi-Fi | Opus | 小智 | 评估 + 摄像头 |
| **T5AI_BOARD_EYES** | T5 | 128×128 ST7735S×2 | ✅ DVP | ❌ | Wi-Fi | Opus | 眼睛 | 表情展示 |
| **T5AI_BOARD_ROBOT** | T5 | 320×172 ST7789P3 | ✅ UVC | ✅ | Wi-Fi | Opus | 机器人 | 机器人应用 |
| **T5AI_BOARD_DESKTOP** | T5 | 320×240 ST7789V2 | ✅ DVP | ✅ | Wi-Fi | Opus | 默认 wukong | 桌面设备 |
| **T2AI_BOARD** | T2 | 320×480 ILI9488 | ❌ | ❌ | Wi-Fi | Opus | 默认 wukong | T2 标准开发 |
| **T3AI_BOARD** | T3 | 无 | ❌ | ❌ | Wi-Fi | UART CI1302 Speex | 无 | 无屏语音 |
| **T3_V_PRO_BOARD** | T3 | 320×480 ILI9488 | ❌ | ❌ | Wi-Fi | UART CI1302 Speex | 默认 wukong | T3 带屏 |
| **T1AI_BOARD** | T1 | 320×480 ILI9488 | ❌ | ❌ | Wi-Fi | UART CI1302 Speex | 默认 wukong | T1 带屏 |
| **L511_Y6E_BOARD** | L511 | 无 | ❌ | ❌ | 蜂窝 | UART CI1302 Speex | 无 | 蜂窝无屏语音 |
| **L511_Y7PM_BOARD** | L511 | 无 | ❌ | ❌ | 蜂窝 | UART CI1302 Speex | 无 | 蜂窝无屏语音 |
| **LE270AI_BOARD** | LE270 | 无 | ❌ | ❌ | 蜂窝 | UART GX8006 Opus | 无 | 蜂窝无屏语音 |
| **RTL8720CF_VU2_BOARD** | RTL8720CF | 320×480 ILI9488 | ❌ | ❌ | Wi-Fi | UART CI1302 Speex | 默认 wukong | RTL8720 带屏 |
| **Ubuntu** | — | 无 | 无 | 无 | — | Speex | 无 | 模拟环境 |

> 摄像头列「✅」表示硬件支持，需在 menuconfig 中开启 `ENABLE_TUYA_CAMERA` 才启用。

## 设计要点与坑

**board init 时序**：`tuya_device_board_init()` 固定按 GPIO → 外设（显示屏/摄像头/按键）→ UI 三步走；
UI 步骤要么什么都不做（走默认 wukong）要么调用 `ui_app_register_board_ui()`，必须放在外设初始化之后、
函数返回之前——UI 框架的 `ui_app_init()` 在此之后才被上层调用。

**UI 两种接入方式的关键约束**：不放 `ui/` 目录 = 默认 wukong 页面；放 `ui/` 目录并实现
`app_ui_init()` / `app_ui_msg_handler()` = 自定义 UI，但**必须**在 `tuya_device_board_init()` 里调用
`ui_app_register_board_ui(app_ui_init, app_ui_msg_handler)` 完成注册，且这次注册必须先于框架的
`ui_app_init()` 执行——否则框架检测不到注册，仍然会走默认 wukong 页面（常见踩坑：注册代码写对了但放
错了调用时机）。消息类型定义见 `src/miscs/display/tuya_ai_display.h`，业务消息经
`src/ui/port/tuya_ai_display_stub.c` 桥接到 UI 框架。

自定义 board UI 需要实现的两个接口签名（`src/ui/ui_app.h` 的 `ui_board_init_fn`/`ui_board_msg_fn`）：

| 接口 | 签名 | 说明 |
|------|------|------|
| `app_ui_init` | `void app_ui_init(void)` | 建屏：创建界面、注册回调等 |
| `app_ui_msg_handler` | `void app_ui_msg_handler(TY_DISPLAY_MSG_T *msg)` | 处理显示层下发的消息（对话、状态、表情等） |

两文件对照示例（以 `T5AI_BOARD_EYES` 为例，已核对与当前代码一致）：

```c
/* src/boards/<BOARD>/ui/<app>.c */
void app_ui_init(void)
{
    /* 创建界面 … */
}

void app_ui_msg_handler(TY_DISPLAY_MSG_T *msg)
{
    /* 处理消息 … */
}
```

```c
/* src/boards/<BOARD>/tuya_device_board.c */
#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
extern void app_ui_init(void);
extern void app_ui_msg_handler(TY_DISPLAY_MSG_T *msg);
#endif

OPERATE_RET tuya_device_board_init(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;

#if defined(ENABLE_TUYA_UI) && (ENABLE_TUYA_UI == 1)
    /* …显示屏等外设初始化… */
    ui_app_register_board_ui(app_ui_init, app_ui_msg_handler);
#endif

    return rt;
}
```

**T5 SPI0 pinmux 坑**：T5 平台 SPI0 默认 pinmux 在 14/15/16/17 脚；屏接在 44-47 脚（如 ROBOT/EVB_PRO）
必须在板级初始化里显式重配 pinmux，否则显示静默失败且无任何报错。详见
[适配新板 · 常见问题](../../docs/howto/porting-new-board.md)。

**RGB565 字节交换坑**：UI 框架按原生 RGB565 渲染，走 8-bit SPI 的屏若背景发粉/颜色不对，需要开启
`UI_LCD_RGB565_BYTE_SWAP`。详见 [适配新板 · 常见问题](../../docs/howto/porting-new-board.md)。

## 改动指南

- **添加新开发板**（建目录、接入编译系统、补 Kconfig 默认值、编译烧录验证）：完整步骤见
  [适配一块新板子](../../docs/howto/porting-new-board.md)。
- **定制 UI**（改已有页面 / 新增页面 / 整板自定义 UI 三种改法怎么选）：见
  [定制 UI](../../docs/howto/customize-ui.md)。
- **添加蜂窝模组支持**：任一硬件均可支持蜂窝模组，在 `make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai`
  中开启 `ENABLE_CELLULAR_DONGLE`（USB 蜂窝 Dongle）。L511 / LE270 等板型自带蜂窝，见各自
  `tuya_cellular_cloud.c`。
- **添加语音芯片支持**：任一硬件均可外接语音芯片（替代板载麦克风/扬声器做唤醒与编解码）。在
  `make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai` 中勾选 `USING_UART_AUDIO_INPUT` /
  `USING_UART_AUDIO_OUTPUT`，并在对应语音芯片的 Kconfig choice 中选择厂商与编码格式。已支持：

  | 芯片 | 厂商 | 编码格式 |
  |------|------|----------|
  | **GX8006** | 国芯 | Opus — [唤醒词&固件](https://tuyaos.com/viewtopic.php?t=9147) |
  | **GX8008C** | 国芯 | Opus — [唤醒词&固件](https://tuyaos.com/viewtopic.php?t=9166) |
  | **CI1302** | 启英泰伦 | Speex — [唤醒词&固件](https://tuyaos.com/viewtopic.php?t=9148) |

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
