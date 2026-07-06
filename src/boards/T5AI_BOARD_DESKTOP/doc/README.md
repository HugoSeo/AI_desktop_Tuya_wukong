# T5AI_BOARD_DESKTOP 用户操作与开发指南

1、T5AI_BOARD_DESKTOP 是悟空 AI 桌面机器人的开发板方案，基于 T5 SoC 模组，配备 320×240 ST7789V2 LCD 触摸屏，采用 LVGL 图形框架构建完整的交互 UI。支持 AI 语音对话、摄像头拍照与侦测、音乐播放、录音、P2P 通话等功能。

2、桌面机器人UI资源较多，所有的资源都要放在在SD卡中

- 先将SD卡格式化成exFAT
- 解压t5_fs.zip，将其解压到t5_fs
- 将t5_fs整个拷贝到sd中
- 在开启TUYA_FILE_SYSTEM与FILE_SYSTEM_LFS_SD宏之后，文件系统的根目录为t5_fs
- 该文件系统基于ui组件创建

---

## 目录

- [硬件规格](#硬件规格)
- [GPIO 引脚映射](#gpio-引脚映射)
- [构建与烧录](#构建与烧录)
- [SD 卡与文件系统](#sd-卡与文件系统)
- [UI 框架架构](#ui-框架架构)
- [页面总览与导航](#页面总览与导航)
- [启动引导流程](#启动引导流程)
- [首页三屏](#首页三屏)
- [设备模式切换](#设备模式切换)
- [闲聊子模式切换](#闲聊子模式切换)
- [聊天页面](#聊天页面)
- [个人中心](#个人中心)
- [音乐播放器](#音乐播放器)
- [相机](#相机)
- [相册](#相册)
- [录音](#录音)
- [通话（P2P）](#通话p2p)
- [侦测](#侦测)
- [设置](#设置)
- [事件系统](#事件系统)
- [UI 资源清单](#ui-资源清单)
- [源代码结构](#源代码结构)
- [参考文档](#参考文档)

---

## 硬件规格

| 项目 | 规格 |
|------|------|
| SoC 模组 | T5-E1（Tuya T5 系列） |
| 产品 PID | `gcwfmdfkv6824tuh` |
| 显示屏 | 320×240 LCD，ST7789V2 驱动，SPI 接口 |
| 显示帧率 | 15 FPS |
| 触摸屏 | 电容触摸，支持滑动、点击、双击手势 |
| 摄像头 | DVP 接口（GC2145），ISP 分辨率 480×480，10 FPS |
| 摄像头输出 | JPEG + YUV422 双输出，JPEG 质量 10-25 KB |
| 音频输入 | UART 音频编解码器（GX8006 / CI1302） |
| 音频编码 | OPUS / SPEEX（可配置） |
| 存储 | SD 卡（exFAT 格式），用于 UI 资源和用户数据 |
| 电池管理 | 支持（可选），GPIO 充电检测 + 电量 ADC |
| 电源控制 | 长按 5 秒关机，GPIO 4 锁电 |
| 运动控制 | IMU 传感器（可选） |
| 通信接口 | WiFi、蓝牙 |

---

## GPIO 引脚映射

### 电源与按键

| GPIO | 功能 | 说明 |
|------|------|------|
| GPIO 3 | `DEVICE_POWER_NET_KEY_PIN` | 电源/配网按键，长按 5 秒关机 |
| GPIO 4 | `DEVICE_POWER_PIN` | 电源锁存，上电瞬间拉高保持供电 |

### 音频

| GPIO | 功能 | 说明 |
|------|------|------|
| GPIO 28 | `TUYA_AI_TOY_AUDIO_TRIGGER_PIN` | 音频触发/麦克风按键 |
| GPIO 28 | `TUYA_AI_TOY_SPK_EN_PIN` | 扬声器使能 |

### 摄像头（DVP）

| GPIO | 功能 | 说明 |
|------|------|------|
| GPIO 49 | DVP 电源控制 | 摄像头供电开关 |
| GPIO 50 | DVP 复位 | 低电平有效 |
| GPIO 20 | I2C0 SCL | 摄像头 I2C 时钟 |
| GPIO 21 | I2C0 SDA | 摄像头 I2C 数据 |

### 外设总线

| GPIO | 功能 | 说明 |
|------|------|------|
| GPIO 14-19 | SDIO | SD 卡接口（CLK/CMD/DATA0-3） |
| GPIO 20-21 | I2C0 | 摄像头/传感器通信 |
| GPIO 40-41 | UART2 | 音频编解码器通信（RX/TX） |
| GPIO 44-47 | SPI0 | LCD 显示屏接口（CLK/CS/MOSI/MISO） |

### 电池（可选）

| GPIO | 功能 | 说明 |
|------|------|------|
| 充电检测 GPIO | `TUYA_AI_TOY_CHARGE_PIN` | 充电状态检测 |
| 电量 ADC GPIO | `TUYA_AI_TOY_BATTERY_CAP_PIN` | 电池电量采集 |

---

## 构建与烧录

### 前置条件

- 完成 `prepare.sh` 初始化（下载工具链和组件）
- 确保 `.config` 中选择了 `CONFIG_T5AI_BOARD_DESKTOP=y`

### 构建命令

```bash
# 生成配置 选择 T5AI_BOARD_DESKTOP
make app_config_choice APP_NAME=tuyaos_demo_wukong_ai

# 应用级 menuconfig（调整功能开关）
make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai

# 配置变更后重新生成头文件
make app_config APP_NAME=tuyaos_demo_wukong_ai
```

### 关键 Kconfig 配置项

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `CONFIG_T5AI_BOARD_DESKTOP` | 选择桌面机器人开发板 | y |
| `CONFIG_ENABLE_TUYA_CAMERA` | 启用摄像头 | 可选 |
| `CONFIG_CAMERA_TYPE_DVP` | DVP 摄像头接口 | y |
| `CONFIG_ENABLE_TUYA_UI` | 启用 LVGL UI | y |
| `CONFIG_ENABLE_BATTERY` | 启用电池管理 | 可选 |
| `CONFIG_DEVICE_POWER_CONTROL` | 启用长按关机 | y |
| `ENABLE_APP_OPUS_ENCODER` | 使用 OPUS 编码 | 可选 |
| `ENABLE_APP_SPEEX_ENCODER` | 使用 SPEEX 编码 | 可选 |
| `ENABLE_TOOLKITS_PLAYBACK` | 启用音乐播放功能 | 可选 |
| `TUYA_FILE_SYSTEM` | 启用文件系统 | y |
| `FILE_SYSTEM_LFS_SD` | 文件系统基于 SD 卡 | y |

### 条件编译说明

`local.mk` 中根据 `CONFIG_T5AI_BOARD_DESKTOP=y` 条件编译以下内容：
- 板级头文件和源文件（`tuya_device_board.c`）
- 摄像头驱动（`tuya_device_camera.c`，需 `CONFIG_ENABLE_TUYA_CAMERA=y`）
- UI 源文件目录（`ui/` 下所有 `.c` 文件）
- 运动控制和 IMU 驱动

---

## SD 卡与文件系统

桌面机器人 UI 资源较多，所有资源文件存放在 SD 卡中。

### 准备步骤

1. 将 SD 卡格式化为 **exFAT** 格式
2. 解压 `doc/t5_fs.zip` 得到 `t5_fs` 目录
3. 将整个 `t5_fs` 目录拷贝到 SD 卡根目录
4. 开启 `TUYA_FILE_SYSTEM` 与 `FILE_SYSTEM_LFS_SD` 宏后，文件系统根目录为 `/t5_fs`

### 文件系统目录结构

```
/t5_fs/
├── font/                           # 字体资源
│   └── AlibabaPuHuiTi3_18_Regular.bin  # TTF 字体文件
├── music/                          # 音乐文件存放目录
├── tmp/
│   ├── picture/                    # 相册照片（TUYA<n>.jpeg，最多 100 张）
│   ├── record/                     # 录音文件（.wav 格式）
│   │   └── record_list.json        # 录音元数据索引
│   ├── take_photo.jpeg             # 拍照临时文件
│   └── ai_jpeg_msg.jpeg            # AI 生成图片缓存
├── (emoji GIF 资源)                # 20 种表情 GIF 动画
└── (UI 图标资源)                   # 应用图标、功能图标等
```

### 关键存储路径

| 路径 | 用途 | 限制 |
|------|------|------|
| `/t5_fs/music/` | 音乐文件库 | - |
| `/t5_fs/tmp/picture/` | 相册照片 | 最多 100 张 |
| `/t5_fs/tmp/record/` | 录音文件 | 最多 100 条 |
| `/t5_fs/tmp/take_photo.jpeg` | 拍照快照 | 单文件覆盖 |
| `/t5_fs/tmp/ai_jpeg_msg.jpeg` | AI 生成图片 | 单文件覆盖 |
| `/t5_fs/font/` | 字体资源 | - |

---

## UI 框架架构

### 技术栈

- **图形框架**：LVGL（Light and Versatile Graphics Library）
- **屏幕分辨率**：320×240 像素（`DESK_LCD_WIDTH` × `DESK_LCD_HEIGHT`）
- **字体**：阿里巴巴普惠体 3（AlibabaPuHuiTi3），支持 16/18/20/30/40/65/120 px 7 种字号
- **语言支持**：简体中文 / English

### 页面管理框架

UI 采用统一的页面管理框架（`desk_handle_ui`），支持异步页面切换，共定义 **19 个页面**：

| 页面 ID | 枚举值 | 说明 |
|---------|--------|------|
| `DHUI_SCREEN_ID_STARTUP` | 0 | 启动画面 |
| `DHUI_SCREEN_ID_LANGUAGE` | 1 | 语言选择 |
| `DHUI_SCREEN_ID_QRCODE` | 2 | 配网二维码 |
| `DHUI_SCREEN_ID_NETWORK_CFG` | 3 | 网络配置 |
| `DHUI_SCREEN_ID_HOME1` | 4 | 首页1 — 表情屏 |
| `DHUI_SCREEN_ID_HOME2` | 5 | 首页2 — 时钟日期屏 |
| `DHUI_SCREEN_ID_HOME3` | 6 | 首页3 — 快捷设置屏 |
| `DHUI_SCREEN_ID_CHAT` | 7 | AI 聊天页面 |
| `DHUI_SCREEN_ID_PERSONAL_CENTER` | 8 | 个人中心 |
| `DHUI_SCREEN_ID_SETTINGS` | 9 | 设置 |
| `DHUI_SCREEN_ID_MUSIC` | 10 | 音乐播放器 |
| `DHUI_SCREEN_ID_MUSIC_PLAYLIST` | 11 | 音乐播放列表 |
| `DHUI_SCREEN_ID_PHOTO` | 12 | 相册 |
| `DHUI_SCREEN_ID_CAMERA` | 13 | 相机 |
| `DHUI_SCREEN_ID_RECORD` | 14 | 录音 |
| `DHUI_SCREEN_ID_RECORD_LIST` | 15 | 录音列表 |
| `DHUI_SCREEN_ID_DEVICE_MODE` | 16 | 设备模式选择 |
| `DHUI_SCREEN_ID_DETECTION` | 17 | 侦测消息 |
| `DHUI_SCREEN_ID_CALL` | 18 | P2P 通话 |

### 页面切换模式

| 切换类型 | 枚举值 | 说明 |
|---------|--------|------|
| `DHUI_SWITCH_PERMANENT` | 0 | 永久切换：旧页面资源释放，不可回退复用 |
| `DHUI_SWITCH_TEMPORARY` | 1 | 临时切换：旧页面保留在内存中，可快速回退 |
| `DHUI_SWITCH_DYNAMIC` | 2 | 动态切换：旧页面资源先释放再重建 |

### 切换动画

- 动画时长：**50 ms**（`DHUI_SWITCH_DURATION_MS`）
- 动画延迟：**0 ms**（`DHUI_SWITCH_DELAY_MS`）
- 动画类型：淡入淡出

### 页面注册机制

每个页面通过 `dhui_screen_desc_t` 描述结构注册：

```c
typedef struct {
    dhui_setup_scr_cb setup;        // 页面创建回调：创建并返回 LVGL 屏幕对象
    dhui_res_clear_cb res_clear;    // 资源清理回调（可选）：离开页面时释放资源
    uint32_t default_back_id;       // 默认返回目标页面 ID
    const char *name;               // 调试用页面名称
} dhui_screen_desc_t;
```

核心 API：
- `desk_handle_ui_init()` — 初始化页面导航框架
- `desk_handle_ui_register_all()` — 批量注册所有页面
- `desk_handle_ui_switch_to(id, anim, type)` — 异步切换到目标页面
- `desk_handle_ui_back(anim, type)` — 返回上一页面
- `desk_handle_ui_back_to(id, anim, type)` — 返回指定页面
- `desk_handle_ui_get_current_screen_id()` — 获取当前页面 ID

---

## 页面总览与导航

### 页面结构

```
┌───────────────────────────────────────────────────────────────┐
│                        启动引导流程                            │
│   启动画面 → 语言选择 → 二维码配网 → 网络配置 → 首页           │
└───────────────────────────────────────────────────────────────┘
                               ↓
┌───────────────────────────────────────────────────────────────┐
│                    首页三屏（滑动切换）                         │
│                                                               │
│   ← 首页1（表情） ↔ 首页2（时钟日期） ↔ 首页3（快捷设置） →  │
│        ↕                    ↕                                 │
│      聊天页面           个人中心                                │
└───────────────────────────────────────────────────────────────┘
                               ↓
┌───────────────────────────────────────────────────────────────┐
│                       个人中心                                 │
│                                                               │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐     │
│  │ 音乐 │ │ 相机 │ │ 相册 │ │ 录音 │ │ 设置 │ │ 模式 │     │
│  └──────┘ └──────┘ └──────┘ └──────┘ └──────┘ └──────┘     │
│  ┌──────┐ ┌──────┐                                           │
│  │ 侦测 │ │ 通话 │                                           │
│  └──────┘ └──────┘                                           │
└───────────────────────────────────────────────────────────────┘
```

### 手势导航总表

| 当前页面 | 手势 | 目标页面 |
|---------|------|---------|
| 首页1（表情） | 左滑 | 首页2（时钟日期） |
| 首页1（表情） | 右滑 | 聊天页面 |
| 首页1（表情） | 下滑 | 首页3（快捷设置） |
| 首页1（表情） | 双击 | 切换表情动画 |
| 首页2（时钟日期） | 左滑 | 个人中心 |
| 首页2（时钟日期） | 右滑 | 首页1（表情） |
| 首页2（时钟日期） | 下滑 | 首页3（快捷设置） |
| 首页3（快捷设置） | 上滑 | 首页2（时钟日期） |
| 聊天页面 | 左滑 | 首页1（表情） |
| 聊天页面 | 右滑 | 上一页面 |
| 相册 | 左滑 | 下一张照片 |
| 相册 | 右滑 | 上一张照片 |

### 手势参数

| 参数 | 值 | 说明 |
|------|-----|------|
| `CLICKED_EVENT_TIME` | 200 ms | 点击事件判定时长 |
| `CLICKED_RESET_TIME` | 300 ms | 点击重置时长 |
| `CLICKED_RESET_NUM` | 5 | 连续点击重置计数 |

### 返回逻辑

所有功能页面（音乐、相机、相册、录音、侦测、通话、设备模式、设置）均通过左上角**返回按钮**回到**个人中心**。音乐播放列表返回音乐主页，录音列表返回录音主页。

---

## 启动引导流程

设备首次上电或未配网时，会进入启动引导流程：

```
启动画面（Startup）→ 语言选择（Language）→ 二维码配网（QR Code）→ 网络配置（Network Config）→ 首页
```

### 各阶段说明

| 阶段 | 页面 ID | 说明 |
|------|---------|------|
| 启动画面 | `DHUI_SCREEN_ID_STARTUP` | 显示品牌动画，系统初始化 |
| 语言选择 | `DHUI_SCREEN_ID_LANGUAGE` | 选择系统语言（简体中文 / English），选择后持久化存储 |
| 二维码配网 | `DHUI_SCREEN_ID_QRCODE` | 显示配网二维码，用户使用涂鸦 App 扫码 |
| 网络配置 | `DHUI_SCREEN_ID_NETWORK_CFG` | 显示配网进度，包含等待动画（表情 GIF）；配网成功跳转首页，失败可重试 |

### 配网回调

- `event_netcfg_start(data)` — 配网开始事件
- `cfg_network_success()` — 配网成功，跳转首页
- `cfg_network_failed()` — 配网失败，显示重试提示

---

## 首页三屏

### 首页1 — 表情屏

AI 机器人表情动画展示页，设备空闲时为主显示页面。

- **20 种表情**动画（GIF 格式），包括：

  | 序号 | 表情 | 文件名 |
  |------|------|--------|
  | 0 | 默认 | `00_Default.gif` |
  | 1 | 开心 | `01_Happy.gif` |
  | 2 | 困惑 | `02_Confused.gif` |
  | 3 | 害羞 | `03_Shy.gif` |
  | 4 | 哭泣 | `04_Cry.gif` |
  | 5 | 愤怒 | `05_Angry.gif` |
  | 6 | 害怕 | `06_Scared.gif` |
  | 7 | 惊讶 | `07_Surprised.gif` |
  | 8 | 失望 | `08_Disappointed.gif` |
  | 9 | 烦躁 | `09_Annoyed.gif` |
  | 10 | 思考 | `10_Thinking.gif` |
  | 11 | 大笑 | `11_Laugh.gif` |
  | 12 | 搞笑 | `12_Funny.gif` |
  | 13 | 喜爱 | `13_Love.gif` |
  | 14 | 尴尬 | `14_Embarrassed.gif` |
  | 15 | 眨眼 | `15_Blink.gif` |
  | 16 | 酷 | `16_Cool.gif` |
  | 17 | 放松 | `17_Relaxed.gif` |
  | 18 | 美味 | `18_Delicious.gif` |
  | 19 | 不开心 | `19_Unhappy.gif` |

- 每 5 秒自动轮播不同表情
- 双击屏幕可手动切换表情
- AI 对话时根据回复情感自动更新表情（通过 `receive_emotional_feedback()` 回调）
- 每种表情有两个版本：显示压缩版和原始高质量版

### 首页2 — 时钟日期屏

时间与日期信息展示页。

- 显示当前时间（大字体时分，每 200 ms 通过 `handle_home2_update_timer()` 刷新）
- 显示当前日期（月/日/星期），支持中英文格式
- 顶部状态栏：
  - WiFi 信号状态图标（24×24 / 30×30 两种尺寸）
  - 电池电量图标（每 60 秒通过 `handle_home2_battery_timer()` 刷新）
- 页面指示点（`point_1.png` / `point_2.png`）标识当前所在首页

### 首页3 — 快捷设置屏

下拉快捷面板，可快速调节设备参数。

- **音量滑块**（左）：调节设备播放音量（0-100），松手后立即生效
- **亮度滑块**（中）：调节屏幕亮度（0-100）
- **闹钟音量滑块**（右）：调节闹钟音量（0-100）
- 底部上拉按钮：点击收起面板回到上一首页屏
- 从首页1或首页2下滑均可进入，收起时回到来源页面

---

## 设备模式切换

设备支持 **6 种设备模式**，通过 UI 可切换其中 4 种，其余 2 种由系统自动管理：

| 设备模式 | 枚举值 | 说明 | UI 可切换 |
|---------|--------|------|----------|
| 闲聊模式 | `AI_DEVICE_MODE_CHAT` | AI 语音对话，支持多种触发方式 | ✅ |
| 翻译模式 | `AI_DEVICE_MODE_TRANSLATE` | 实时语音翻译 | ✅ |
| 生图模式 | `AI_DEVICE_MODE_DRAW` | AI 图片生成 | ✅ |
| 侦测模式 | `AI_DEVICE_MODE_DETECTION` | 运动/物体检测与告警 | ✅ |
| P2P 模式 | - | 音视频通话（由通话页面触发） | 系统管理 |
| 录音模式 | - | 音频录制（由录音页面触发） | 系统管理 |

### 设备模式页面

进入路径：个人中心 → 设备模式图标

- 显示 2×2 模式网格（闲聊、翻译、生图、侦测）
- 当前激活模式以蓝色高亮显示，其余为灰色
- 每种模式有独立图标：`ChatMode.png`、`TranslationMode.png`、`PictureMode.png`、`DetectionMode.png`
- 最多支持 6 个模式图标（`DEVICE_MODE_ICON_MAX = 6`）

### 切换方式

**方式一：UI 切换**

1. 从首页2左滑进入**个人中心**
2. 点击**设备模式**图标（`ModeSwitcherEntry.png`）
3. 在模式网格中选择目标模式
4. 点击后自动切换并跳转到**聊天页面**，显示"已成功切换到 [模式名]"

**方式二：物理按键双击**

在闲聊模式下，双击设备物理按键会**循环切换闲聊子模式**（详见下节）。若当前为非闲聊模式，双击会先切回闲聊模式。

---

## 闲聊子模式切换

闲聊模式（`AI_DEVICE_MODE_CHAT`）下包含 **4 种子模式**，决定 AI 对话的触发方式：

| 子模式 | 触发方式 | 说明 |
|-------|---------|------|
| 长按模式 | 长按物理按键说话，松手发送 | 适合嘈杂环境，手动控制录音起止 |
| 按键模式 | 短按按键开始说话，AI 自动检测语音结束 | 单次按键，回合制对话 |
| 唤醒模式 | 说出唤醒词触发，自动检测语音结束 | 免按键交互，唤醒词触发 |
| 自由模式 | 持续监听 + 唤醒词触发 | 自由对话，无需按键，支持连续对话 |

### 切换方式

- **物理按键双击**：在闲聊模式下，双击按键循环切换子模式（长按 → 按键 → 唤醒 → 自由 → 长按 ...），设备会播放切换提示音
- **App / MCP 指令**：通过涂鸦 App 远程设置或 MCP 工具控制切换
- 切换后屏幕中央会弹出当前子模式名称（黄色文字，显示约 1.2 秒），聊天页面顶部模式标签同步更新

### 聊天页面模式标签显示

| 子模式 | 显示文本 |
|-------|---------|
| 长按 | 闲聊模式: 长按 |
| 按键 | 闲聊模式: 按键 |
| 唤醒 | 闲聊模式: 唤醒 |
| 自由 | 闲聊模式: 自由 |

非闲聊设备模式时显示对应模式名（如"翻译模式"、"生图模式"、"侦测模式"）。

---

## 聊天页面

AI 对话的核心交互页面。

### 进入方式

- 首页1右滑
- 设备模式切换后自动跳转
- 相册中上传图片给 AI 分析后跳转

### 界面元素

- **顶部**：AI 头像图标（`ai_icon.png`）+ 当前模式标签（通过 `desk_chat_refresh_mode_label()` 刷新）
- **消息区域**（`msg_container`）：可滚动的对话气泡列表
  - AI 消息：左对齐，透明背景
  - 用户消息：右对齐，紫色调背景（`#B8BDDE`）
  - 消息气泡圆角 15px，内边距 12px
- **图片区域**（`picture_cont`）：AI 生成图片的展示区（生图模式时显示）
  - 加载动画（`picture_spinner`）
  - 图片画布（`picture_canvas`）
  - 支持从内存缓冲区或文件路径加载图片

### 消息缓冲区

| 参数 | 值 |
|------|-----|
| ASR 消息缓冲区 | 1024 字节（`AI_ASR_MESSAGE_LEN`） |
| TTS 消息缓冲区 | 2048 字节（`AI_TTS_MESSAGE_LEN`） |

### 对话状态机

```
用户操作（按键/唤醒词）→ 录音（LISTEN）→ 上传（UPLOAD）→ AI 思考（THINK）→ 播放回复（SPEAK）→ 空闲（IDLE）
```

### 流式输出

AI 回复文本以流式方式逐步显示在消息气泡中，通过 `desk_chat_session_t` 管理会话状态：
- `ai_stream_active` — 标记流式输出是否正在进行
- `pending_ai_stop` — 标记是否有待处理的停止事件
- `active_ai_label` — 当前正在接收流式文本的 LVGL 标签对象

支持待处理通知消息（`desk_chat_set_pending_notify()` / `desk_chat_flush_pending_notify()`），在适当时机显示系统提示。

---

## 个人中心

功能入口聚合页面，所有设备功能均可从此页面进入。

### 进入方式

- 首页2左滑

### 界面元素

- **顶部**：返回按钮（`back_left_24_24.png`）+ 标题
- **角色名称**（`role_name`）：显示当前设备模式信息（如"闲聊模式: 按键"），通过 `desk_personal_refresh_role_name()` 刷新
- **功能网格**：8 个功能入口图标

| 图标 | 功能 | 目标页面 |
|------|------|---------|
| `music_app.png` | 音乐 | 音乐播放器 |
| `camera_app.png` | 相机 | 相机实时预览 |
| `photo_app.png` | 相册 | 照片浏览 |
| `record_app.png` | 录音 | 录音功能 |
| `settings_app.png` | 设置 | 设置页面 |
| `ModeSwitcherEntry.png` | 设备模式 | 模式选择 |
| `detection_app.png` | 侦测 | 侦测消息列表 |
| `call_app.png` | 通话 | P2P 通话 |

---

## 音乐播放器

### 进入方式

个人中心 → 音乐图标

### 主播放页面

- **背景**：专辑封面/默认背景（`music_background.png`）
- **唱片动画**：旋转唱片（`music_disk.png`）+ 唱针（`music_styli.png`）
- **歌曲信息**：歌曲标题、歌手名称
- **播放控制**：
  - `play_previous.png` 上一首
  - `play_music.png` / `pause_music.png` 播放/暂停
  - `play_next.png` 下一首
- **播放模式**：点击图标循环切换
  - `play_mode_loop.png` 列表循环
  - `play_mode_sloop.png` 单曲循环
  - `play_mode_random.png` 随机播放
- **播放列表按钮**（`play_list.png`）：进入播放列表页面

### 播放列表页面

- 显示所有可播放曲目列表
- 每项显示歌曲名称和播放状态图标
- 点击曲目开始播放并返回主播放页
- 返回按钮回到音乐主页

### 音乐存储路径

`/t5_fs/music/`

---

## 相机

### 进入方式

个人中心 → 相机图标

### 功能说明

- 进入后显示**实时摄像头画面预览**
  - 摄像头 ISP 分辨率：480×480
  - 画面缩放适配 320×240 显示屏
  - YUV422 预览流 + JPEG 抓拍流（双输出模式）
- **AI 相机开关**（`ai_camera_on.png` / `ai_camera_off.png`）：
  - 开启时，拍照后自动发送给 AI 进行内容描述分析
  - 关闭时，仅保存照片
- 点击**拍照按钮**：
  1. 抓拍当前画面为 JPEG 图片
  2. 保存到 `/t5_fs/tmp/take_photo.jpeg`（临时文件）
  3. 同时保存到相册（`/t5_fs/tmp/picture/`）
  4. 如果 AI 相机开启，图片自动发送给 AI 分析
- 点击返回按钮退出相机

### 摄像头 API

```c
// 初始化
tuya_desktop_camera_init()

// 模式切换
tuya_device_camera_switch_to_jpeg_mode()  // JPEG 模式（拍照）
tuya_device_camera_switch_to_h264_mode()  // H.264 模式（视频流）

// YUV 流控制（引用计数）
tuya_device_camera_yuv_acquire(user)      // 获取 YUV 流（YUV_USER_PREVIEW / YUV_USER_MD）
tuya_device_camera_yuv_release(user)      // 释放 YUV 流

// JPEG 帧获取
tuya_device_camera_get_jpeg_frame(data, len, user_data)
```

---

## 相册

### 进入方式

个人中心 → 相册图标

### 功能说明

- 进入后默认显示**最新一张照片**
- **浏览照片**：左滑查看下一张，右滑查看上一张
- **删除照片**：点击右上角红色删除按钮（`delete_24_24.png`），移除当前照片
- **AI 分析**：点击左侧上传按钮，将当前照片发送给 AI 分析图片内容，自动跳转到聊天页面查看分析结果

### 存储说明

| 项目 | 说明 |
|------|------|
| 存储路径 | `/t5_fs/tmp/picture/` |
| 文件命名格式 | `TUYA<序号>.jpeg` |
| 最大存储数量 | 100 张 |

---

## 录音

### 进入方式

个人中心 → 录音图标

### 录音主页面

- 中央显示录音计时器（MM:SS 格式）
- 录音状态图标：
  - `record_default.png` — 空闲状态
  - `record_ing.png` — 录音中（红色圆点）
  - `record_pause.png` — 已暂停

### 操作流程

1. **开始录音**：点击录音按钮，计时器开始计时
2. **暂停/继续**：录音过程中点击暂停按钮，再次点击继续录音
3. **保存录音**：点击保存按钮，录音文件以 WAV 格式保存
4. **查看录音列表**：点击列表按钮（`record_list.png`）进入录音列表页

### 录音列表页面

- 显示所有已保存的录音文件
- 每条记录显示：录音名称、时长、创建时间
- 播放控制：
  - `record_play_playing.png` / `record_play_pause.png` 播放/暂停
  - `fast_forward.png` 快进
  - `fast_back.png` 快退
  - `delete_24_24.png` 删除

### 存储说明

| 项目 | 说明 |
|------|------|
| 存储路径 | `/t5_fs/tmp/record/` |
| 文件格式 | WAV |
| 元数据文件 | `/t5_fs/tmp/record/record_list.json` |
| 最大存储数量 | 100 条 |

---

## 通话（P2P）

基于 P2P 协议的音视频通话功能，可与涂鸦 App 端建立实时通话。

### 进入方式

个人中心 → 通话图标

### 界面元素

- **通话状态标签**：显示当前状态
- **呼叫按钮**（右侧，`call_answer.png`）：发起呼叫
- **挂断按钮**（左侧，`call_hangup.png`）：结束通话

### 通话流程

1. **发起呼叫**：点击呼叫按钮 → 状态显示"呼叫中..."（30 秒超时）
2. **等待接听**：App 端收到来电通知并接听
3. **通话中**：接通后状态显示"通话中..."，双方可进行语音对话
4. **挂断**：点击挂断按钮或点击返回按钮均可结束通话

### 通话状态

| 状态 | 说明 |
|------|------|
| 空闲 | 无活动通话 |
| 呼叫中 | 已发起呼叫，等待对方接听（30 秒超时） |
| 通话中 | 双方已接通，语音通话进行中 |

---

## 侦测

AI 驱动的运动/物体检测功能，利用摄像头画面进行实时分析并记录告警事件。

### 进入方式

- 个人中心 → 侦测图标
- 或通过设备模式切换至**侦测模式**

### 界面元素

- **侦测消息列表**（`det_msg_list.png`）：按时间倒序展示检测事件
- **每条消息显示**（`det_msg.png`）：事件标题、检测时间、附带的抓拍图片
- **页码下拉框**（右上角）：翻页查看更多历史记录
- **AI 检测按钮**（顶部）：手动触发一次检测查询

### 工作原理

- 侦测模式下，设备通过摄像头对比帧差检测运动
- 运动检测通过 `tuya_device_camera_md_start(cb)` 注册 YUV 帧回调
- 检测灵敏度分 3 级，默认阈值 50
- 相邻检测事件最小间隔 10 秒（冷却时间）
- AI 对话期间自动暂停侦测，避免干扰
- 检测结果查询范围：最近 24 小时
- 每页显示 10 条消息，最多 20 页

---

## 设置

### 进入方式

个人中心 → 设置图标

### 设置项

#### 网络设置

- 显示当前网络连接状态（已连接/未连接）
- 连点五次弹出配网重置确认，确认按键倒计时 5 秒后可点击重置设备
- 已连接时显示 WiFi 信号图标（`wifi_30_30.png`）和网络状态图标（`network_30_30.png`）
- 未连接时显示配网二维码（`tuya_app_qr.png`），使用涂鸦 App 扫码配网

#### 语言设置

| 枚举值 | 语言 |
|--------|------|
| `DESK_CHINESE`（0） | 简体中文 |
| `DESK_ENGLISH`（1） | English |

- 切换后立即生效，设置持久化存储（键名：`ai_desk_language`）
- 影响范围：日期格式、星期显示、UI 文本

---

## 事件系统

UI 通过统一的消息分发机制接收和处理 AI 系统事件。消息通过 `app_ui_msg_handler()` 入口分发到各处理函数。

### 消息类型

| 消息类型 | 说明 | 处理函数 |
|---------|------|---------|
| `TY_DISPLAY_TP_HUMAN_CHAT` | 用户输入消息（ASR 识别文本） | `receive_ai_message_data()` |
| `TY_DISPLAY_TP_AI_CHAT_START` | AI 开始回复 | `receive_ai_message_data()` |
| `TY_DISPLAY_TP_AI_CHAT_DATA` | AI 流式回复数据（TTS 文本片段） | `receive_ai_message_data()` |
| `TY_DISPLAY_TP_AI_CHAT_STOP` | AI 回复结束 | `receive_ai_message_data()` |
| `TY_DISPLAY_TP_EMOJI` | 情感反馈（更新表情） | `receive_emotional_feedback()` |
| `TY_DISPLAY_TP_CHAT_MODE` | 聊天子模式变更 | `receive_ai_chat_mode_data()` |
| `TY_DISPLAY_TP_STAT_NET` | 网络状态更新 | `receive_network_status_data()` |
| `TY_DISPLAY_TP_AI_IMAGE` | AI 生成图片 | `receive_ai_picture_data()` |
| `TY_DISPLAY_TP_CLEAR_ATTACHMENT` | 清除缓存附件 | （空操作） |

### 消息数据结构

```c
typedef struct {
    char *asr_txt;       // ASR 语音识别文本
    int asr_len;         // ASR 文本长度
    char *tts_txt;       // TTS 文本（AI 回复）
    int tts_len;         // TTS 文本长度
} ai_message_t;
```

---

## UI 资源清单

### 字体

| 字号 | 字体变量名 | 用途 |
|------|-----------|------|
| 16px | `AlibabaPuHuiTi3_Regular16` | 小号文字 |
| 18px | `AlibabaPuHuiTi3_Regular18_Static` | 常规文字（静态编译） |
| 20px | `AlibabaPuHuiTi3_Regular20` | 中号文字 |
| 30px | `AlibabaPuHuiTi3_Regular30` | 标题文字 |
| 40px | `AlibabaPuHuiTi3_Regular40` | 大标题 |
| 65px | `AlibabaPuHuiTi3_Regular65` | 时钟数字 |
| 120px | `AlibabaPuHuiTi3_Regular120` | 超大数字 |

TTF 字体路径：`/t5_fs/font/AlibabaPuHuiTi3_18_Regular.bin`

字体加载模式由 `AI_CHAT_FONT_USED_STATIC` 控制：
- `1`：使用静态编译字体（默认）
- `0`：从 SD 卡 bin 文件动态加载

### 图标分类

**应用图标**（个人中心网格）：`alarm_app.png`、`calendar_app.png`、`camera_app.png`、`clock_app.png`、`file_app.png`、`music_app.png`、`photo_app.png`、`record_app.png`、`settings_app.png`、`weather_app.png`、`detection_app.png`、`call_app.png`

**导航图标**：`back_left_24_24.png`、`back_top_24_24.png`、`up.png`

**状态图标**：`wifi_24_24.png`、`wifi_30_30.png`、`network_30_30.png`

**方向箭头**：`arrow_yellow.png`、`arrow_black.png`、`arrow_white.png`

**音乐图标**：`music_background.png`、`music_disk.png`、`music_styli.png`、`play_mode_random.png`、`play_mode_loop.png`、`play_mode_sloop.png`、`play_previous.png`、`play_next.png`、`play_list.png`、`play_music.png`、`pause_music.png`

**录音图标**：`record_default.png`、`record_ing.png`、`record_pause.png`、`record_list.png`、`record_play_playing.png`、`record_play_pause.png`、`fast_forward.png`、`fast_back.png`、`delete_24_24.png`

**设备模式图标**：`ModeSwitcherEntry.png`、`ChatMode.png`、`TranslationMode.png`、`PictureMode.png`、`DetectionMode.png`、`RecordingMode.png`、`P2PMode.png`

**侦测与通话图标**：`det_msg.png`、`det_msg_list.png`、`call_answer.png`、`call_hangup.png`

**其他图标**：`ai_icon.png`、`brightness.png`、`volume.png`、`clock_vol.png`、`point_1.png`、`point_2.png`、`tuya_app_qr.png`、`icon_dog.png`、`ai_camera_on.png`、`ai_camera_off.png`

---

## 源代码结构

### 板级文件

| 文件 | 说明 |
|------|------|
| `tuya_device_board.h` | 板级头文件：PID 定义、初始化函数声明 |
| `tuya_device_board.c` | 板级初始化：GPIO 复用配置、电源按键、运动控制、网络事件订阅 |
| `tuya_device_camera.h` | 摄像头接口：YUV/JPEG/H.264 流控制、运动检测回调 |
| `tuya_device_camera.c` | 摄像头实现：帧处理回调、照片管理 |

### UI 框架文件

| 文件 | 说明 |
|------|------|
| `ui/desktop_app.c` | UI 应用入口：`app_ui_init()` 初始化、`app_ui_msg_handler()` 消息分发 |
| `ui/desk_handle_ui.c/h` | 页面导航框架：19 个页面注册、异步切换、返回栈管理 |
| `ui/desk_event_handle.c/h` | 事件分发中心：手势、按键、AI 消息路由、全局状态管理 |

### UI 页面文件

| 文件 | 说明 |
|------|------|
| `ui/desk_startup.c/h` | 启动引导：启动画面 → 语言选择 → 二维码配网 → 网络配置 |
| `ui/desk_home.c/h` | 首页三屏：表情 GIF 轮播、时钟日期、快捷设置滑块 |
| `ui/desk_chat.c/h` | 聊天页面：消息气泡、流式文本、图片展示、模式标签 |
| `ui/desk_personal.c/h` | 个人中心：功能网格、角色名称、设备模式选择页 |
| `ui/desk_func_music.c/h` | 音乐播放器：主播放页 + 播放列表页 |
| `ui/desk_func_camera.c/h` | 相机：实时预览、AI 相机开关、拍照 |
| `ui/desk_func_photo.c/h` | 相册：照片浏览、删除、AI 分析上传 |
| `ui/desk_func_record.c/h` | 录音：录制、暂停、保存 + 录音列表播放管理 |
| `ui/desk_func_call.c/h` | P2P 通话：呼叫/挂断、状态显示 |
| `ui/desk_func_detection.c/h` | 侦测：告警消息列表、分页、手动触发 |
| `ui/desk_func_settings.c/h` | 设置：网络配置、语言切换 |

### 资源文件

| 文件 | 说明 |
|------|------|
| `ui/res/desk_ui_res.h` | UI 资源定义：所有图标路径、字体声明、表情 GIF 列表、颜色常量 |
| `ui/res/font/*.c` | 预编译字体数据（阿里巴巴普惠体 3，多种字号） |

---

## 参考文档

### 板级文档（`doc/` 目录）

| 文件 | 说明 |
|------|------|
| `Baseboard.pdf` | 底板硬件原理图 |
| `T5-E1.pdf` | T5-E1 模组数据手册 |
| `MIC.pdf` | 麦克风规格文档 |
| `t5_fs.zip` | 预构建文件系统资源包（5.2 MB） |
| `README.md` | SD 卡准备简要说明 |

### 项目文档

| 文件 | 说明 |
|------|------|
| `docs/QUICKSTART.md` | 环境搭建、构建、烧录、运行快速入门 |
| `src/wukong/README_CN.md` | AI 核心模块（Wukong）详细文档 |
| `src/mode/README_CN.md` | 对话模式详细文档 |
| `src/boards/README_CN.md` | 板级支持总览文档 |

### 板级初始化流程

```
tuya_device_board_init()
├── __desktop_io_init()           # GPIO 复用配置（SDIO/I2C/SPI/UART）
├── __desktop_key_init()          # 电源按键初始化
│   ├── GPIO 4 拉高锁电           # 维持系统供电
│   └── GPIO 3 注册关机回调       # 长按 5 秒关机
├── tuya_motion_ctrl_init()       # 运动控制初始化
├── tuya_ai_toy_charge_level_set()# 电池充电电平配置
├── ty_subscribe_event()          # 订阅网络就绪事件（获取可用语言列表）
└── wukong_playback_ctrl_register_storage()  # 注册音乐播放列表存储（可选）
```
