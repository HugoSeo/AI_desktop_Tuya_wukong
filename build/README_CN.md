# Wukong AI 项目配置指南

本文档指导开发者根据自身需求对 `tuyaos_demo_wukong_ai` 项目进行配置，涵盖板型选择、功能开关、AI 模式、UI、音频等配置项。

## 一、配置入口与流程

### 1.1 配置命令

| 命令 | 用途 |
|------|------|
| `make app_config_choice APP_NAME=tuyaos_demo_wukong_ai` | **选择板型**：从 `build/appconfig/` 列出可用板型，选择后加载对应默认配置并生成 `.h` |
| `make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai` | **修改配置**：打开 menuconfig 界面，调整功能开关等配置项 |
| `make app_config APP_NAME=tuyaos_demo_wukong_ai` | **生成配置**：根据当前 `tuya_app.config` 生成 `tuya_app_config.h` |

在 Tuya Wind IDE 中，可通过菜单或右键工程选择「配置」进入 menuconfig。

### 1.2 配置生效流程

```
┌───────────────────┐     ┌──────────────────────┐     ┌─────────────────┐
│ 1. 选择板型         │ ──► │ 2. 进入 menuconfig    │ ──► │ 3. 修改配置项    │
│ app_config_choice  │     │ app_menuconfig       │     │ 保存退出         │
└───────────────────┘     └──────────────────────┘     └─────────────────┘
                                                              │
                                                              ▼
┌─────────────────┐     ┌──────────────────────┐     ┌─────────────────┐
│ 6. 编译固件      │ ◄── │ 5. 编译工程          │ ◄── │ 4. 生成配置文件  │
└─────────────────┘     └──────────────────────┘     │ app_config       │
                                                     └─────────────────┘
```

**说明**：
- 首次使用或切换板型时，先执行 `make app_config_choice` 选择板型
- `app_config_choice` 和 `app_menuconfig` 保存后都会自动调用 `app_config` 生成配置文件
- 不切换板型时，可直接使用 `app_menuconfig` 修改功能配置

---

## 二、按需选型指南

### 2.1 板型选择

通过 `make app_config_choice` 选择板型，系统会从 `build/appconfig/` 加载对应的默认配置。menuconfig 中的 Board type 仅用于展示，切换板型请使用 `app_config_choice`。

当前支持的板型：

| 板型 | 模组 | 显示屏 | 摄像头 | 电池 | 使用场景 |
|------|------|--------|--------|------|----------|
| **T5AI_BOARD** | T5 | 320×480 ILI9488 | DVP | ❌ | 标准开发 |
| **T5AI_BOARD_EVB** | T5 | 240×240 ST7789 | ❌ | ✅ | 白盒评估板 |
| **T5AI_BOARD_EVB_PRO** | T5 | 240×240 ST7789 | DVP | ✅ | 评估板 Pro 版（支持摄像头） |
| **T5AI_BOARD_EYES** | T5 | 128×128 ST7735S | DVP | ❌ | 眼睛表情板 |
| **T5AI_BOARD_ROBOT** | T5 | 320×172 ST7789P3 | UVC | ✅ | 机器狗 |
| **T5AI_BOARD_DESKTOP** | T5 | 320×240 ST7789V2 | DVP | ✅ | 桌面设备 |
| **LE270AI_BOARD** | — | — | ❌ | ❌ | UART 外接语音芯片 |
| **WUKONG_BOARD_UBUNTU** | — | — | — | — | PC 模拟调试 |

板型硬件详细说明请参考 [src/boards/README_CN.md](../src/boards/README_CN.md)。

### 2.2 按功能需求选择

| 需求 | 配置项 | 说明 |
|------|--------|------|
| 无屏幕、纯语音 | 选择板型后**关闭** `ENABLE_TUYA_UI` | 无 LVGL、无显示，节省资源 |
| 需要摄像头 | 选择 T5AI_BOARD / EYES / EVB_PRO / ROBOT / DESKTOP 后，开启 `ENABLE_TUYA_CAMERA` | 支持 P2P 视频、多模态 AI |
| 需要 P2P 视频通话 | 开启 `ENABLE_TUYA_CAMERA` 后，再开启 `ENABLE_AI_MODE_P2P` | 点对点音视频通信 |
| 蜂窝网络（4G 等） | 开启 `ENABLE_CELLULAR_DONGLE` | USB 蜂窝模组 |
| 文件系统（资源存储） | 开启 `ENABLE_TUYA_UI` 后，开启 `TUYA_FILE_SYSTEM` | LittleFS 可选 SD 卡或 SPI Flash |
| AI 绘图 | 选择 T5AI_BOARD 后，开启 `ENABLE_TUYA_PICTURE` | 自动切换为桌面风格 UI |
| 翻译/录音/AI 检测 | 选择 T5AI_BOARD_DESKTOP | 桌面版默认启用这些模式 |

### 2.3 按 AI 对话模式选择

板型选定后，会**自动启用**部分 AI 模式（由板型 `select` 决定）。下表为各板型默认启用的模式：

| 板型 | HOLD | ONESHOT | WAKEUP | FREE | TRANSLATE | RECORD | DETECTION | P2P |
|------|------|---------|--------|------|-----------|--------|-----------|-----|
| T5AI_BOARD | ✓ | ✓ | ✓ | ✓ | - | - | - | 需开摄像头 |
| T5AI_BOARD_EVB | ✓ | ✓ | ✓ | ✓ | - | - | - | - |
| T5AI_BOARD_EVB_PRO | ✓ | ✓ | ✓ | ✓ | - | - | - | 需开摄像头 |
| T5AI_BOARD_EYES | ✓ | ✓ | ✓ | ✓ | - | - | - | 需开摄像头 |
| T5AI_BOARD_ROBOT | ✓ | ✓ | ✓ | ✓ | - | - | - | 需开摄像头 |
| T5AI_BOARD_DESKTOP | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | 需开摄像头 |
| LE270AI_BOARD | ✓ | ✓ | ✓ | ✓ | - | - | - | - |
| WUKONG_BOARD_UBUNTU | ✓ | - | - | - | - | - | - | - |

**TUYA_AI_CHAT_DEFAULT_MODE**：上电默认进入的对话模式（0–5 对应 hold/oneshot/wakeup/free/translate/p2p）。

**如何修改板型支持的对话模式**：在 `build/APPconfig` 中，于对应板型的 `config` 块下添加或删除 `select ENABLE_AI_MODE_xxx`。例如为某板型增加 WAKEUP 模式，添加 `select ENABLE_AI_MODE_WAKEUP`；删除某模式则去掉对应 `select` 行。P2P 模式需先开启 `ENABLE_TUYA_CAMERA`，再在子项中开启 `ENABLE_AI_MODE_P2P`。修改后执行 `make app_config` 生成配置。

**如何修改默认对话模式**：在 menuconfig 中找到 `TUYA_AI_CHAT_DEFAULT_MODE`，将数值改为 0–5 中所需模式，保存后执行 `make app_config` 生成配置。注意：所选模式必须是当前板型已 `select` 的模式，否则会 fallback 到其他已启用模式。用户也可在运行时通过 UI 切换模式，切换结果会保存并在下次上电时优先使用。更多说明见 3.11 节。

---

## 三、配置项详解

配置项按 Kconfig 层级组织，menuconfig 中从上到下依次为：板型选择 → 板级硬件（引脚、显示、电池） → 功能模块（摄像头、MCP 工具集、UI、音频等） → 设备默认值。

### 3.1 板型选择 (BOARD_TYPE)

板型通过 `make app_config_choice` 切换，每个板型的默认配置保存在 `build/appconfig/<板型名>` 文件中。选择板型后，该文件会被加载为当前配置。

menuconfig 中的 Board type 选项标注为「use 'make app_config_choice' to switch」，仅供查看当前板型，不建议在 menuconfig 中直接修改。

各板型配置项及自动关联说明请参考 [src/boards/README_CN.md](../src/boards/README_CN.md)。

**板型自动关联配置（隐藏项）**：

选择板型后，以下配置会被自动 `select`（不在 menuconfig 中显示）：

| 配置项 | 说明 | 关联板型 |
|--------|------|----------|
| TUYA_MODULE_T5 | 使用 T5 模组 | T5AI_BOARD 系列 |
| ENABLE_AI_MODE_HOLD/ONESHOT/WAKEUP/FREE | AI 对话模式 | 由各板型 select |
| ENABLE_AI_MODE_TRANSLATE/RECORD/DETECTION | 扩展 AI 模式 | 仅 DESKTOP |
| ENABLE_DEFAULT_SESSION | 启用默认会话 | 所有板型（默认 y） |
| TUYA_DMA2D_SHARE | DMA2D 硬件加速共享 | T5AI_BOARD |
| USING_BOARD_AUDIO_INPUT/OUTPUT | 板载音频 | 多数 T5 板型 |
| USING_UART_AUDIO_INPUT/OUTPUT | UART 外接音频 | LE270AI_BOARD |

### 3.2 引脚配置 (Pin Configuration)

板型选定后会自动填入默认值，通常无需手动修改。设为 64 表示禁用该功能。

| 配置项 | 说明 | T5AI_BOARD | EVB | EVB_PRO | EYES | ROBOT | DESKTOP | LE270AI |
|--------|------|-----------|-----|---------|------|-------|---------|---------|
| TUYA_AI_TOY_AUDIO_TRIGGER_PIN_NUM | 对话触发按键 GPIO | 12 | 4 | 6 | 12 | 5 | 28 | 5 |
| TUYA_AI_TOY_SPK_EN_PIN_NUM | 喇叭功放使能 GPIO | 28 | 19 | 26 | 28 | 26 | 26 | 28 |
| TUYA_AI_TOY_LED_PIN_NUM | 状态指示 LED GPIO | 56 | 8 | 25 | 64 | 64 | 64 | 56 |
| TUYA_AI_TOY_NET_PIN_NUM | 网络状态指示灯 GPIO | 64 | 12 | 7 | 64 | 4 | 64 | 64 |

**电源控制** (`DEVICE_POWER_CONTROL`)：启用设备开关机控制（长按关机等），桌面版默认开启，其他板型默认关闭。

| 子配置 | 说明 | 默认值 |
|--------|------|--------|
| DEVICE_POWER_NET_KEY_PIN_NUM | 电源/网络按键引脚 | 3 |
| DEVICE_POWER_PIN_NUM | 电源锁存控制引脚 | 4 |

### 3.3 显示配置 (Display Configuration)

板型选定后会自动填入默认值。如果使用自定义屏幕需要手动调整。

| 配置项 | 说明 | T5AI_BOARD | EVB | EVB_PRO | EYES | ROBOT | DESKTOP |
|--------|------|-----------|-----|---------|------|-------|---------|
| TUYA_LCD_IC_NAME_STR | LCD 驱动 IC 型号 | ili9488 | spi_st7789 | spi_st7789 | spi_st7735s | spi_st7789p3 | spi_st7789v2 |
| TUYA_LCD_WIDTH_VAL | 屏幕宽度（像素） | 320 | 240 | 240 | 128 | 320 | 320 |
| TUYA_LCD_HEIGHT_VAL | 屏幕高度（像素） | 480 | 240 | 240 | 128 | 172 | 240 |
| LCD_FPS_VAL | 刷新帧率 (1–60) | 10 | 15 | 15 | 10 | 10 | 15 |
| TUYA_LCD_ROTATION_VAL | 旋转 (0=0°/1=90°/2=180°/3=270°) | 0 | 0 | 0 | 0 | 0 | 0 |

**LCD_SPI_DEVICE_NUM**：LCD 使用的 SPI 设备编号，仅在 T5AI_BOARD_EYES 板型可见，默认值 1。

> **宽度/高度为 0** 表示无屏幕。帧率越高越流畅但 CPU 占用越大。

### 3.4 电池管理 (ENABLE_BATTERY)

电池管理功能，包括充电检测和电量显示。

- **默认开启板型**：T5AI_BOARD_DESKTOP、T5AI_BOARD_EVB、T5AI_BOARD_EVB_PRO、T5AI_BOARD_ROBOT
- 其他板型默认关闭

| 子配置 | 说明 | EVB | EVB_PRO | ROBOT | DESKTOP |
|--------|------|-----|---------|-------|---------|
| TUYA_AI_TOY_CHARGE_PIN_NUM | 充电状态检测 GPIO | 21 | 23 | 21 | 2 |
| TUYA_AI_TOY_BATTERY_CAP_PIN_NUM | 电池电量 ADC 采集 GPIO | 28 | 22 | 28 | 12 |

### 3.5 摄像头 (ENABLE_TUYA_CAMERA)

- **可见条件**：T5AI_BOARD、T5AI_BOARD_EYES、T5AI_BOARD_ROBOT、T5AI_BOARD_EVB_PRO、T5AI_BOARD_DESKTOP
- 默认关闭，需手动开启

| 子配置 | 说明 | 默认 |
|--------|------|------|
| ENABLE_AI_MODE_P2P | 启用 P2P 视频通话模式 | n |

**摄像头硬件配置** (Camera Hardware Configuration)：

| 配置项 | 说明 | T5AI_BOARD | EYES | EVB_PRO | ROBOT | DESKTOP |
|--------|------|-----------|------|---------|-------|---------|
| Camera interface type | DVP 或 UVC | DVP | DVP | DVP | **UVC** | DVP |
| TUYA_AI_TOY_ISP_WIDTH_VAL | 采集宽度 | 480 | 480 | 480 | 320 | 480 |
| TUYA_AI_TOY_ISP_HEIGHT_VAL | 采集高度 | 480 | 480 | 480 | 240 | 480 |
| TUYA_AI_TOY_ISP_FPS_VAL | 采集帧率 (1–30) | 10 | 10 | 10 | 15 | 10 |
| TUYA_AI_TOY_POWER_PIN_NUM | 摄像头电源 GPIO (64=无需控制) | 51 | 51 | 64 | 6 | 64 |
| TUYA_AI_TOY_I2C_CLK_PIN_NUM | I2C 时钟 GPIO (DVP 用) | 13 | 13 | 64 | 64 | 64 |
| TUYA_AI_TOY_I2C_SDA_PIN_NUM | I2C 数据 GPIO (DVP 用) | 15 | 15 | 64 | 64 | 64 |

### 3.6 AI 绘图 (ENABLE_TUYA_PICTURE)

- **可见条件**：仅 T5AI_BOARD
- UI 风格通过 `T5AI_BOARD UI style` 互斥选择：通用 view UI（`ENABLE_VIEW_UI`）或微信风格 UI（`ENABLE_T5AI_BOARD_UI_WECHAT`）；开启 AI 绘图时默认选择 view UI

| 子配置 | 说明 | 默认 |
|--------|------|------|
| TUYA_PICTURE_ALBUM_NAME | 图片相册名称 | `ai_picture` |
| TUYA_PICTURE_ALBUM_MAX_IMAGE_CNT | 相册中最大图片数量 | 10 |
| TUYA_PICTURE_DEF_OUTPUT_WIDTH | AI 绘图默认输出宽度 | 与 LCD 宽度一致 |
| TUYA_PICTURE_DEF_OUTPUT_HEIGHT | AI 绘图默认输出高度 | 与 LCD 高度一致 |
| ENABLE_TAL_IMAGE | 图像处理库（JPEG 解码/YUV 转换/缩放） | y |

**图片相册** (`ENABLE_IMAGE_ALBUM`)：由 `ENABLE_TUYA_PICTURE` 自动 select。

| 子配置 | 说明 | 默认 |
|--------|------|------|
| ENABLE_IMAGE_ALBUM_STORAGE_MEM | 相册 RAM 存储（访问快，断电丢失） | y |
| ENABLE_IMAGE_ALBUM_STORAGE_SD | 相册文件系统存储（SD 卡/Flash，断电保留） | n |

### 3.7 MCP 工具集 (ENABLE_TUYA_TOOLKITS)

为 AI 提供设备控制和扩展能力，各工具按需开启。

| 配置项 | 说明 | 可见条件 | 默认 |
|--------|------|----------|------|
| ENABLE_TOOLKITS_CAMERA | 摄像头工具（AI 拍照/识别） | 需开启 ENABLE_TUYA_CAMERA | n |
| ENABLE_TOOLKITS_MOTION | 运动控制工具（机器人运动） | 仅 T5AI_BOARD_ROBOT | n |
| ENABLE_APP_MOTION_ROTATION_MCP | 旋转/运动传感器工具 | 仅 T5AI_BOARD_DESKTOP | y |
| ENABLE_TOOLKITS_TM | 时间管理工具（日程、提醒） | 所有板型 | n |
| ENABLE_TOOLKITS_CONTROL | 设备控制工具（音量、亮度等） | 所有板型 | y |
| ENABLE_TOOLKITS_PLAYBACK | 音频播放工具（播放/暂停/切换） | 所有板型 | n |
| ENABLE_TOOLKITS_IMM | 即时通讯工具（发送消息） | 所有板型 | n |
| ENABLE_TOOLKITS_SOCIAL | 社交互动工具（好友互动） | 所有板型 | n |

### 3.8 UI 显示系统 (ENABLE_TUYA_UI)

启用基于 LVGL 的 UI 显示系统。开启后会自动 select 以下依赖：

| 自动 select | 说明 |
|------------|------|
| TUYA_CPU_ARCH_SMP | 多核 SMP 支持 |
| TUYA_LIB_FREETYPE | FreeType 字体渲染引擎 |
| TUYA_LIBJPEG_TURBO | libjpeg-turbo JPEG 解码库 |
| TUYA_MULTI_TYPES_LCD | 多类型 LCD 支持 |
| TUYA_APP_DRIVERS_TP | 触摸屏驱动 |
| TUYA_LVGL_DRAW_WITH_ARM2D | ARM-2D 硬件渲染加速（启用 DSP） |
| RGB565_COLOR_SWAP_LVGL | RGB565 颜色字节交换 |
| RGB888_COLOR_SWAP_LVGL | RGB888 颜色字节交换 |

**UI 子配置项**：

| 配置项 | 说明 | 默认 |
|--------|------|------|
| TUYA_LVGL_VERSION | LVGL 版本（8 或 9） | 8 |
| TUYA_IMG_DIRECT_FLUSH | 图片直接刷屏，跳过 LVGL 渲染，降低延迟（依赖 SMP） | n |
| TUYA_DMA2D_SHARE | 启用 DMA2D 硬件加速共享 | n（T5AI_BOARD 自动 select） |
| TUYA_FILE_SYSTEM | 启用文件系统（LittleFS） | n |

**文件系统存储介质选择** (`TUYA_FILE_SYSTEM` 开启后可见)：

| 选项 | 说明 |
|------|------|
| FILE_SYSTEM_LFS_SPI_FLASH | 使用 SPI Flash 作为 LittleFS 存储介质（**默认**） |
| FILE_SYSTEM_LFS_SD | 使用 SD 卡作为 LittleFS 存储介质 |

### 3.9 音频配置 (Audio Configuration)

#### 3.9.1 音频输入

**音频输入源** (`Audio input source`)：

| 选项 | 说明 | 默认 |
|------|------|------|
| USING_BOARD_AUDIO_INPUT | 板载麦克风 | 大多数板型默认 |
| USING_UART_AUDIO_INPUT | UART 外接编解码芯片 | LE270AI_BOARD 默认 |

| 配置项 | 说明 | 范围 | 默认 |
|--------|------|------|------|
| INPUT_BOARD_STACK_SIZE | 音频采集线程栈大小（字节） | 2560–41472 | 2560 |

**音频前端引擎** (`Audio frontend engine`)：

| 选项 | 说明 |
|------|------|
| USING_TUYA_AUDIO_FRONTEND | 涂鸦自研（AEC 回声消除 / AGC 增益控制 / NS 噪声抑制）(**默认**) |
| USING_3RD_AUDIO_FRONTEND | 第三方前端处理库 |

**编码器**：

| 配置项 | 说明 | 默认 |
|--------|------|------|
| ENABLE_APP_OPUS_ENCODER | 启用 OPUS 编码器 | T5 模组默认 y，其他 n |
| ENABLE_APP_SPEEX_ENCODER | 启用 SPEEX 编码器（CPU 占用低于 OPUS） | n |
| APP_OPUS_ENCODER_BITRATE | OPUS 编码比特率（8000–128000 bps） | 16000 |
| APP_OPUS_ENCODER_BANDWIDTH | OPUS 编码带宽 | 1102 |

OPUS 编码带宽取值：

| 值 | 含义 |
|----|------|
| 1101 | 窄带 4kHz |
| 1102 | 中带 6kHz（**默认**） |
| 1103 | 宽带 8kHz |
| 1104 | 超宽带 12kHz |
| 1105 | 全带 20kHz |

#### 3.9.2 音频输出

**音频输出目标** (`Audio output target`)：

| 选项 | 说明 | 默认 |
|------|------|------|
| USING_BOARD_AUDIO_OUTPUT | 板载 DAC/I2S | 大多数板型默认 |
| USING_UART_AUDIO_OUTPUT | UART 外接编解码芯片 | LE270AI_BOARD 默认 |

**解码器 / 播放器**：

| 配置项 | 说明 | 依赖 | 默认 |
|--------|------|------|------|
| AI_PLAYER_LITE | 精简播放器，减少 RAM 占用（禁用 OPUS/OGG 解码器） | — | n |
| AI_PLAYER_DECODER_OPUS_ENABLE | 启用裸 OPUS 解码（实时 AI 语音流） | 非 LITE 模式 | y |
| AI_PLAYER_DECODER_OPUS_FRAME_SIZE | OPUS 解码帧长（10–60 ms） | OPUS 解码启用 | 40 |
| AI_PLAYER_DECODER_OPUS_KBPS | OPUS 解码比特率（16–128 kbps） | OPUS 解码启用 | 16 |
| AI_PLAYER_DECODER_OGGOPUS_ENABLE | 启用 OGG-OPUS 容器解码 | 非 LITE 模式 | y |

#### 3.9.3 UART Codec 配置

仅在音频输入选择 `USING_UART_AUDIO_INPUT` 时可见。

**UART 编解码芯片选择** (`UART codec chip vendor`)：

| 选项 | 说明 | 默认 |
|------|------|------|
| UART_CODEC_VENDOR_GX8006 | 国芯 GX8006 | LE270AI_BOARD 默认 |
| UART_CODEC_VENDOR_CI1302 | 启英泰伦 CI1302 | 其他板型默认 |

| 配置项 | 说明 | LE270AI 默认 | 其他默认 |
|--------|------|-------------|---------|
| UART_CODEC_UART_PORT_NUM | 通信 UART 端口号 (0 或 2) | 2 | 2（Ubuntu=0） |
| UART_CODEC_BOOT_IO_NUM | 编解码芯片 BOOT 模式 GPIO | 25 | 2 |
| UART_CODEC_POWER_IO_NUM | 编解码芯片电源控制 GPIO | 31 | 3 |
| UART_CODEC_SPK_FLOWCTL_IO_NUM | 喇叭播放流控 GPIO | 6 | 32 |
| UART_CODEC_SPK_FLOWCTL_ACTIVE_HIGH | 流控引脚有效电平 (y=高电平) | y | y（Ubuntu=n） |

**上传音频格式** (`Upload audio format`)：

| 选项 | 说明 | 默认 |
|------|------|------|
| UART_CODEC_FMT_SPEEX | SPEEX（CPU 占用低） | 通用默认 |
| UART_CODEC_FMT_OPUS | OPUS（压缩率更高） | LE270AI_BOARD 默认 |

#### 3.9.4 Codec 性能测试

`CODEC_BENCH_TEST`：编解码器基准测试模式，开启后跳过 AI 初始化，仅通过按键触发编码器性能测试（IoT/WiFi 正常运行）。默认关闭。

### 3.10 蜂窝网络 (ENABLE_CELLULAR_DONGLE)

启用 USB 蜂窝网络模块（4G），通过蜂窝网络联网代替 WiFi。默认关闭，所有板型均可使用。

### 3.11 AI 对话默认模式 (TUYA_AI_CHAT_DEFAULT_MODE)

| 选项 | 值 | 模式 | 说明 |
|------|-----|------|------|
| AI_CHAT_DEFAULT_HOLD | 0 | hold | 长按触发 |
| AI_CHAT_DEFAULT_ONESHOT | 1 | oneshot | 单次按键 |
| AI_CHAT_DEFAULT_WAKEUP | 2 | wakeup | 关键词唤醒 |
| AI_CHAT_DEFAULT_FREE | 3 | free | 自由对话 |
| AI_CHAT_DEFAULT_TRANSLATE | 4 | translate | 翻译模式 |
| AI_CHAT_DEFAULT_P2P | 5 | p2p | P2P 模式（需开启摄像头） |

**如何修改默认模式**：若当前默认模式不满足需求，可按以下方式修改：

1. **通过 menuconfig（推荐）**
   - 执行 `make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai`
   - 在 "Default AI chat mode" choice 中选择所需模式
   - 保存退出后执行 `make app_config APP_NAME=tuyaos_demo_wukong_ai` 生成配置

2. **约束说明**
   - 所选默认模式必须是**当前板型已启用的模式**（见 2.3 节表格）
   - 若板型未启用该模式，系统会 fallback 到下一个已启用的模式
   - 例如：WUKONG_BOARD_UBUNTU 仅启用 HOLD，即使设为 3（free）也会实际使用 HOLD

3. **运行时切换**
   - 用户可通过 UI 设置切换对话模式，切换结果会保存到 KV 存储
   - 下次上电会优先使用 KV 中保存的模式；若该模式未启用，则使用 `TUYA_AI_CHAT_DEFAULT_MODE`

### 3.12 设备默认值 (Device Defaults)

| 配置项 | 说明 | 范围/类型 | 默认 |
|--------|------|-----------|------|
| TY_SPK_DEFAULT_VOL | 默认扬声器音量，上电后初始音量值 | 0–100 | 70 |
| TY_AI_DEFAULT_LANG | 默认语言 | 0=中文，1=英文 | 1 |
| ENABLE_AUDIO_ANALYSIS | 启用音频分析功能 | bool | n |
| ENABLE_APP_AI_MONITOR | 启用 AI 监控功能（调试/诊断用） | bool | n |
| ENABLE_CLOUD_ALERT | 启用云端提示音，优先使用云端 TTS 播报 | bool | n |
| ENABLE_APP_JOYINSIDE | 启用 joyinside 第三方集成 | bool | n |
| ENABLE_LOW_POWER | 启用低功耗模式（空闲超时后自动进入休眠，按键唤醒） | bool | n |

开启 `ENABLE_LOW_POWER` 后，可选择休眠级别：

| 选项 | 说明 |
|------|------|
| LOW_POWER_LIGHT_SLEEP | 轻度休眠（TUYA_CPU_SLEEP）：关闭外设、降低功耗，按键后快速恢复（**默认**） |
| LOW_POWER_DEEP_SLEEP | 深度休眠（TUYA_CPU_DEEP_SLEEP）：功耗最低，按键唤醒后重启系统 |

`TY_AI_LOW_POWER_MODE` 为隐藏 int 配置，由 choice 自动生成（0=light sleep，1=deep sleep），代码中直接引用该宏判断休眠级别。

---

## 四、常见配置组合示例

### 4.1 最小配置（纯语音，无屏）

```bash
# 1. 选择板型
make app_config_choice APP_NAME=tuyaos_demo_wukong_ai  # 选择 T5AI_BOARD 或 T5AI_BOARD_EVB
# 2. 进入 menuconfig 关闭不需要的功能
make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai
#    ENABLE_TUYA_UI     = n
#    ENABLE_TUYA_CAMERA = n
```

### 4.2 标准开发板（语音 + 屏 + 摄像头）

```bash
make app_config_choice APP_NAME=tuyaos_demo_wukong_ai  # 选择 T5AI_BOARD
make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai
#    ENABLE_TUYA_UI     = y
#    ENABLE_TUYA_CAMERA = y
#    ENABLE_AI_MODE_P2P = n（按需开启）
```

### 4.3 Ubuntu 模拟

```bash
make app_config_choice APP_NAME=tuyaos_demo_wukong_ai  # 选择 Ubuntu
```

### 4.4 便携评估板（小屏 + 电池）

```bash
make app_config_choice APP_NAME=tuyaos_demo_wukong_ai  # 选择 T5AI_BOARD_EVB
make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai
#    ENABLE_TUYA_UI = y
```

### 4.5 机器狗（宽屏 + UVC 摄像头 + 电池）

```bash
make app_config_choice APP_NAME=tuyaos_demo_wukong_ai  # 选择 T5AI_BOARD_ROBOT
make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai
#    ENABLE_TUYA_UI     = y
#    ENABLE_TUYA_CAMERA = y
#    ENABLE_TOOLKITS_MOTION = y（需要机器人运动控制时开启）
```

### 4.6 桌面设备（全功能）

```bash
make app_config_choice APP_NAME=tuyaos_demo_wukong_ai  # 选择 T5AI_BOARD_DESKTOP
make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai
#    ENABLE_TUYA_UI     = y
#    ENABLE_TUYA_CAMERA = y（按需）
#    默认已启用翻译、录音、AI检测模式
```

### 4.7 UART 外接语音芯片

```bash
make app_config_choice APP_NAME=tuyaos_demo_wukong_ai  # 选择 LE270AI_BOARD
# 默认使用 UART 音频输入/输出、GX8006 芯片、OPUS 编码
```

---

## 五、配置后检查

1. **确认配置已生成**：检查 `include/tuya_app_config.h` 或 `build/tuya_app.config` 中是否有对应宏定义
2. **确认板级文件**：`local.mk` 会根据 `CONFIG_T5AI_BOARD` 等选择 `src/boards/xxx/` 下的板级实现
3. **重新编译**：配置变更后务必执行 `make app_config` 再编译

---

## 六、新增 BOARD_TYPE 指导

本节仅描述如何在 `build/APPconfig` 中新增板型配置。`local.mk` 修改、板级目录创建、业务代码适配等请参考 [src/boards/README_CN.md](../src/boards/README_CN.md) 中「添加新开发板」章节。

### 6.1 在 choice 中新增 config

在 `build/APPconfig` 的 `choice` 块中新增 `config`，并设置 `select` 关联：

```kconfig
        config MY_CUSTOM_BOARD
            bool "MY_CUSTOM_BOARD"
            select TUYA_MODULE_T5
            select USING_BOARD_AUDIO_INPUT
            select USING_BOARD_AUDIO_OUTPUT
            select ENABLE_AI_MODE_HOLD
            select ENABLE_AI_MODE_ONESHOT
            select ENABLE_AI_MODE_WAKEUP
            select ENABLE_AI_MODE_FREE
```

- **音频**：板载麦克风/喇叭用 `USING_BOARD_AUDIO_INPUT/OUTPUT`，UART 外接用 `USING_UART_AUDIO_INPUT/OUTPUT`
- **AI 模式**：按需 `select ENABLE_AI_MODE_xxx`

### 6.2 添加默认配置到 build/appconfig/

在 `build/appconfig/` 目录下新建以板型名命名的配置文件（如 `MY_CUSTOM_BOARD`），内容为该板型的完整 `.config` 快照。可通过以下方式生成：

1. 先选择一个相近的板型：`make app_config_choice APP_NAME=tuyaos_demo_wukong_ai`
2. 进入 menuconfig 修改配置：`make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai`
3. 保存后将 `build/tuya_app.config` 复制为 `build/appconfig/MY_CUSTOM_BOARD`

该文件将作为 `make app_config_choice` 的可选项，用户选择后自动加载。

### 6.3 摄像头可见条件（可选）

若新板型支持摄像头，在 `src/drivers/app_tuya_camera/Kconfig` 的 `ENABLE_TUYA_CAMERA` 的 `if` 条件中加入新板型：

```kconfig
if (T5AI_BOARD || T5AI_BOARD_EYES || ... || MY_CUSTOM_BOARD)
```

### 6.4 在各 Kconfig 中添加默认值

若新板型的引脚、分辨率等与现有板型不同，需在以下 Kconfig 文件中为新板型添加 `default ... if MY_CUSTOM_BOARD`：

- `src/boards/Kconfig` — 引脚、显示参数
- `src/miscs/battery/Kconfig` — 电池引脚
- `src/drivers/app_tuya_camera/Kconfig` — 摄像头参数

---

## 七、配置文件索引

| 文件 | 说明 |
|------|------|
| `build/APPconfig` | 应用层 Kconfig 主定义（板型 choice、AI 模式、Device Defaults） |
| `build/tuya_app.config` | 当前应用层配置快照（menuconfig 输出） |
| `build/appconfig/<板型名>` | 各板型默认配置（`app_config_choice` 可选项） |
| `src/boards/Kconfig` | 引脚配置、显示配置 |
| `src/miscs/battery/Kconfig` | 电池管理配置 |
| `src/drivers/app_tuya_camera/Kconfig` | 摄像头配置 |
| `src/wukong/picture/Kconfig` | AI 绘图配置 |
| `src/wukong/mcp/Kconfig` | MCP 工具集配置 |
| `src/miscs/gui/Kconfig` | UI/LVGL 显示系统配置 |
| `src/miscs/audio_player/Kconfig` | 音频输入/输出/UART Codec 配置 |
| `local.mk` | 编译系统：根据配置宏选择源文件和头文件路径 |

---
