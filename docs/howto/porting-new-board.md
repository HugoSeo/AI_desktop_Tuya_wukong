# 适配一块新板子

## 目标

在 wukong demo 中新增一个板型（本文以 `T5AI_BOARD_NEW` 为例），完成从建目录、接入编译系统到编译烧录验证的全过程。完成后可用 `make app_config_choice` 一键切换到新板。

## 前置条件

- 已按[快速开始](../quickstart.md)跑通任意一块现有板（能编译、烧录、对话）。
- 手上有新板的硬件资料：各功能引脚（音频触发键、扬声器使能、LED、配网键、电池检测）、屏幕型号与分辨率、摄像头型号（如有）。
- 从[整体架构](../architecture.md)的功能模块表和 `src/boards/README.md` 的开发板对比表中，找一块**硬件形态最接近的现有板**作为模板（例：带 240×240 SPI 屏 + 电池 → 参考 `T5AI_BOARD_EVB`；无屏纯语音 → 参考 `T3AI_BOARD`）。

## 步骤

### 1. 创建板级目录

以模板板为底本复制改名：

```bash
cd apps/tuyaos_demo_wukong_ai
cp -r src/boards/T5AI_BOARD_EVB src/boards/T5AI_BOARD_NEW
```

板级目录的最小构成是 `tuya_device_board.c` / `tuya_device_board.h`，核心是实现：

```c
OPERATE_RET tuya_device_board_init(VOID)
{
    // 1. GPIO / pinmux 初始化
    // 2. 外设初始化：显示屏、摄像头、按键等
    // 3. UI 接入：默认 wukong UI 或注册自定义 board UI（见步骤 6）
    return OPRT_OK;
}
```

### 2. 在板型 choice 中注册新板

编辑 `build/APPconfig`，在 `Board type` choice 里（`default T5AI_BOARD` 那个 choice 块）新增：

```kconfig
        config T5AI_BOARD_NEW
            bool "T5AI_BOARD_NEW"
```

### 3. 为新板补硬件默认值

编辑 `src/boards/Kconfig`，为新板在各配置项加 `default ... if T5AI_BOARD_NEW`。至少覆盖引脚类配置（引脚值 64 表示禁用该功能）：

```kconfig
    config TUYA_AI_TOY_AUDIO_TRIGGER_PIN_NUM
        int "Audio trigger GPIO pin number (64=disabled)"
        default 4  if T5AI_BOARD_NEW      # ← 新增，按实际硬件填
        ...

    config TUYA_AI_TOY_SPK_EN_PIN_NUM
        default 19 if T5AI_BOARD_NEW
        ...
```

有屏、有电池的板子同样在对应的屏参 / 电池配置项加 `if T5AI_BOARD_NEW` 默认值（照抄模板板那一行改值即可）。

### 4. 接入编译系统

编辑应用根目录 `local.mk`，仿照现有板加一段选板块：

```makefile
ifeq ($(CONFIG_T5AI_BOARD_NEW), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD_NEW/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T5AI_BOARD_NEW/tuya_device_board.c
endif
```

板子如有自定义 UI 源文件（步骤 6 方式二），也在此块中一并加入 `LOCAL_SRC_FILES`。

### 5. 生成板型预置配置

`build/appconfig/<板型名>` 是各板的预置配置文件。复制模板板的文件后把板型开关改过来：

```bash
cp build/appconfig/T5AI_BOARD_EVB build/appconfig/T5AI_BOARD_NEW
# 编辑 build/appconfig/T5AI_BOARD_NEW：
#   CONFIG_T5AI_BOARD_EVB=y        改为  # CONFIG_T5AI_BOARD_EVB is not set
#   加一行                          CONFIG_T5AI_BOARD_NEW=y
```

然后在 SDK 根目录选择并生成配置：

```bash
make app_config_choice APP_NAME=tuyaos_demo_wukong_ai   # 列表中选 T5AI_BOARD_NEW
make app_config APP_NAME=tuyaos_demo_wukong_ai          # 生成 include/tuya_app_config.h
```

> **注意**：改过 `APPconfig` / `Kconfig` / appconfig 文件后必须重跑 `make app_config`，否则配置头不更新，改动不生效。

### 6. 接入 UI（带屏板子）

两种方式二选一（详见 `src/boards/README.md` 的"UI 实现"一节）：

- **默认 wukong UI**：板级目录不放 `ui/`，什么都不用做，框架 `ui_app_init()` 自动走完整 wukong 页面，分辨率自适应。
- **自定义 board UI**：在 `src/boards/T5AI_BOARD_NEW/ui/` 下实现 `app_ui_init()` 和 `app_ui_msg_handler()`，并在 `tuya_device_board_init()` 中注册：

```c
ui_app_register_board_ui(app_ui_init, app_ui_msg_handler);
```

无屏板子在 menuconfig 中关闭 `ENABLE_TUYA_UI` 即可。

### 7. 编译

```bash
make app APP_NAME=tuyaos_demo_wukong_ai
```

## 验证方法

1. 烧录固件，上电看串口日志：`tuya_device_board_init` 无报错，设备正常进入配网/待机。
2. 逐项过硬件功能：音频触发键按下能进入对话；LED 状态指示正常；有屏板子开机出画面、触摸可用；有摄像头的板子跑一次拍照/识图。
3. 有屏板子确认 UI 页面切换、中文字体渲染正常。
4. 声学验收按[硬件声学结构测试指南](../troubleshooting/acoustic-test.md)执行；语音链路问题参考[语音算法调试指南](../troubleshooting/audio-tuning.md)。

## 常见问题

**SPI 屏完全不亮（T5 平台）**
T5 的 SPI0 默认 pinmux 在 14/15/16/17 脚；如果你的屏接在 44-47 脚，必须在板级初始化里显式重配 pinmux，否则显示静默失败（无任何报错）。参考 `src/boards/T5AI_BOARD_ROBOT/tuya_device_board.c` 中的做法：

```c
tkl_io_pinmux_config(TUYA_IO_PIN_45, TUYA_SPI0_CS);
tkl_io_pinmux_config(TUYA_IO_PIN_44, TUYA_SPI0_CLK);
tkl_io_pinmux_config(TUYA_IO_PIN_46, TUYA_SPI0_MOSI);
tkl_io_pinmux_config(TUYA_IO_PIN_47, TUYA_SPI0_MISO);
```

**屏幕颜色不对（整体偏粉/红蓝互换）**
UI 框架按原生 RGB565 渲染；走 8-bit SPI 的屏若出现背景发粉，需要在刷屏层开启字节交换：板级编译选项定义 `UI_LCD_RGB565_BYTE_SWAP=1`（生效点见 `src/ui/port/ui_port_disp_dma2d.c`）。

**menuconfig 里改了配置但不生效**
少跑了 `make app_config APP_NAME=tuyaos_demo_wukong_ai`——所有 `CONFIG_*` 改动都要经它落到 `include/tuya_app_config.h`。

**自定义 UI 没显示，走的还是默认页面**
检查两点：`ui_app_register_board_ui()` 是否在 `tuya_device_board_init()` 里被调用（注册必须发生在 UI 初始化之前）；自定义 UI 的 `.c` 文件是否加进了 `local.mk` 的选板块。

**按键 / LED 没反应**
对应引脚的 Kconfig 默认值是否为 64（禁用）；`make app_config_choice` 选的是否真是新板（看 `include/tuya_app_config.h` 里 `CONFIG_T5AI_BOARD_NEW` 是否为 1）。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
