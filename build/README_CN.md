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
| **T2AI_BOARD** | T2 | 320×480 ILI9488 | ❌ | ❌ | T2 标准开发 |
| **T3AI_BOARD** | T3 | 无 | ❌ | ❌ | T3 无屏语音方案 |
| **T3_V_PRO_BOARD** | T3 | 320×480 ILI9488 | ❌ | ❌ | T3 Pro（带屏） |
| **T1AI_BOARD** | T1 | 320×480 ILI9488 | ❌ | ❌ | T1 带屏方案 |
| **L511_Y6E_BOARD** | L511 | 无 | ❌ | ❌ | L511 蜂窝无屏语音 |
| **L511_Y7PM_BOARD** | L511 | 284×240 jb9853a | ❌ | ❌ | L511 蜂窝带屏语音 |
| **LE270AI_BOARD** | LE270 | 无 | ❌ | ❌ | LE270 蜂窝无屏语音 |
| **RTL8720CF_VU2_BOARD** | RTL8720CF | 320×480 ILI9488 | ❌ | ❌ | RTL8720CF 带屏方案 |
| **WUKONG_BOARD_UBUNTU** | — | — | — | — | PC 模拟调试 |

板型硬件详细说明请参考 [src/boards/README_CN.md](../src/boards/README_CN.md)。

### 2.2 按功能需求选择

| 需求 | 配置项 | 说明 |
|------|--------|------|
| 无屏幕、纯语音 | 选择板型后**关闭** `ENABLE_TUYA_UI` | 无 LVGL、无显示，节省资源 |
| 需要摄像头 | 选择 T5AI_BOARD / EYES / EVB_PRO / ROBOT / DESKTOP 后，开启 `ENABLE_TUYA_CAMERA` | 支持 P2P 视频、多模态 AI |
| 需要 P2P 视频通话 | 开启 `ENABLE_TUYA_CAMERA` 后，再开启 `ENABLE_AI_MODE_P2P` | 点对点音视频通信 |
| 蜂窝网络（4G 等） | 开启 `ENABLE_CELLULAR_DONGLE` | USB 蜂窝模组 |
| AI 绘图 / 识图 | 启用 `ENABLE_AI_MODE_PICTURE`（T5AI_BOARD / DESKTOP 默认启用），自动开启 `ENABLE_TUYA_PICTURE` | 含图片相册与图像处理 |
| 翻译/录音/AI 检测 | 选择 T5AI_BOARD_DESKTOP（默认启用 TRANSLATE/RECORD/PICTURE/DETECTION） | 桌面版全模式 |

### 2.3 按 AI 对话模式选择

板型选定后，会**自动启用**部分 AI 模式（由板型 `select` 决定）。下表为各板型默认启用的模式：

| 板型 | HOLD | ONESHOT | WAKEUP | FREE | TRANSLATE | RECORD | PICTURE | DETECTION | P2P |
|------|------|---------|--------|------|-----------|--------|---------|-----------|-----|
| T5AI_BOARD | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| T5AI_BOARD_EVB | ✓ | ✓ | ✓ | ✓ | - | - | - | - | - |
| T5AI_BOARD_EVB_PRO | ✓ | ✓ | ✓ | ✓ | - | - | - | - | - |
| T5AI_BOARD_EYES | ✓ | ✓ | ✓ | ✓ | - | - | - | - | - |
| T5AI_BOARD_ROBOT | ✓ | ✓ | ✓ | ✓ | - | - | - | - | - |
| T5AI_BOARD_DESKTOP | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ |
| T2AI_BOARD | ✓ | ✓ | ✓ | ✓ | - | - | - | - | - |
| T3AI_BOARD | ✓ | ✓ | ✓ | ✓ | - | - | - | - | - |
| T3_V_PRO_BOARD | ✓ | ✓ | ✓ | ✓ | - | - | - | - | - |
| T1AI_BOARD | ✓ | ✓ | ✓ | ✓ | - | - | - | - | - |
| L511_Y6E_BOARD | ✓ | ✓ | ✓ | ✓ | - | - | - | - | - |
| L511_Y7PM_BOARD | ✓ | ✓ | ✓ | ✓ | - | - | - | - | - |
| LE270AI_BOARD | ✓ | ✓ | ✓ | ✓ | - | - | - | - | - |
| RTL8720CF_VU2_BOARD | ✓ | ✓ | ✓ | ✓ | - | - | - | - | - |
| WUKONG_BOARD_UBUNTU | ✓ | - | - | - | - | - | - | - | - |

> P2P / PICTURE / DETECTION 模式运行时依赖摄像头（`ENABLE_TUYA_CAMERA`）；T5AI_BOARD 与 DESKTOP 的预置配置默认启用全部 9 个模式。

**TUYA_AI_CHAT_DEFAULT_MODE**：上电默认进入的对话模式（0–8 对应 hold/oneshot/wakeup/free/translate/p2p/record/picture/detection，详见 3.11 节）。

**如何修改板型支持的对话模式**：各板型启用的模式记录在预置配置文件 `build/appconfig/<板型名>` 中（`CONFIG_ENABLE_AI_MODE_xxx=y` 行）。增删模式可直接编辑该文件，或 `make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai` 交互勾选后保存。P2P 模式需先开启 `ENABLE_TUYA_CAMERA`，再开启 `ENABLE_AI_MODE_P2P`。修改后执行 `make app_config APP_NAME=tuyaos_demo_wukong_ai` 生成配置。详见[新增或裁剪对话模式](../docs/howto/add-dialog-mode.md)。

**如何修改默认对话模式**：在 menuconfig 中找到 `TUYA_AI_CHAT_DEFAULT_MODE`，将数值改为 0–8 中所需模式，保存后执行 `make app_config` 生成配置。注意：所选模式必须是当前板型已启用的模式，否则会 fallback 到其他已启用模式。用户也可在运行时通过 UI 切换模式，切换结果会保存并在下次上电时优先使用。更多说明见 3.11 节。

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
| ENABLE_AI_MODE_HOLD/ONESHOT/WAKEUP/FREE | AI 对话模式 | 由各板型 appconfig 预置 |
| ENABLE_AI_MODE_TRANSLATE/RECORD/DETECTION | 扩展 AI 模式 | 仅 DESKTOP |
| ENABLE_DEFAULT_SESSION | 启用默认会话 | 所有板型（默认 y） |
| USING_BOARD_AUDIO_INPUT/OUTPUT | 板载音频 | 多数 T5 板型 |
| USING_UART_AUDIO_INPUT/OUTPUT | UART 外接音频 | LE270AI_BOARD |

### 3.2 引脚配置 (Pin Configuration)

板型选定后会自动填入默认值（来自 `src/boards/Kconfig` 的 `default ... if <BOARD>`），通常无需手动修改。设为 64 表示禁用该功能。

| 板型 | AUDIO_TRIGGER | SPK_EN | LED | NET |
|------|---------------|--------|-----|-----|
| T5AI_BOARD | 12 | 28 | 56 | 64 |
| T5AI_BOARD_EVB | 4 | 19 | 8 | 12 |
| T5AI_BOARD_EVB_PRO | 6 | 26 | 25 | 7 |
| T5AI_BOARD_EYES | 12 | 28 | 64 | 64 |
| T5AI_BOARD_ROBOT | 5 | 26 | 64 | 4 |
| T5AI_BOARD_DESKTOP | 28 | 26 | 64 | 64 |
| T2AI_BOARD | 12 | 28 | 56 | 64 |
| T3AI_BOARD | 64 | 26 | 64 | 64 |
| T3_V_PRO_BOARD | 64 | 26 | 64 | 64 |
| T1AI_BOARD | 64 | 26 | 64 | 64 |
| L511_Y6E_BOARD | 25 | 64 | 16 | 64 |
| L511_Y7PM_BOARD | 25 | 64 | 16 | 64 |
| LE270AI_BOARD | 5 | 28 | 56 | 64 |
| RTL8720CF_VU2_BOARD | 64 | 26 | 64 | 64 |

> UART 外接语音芯片的板型（T3/T1/L511/LE270/RTL8720）音频走 UART codec，板载引脚多为 64（禁用）。

**电源控制** (`DEVICE_POWER_CONTROL`)：启用设备开关机控制（长按关机等），桌面版默认开启，其他板型默认关闭。

| 子配置 | 说明 | 默认值 |
|--------|------|--------|
| DEVICE_POWER_NET_KEY_PIN_NUM | 电源/网络按键引脚 | 3 |
| DEVICE_POWER_PIN_NUM | 电源锁存控制引脚 | 4 |

### 3.3 显示配置 (Display Configuration)

板型选定后会自动填入默认值。如果使用自定义屏幕需要手动调整。

| 板型 | LCD 驱动 IC | 宽×高 | FPS |
|------|------------|-------|-----|
| T5AI_BOARD | ili9488 | 320×480 | 10 |
| T5AI_BOARD_EVB | spi_st7789 | 240×240 | 15 |
| T5AI_BOARD_EVB_PRO | spi_st7789 | 240×240 | 15 |
| T5AI_BOARD_EYES | spi_st7735s | 128×128 | 10 |
| T5AI_BOARD_ROBOT | spi_st7789p3 | 320×172 | 10 |
| T5AI_BOARD_DESKTOP | spi_st7789v2 | 320×240 | 15 |
| T2AI_BOARD | ili9488 | 320×480 | 10 |
| T3AI_BOARD | — | 无屏 | — |
| T3_V_PRO_BOARD | ili9488 | 320×480 | 10 |
| T1AI_BOARD | ili9488 | 320×480 | 10 |
| L511_Y6E_BOARD | — | 无屏 | — |
| L511_Y7PM_BOARD | — | 无屏 | — |
| LE270AI_BOARD | — | 无屏 | — |
| RTL8720CF_VU2_BOARD | ili9488 | 320×480 | 10 |

`TUYA_LCD_ROTATION_VAL`（旋转 0=0°/1=90°/2=180°/3=270°）所有板型默认 0。`LCD_SPI_DEVICE_NUM`（LCD SPI 设备编号）仅 T5AI_BOARD_EYES 可见，默认 1（眼睛板用 SPI1 双屏）。

> **宽度/高度为 0 / IC 为空** 表示无屏幕。帧率越高越流畅但 CPU 占用越大。

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

- **可见条件**：启用 `ENABLE_AI_MODE_PICTURE`（T5AI_BOARD / T5AI_BOARD_DESKTOP 默认启用）
- 开启后自动 `select` `ENABLE_IMAGE_ALBUM` 与 `ENABLE_TAL_IMAGE`

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

启用 Vue-inspired 响应式 LVGL UI 框架（`src/ui/`）。开启后自动 `select` 以下依赖：

| 自动 select | 说明 |
|------------|------|
| TUYA_CPU_ARCH_SMP | 多核 SMP 支持（再自动 select `TUYA_MULTI_TYPES_LCD` 多类型 LCD、`TUYA_APP_DRIVERS_TP` 触摸屏驱动） |
| TUYA_LIBJPEG_TURBO | libjpeg-turbo JPEG 解码库 |

**框架配置**：

| 配置项 | 说明 | 默认 |
|--------|------|------|
| UI_DMA2D_ENABLE | DMA2D GPU 加速（填充/混合） | y |
| UI_WUKONG_PAGES | 编译标准 wukong 页面集 + 后台服务 | y |
| UI_LCD_RGB565_BYTE_SWAP | RGB565 字节交换（8 位 SPI 屏需要，如 DESKTOP 的 st7789v2） | n |

> `UI_WUKONG_PAGES=y` 时编译完整 wukong 页面（Home/Chat/Settings/Camera/Music 等）；自定义 board UI 的板（ROBOT/EVB/EYES 经 `ui_app_register_board_ui()` 注册）可关闭此项，仅保留框架核心以节省 flash。

**输入设备**：`UI_INDEV_TOUCH`（触摸，默认 y）、`UI_INDEV_KEYPAD`（物理键盘）、`UI_INDEV_ENCODER`（旋转编码器）、`UI_INDEV_GESTURE`（手势/IMU）。

**语言**：`UI_I18N_LANG_ZH`（简中，默认 y）、`UI_I18N_LANG_EN`（英文，默认 y）。

**UI Features**（仅 `UI_WUKONG_PAGES=y` 时可见，按需编译各功能页）：

| 配置项 | 说明 | 依赖 | 默认 |
|--------|------|------|------|
| UI_FEATURE_CAMERA | 相机与相册 | ENABLE_TUYA_CAMERA | y |
| UI_FEATURE_MUSIC | 音乐播放 | — | y |
| UI_FEATURE_RECORDING | 录音与云端转写 | ENABLE_AI_MODE_RECORD | y |
| UI_FEATURE_TIME | 时间管理（闹钟/日程/番茄钟） | — | y |
| UI_FEATURE_DETECTION | AI 侦测记录 | ENABLE_AI_MODE_DETECTION | y |
| UI_FEATURE_CALL | P2P 通话 | ENABLE_AI_MODE_P2P | y |
| UI_FEATURE_FILES | 文件浏览 | — | y |
| UI_FEATURE_AUDIO_DIAG | 音频诊断 | — | y |
| UI_FEATURE_WEATHER | 天气 | — | y |

文件系统由 UI 框架内部 `ui_svc_fs` 挂载 SD 卡到 `/sdcard`，无独立 Kconfig 项。详见 [src/ui/README.md](../src/ui/README.md)。

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
| AI_CHAT_DEFAULT_RECORD | 6 | record | 录音模式 |
| AI_CHAT_DEFAULT_PICTURE | 7 | picture | 生图/识图模式 |
| AI_CHAT_DEFAULT_DETECTION | 8 | detection | 侦测模式 |

> 0–3 为闲聊子模式（hold/oneshot/wakeup/free），4–8 为设备模式（translate/p2p/record/picture/detection）。choice 定义在 `src/mode/Kconfig`。

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
| ENABLE_AI_MF_TEST | 启用 AI 产测功能 | bool | y |
| ENABLE_LOW_POWER | 启用低功耗模式（空闲超时后自动进入休眠，按键唤醒） | bool | n |
| ENABLE_CLI_TEXT_INPUT | 启用 CLI 文本输入（串口发文本代替语音，自动化测试，T5 Only） | bool | n |
| ENABLE_APP_KV_CACHE | 启用 KV cache（缓存 KV 写到 RAM，播放时延迟同步防卡音） | bool | n |

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
| `build/APPconfig` | 应用层 Kconfig 主定义（板型 choice、AI Cloud Provider、IM Channel、Device Defaults、蜂窝） |
| `build/tuya_app.config` | 当前应用层配置快照（menuconfig 输出） |
| `build/appconfig/<板型名>` | 各板型默认配置（`app_config_choice` 可选项） |
| `src/boards/Kconfig` | 引脚配置、显示配置 |
| `src/mode/Kconfig` | AI 对话模式开关、默认对话模式 choice |
| `src/ui/Kconfig` | UI 框架（LVGL、页面集、输入设备、i18n、UI Features） |
| `src/miscs/battery/Kconfig` | 电池管理配置 |
| `src/drivers/app_tuya_camera/Kconfig` | 摄像头配置 |
| `src/wukong/picture/Kconfig` | AI 绘图 / 相册配置 |
| `src/wukong/video/Kconfig` | 视频输入源配置 |
| `src/wukong/mcp/Kconfig` | MCP 工具集配置 |
| `src/miscs/audio_player/Kconfig` | 音频输入/输出/UART Codec 配置 |
| `local.mk` | 编译系统：根据配置宏选择源文件和头文件路径 |

---
