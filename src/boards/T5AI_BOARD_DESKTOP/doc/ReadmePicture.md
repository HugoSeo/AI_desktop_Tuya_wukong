# 桌面机器人生图模式（AI Picture Mode）技术文档

## 1. 概述

生图模式（Picture Mode）是桌面机器人的核心 AI 交互模式之一，允许用户通过自然语言描述触发 AI 文生图（Text-to-Image）能力，将生成的图片保存至本地相册并实时显示在屏幕上。

**功能特性：**
- 支持按键一键唤醒和语音关键词唤醒两种触发方式
- 支持 VAD（语音活动检测）自动判断语音结束
- 支持将已有图片加入队列随生图请求一并上传（图生图/编辑图片）
- 生成的图片以 JPEG 格式分块流式接收，累积完成后存入本地相册
- 生图过程中提供实时 TTS 语音反馈和流式文字显示

**相关核心文件：**

| 文件 | 说明 |
|------|------|
| `src/mode/wukong_ai_mode_picture.c` | 生图模式主实现 |
| `src/mode/wukong_ai_mode.h` | 模式管理接口定义 |
| `src/wukong/picture/wukong_picture.h/.c` | 图片相册管理 |
| `src/wukong/picture/wukong_picture_input.h/.c` | 图片输入队列 |
| `src/wukong/picture/wukong_picture_output.h/.c` | AI 生成图片接收与保存 |
| `include/tuya_app_config.h` | 配置参数定义 |

---

## 2. 编译配置

生图模式由以下宏控制开关，定义于 `tuya_app_config.h`：

```c
#define ENABLE_AI_MODE_PICTURE         1    // 启用生图模式
#define ENABLE_TUYA_PICTURE            1    // 启用图片管理模块
#define ENABLE_IMAGE_ALBUM             1    // 启用本地图片相册
#define ENABLE_IMAGE_ALBUM_STORAGE_MEM 1    // 使用内存存储（可改为 SD 卡）
```

---

## 3. 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `AI_AGENT_SCODE_PICTURE` | `"VOICE_TO_IMAGE"` | 生图解决方案 Scode，用于标识 AI 服务端调用的能力 |
| `TUYA_PICTURE_ALBUM_NAME` | `"ai_picture"` | 本地相册名称 |
| `TUYA_PICTURE_ALBUM_MAX_IMAGE_CNT` | `10` | 相册最多保留图片数量（超出时自动淘汰最旧的） |
| `TUYA_PICTURE_DEF_OUTPUT_WIDTH` | `320` | 生成图片默认宽度（像素） |
| `TUYA_PICTURE_DEF_OUTPUT_HEIGHT` | `240` | 生成图片默认高度（像素） |
| `WUKONG_PICTURE_INPUT_MAX_NUM` | `3` | 输入图片队列最大长度 |
| `WUKONG_PICTURE_OUTPUT_MAX_NUM` | `12` | 同时处理的输出流最大数量 |
| `WUKONG_PICTURE_NAME_MAX_LEN` | `64` | 图片文件名最大长度（字节） |

---

## 4. 状态机

### 4.1 状态定义

生图模式复用了通用聊天状态机枚举（定义于 `wukong_ai_mode.h`）：

```c
typedef enum {
    AI_CHAT_INIT,    // 初始化，模式首次进入
    AI_CHAT_IDLE,    // 空闲，等待用户输入
    AI_CHAT_LISTEN,  // 监听，等待用户语音
    AI_CHAT_UPLOAD,  // 上传，VAD 检测到语音结束，停止输入
    AI_CHAT_THINK,   // 思考，AI 服务端处理中
    AI_CHAT_SPEAK,   // 播报，TTS 语音播放中
    AI_CHAT_INVALID, // 无效（未初始化）
} AI_CHAT_STATE_E;
```

### 4.2 各状态行为

| 状态 | LED 行为 | 其他操作 |
|------|----------|---------|
| **IDLE** | 关闭 | 关闭空闲定时器、打开低功耗定时器、禁用唤醒 |
| **LISTEN** | 500ms 闪烁 | 打开空闲定时器、关闭低功耗定时器、**启用唤醒** |
| **UPLOAD** | 不变 | 禁用唤醒、调用 `tuya_ai_input_stop()` |
| **THINK** | 2000ms 缓闪 | 打开空闲定时器、禁用唤醒 |
| **SPEAK** | 常亮 | 关闭空闲定时器（防止播放期间超时退出） |

### 4.3 状态转换图

```
             ┌─────────────────────────────────────┐
             │             AI_CHAT_INIT             │
             └──────────────────┬──────────────────┘
                                │ 初始化完成
                                ▼
             ┌─────────────────────────────────────┐
     ┌──────►│            AI_CHAT_IDLE              │◄──────────────────┐
     │       └──────────────────┬──────────────────┘                   │
     │                          │ 按键 / KWS 唤醒 / 空闲超时重置       │
     │                          ▼                                       │
     │       ┌─────────────────────────────────────┐                   │
     │       │           AI_CHAT_LISTEN             │                   │
     │       └──────────────────┬──────────────────┘                   │
     │                          │ VAD_STOP / 长按释放                   │
     │ ASR错误/空               ▼                                       │
     │       ┌─────────────────────────────────────┐                   │
     └───────┤           AI_CHAT_UPLOAD             │                   │
             └──────────────────┬──────────────────┘                   │
                                │ ASR_OK                                │
                                ▼                                       │
             ┌─────────────────────────────────────┐                   │
             │            AI_CHAT_THINK             │                   │
             └──────────────────┬──────────────────┘                   │
                                │ TTS_PRE 事件                          │
                                ▼                                       │
             ┌─────────────────────────────────────┐                   │
             │            AI_CHAT_SPEAK             │                   │
             └──────────────────┬──────────────────┘                   │
                                │ 播放结束 / 收到图片完成事件            │
                                └───────────────────────────────────────┘
```

### 4.4 状态转换事件映射

事件由 `wukong_ai_picture_event_cb()` 处理（`wukong_ai_mode_picture.c:368`）：

| 触发事件 | 当前状态 | 目标状态 |
|---------|---------|---------|
| `ASR_EMPTY` / `ASR_ERROR` | UPLOAD | IDLE |
| `ASR_OK` | UPLOAD | THINK |
| `TTS_PRE` | THINK | SPEAK |
| `PLAY_END`（TTS 已说完，无图片待显示） | SPEAK | IDLE |
| `PLAY_END`（TTS 已说完，图片显示中） | SPEAK | LISTEN |
| `ACCEPT_PICTURE`（图片保存完成） | - | IDLE |
| `SEND_PICTURE_END`（输入图片发送完） | - | IDLE |

---

## 5. 触发方式

### 5.1 按键触发

回调函数：`wukong_ai_picture_key_cb()`（`wukong_ai_mode_picture.c:614`）

| 按键事件 | 行为 |
|---------|------|
| `NORMAL_KEY`（单按） | 重置状态，切换至 LISTEN |
| `LONG_KEY`（长按） | 启动监听 |
| `RELEASE_KEY`（长按释放） | 若 `wakeup_stat=TRUE`，切换至 UPLOAD，触发上传 |

### 5.2 语音关键词唤醒（KWS）

生图模式在 LISTEN 状态下会启用 KWS（Keyword Spotting），检测到唤醒词后由 `wukong_ai_picture_wakeup()` 处理，自动切换至 LISTEN 状态重新开始监听。

### 5.3 VAD 自动检测

回调函数：`wukong_ai_picture_vad()`（`wukong_ai_mode_picture.c:555`）

```c
WUKONG_AUDIO_VAD_START:
    tuya_ai_agent_set_scode("VOICE_TO_IMAGE")
    tuya_ai_input_start(FALSE)
    // 若队列中有待发图片，立即发送
    if (wukong_picture_input_get_num() > 0) {
        wukong_picture_input_from_album()
    }
    tuya_ai_toy_idle_timer_ctrl(FALSE)  // 暂停空闲超时

WUKONG_AUDIO_VAD_STOP:
    tuya_ai_input_stop()
    tuya_ai_toy_idle_timer_ctrl(TRUE)   // 恢复空闲超时
```

---

## 6. 数据流程

### 6.1 完整请求-响应流程

```
用户           设备 App                  AI 服务端           本地相册
 │                │                         │                   │
 │ 说出生图描述   │                         │                   │
 │───────────────►│                         │                   │
 │                │ VAD_START               │                   │
 │                │ set_scode("VOICE_TO_IMAGE")                  │
 │                │ input_start()           │                   │
 │                │ [发送队列中的图片]       │                   │
 │                │                         │                   │
 │ 语音结束       │                         │                   │
 │───────────────►│                         │                   │
 │                │ VAD_STOP                │                   │
 │                │ input_stop()            │                   │
 │                │────────── ASR + 生图请求 ──────────────────►│
 │                │                         │                   │
 │                │◄────── TEXT_STREAM（AI 描述文字流）          │
 │                │◄────── TTS_PRE（TTS 即将开始）              │
 │                │ [切换至 SPEAK 状态]     │                   │
 │◄──── TTS 语音播报                        │                   │
 │                │                         │                   │
 │                │◄───── IMAGE chunk 1     │                   │
 │                │◄───── IMAGE chunk 2     │                   │
 │                │◄───── IMAGE chunk N     │                   │
 │                │ [累积完成]              │                   │
 │                │──────────────────── 保存图片 ──────────────►│
 │                │◄─────────── ACCEPT_PICTURE(filename)        │
 │                │ [显示图片]              │                   │
 │◄──── 图片显示  │                         │                   │
 │                │ [切换至 IDLE]           │                   │
```

### 6.2 图片输出累积机制

AI 返回的图片为 JPEG 分块流，由 `wukong_picture_output_save_to_album()` 累积处理（`wukong_picture_output.c`）：

```
第 1 块到达 → 分配 total_len 字节缓冲区，is_start=true，offset=0
第 N 块到达 → 校验 total_len 一致性，追加写入 acc_buf[offset]
最后一块    → offset >= total_len，调用 wukong_picture_save_to_album()
              文件名格式：wukong_pic_{timestamp}
              发送事件：WUKONG_AI_EVENT_ACCEPT_PICTURE
              释放累积缓冲区
```

### 6.3 图片输入队列机制（图生图）

支持在生图请求中附带已有图片（用于编辑、参考等场景）：

```c
// 添加图片到待发队列（不立即读取，仅锁定引用）
wukong_picture_input_add_from_album(char *filename, char *text);

// VAD_START 时批量发送（逐张读取→发送→释放，节省内存峰值）
wukong_picture_input_from_album();

// 查询队列中待发图片数
wukong_picture_input_get_num();
```

---

## 7. 显示与 UI 集成

生图模式通过 `tuya_ai_display_msg()` 向 UI 层发送展示事件（需开启 `ENABLE_TUYA_UI`）：

| 时机 | 显示类型 | 说明 |
|------|---------|------|
| ASR 识别完成 | `TY_DISPLAY_TP_HUMAN_CHAT` | 显示用户说的文字 |
| AI 文字流开始 | `TY_DISPLAY_TP_AI_CHAT_START` | 开始显示 AI 回复 |
| AI 文字流数据 | `TY_DISPLAY_TP_AI_CHAT_DATA` | 流式追加显示 |
| AI 文字流结束 | `TY_DISPLAY_TP_AI_CHAT_STOP` | 文字流结束 |
| 情绪表情 | `TY_DISPLAY_TP_EMOJI` | 显示情绪动画 |
| **图片生成完成** | **`TY_DISPLAY_TP_AI_IMAGE`** | **显示生成的图片（文件名）** |
| 清空附件 | `TY_DISPLAY_TP_CLEAR_ATTACHMENT` | 清除图片区域 |

图片显示入口函数（`wukong_ai_mode_picture.c:104`）：

```c
STATIC VOID __ai_picture_image(char *name)
{
    PR_NOTICE("[pic_chain] picture mode recv ACCEPT_PICTURE, name:%s", name);
#ifdef ENABLE_TUYA_UI
    tuya_ai_display_msg(name, strlen(name), TY_DISPLAY_TP_AI_IMAGE);
#endif
}
```

---

## 8. AI Agent 接口调用序列

```c
// 1. 标识当前使用的 AI 能力
tuya_ai_agent_set_scode("VOICE_TO_IMAGE");

// 2. 开启输入会话
tuya_ai_input_start(FALSE);   // FALSE = 普通启动（非强制重置）

// 3a. 可选：发送参考图片（图生图场景）
tuya_ai_image_input(timestamp, image_data, image_len, image_len);

// 3b. 可选：发送图片对应的文字说明
tuya_ai_text_input(text_data, text_len, text_len);

// 4. 结束输入，触发 AI 推理
tuya_ai_input_stop();

// 5. 通过事件回调接收响应
//    WUKONG_AI_EVENT_ASR_OK        → 语音识别成功，进入 THINK 状态
//    WUKONG_AI_EVENT_TTS_PRE       → TTS 即将开始，进入 SPEAK 状态
//    WUKONG_AI_EVENT_TEXT_STREAM_* → 流式文字响应
//    WUKONG_AI_EVENT_ACCEPT_PICTURE → 图片生成并保存完成
```

图片输出尺寸在 AI Client 启动时通过事件系统配置（`wukong_picture_output.c: __set_output_picture_size_cb()`）：

```json
{
    "sys.device.img_resize.width": 320,
    "sys.device.img_resize.height": 240
}
```

---

## 9. 模式注册与初始化

生图模式通过统一的模式管理框架注册（`wukong_ai_mode.c`）：

```c
OPERATE_RET ai_picture_register(AI_CHAT_MODE_HANDLE_T **cb)
{
    s_ai_picture_cb.on_init        = wukong_ai_picture_int_cb;
    s_ai_picture_cb.on_deinit      = wukong_ai_picture_deint_cb;
    s_ai_picture_cb.on_key         = wukong_ai_picture_key_cb;
    s_ai_picture_cb.on_task        = wukong_ai_picture_task_cb;
    s_ai_picture_cb.on_event       = wukong_ai_picture_event_cb;
    s_ai_picture_cb.on_wakeup      = wukong_ai_picture_wakeup;
    s_ai_picture_cb.on_vad         = wukong_ai_picture_vad;
    s_ai_picture_cb.on_client      = wukong_ai_picture_client_run;
    s_ai_picture_cb.on_notify_idle = wukong_ai_picture_notify_idle_cb;
    *cb = &s_ai_picture_cb;
    return OPRT_OK;
}
```

初始化时（`on_init`）会：
1. 将 VAD 配置为自动模式
2. 使能 KWS（关键词识别）
3. 切换至 IDLE 状态

---

## 10. 调试日志

```
# 状态切换日志（DEBUG 级别）
[====ai_picture] idle
[====ai_picture] listen
[====ai_picture] upload
[====ai_picture] think
[====ai_picture] speak

# 图片链路日志（NOTICE 级别）
[pic_chain] start accumulating, total_len:xxxxx
[pic_chain] chunk accumulated, offset:xxxx/xxxxx
[pic_chain] all chunks received, total:xxxxx, saving to album
[pic_chain] album saved, filename:wukong_pic_1714012345, size:xxxxx
[pic_chain] picture mode recv ACCEPT_PICTURE, name:wukong_pic_1714012345
```

---

## 11. UI 流程详解（T5AI_BOARD_DESKTOP）

本节描述在 `T5AI_BOARD_DESKTOP` 板型下，生图模式从状态机事件到屏幕渲染的完整 UI 链路（相关文件：`src/miscs/gui/display/tuya_ai_display.c`、`src/boards/T5AI_BOARD_DESKTOP/ui/`）。

### 11.1 消息分发架构

```
模式层（wukong_ai_mode_picture.c）
    │  tuya_ai_display_msg(data, len, type)
    ▼
tuya_ai_display.c
    │  枚入链表 ai_txt_list_head（mutex 保护，线程安全）
    │  tuya_disp_gui_event_send(LLV_EVENT_USER_PRIVATE)
    ▼
LVGL 主任务（持有 lv_vendor_disp_lock）
    │  app_ui_msg_handler(TY_DISPLAY_MSG_T *)    ← desktop_app.c
    ▼
各业务处理函数（desk_event_handle.c / desk_chat.c）
```

消息在 `tuya_ai_text_msg_notify()` 中入队，统一在 LVGL 主任务上下文中处理，避免多线程并发渲染问题。`EVENT_GUI_READY_NOTIFY` 事件触发积压消息的补发。

### 11.2 `app_ui_msg_handler()` 消息路由

| 消息类型 | 处理函数 |
|---------|---------|
| `TY_DISPLAY_TP_HUMAN_CHAT` | `receive_ai_message_data()` |
| `TY_DISPLAY_TP_AI_CHAT_START/DATA/STOP` | `receive_ai_message_data()` |
| `TY_DISPLAY_TP_EMOJI` | `receive_emotional_feedback()` |
| `TY_DISPLAY_TP_AI_IMAGE` | `receive_ai_picture_data()` |
| `TY_DISPLAY_TP_CLEAR_ATTACHMENT` | `desk_chat_refresh_stat_label(GUI_STAT_IDLE)` |
| `TY_DISPLAY_TP_CHAT_STAT` | `desk_chat_refresh_stat_label(data[0])` |

### 11.3 聊天文本气泡渲染流程

**ASR 识别完成（`TY_DISPLAY_TP_HUMAN_CHAT`）：**

```
receive_ai_message_data()
  1. 隐藏 picture_spinner（若有残留）
  2. 将识别文本写入 asr_txt 缓冲区
  3. 若当前屏幕已是 CHAT 屏：
       set_chat_message() → 创建用户消息气泡（右对齐）
  4. 若不在 CHAT 屏：
       desk_handle_ui_switch_to(DHUI_SCREEN_ID_CHAT)
       → 屏幕就绪后由 desk_chat_session_resume_on_chat_ready() 补发
  5. 若当前为生图模式（AI_DEVICE_MODE_PICTURE）：
       desk_chat_picture_spinner_show()  ← 显示等待 Spinner
```

**AI 文字流（`TY_DISPLAY_TP_AI_CHAT_START/DATA/STOP`）：**

```
AI_CHAT_START:
  desk_chat_picture_spinner_hide()   ← 收到 AI 回复，隐藏 Spinner
  重置会话状态，创建 AI 消息气泡（左对齐）
  lv_label_set_text() 设置初始文本

AI_CHAT_DATA:
  追加文本到 tts_txt 缓冲区（翻译模式为覆盖）
  lv_label_set_text() 更新气泡内容（流式刷新）
  lv_obj_scroll_to_view() 滚动到最新消息

AI_CHAT_STOP:
  渲染最终完整文本
  disp_picture_message()  ← 若此时图片已就绪则立即显示
  重置 ai_stream_active 标志
```

### 11.4 AI 生图结果显示流程

```
tuya_ai_display_msg(filename, len, TY_DISPLAY_TP_AI_IMAGE)
    ↓
receive_ai_picture_data(pic_name)          [desk_event_handle.c:934]
    │
    ├─ desk_chat_picture_spinner_hide()    // 隐藏等待 Spinner
    │
    ├─ wukong_picture_get_by_name()        // 从相册读取 JPEG 原始数据
    │
    ├─ set_picture_message(data, len)      // 将数据写入临时文件
    │    AI_CHAT_JPEG_MSG_PATH             // 全屏参数：320×240，旋转=0
    │    is_need_show = true
    │
    ├─ 若当前不在 CHAT 屏：
    │    desk_handle_ui_switch_to(DHUI_SCREEN_ID_CHAT)
    │    → 屏幕就绪回调中调用 disp_picture_message()
    │
    └─ 若已在 CHAT 屏：
         disp_picture_message()            // 立即渲染
```

**`disp_picture_message()` 渲染细节：**

```
disp_picture_message()                     [desk_chat.c:540]
  1. 检查 is_need_show 标志，否则直接返回
  2. 删除旧的 picture_canvas（如有）
  3. jpg_img_unload() 释放旧 JPEG 解码数据
  4. jpg_img_load_with_scale() 解码 JPEG（scale 参数决定尺寸）
  5. lv_img_create(picture_cont) 创建图片组件
     lv_img_set_src() / lv_img_set_angle() 设置源和旋转
     lv_obj_set_size() 设置显示区域
     注册 return_chat_content_event_cb 点击返回事件
  6. 保持 picture_cont 隐藏（图片容器），显示 msg_container（消息列表）
  7. create_chat_message() 在消息列表添加 "查看图片" 链接
     注册 view_photo_event_cb：点击切换至图片全屏视图
  8. lv_obj_scroll_to_view() 滚动到最新条目
  9. 重置 is_need_show = false
```

**图片全屏交互：**

| 操作 | 行为 |
|------|------|
| 点击 "查看图片" 文字链接 | 隐藏 msg_container，显示 picture_cont（全屏图片） |
| 点击图片 | 隐藏 picture_cont，显示 msg_container（返回消息列表） |

### 11.5 等待 Spinner 生命周期

Spinner 控件（`lv_chat_ui_t.picture_spinner`）用于在生图等待期间给用户以视觉反馈：

```
用户语音结束 → HUMAN_CHAT 消息 → 生图模式检测 → spinner_show()
                                                        ↓
                                          等待 AI 处理中（THINK 状态）
                                                        ↓
                  ┌─────────────────────────────────────┤
                  │                                     │
            收到文字流开始                          收到图片结果
         (AI_CHAT_START)                          (AI_IMAGE)
                  │                                     │
            spinner_hide()                       spinner_hide()
```

### 11.6 状态标签同步

每次状态机发生切换，生图模式通过 `TY_DISPLAY_TP_CHAT_STAT` 消息携带新状态值，驱动聊天屏幕标题栏的状态标签（`desk_chat_refresh_stat_label()`）：

```c
// wukong_ai_mode_picture.c，每次状态切换后执行：
tuya_ai_display_msg(&s_ai_picture.state, 1, TY_DISPLAY_TP_CHAT_STAT);
```

生图完成或输入图片发送完毕（`WUKONG_AI_EVENT_ACCEPT_PICTURE` / `WUKONG_AI_EVENT_SEND_PICTURE_END`）后，发送 `TY_DISPLAY_TP_CLEAR_ATTACHMENT` 将状态标签重置为 IDLE：

```c
tuya_ai_display_msg(NULL, 0, TY_DISPLAY_TP_CLEAR_ATTACHMENT);
// UI 侧：desk_chat_refresh_stat_label(GUI_STAT_IDLE)
```

### 11.7 屏幕切换时序

```
首次触发生图请求（不在 CHAT 屏）：
  HUMAN_CHAT → desk_handle_ui_switch_to(DHUI_SCREEN_ID_CHAT)
               ↓
  屏幕加载完成 → desk_chat_session_resume_on_chat_ready()
               ↓（补发积压的文本和图片消息）
  TTS 文字气泡渲染 + disp_picture_message()

已在 CHAT 屏时，所有消息直接原地渲染，无屏幕切换。
```

---

## 12. 常见问题

| 现象 | 可能原因 | 排查方向 |
|------|---------|---------|
| 生图无响应 | AI Agent 未就绪 | 检查 `tuya_ai_agent_is_ready()` 返回值 |
| 图片显示空白 | `ENABLE_TUYA_UI` 未定义 | 确认编译宏已开启 |
| 生成图片丢失 | 相册满（超过 10 张） | 调整 `TUYA_PICTURE_ALBUM_MAX_IMAGE_CNT` |
| 分辨率不符 | 输出尺寸配置未生效 | 检查 `EVENT_AI_CLIENT_RUN` 事件订阅是否正常 |
| 图片队列不上传 | VAD_START 时队列为空 | 确认 `wukong_picture_input_add_from_album()` 在启动前被调用 |
