# 板级支持包 (BSP)

## 📋 概述

`boards` 目录包含了不同硬件开发板的板级支持包（BSP）。每个开发板都有独立的配置、GPIO 引脚定义、显示设置、摄像头支持；**UI 使用统一接口**（`tuya_ai_display.h`），各板在各自 `ui/` 目录下实现不同的界面与交互。

## 📁 目录结构

```
boards/
├── T5AI_BOARD/           # T5AI 标准开发板
├── T5AI_BOARD_EVB/       # T5AI EVB 评估板
├── T5AI_BOARD_EYES/      # T5AI 眼睛版开发板
├── T5AI_BOARD_ROBOT/     # T5AI 机器人版开发板
├── T5AI_BOARD_DESKTOP/   # T5AI 桌面版开发板
└── Ubuntu/               # Ubuntu 模拟环境
```

## 🔧 板级配置

### 1. T5AI_BOARD - 标准开发板

**使用场景**: 标准开发、演示、原型制作
**原理图**: [原理图](https://developer.tuya.com/cn/docs/iot-device-dev/T5-E1-IPEX-development-board?id=Ke9xehig1cabj)


**硬件配置**:
- **显示屏**: 3.5寸 LCD (320×480)，ILI9488 驱动
- **摄像头**: 支持 DVP 摄像头 (480×480@10fps，MJPEG)
- **音频触发引脚**: GPIO12
- **扬声器使能引脚**: GPIO28
- **LED 引脚**: GPIO56
- **配网引脚**: 未使用
- **音频编码器**: Opus 编码器

**特性**:
- ✅ 大屏幕支持丰富 UI
- ✅ 摄像头支持多模态 AI
- ✅ 触摸屏支持
- ✅ 微信风格聊天界面

**配置详情**:
```c
// 显示屏
#define TUYA_LCD_IC_NAME     "ili9488"
#define TUYA_LCD_WIDTH        320
#define TUYA_LCD_HEIGHT       480
#define LCD_FPS               10

// 摄像头（如果启用）
#define TUYA_AI_TOY_CAMERA_TYPE  TKL_VI_CAMERA_TYPE_DVP
#define TUYA_AI_TOY_ISP_WIDTH    480
#define TUYA_AI_TOY_ISP_HEIGHT   480
#define TUYA_AI_TOY_ISP_FPS      10

// 音频
#define ENABLE_APP_OPUS_ENCODER 1
```

**PID 配置**:
- 默认 PID: `badnnka7rzrm2idb`

**UI**: 微信风格聊天界面 (`wechat_app.c`)

---

### 2. T5AI_BOARD_EVB - 白盒子

**使用场景**: 产品评估、测试、便携设备
**原理图**: [原理图](https://developer.tuya.com/cn/docs/iot/T5AI-EVB-DATA-SHEET?id=Kelbkhytqktgm)

**硬件配置**:
- **显示屏**: 小尺寸 LCD (240×240)，ST7789 驱动
- **摄像头**: 不支持
- **音频触发引脚**: GPIO4
- **扬声器使能引脚**: GPIO19
- **LED 引脚**: GPIO8
- **配网引脚**: GPIO12
- **电池**: 支持（充电引脚 GPIO21，容量引脚 GPIO28）
- **音频编码器**: Opus 编码器

**特性**:
- ✅ 紧凑设计
- ✅ 电池供电
- ✅ 网络状态指示
- ✅ 丰富的表情支持
- ✅ 小智风格聊天界面

**配置详情**:
```c
// 显示屏
#define TUYA_LCD_IC_NAME     "spi_st7789"
#define TUYA_LCD_WIDTH        240
#define TUYA_LCD_HEIGHT       240
#define LCD_FPS               15

// 电池
#define TUYA_AI_TOY_BATTERY_ENABLE 1
#define TUYA_AI_TOY_CHARGE_PIN     TUYA_GPIO_NUM_21
#define TUYA_AI_TOY_BATTERY_CAP_PIN TUYA_GPIO_NUM_28
```

**PID 配置**:
- PID: `e3jrgtmuqsljru1t`

**UI**: 小智风格聊天界面，支持表情 (`xiaozhi_app.c`)

---

### 3. T5AI_BOARD_EYES - 眼睛版开发板

**使用场景**: 表情交互、情绪显示、玩具应用
**原理图**: [原理图](https://developer.tuya.com/cn/docs/iot-device-dev/T5-E1-IPEX-development-board?id=Ke9xehig1cabj)

**硬件配置**:
- **显示屏**: 小尺寸圆形/方形显示屏 (128×128)，ST7735S 驱动
- **摄像头**: 支持 DVP 摄像头 (480×480@10fps，MJPEG)
- **音频触发引脚**: GPIO12
- **扬声器使能引脚**: GPIO28
- **LED 引脚**: 未使用
- **配网引脚**: 未使用
- **音频编码器**: Opus 编码器

**特性**:
- ✅ 小屏幕优化用于眼睛表情显示
- ✅ 多种情绪状态（愤怒、困惑、美味等）
- ✅ 摄像头支持视觉交互
- ✅ 生动的 UI 动画

**配置详情**:
```c
// 显示屏 - 小屏幕用于眼睛
#define TUYA_LCD_IC_NAME     "spi_st7735s"
#define TUYA_LCD_WIDTH        128
#define TUYA_LCD_HEIGHT       128
#define LCD_FPS               10

// 摄像头
#define TUYA_AI_TOY_CAMERA_TYPE  TKL_VI_CAMERA_TYPE_DVP
#define TUYA_AI_TOY_ISP_WIDTH    480
#define TUYA_AI_TOY_ISP_HEIGHT   480
```

**UI**: 眼睛表情应用 (`eyes_app.c`)，包含多种情绪资源

---

### 4. T5AI_BOARD_ROBOT - 机器狗

**使用场景**: 机器人应用、移动设备、高级交互
**原理图**: [原理图](./T5AI_BOARD_ROBOT/T5AI-DOG_V1.pdf)

**硬件配置**:
- **显示屏**: 宽屏 (320×172)，ST7789P3 驱动（横屏）
- **摄像头**: 支持 UVC 摄像头 (640×480@15fps，MJPEG)
- **音频触发引脚**: GPIO5
- **扬声器使能引脚**: GPIO26
- **LED 引脚**: 未使用
- **配网引脚**: GPIO4
- **电池**: 支持（充电引脚 GPIO21，容量引脚 GPIO28）
- **音频编码器**: Opus 编码器

**特性**:
- ✅ 宽屏显示用于机器人面部/界面
- ✅ 高分辨率摄像头 (640×480)
- ✅ 电池供电用于移动机器人
- ✅ 网络连接
- ✅ 机器人风格 UI 和动画

**配置详情**:
```c
// 显示屏 - 宽屏横屏
#define TUYA_LCD_IC_NAME     "spi_st7789p3"
#define TUYA_LCD_WIDTH        320
#define TUYA_LCD_HEIGHT       172
#define LCD_FPS               10

// 摄像头 - UVC 类型，更高分辨率
#define TUYA_AI_TOY_CAMERA_TYPE  TKL_VI_CAMERA_TYPE_UVC
#define TUYA_AI_TOY_ISP_WIDTH    640
#define TUYA_AI_TOY_ISP_HEIGHT   480
#define TUYA_AI_TOY_ISP_FPS      15
```

**UI**: 机器人应用，包含多种机器人表情和动作


---

### 5. T5AI_BOARD_DESKTOP - 桌面版开发板

**使用场景**: 桌面设备、固定安装、功能完整的应用
**原理图**: [基座原理图](./T5AI_BOARD_DESKTOP/Baseboard.pdf),[主体原理图](./T5AI_BOARD_DESKTOP/T5-E1.pdf)

**硬件配置**:
- **显示屏**: 中等尺寸 LCD (320×240)，ST7789V2 驱动
- **摄像头**: 支持
- **音频触发引脚**: GPIO28
- **扬声器使能引脚**: GPIO26
- **LED 引脚**: 未使用
- **配网引脚**: 未使用
- **电源管理**: 设备电源引脚 GPIO4，电源/网络按键引脚 GPIO3
- **电池**: 支持（充电引脚 GPIO2，容量引脚 GPIO12）
- **音频编码器**: Opus 编码器

**特性**:
- ✅ 完整的桌面应用界面
- ✅ 多页面 UI（主页、聊天、个人中心、设置）
- ✅ 电源管理（长按关机）
- ✅ 网络重置支持（组合键）
- ✅ 丰富的字体资源
- ✅ 完整的交互流程

**配置详情**:
```c
// 显示屏
#define TUYA_LCD_IC_NAME     "spi_st7789v2"
#define TUYA_LCD_WIDTH        320
#define TUYA_LCD_HEIGHT       240
#define LCD_FPS               15

// 电源管理
#define DEVICE_POWER_NET_KEY_PIN  TUYA_GPIO_NUM_3
#define DEVICE_POWER_PIN          TUYA_GPIO_NUM_4

// 电池
#define TUYA_AI_TOY_BATTERY_ENABLE 1
#define TUYA_AI_TOY_CHARGE_PIN     TUYA_GPIO_NUM_2
#define TUYA_AI_TOY_BATTERY_CAP_PIN TUYA_GPIO_NUM_12
```

**UI**: 完整的桌面应用，包含多个页面：
- 启动界面 (`desk_startup.c`)
- 主页 (`desk_home.c`)
- 聊天界面 (`desk_chat.c`)
- 个人中心 (`desk_personal.c`)
- 功能设置 (`desk_func_settings.c`)
- 事件处理 (`desk_event_handle.c`)

---

### 6. Ubuntu - 模拟环境

**使用场景**: PC 端开发、调试、无需硬件的模拟运行

**硬件配置**:
- **显示屏**: 未使用（模拟）
- **摄像头**: 未使用（模拟）
- **音频触发引脚**: 未使用 (GPIO_MAX)
- **扬声器使能引脚**: GPIO26
- **LED 引脚**: 未使用 (GPIO_MAX)
- **配网引脚**: 未使用 (GPIO_MAX)
- **音频编码器**: Speex 编码器（用于 UART 编解码器）

**特性**:
- ✅ UART 编解码器支持（CI1302 芯片）
- ✅ Speex 编码格式
- ✅ 无硬件依赖
- ✅ 便于调试和开发

**配置详情**:
```c
// UART 编解码器配置
#define UART_CODEC_VENDOR_ID         1       // CI1302
#define UART_CODEC_UART_PORT         TUYA_UART_NUM_2
#define UART_CODEC_BOOT_IO           TUYA_GPIO_NUM_2
#define UART_CODEC_POWER_IO          TUYA_GPIO_NUM_3
#define UART_CODEC_UPLOAD_FORMAT     1       // SPEEX

// 音频编码器
#define ENABLE_APP_OPUS_ENCODER 0
#define ENABLE_APP_SPEEX_ENCODER 1
```

---

## 📊 开发板对比表

| 开发板 | 显示屏 | 摄像头 | 电池 | 网络 | 编码器 | UI 风格 | 使用场景 |
|--------|--------|--------|------|------|--------|---------|----------|
| **T5AI_BOARD** | 320×480 | ✅ DVP | ❌ | ❌ | Opus | 微信 | 标准开发 |
| **T5AI_BOARD_EVB** | 240×240 | ❌ | ✅ | ✅ | Opus | 小智 | 评估测试 |
| **T5AI_BOARD_EYES** | 128×128 | ✅ DVP | ❌ | ❌ | Opus | 眼睛 | 表情展示 |
| **T5AI_BOARD_ROBOT** | 320×172 | ✅ UVC | ✅ | ✅ | Opus | 机器人 | 机器人应用 |
| **T5AI_BOARD_DESKTOP** | 320×240 | ✅ DVP | ✅ | ❌ | Opus | 桌面 | 桌面设备 |
| **Ubuntu** | N/A | N/A | N/A | N/A | Speex | N/A | 模拟环境 |

## 🔧 板级初始化

每个开发板在 `tuya_device_board.c` 中实现 `tuya_device_board_init()` 函数：

```c
/**
 * @brief 板级初始化
 * 
 * 初始化 GPIO、外设（显示屏、摄像头、按键）、UI 和其他板级特定组件。
 * 
 * @return 成功返回 OPRT_OK，否则返回错误码
 */
OPERATE_RET tuya_device_board_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    
    // 1. GPIO 初始化
    // 2. 外设初始化（显示屏、摄像头、按键等）
    // 3. 其他板级特定初始化
    
    return rt;
}
```

## 🎨 UI 实现

显示层使用统一接口（`tuya_ai_display.h`），各板在 `ui/` 目录下提供自己的实现。若开发板要实现自己的 UI，只需实现以下两个接口（由 `tuya_ai_display.c` 在初始化与消息派发时调用）：

| 接口 | 说明 |
|------|------|
| `void app_ui_init(void)` | UI 初始化，创建界面、注册回调等 |
| `void app_ui_msg_handler(TY_DISPLAY_MSG_T *msg)` | 处理显示层下发的消息（类型见 `msg->type`，如聊天、状态、表情等） |

消息类型与数据结构定义见 `src/miscs/gui/display/tuya_ai_display.h`。

## ➕ 添加新开发板

### 步骤 1: 创建板级目录

在 `boards/` 目录下创建新目录，例如 `T5AI_BOARD_NEW/`。

### 步骤 2: 实现板级初始化

创建 `tuya_device_board.c` 和 `tuya_device_board.h`，实现板级初始化函数并定义 GPIO 引脚、显示设置等。

### 步骤 3: 实现 UI（可选）

如果需要自定义 UI，在 `ui/` 目录下实现 UI 应用。

### 步骤 4: 配置编译系统

在 `local.mk` 或相关配置文件中添加新开发板的编译选项。
```c
ifeq ($(CONFIG_T5AI_BOARD), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T5AI_BOARD/tuya_device_board.c
endif
```

### 步骤 5: 配置编译系统

使用 `make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai` 选择新增的开发板。
使用 `make app_config APP_NAME=tuyaos_demo_wukong_ai` 生成新开发板的配置文件。

### 步骤 6: 测试验证
按照[硬件声学结构测试指南](../../docs/硬件声学结构测试指南.md)测试开发板的声学性能和结构。
测试相关软件功能。


## ➕ 添加蜂窝模组支持

默认每个硬件都可以支持蜂窝模组，需要在`make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai` 阶段，选择`ENABLE_USB_CELLULUA_DONGLE`

## ➕ 添加语音芯片支持

默认每个硬件都可以支持语音芯片，需要在 `make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai` 或 `./build/APPConfig` 中对应板子下勾选/添加：
```c
        select USING_UART_AUDIO_INPUT
        select USING_UART_AUDIO_OUTPUT
```
目录已经支持3颗语音芯片，包括国芯和启英泰伦：
**GX8006** ：[唤醒词&固件](https://tuyaos.com/viewtopic.php?t=9147)
**GX8008C** ：[唤醒词&固件](https://tuyaos.com/viewtopic.php?t=9166)
**CI1302** : [唤醒词&固件](https://tuyaos.com/viewtopic.php?t=9148)

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。