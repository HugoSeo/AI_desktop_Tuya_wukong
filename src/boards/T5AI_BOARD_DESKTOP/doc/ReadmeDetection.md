# 桌面机器人侦测模式（AI Detection Mode）技术文档

## 1. 概述

侦测模式（Detection Mode）是桌面机器人的 AI 安全监控模式。设备持续对摄像头 YUV 帧执行运动检测算法；当检测到运动时，自动拍摄 JPEG 照片上传至 AI 服务端，由云端分析图像内容并通过 TTS 语音播报结果，完成后恢复检测。

**功能特性：**
- 完全自动触发：无需用户干预，运动检测即触发分析
- 冷却机制防止重复触发（默认 10 秒内仅触发一次）
- 超时保护：云端无响应时自动恢复检测
- 支持 UI 按钮手动触发"获取侦测摘要"
- 按键打断：AI 说话/思考时单按可立即中止并恢复检测
- 侦测记录查询：通过 UI 分页浏览过去 24 小时的侦测事件列表

**相关核心文件：**

| 文件 | 说明 |
|------|------|
| `src/mode/wukong_ai_mode_detection.c` | 侦测模式主实现 |
| `src/mode/wukong_ai_mode.h` | 模式管理接口定义 |
| `src/boards/T5AI_BOARD_DESKTOP/ui/desk_func_detection.c` | 侦测记录 UI 屏幕实现 |
| `src/boards/T5AI_BOARD_DESKTOP/ui/desk_func_detection.h` | 侦测 UI 数据结构定义 |
| `include/tuya_app_config.h` | 配置参数定义 |

---

## 2. 编译配置

侦测模式由以下宏控制开关，定义于 `tuya_app_config.h`：

```c
#define ENABLE_AI_MODE_DETECTION  1    // 启用侦测模式
#define ENABLE_TUYA_PICTURE       1    // 启用图片管理模块（AI 返图时需要）
```

---

## 3. 配置参数

以下参数以宏形式定义于 `wukong_ai_mode_detection.c` 文件头部：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `AI_AGENT_SCODE_DETECTION` | `"MOTION_DETECTION"` | 侦测解决方案 Scode |
| `MD_DS_WIDTH` | `ISP宽度 / 3` | 运动检测下采样宽度（像素） |
| `MD_DS_HEIGHT` | `ISP高度 / 3` | 运动检测下采样高度（像素） |
| `MD_Y_THD` | `50` | Y 通道运动检测阈值（低光照环境可调低至 5） |
| `MD_SENSITIVITY` | `3` | 灵敏度（1–10，越大越灵敏） |
| `MD_SKIP_FRAMES` | `50` | 进入模式后跳过的初始帧数（稳定摄像头） |
| `MD_WARMUP_FRAMES` | `10` | 预热帧数（用于更新参考帧，期间不触发检测） |
| `MD_COOLDOWN_MS` | `10000` | 两次运动事件之间的最短间隔（毫秒） |
| `MD_THINK_TIMEOUT_MS` | `10000` | 云端响应超时保护时间（毫秒） |

---

## 4. 状态机

### 4.1 状态定义

侦测模式复用通用聊天状态机枚举（定义于 `wukong_ai_mode.h`）：

```c
typedef enum {
    AI_CHAT_INIT,    // 初始化，模式首次进入
    AI_CHAT_IDLE,    // 空闲，运动检测持续运行中
    AI_CHAT_LISTEN,  // 未使用（侦测模式无此阶段）
    AI_CHAT_UPLOAD,  // 上传，拍照并发送至 AI 服务端
    AI_CHAT_THINK,   // 思考，AI 服务端分析中
    AI_CHAT_SPEAK,   // 播报，TTS 语音播放中
    AI_CHAT_INVALID, // 无效（未初始化）
} AI_CHAT_STATE_E;
```

### 4.2 各状态行为

| 状态 | 摄像头 MD | 说明 |
|------|-----------|------|
| **IDLE** | 运行 | 持续接收 YUV 帧，执行运动检测算法 |
| **UPLOAD** | 停止 | 拍摄 JPEG，设置 Scode，发送图片+文本到 AI |
| **THINK** | 停止 | 等待云端响应，同时运行 10 秒超时保护 |
| **SPEAK** | 停止 | 播放 TTS 音频，展示文字/情绪/图片 |

### 4.3 状态转换图

```
             ┌─────────────────────────────────────┐
             │             AI_CHAT_INIT             │
             └──────────────────┬──────────────────┘
                                │ 初始化完成
                                ▼
             ┌─────────────────────────────────────┐
     ┌──────►│            AI_CHAT_IDLE              │◄──────────────────────┐
     │       └──────────────────┬──────────────────┘                       │
     │                          │ 运动检测触发 / UI 按钮触发                │
     │                          ▼                                          │
     │       ┌─────────────────────────────────────┐                      │
     │       │           AI_CHAT_UPLOAD             │                      │
     │       └──────────────────┬──────────────────┘                      │
     │ 拍照失败 / 上传失败        │ 图片+文本发送完成                        │
     ├───────────────────────── ▼                                          │
     │       ┌─────────────────────────────────────┐                      │
     │       │            AI_CHAT_THINK             │──── 10s 超时 ────────►│
     │       └──────────────────┬──────────────────┘                      │
     │                          │ TTS_PRE 事件                             │
     │                          ▼                                          │
     │       ┌─────────────────────────────────────┐                      │
     │       │            AI_CHAT_SPEAK             │                      │
     │       └──────────────────┬──────────────────┘                      │
     │                          │ 播放结束 / AI 错误                        │
     └──────────────────────────┴──────────────────────────────────────────┘
```

### 4.4 状态转换事件映射

事件由 `wukong_ai_detection_event_cb()` 处理（`wukong_ai_mode_detection.c:496`）：

| 触发事件 | 当前状态 | 目标状态 | 备注 |
|---------|---------|---------|------|
| 运动检测触发 / UI 按钮 | IDLE | UPLOAD | 由 `wukong_ai_detection_task_cb()` 执行 |
| 拍照或发送失败 | UPLOAD | IDLE | `__md_resume_detection()` |
| 上传完成 | UPLOAD | THINK | 启动超时计时器 |
| `TTS_PRE` | THINK | SPEAK | 清除超时计时器 |
| `PLAY_END` / `PLAY_CTL_END` | SPEAK | IDLE | `__md_resume_detection()` |
| 超时（10s 无 `TTS_PRE`） | THINK | IDLE | `__md_resume_detection()` |
| `ASR_ERROR` / `TTS_ERROR` / `TTS_ABORT` | 非 IDLE | IDLE | `__md_resume_detection()` |
| 按键打断（`NORMAL_KEY`） | THINK / SPEAK | IDLE | 停止播放 + 中断会话 |

---

## 5. 触发方式

### 5.1 运动检测自动触发（主要方式）

```
摄像头驱动 → YUV 帧回调 __on_yuv_frame()
    │
    ├─ 跳过前 MD_SKIP_FRAMES(50) 帧（稳定摄像头）
    ├─ 预热帧 MD_WARMUP_FRAMES(10) 帧（更新参考帧，不触发）
    │
    ├─ __extract_y_downsample()
    │    YUV422(UYVY) → 下采样 → Y 通道（MD_DS_WIDTH × MD_DS_HEIGHT）
    │
    ├─ tuya_ipc_motion(y_buf, &motion_flag, &motion_point)
    │
    └─ motion_flag == 1 && 距上次触发 >= MD_COOLDOWN_MS(10s)
         ↓
         __on_motion_detected()
           s_md_photo_pending = TRUE
           tuya_device_camera_md_stop()    // 停止 YUV 流，防止并发触发
```

实际拍照和上传操作**不在**摄像头回调中执行（回调上下文不可阻塞），而是延迟到 `wukong_ai_detection_task_cb()` 周期任务中处理。

### 5.2 UI 按钮手动触发

侦测记录屏幕的"AI 检测"按钮调用 `__on_get_detection_msg()`（`wukong_ai_mode_detection.c:56`）：

```c
__on_get_detection_msg()
    ├─ 若当前在 THINK/SPEAK：停止播放 + 中断会话
    ├─ tuya_device_camera_md_stop()
    ├─ 状态切换至 THINK
    ├─ set_scode("MOTION_DETECTION")
    ├─ tuya_ai_input_start(TRUE)
    ├─ wukong_ai_agent_send_text("summary")  // 请求摘要，不上传图片
    └─ tuya_ai_input_stop()
```

该触发方式**不拍照**，仅发送文本 `"summary"` 请求云端生成侦测摘要。

### 5.3 按键打断

回调函数：`wukong_ai_detection_key_cb()`（`wukong_ai_mode_detection.c:392`）

| 按键事件 | 当前状态 | 行为 |
|---------|---------|------|
| `NORMAL_KEY`（单按） | THINK / SPEAK | 停止播放 + 发送 `CHAT_BREAK` + 恢复检测 |
| `NORMAL_KEY`（单按） | IDLE | 切换至聊天模式（`AI_DEVICE_MODE_CHAT`） |

---

## 6. 数据流程

### 6.1 运动检测完整请求-响应流程

```
摄像头硬件           设备 App                  AI 服务端
   │                    │                         │
   │ YUV 帧流          │                         │
   │───────────────────►│                         │
   │                    │ __on_yuv_frame()         │
   │                    │ [运动检测算法]           │
   │                    │ s_md_photo_pending=TRUE  │
   │                    │ camera_md_stop()         │
   │                    │                         │
   │                    │ [task_cb 执行拍照]       │
   │                    │ 状态: IDLE → UPLOAD      │
   │                    │                         │
   │ MJPEG 首帧        │                         │
   │───────────────────►│                         │
   │                    │ __md_capture_jpeg()      │
   │                    │ 状态: UPLOAD → THINK     │
   │                    │                         │
   │                    │─── set_scode + 图片 + 文本 ──────────────────►│
   │                    │                         │ [AI 分析图像]       │
   │                    │◄────────────────── TTS_PRE ─────────────────│
   │                    │ 状态: THINK → SPEAK      │                   │
   │                    │◄────────── TEXT_STREAM（分析结果文字流）      │
   │◄──── TTS 语音播报  │                         │                   │
   │                    │◄────────── PLAY_END ────────────────────────│
   │                    │ 状态: SPEAK → IDLE       │                   │
   │                    │ camera_md_start()        │                   │
   │ YUV 帧流（恢复）   │                         │
   │───────────────────►│                         │
```

### 6.2 JPEG 拍照捕获机制

侦测模式使用独立的 MJPEG 一次性捕获接口，不涉及 UI 摄像头页面：

```
__md_capture_jpeg(timeout_ms=5000)
    │
    ├─ tal_semaphore_create_init(s_capture_sem, 0, 1)
    ├─ tuya_device_camera_md_jpeg_start(__on_mjpeg_captured)
    │
    │  [等待信号量，最多 5000ms]
    │
    ├─ 回调 __on_mjpeg_captured(data, len):
    │    └─ 仅接收第一帧，复制到 s_capture_buf，post 信号量
    │
    ├─ tuya_device_camera_md_jpeg_stop()
    ├─ tal_semaphore_release(s_capture_sem)
    │
    └─ 返回 s_capture_buf / s_capture_len（调用方负责 tal_free）
```

### 6.3 AI Agent 接口调用序列

```c
// 1. 标识当前使用的 AI 能力
tuya_ai_agent_set_scode("MOTION_DETECTION");

// 2. 开启输入会话（TRUE = 强制重置）
tuya_ai_input_start(TRUE);

// 3. 发送捕获的图片
wukong_ai_agent_send_image(image_data, image_size);
tal_free(image_data);            // 发送后立即释放

// 4. 发送文本提示词
wukong_ai_agent_send_text("帮我检测下这张图片");

// 5. 结束输入，触发 AI 推理
tuya_ai_input_stop();

// 6. 通过事件回调接收响应
//    WUKONG_AI_EVENT_TTS_PRE       → 进入 SPEAK 状态，清除超时
//    WUKONG_AI_EVENT_TEXT_STREAM_* → 流式显示分析文字
//    WUKONG_AI_EVENT_EMOTION       → 显示情绪表情
//    WUKONG_AI_EVENT_ACCEPT_PICTURE → 云端返图（可选）
//    WUKONG_AI_EVENT_PLAY_END      → 恢复运动检测
```

### 6.4 超时保护机制

```
UPLOAD → THINK 时：
    s_md_think_start_ms = tal_system_get_millisecond()

每次 task_cb 执行时检查：
    elapsed = now - s_md_think_start_ms
    if (elapsed >= MD_THINK_TIMEOUT_MS):
        s_md_think_start_ms = 0
        __md_resume_detection()   // 直接恢复 IDLE

收到 TTS_PRE 时：
    s_md_think_start_ms = 0       // 取消超时计时
```

---

## 7. 显示与 UI 集成

侦测模式通过 `tuya_ai_display_msg()` 向 UI 层发送展示事件（需开启 `ENABLE_TUYA_UI`）：

| 时机 | 显示类型 | 说明 |
|------|---------|------|
| AI 文字流开始 | `TY_DISPLAY_TP_AI_CHAT_START` | 开始显示 AI 分析结果 |
| AI 文字流数据 | `TY_DISPLAY_TP_AI_CHAT_DATA` | 流式追加显示 |
| AI 文字流结束 | `TY_DISPLAY_TP_AI_CHAT_STOP` | 文字流结束 |
| 情绪表情 | `TY_DISPLAY_TP_EMOJI` | 显示情绪动画 |
| **AI 返图完成** | **`TY_DISPLAY_TP_AI_IMAGE`** | **显示 AI 生成的图片（文件名）** |
| 图片传输完成 | `TY_DISPLAY_TP_CLEAR_ATTACHMENT` | 清除附件标识 |

---

## 8. 侦测记录 UI 屏幕（`DHUI_SCREEN_ID_DETECTION`）

侦测记录屏幕（`desk_func_detection.c`）提供历史侦测事件的分页浏览能力。

### 8.1 屏幕布局

| 区域 | 尺寸 | 说明 |
|------|------|------|
| 标题栏 | 320 × 50 px | 返回按钮、标题"侦测记录"、分页下拉菜单、AI 检测按钮 |
| 内容区 | 320 × 190 px | 垂直滚动的侦测消息列表 |
| 消息条目 | 290 × 50 px | msgTitle（标题）+ dateTime（时间）+ attachPics（图片 URL） |

### 8.2 消息列表查询

| 参数 | 值 |
|------|------|
| API 名称 | `thing.ipc.ai.robot.msg.list` |
| API 版本 | `1.0` |
| 查询时间范围 | 过去 24 小时（`3600 * 24` 秒） |
| 每页条目数 | `10` |
| 最大页数 | `20` |
| 返回字段 | `msgTitle`、`dateTime`、`attachPics`、`totalCount` |

### 8.3 UI 交互事件

| 控件 | 事件 | 行为 |
|------|------|------|
| 返回按钮 | CLICKED | 返回上一屏 |
| 列表按钮 | CLICKED | 切换分页下拉菜单显示/隐藏 |
| 分页下拉菜单 | VALUE_CHANGED | 查询新页码消息列表 |
| AI 检测按钮 | CLICKED | 调用 `__on_get_detection_msg()`（手动触发摘要请求） |
| 消息条目 | CLICKED | 打印 attachPics URL（调试） |

---

## 9. 模式注册与初始化

侦测模式通过统一的模式管理框架注册（`wukong_ai_mode.c`）：

```c
OPERATE_RET ai_detection_register(AI_CHAT_MODE_HANDLE_T **cb)
{
    s_ai_detection_cb.on_init        = wukong_ai_detection_init_cb;
    s_ai_detection_cb.on_deinit      = wukong_ai_detection_deinit_cb;
    s_ai_detection_cb.on_key         = wukong_ai_detection_key_cb;
    s_ai_detection_cb.on_task        = wukong_ai_detection_task_cb;
    s_ai_detection_cb.on_event       = wukong_ai_detection_event_cb;
    s_ai_detection_cb.on_wakeup      = wukong_ai_detection_wakeup_cb;  // 空实现
    s_ai_detection_cb.on_vad         = wukong_ai_detection_vad_cb;     // 空实现
    s_ai_detection_cb.on_client      = wukong_ai_detection_client_cb;  // 空实现
    s_ai_detection_cb.on_notify_idle = wukong_ai_detection_notify_idle_cb;
    s_ai_detection_cb.on_audio_input = NULL;
    *cb = &s_ai_detection_cb;
    return OPRT_OK;
}
```

初始化时（`on_init`）会：
1. 禁用 KWS（侦测模式不需要关键词唤醒）
2. 设置 VAD 为手动模式，关闭唤醒
3. 初始化运动检测算法（`__md_algo_init()`）
4. 启动 YUV 帧回调（`tuya_device_camera_md_start()`）
5. 切换至 IDLE 状态

反初始化时（`on_deinit`）会：
1. 停止摄像头 MD 流
2. 若正在 THINK/SPEAK，停止播放并发送 `CHAT_BREAK`
3. 重置所有运动检测状态变量

---

## 10. UI 流程详解（T5AI_BOARD_DESKTOP）

### 10.1 消息分发架构

与生图模式相同，侦测模式的所有 UI 消息均通过 `tuya_ai_display_msg()` 进入显示队列，在 LVGL 主任务上下文中分发至 `app_ui_msg_handler()`，再路由到具体处理函数。详见 `ReadmePicture.md § 11.1`。

### 10.2 运动触发时的 UI 流程

```
运动触发（__on_motion_detected）
    │ [无 UI 动作，摄像头停止]
    ▼
task_cb 执行拍照（后台静默，无 UI 反馈）
    │
    ▼
云端响应 → TEXT_STREAM → 在 CHAT 屏聊天列表中流式显示分析结果气泡
    │
    ├─ 若当前不在 CHAT 屏：desk_handle_ui_switch_to(DHUI_SCREEN_ID_CHAT)
    └─ 若已在 CHAT 屏：直接渲染 AI 文字气泡
```

侦测模式触发时**不显示用户消息气泡**（无 `TY_DISPLAY_TP_HUMAN_CHAT` 发送），AI 分析结果直接以 AI 气泡形式呈现在聊天列表中。

### 10.3 UI 按钮手动触发的流程差异

| 对比项 | 运动检测自动触发 | UI 按钮手动触发 |
|--------|----------------|----------------|
| 是否上传图片 | 是（JPEG 截图） | 否（仅发送文本 `"summary"`） |
| 状态路径 | IDLE → UPLOAD → THINK → SPEAK → IDLE | IDLE → THINK → SPEAK → IDLE |
| 超时保护 | 有 | 有 |
| 用户消息气泡 | 无 | 无 |

### 10.4 AI 返图显示流程

若 AI 服务端针对检测图像生成了图片响应，则与生图模式相同：

```
WUKONG_AI_EVENT_ACCEPT_PICTURE → __md_image(name)
    → tuya_ai_display_msg(name, len, TY_DISPLAY_TP_AI_IMAGE)
    → receive_ai_picture_data()
    → wukong_picture_get_by_name() → set_picture_message()
    → disp_picture_message()     // 在聊天列表中添加"查看图片"链接
```

---

## 11. 调试日志

```
# 状态切换日志（DEBUG 级别）
[ai_detection] init
[ai_detection] detection resumed
[ai_detection] cloud response timeout (10023 ms), resuming detection

# 运动检测与拍照日志（INFO/DEBUG 级别）
[ai_detection] motion! flag=1 point=(120,80)
[ai_detection] photo captured, size=48576, sending to cloud

# 事件处理日志（DEBUG 级别）
[ai_detection] event type: 5, state: 3      // TTS_PRE，状态切换
[ai_detection] playback ended, resuming detection
[ai_detection] stale TTS_PRE ignored, state=4

# 算法初始化日志（DEBUG 级别）
[ai_detection] MD algo init ok, 160x160
```

---

## 12. 常见问题

| 现象 | 可能原因 | 排查方向 |
|------|---------|---------|
| 侦测无响应 | 相机未就绪 | 确认 `tuya_device_camera_md_start()` 返回值 |
| 误触发频繁 | 灵敏度过高或阈值过低 | 调大 `MD_SENSITIVITY` 或 `MD_Y_THD` |
| 冷却期间不触发 | 符合预期 | `MD_COOLDOWN_MS` 控制最短间隔，默认 10 秒 |
| 拍照超时（OPRT_TIMEOUT） | MJPEG 流未就绪 | 检查摄像头硬件初始化是否完成 |
| 云端超时后不恢复 | 超时保护未启动 | 确认 `s_md_think_start_ms` 在 UPLOAD→THINK 时被赋值 |
| 侦测记录列表为空 | 云端无数据 / API 权限问题 | 检查 `thing.ipc.ai.robot.msg.list` 返回的 `totalCount` |
| AI 分析结果不显示 | `ENABLE_TUYA_UI` 未定义 | 确认编译宏已开启 |
| 切换至聊天模式失败 | 单按时状态非 IDLE | 此时单按会先打断 AI，需再次单按才能切换 |
