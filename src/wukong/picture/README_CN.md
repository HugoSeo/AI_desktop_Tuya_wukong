# 图片模块

## 概述

图片模块为 Wukong AI 系统提供 AI 生图能力，包括云端下行图片接收与存储、设备侧图片上行发送（多模态输入）、相册管理（浏览、缩略图、删除）等功能。该模块构建在 `image_album` 存储组件和 `tal_image` 图像处理库之上。

## 目录结构

```
picture/
├── wukong_picture.h/c              # 相册管理（初始化、存储、浏览、缩略图、删除）
├── wukong_picture_output.h/c       # 云端下行：JPEG 分片累加 → 入库 → 事件通知
├── wukong_picture_input.h/c        # 设备上行：相册选图入队 → 发送给 AI Agent
├── Kconfig                         # 配置项定义
└── README_CN.md
```

## 配置

### 开启图片功能

图片功能默认在 T5AI_BOARD 上可用。通过 `menuconfig` 开启：

```
Component config  --->
    Wukong AI  --->
        [*] ENABLE_TUYA_PICTURE    # 启用 AI 绘图功能
```

开启 `ENABLE_TUYA_PICTURE` 后会自动选中以下依赖：

- `ENABLE_IMAGE_ALBUM` — 相册存储组件

T5AI_BOARD 的 UI 风格通过 `T5AI_BOARD UI style` 互斥选择，开启图片功能时默认选择 `ENABLE_VIEW_UI`。

### 编译时标志

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| `CONFIG_ENABLE_TUYA_PICTURE` | 启用 AI 绘图功能（仅 T5AI_BOARD 可见） | y |
| `CONFIG_ENABLE_IMAGE_ALBUM` | 启用图片相册组件（由上项自动 select） | — |
| `CONFIG_ENABLE_IMAGE_ALBUM_STORAGE_MEM` | 内存存储后端 | y |
| `CONFIG_ENABLE_IMAGE_ALBUM_STORAGE_SD` | SD 卡/文件系统存储后端 | n |

### 运行时参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `TUYA_PICTURE_ALBUM_NAME` | 相册名称 | `ai_picture` |
| `TUYA_PICTURE_ALBUM_MAX_IMAGE_CNT` | 相册最大图片数（超出自动删除最旧） | 10 |
| `TUYA_PICTURE_DEF_OUTPUT_WIDTH` | AI 输出图片宽度 | 与 LCD 宽度一致 |
| `TUYA_PICTURE_DEF_OUTPUT_HEIGHT` | AI 输出图片高度 | 与 LCD 高度一致 |
| `WUKONG_PICTURE_INPUT_MAX_NUM` | 上行队列容量 | 3 |
| `WUKONG_PICTURE_OUTPUT_MAX_NUM` | 下行最大图片数 | 12 |

### 存储后端选择

根据硬件资源选择存储后端：

- **内存后端**（`ENABLE_IMAGE_ALBUM_STORAGE_MEM`）：图片存放在 RAM 中，读写快但断电丢失，适合内存充裕的场景。
- **SD 卡后端**（`ENABLE_IMAGE_ALBUM_STORAGE_SD`）：图片持久化到 SD 卡/文件系统，适合需要掉电保存的场景。

两个后端可同时开启，通过 `image_album` 组件的存储类型参数选择实际使用的后端。

## 数据流

### 云端 AI 生图（下行）

```
┌──────────┐     ┌─────────────────────┐     ┌───────────────────┐     ┌──────────┐
│ 云端 AI   │     │ wukong_ai_agent     │     │ picture_output    │     │ 事件系统  │
│          │     │                     │     │                   │     │          │
│ AI_PT_   │────►│ media_data_cb()     │────►│ save_to_album()   │────►│ ACCEPT_  │
│ IMAGE    │     │ type==AI_PT_IMAGE   │     │ 分片累加 → 入库     │     │ PICTURE  │
└──────────┘     └─────────────────────┘     └───────────────────┘     └────┬─────┘
                                                                            │
                 ┌─────────────────────┐     ┌───────────────────┐          │
                 │ UI 层               │     │ image_album       │          │
                 │                     │◄────│                   │◄─────────┘
                 │ JPEG 解码 → 显示     │     │ 按名读取 JPEG 数据 │   tuya_ai_display_msg
                 └─────────────────────┘     └───────────────────┘   (TY_DISPLAY_TP_AI_IMAGE)
```

### 设备图片上行（多模态输入）

```
┌──────────┐     ┌─────────────────────┐     ┌───────────────────┐     ┌──────────┐
│ UI 相册   │     │ picture_input       │     │ AI Agent          │     │ 云端 AI   │
│          │     │                     │     │                   │     │          │
│ 用户选图  │────►│ add_from_album()    │     │                   │     │          │
│          │     │ retain-lock, 入队    │     │                   │     │          │
│          │     │         │           │     │                   │     │          │
│ VAD 开始  │────►│ from_album()        │────►│ tuya_ai_image_    │────►│ 图像理解  │
│ (按住说话) │     │ 逐张读取 → 发送 → 释放│     │ input()           │     │          │
└──────────┘     └─────────────────────┘     └───────────────────┘     └──────────┘
```

## 核心组件

### 1. 相册管理 (`wukong_picture`)

在 `image_album` 组件之上封装相册生命周期管理，提供初始化、保存、顺序浏览、缩略图、批量删除等功能。

#### 关键数据结构

```c
/** 图片元数据与文件数据容器 */
typedef struct {
    char name[WUKONG_PICTURE_NAME_MAX_LEN + 1];
    uint16_t width;
    uint16_t height;
    uint32_t len;
    uint8_t *data;    /* JPEG 原始字节，需调用 wukong_picture_free_pic_info() 释放 */
} WUKONG_PICTURE_INFO_T;

/** 缩略图条目（RGB565 像素数据） */
typedef struct {
    char name[WUKONG_PICTURE_NAME_MAX_LEN + 1];
    uint16_t width;
    uint16_t height;
    uint32_t size;
    uint8_t *data;    /* RGB565 像素缓冲，解码失败时为 NULL */
} WUKONG_PICTURE_THUMB_T;

/** 缩略图列表容器 */
typedef struct {
    WUKONG_PICTURE_THUMB_T *items;
    uint32_t count;
} WUKONG_PICTURE_THUMB_LIST_T;
```

#### 核心 API

```c
/* 初始化 */
OPERATE_RET wukong_picture_init(void);

/* 保存与读取 */
OPERATE_RET wukong_picture_save_to_album(uint8_t *picture, uint32_t len, char name[]);
OPERATE_RET wukong_picture_get_by_name(const char *name, WUKONG_PICTURE_INFO_T *pic);
VOID_T      wukong_picture_free_pic_info(WUKONG_PICTURE_INFO_T *pic);

/* 顺序浏览 */
OPERATE_RET wukong_picture_open_album(void);
uint32_t    wukong_picture_get_count(void);
OPERATE_RET wukong_picture_seek_to_photo(uint32_t one_based);
OPERATE_RET wukong_picture_get_prev(WUKONG_PICTURE_INFO_T *pic);
OPERATE_RET wukong_picture_get_next(WUKONG_PICTURE_INFO_T *pic);
OPERATE_RET wukong_picture_close_album(void);

/* 缩略图 */
OPERATE_RET wukong_picture_get_thumb_list(uint16_t thumb_w, uint16_t thumb_h, WUKONG_PICTURE_THUMB_LIST_T *list);
VOID_T      wukong_picture_free_thumb_list(WUKONG_PICTURE_THUMB_LIST_T *list);

/* 删除 */
OPERATE_RET wukong_picture_delete_current(void);
OPERATE_RET wukong_picture_delete_batch(const char *names[], uint32_t count);
```

### 2. 云端下行 (`wukong_picture_output`)

接收云端 AI 生成的图片。云端以 `AI_PT_IMAGE` 类型分片流式下发 JPEG 数据，本模块负责累加分片、保存到相册、通知 UI 刷新。

#### 工作流程

1. **首片到达**：根据 `total_len` 分配累加缓冲区
2. **后续分片**：`memcpy` 追加到缓冲区，校验 `total_len` 一致性
3. **末片到达**（`offset >= total_len`）：
   - 调用 `wukong_picture_save_to_album()` 保存到相册（文件名格式 `wukong_pic_<timestamp>`）
   - 触发 `wukong_ai_event_notify(WUKONG_AI_EVENT_ACCEPT_PICTURE, name)`
   - 释放累加缓冲区

#### 尺寸同步

初始化时通过订阅 `EVENT_AI_CLIENT_RUN` 事件，将设备期望的输出图片尺寸（`TUYA_PICTURE_DEF_OUTPUT_WIDTH/HEIGHT`，默认与 LCD 分辨率一致）推送到云端，供云端按设备分辨率生成图片。

#### 核心 API

```c
OPERATE_RET wukong_picture_output_set_size(uint16_t width, uint16_t height);
OPERATE_RET wukong_picture_output_save_to_album(uint8_t *data, uint32_t len, uint32_t total_len);
```

### 3. 设备上行 (`wukong_picture_input`)

将相册中的图片发送给 AI Agent，支持多模态对话（图+文）。设计上采用「延迟读取」策略：入队时仅 retain-lock 不读文件数据，发送时逐张读取、发送、释放，以减少峰值内存占用。

#### 工作流程

1. **入队**：UI 调用 `wukong_picture_input_add_from_album(name, text)` 将图片名加入队列（最多 3 张）
2. **发送**：VAD 开始时，交互模式调用 `wukong_picture_input_from_album()` 逐张发送：
   - `image_album_read()` 读取 JPEG 数据
   - `tuya_ai_image_input()` 上行到云端
   - 可选 `tuya_ai_text_input()` 发送附带文本
   - 释放数据和 retain-lock
3. **完成通知**：触发 `WUKONG_AI_EVENT_SEND_PICTURE_END`

#### 核心 API

```c
OPERATE_RET wukong_picture_input_add_from_album(char *filename, char *text);
OPERATE_RET wukong_picture_input_del_from_album(char *filename);
OPERATE_RET wukong_picture_input_from_album(void);
uint32_t    wukong_picture_input_get_num(void);
```

## 使用示例

### 示例 1：接收云端 AI 生图（下行）并自定义处理

当云端 AI 生成图片后，系统会触发 `WUKONG_AI_EVENT_ACCEPT_PICTURE` 事件。在对话模式的事件处理函数中订阅该事件，即可获取图片数据：

```c
STATIC VOID_T __on_accept_picture(char *name)
{
    WUKONG_PICTURE_INFO_T pic = {0};
    OPERATE_RET rt = wukong_picture_get_by_name(name, &pic);
    if (rt != OPRT_OK) {
        PR_ERR("get picture failed, rt:%d", rt);
        return;
    }

    PR_NOTICE("recv picture: %s, %ux%u, %u bytes",
              pic.name, pic.width, pic.height, pic.len);

    /* pic.data 为完整的 JPEG 原始字节，可按需处理：
     * - 解码显示到 LCD
     * - 通过串口/SPI 转发给外部显示设备
     * - 保存到 SD 卡
     */
    my_custom_display(pic.data, pic.len, pic.width, pic.height);

    wukong_picture_free_pic_info(&pic);
}

/* 在对话模式的事件分发中注册处理 */
case WUKONG_AI_EVENT_ACCEPT_PICTURE: {
    __on_accept_picture((char *)event->data);
    break;
}
```

### 示例 2：从相册选图 + 文字发送给 AI（图文多模态对话）

完整展示从相册中选择一张图片，附带文字提示，发送给云端 AI 进行理解的典型流程。该流程分两个阶段：**选图入队**（用户交互时）和**实际发送**（VAD 触发后框架自动调用）。

```c
/**
 * 阶段 1：用户在相册中选图 — 将图片加入上行队列
 * 参考 app_ui_aciton.c 中 TY_DISP_ACT_ALBUM_AI_RECOGNIZE 的实现
 */
STATIC VOID_T __on_user_select_picture(char *pic_name)
{
    /* 将选中的图片入队，附带一段提示文字 */
    OPERATE_RET rt = wukong_picture_input_add_from_album(pic_name, "请描述这张图片的内容");
    if (rt != OPRT_OK) {
        PR_ERR("add picture failed, rt:%d", rt);
        return;
    }

    PR_DEBUG("picture queued: %s, pending count: %u",
             pic_name, wukong_picture_input_get_num());

    /* 入队后可在 UI 上展示附件预览（可选） */
    WUKONG_PICTURE_INFO_T pic = {0};
    if (OPRT_OK == wukong_picture_get_by_name(pic_name, &pic)) {
        ui_chat_set_attachment_jpeg(pic.data, pic.len);
        wukong_picture_free_pic_info(&pic);
    }
}

/**
 * 阶段 2：VAD 开始后，框架自动调用 wukong_picture_input_from_album()
 * 内部逐张执行：读取 JPEG → tuya_ai_image_input() → tuya_ai_text_input() → 释放
 * 全部发送完成后触发 WUKONG_AI_EVENT_SEND_PICTURE_END 事件
 *
 * 开发者通常无需手动调用此函数，交互模式框架会在合适时机自动触发。
 * 如需手动控制发送时机，可直接调用：
 */
wukong_picture_input_from_album();
```

如果图片不在相册中（如摄像头实时拍照），可跳过队列直接发送：

```c
/**
 * 直接发送一张 JPEG 图片给 AI（不经过相册队列）
 * 参考 wukong_picture_input_recognize() 的实现
 */
STATIC VOID_T __send_jpeg_directly(uint8_t *jpeg_data, uint32_t jpeg_len)
{
    tuya_ai_input_start(TRUE);

    /* 发送图片数据 */
    wukong_ai_agent_send_image(jpeg_data, jpeg_len);

    /* 发送附带的文字提示 */
    CHAR_T *prompt = "这张图里有几个人？";
    wukong_ai_agent_send_text(prompt);

    tuya_ai_input_stop();
}
```

### 示例 3：图片入队后取消发送

用户选图后、VAD 触发前，可以取消已入队的图片：

```c
/* 入队 */
wukong_picture_input_add_from_album("wukong_pic_12345", "帮我看看这张图");
PR_DEBUG("queued: %u", wukong_picture_input_get_num());  /* 输出 1 */

/* 用户反悔，取消该图片 */
wukong_picture_input_del_from_album("wukong_pic_12345");
PR_DEBUG("queued: %u", wukong_picture_input_get_num());  /* 输出 0 */
```

### 示例 4：顺序浏览相册

```c
wukong_picture_open_album();
uint32_t count = wukong_picture_get_count();
for (uint32_t i = 0; i < count; i++) {
    WUKONG_PICTURE_INFO_T pic = {0};
    if (OPRT_OK == wukong_picture_get_next(&pic)) {
        // pic.data 为 JPEG 原始字节，pic.len 为长度
        process_picture(&pic);
        wukong_picture_free_pic_info(&pic);
    }
}
wukong_picture_close_album();
```

### 示例 5：获取缩略图列表

```c
WUKONG_PICTURE_THUMB_LIST_T thumb_list = {0};
if (OPRT_OK == wukong_picture_get_thumb_list(80, 80, &thumb_list)) {
    for (uint32_t i = 0; i < thumb_list.count; i++) {
        WUKONG_PICTURE_THUMB_T *thumb = &thumb_list.items[i];
        // thumb->data 为 RGB565 像素数据
        draw_thumbnail(thumb->data, thumb->width, thumb->height);
    }
    wukong_picture_free_thumb_list(&thumb_list);
}
```

## 图片处理扩展

默认情况下，云端下行图片仅通过 `tuya_ai_display_msg()` 推送到 UI 层显示。如果开发者希望使用其他方式处理图片（如外接屏幕、串口转发、保存到外部存储等），可通过以下方式扩展。

### 方式 1：在对话模式中订阅事件

最简单的扩展方式。在对应的 `wukong_ai_mode_*.c` 事件处理函数中，增加对 `WUKONG_AI_EVENT_ACCEPT_PICTURE` 事件的处理：

```c
/* 示例：在 oneshot 模式中处理下行图片 */
case WUKONG_AI_EVENT_ACCEPT_PICTURE: {
    char *name = (char *)event->data;
    WUKONG_PICTURE_INFO_T pic = {0};
    if (OPRT_OK == wukong_picture_get_by_name(name, &pic)) {
        /* 自定义处理：转发到串口外设 */
        uart_send_jpeg(pic.data, pic.len);
        wukong_picture_free_pic_info(&pic);
    }
    break;
}
```

> **注意**：当前仅 `wukong_ai_mode_hold.c`（hold 模式）订阅了 `ACCEPT_PICTURE` 事件并推送 UI 显示。若需要在其他对话模式（oneshot / wakeup / free 等）下也处理 AI 生成的图片，需在对应的 `wukong_ai_mode_*.c` 中增加上述事件处理。

### 方式 2：替换或扩展 UI 显示回调

如果不使用默认的 LCD 显示，可以在 hold 模式的 `__ai_hold_image()` 函数中，将 `tuya_ai_display_msg()` 替换为自定义处理逻辑：

```c
/* src/mode/wukong_ai_mode_hold.c 中的原始实现 */
STATIC VOID __ai_hold_image(char *name)
{
#ifdef ENABLE_TUYA_UI
    tuya_ai_display_msg(name, strlen(name), TY_DISPLAY_TP_AI_IMAGE);
#endif
}

/* 改为自定义实现 */
STATIC VOID __ai_hold_image(char *name)
{
    WUKONG_PICTURE_INFO_T pic = {0};
    if (OPRT_OK == wukong_picture_get_by_name(name, &pic)) {
        /* 方案 A：通过 SPI 发送到外接 e-Paper 屏 */
        epaper_display_jpeg(pic.data, pic.len, pic.width, pic.height);

        /* 方案 B：保存到 SD 卡指定目录 */
        save_to_sdcard("/sdcard/ai_images/", name, pic.data, pic.len);

        /* 方案 C：通过网络上传到自建服务器 */
        http_upload_image(pic.data, pic.len);

        wukong_picture_free_pic_info(&pic);
    }
}
```

### 方式 3：自定义上行图片来源

默认上行流程从相册读取图片。如果图片来源不是相册（如摄像头实时拍照），可直接调用底层 API：

```c
/* 从摄像头获取 JPEG 数据后直接上行 */
uint8_t *jpeg_data = camera_capture_jpeg(&jpeg_len);

tuya_ai_input_start(TRUE);

wukong_ai_agent_send_image(jpeg_data, jpeg_len);

/* 可选：附带文本描述 */
char *prompt = "请描述这张照片";
wukong_ai_agent_send_text(prompt);

tuya_ai_input_stop();

camera_free_jpeg(jpeg_data);
```

## 依赖组件

| 组件 | 路径 | 说明 |
|------|------|------|
| image_album | `src/miscs/image_album/` | 相册存储抽象层，支持内存和 SD 卡后端 |
| tal_image | `src/miscs/tal_image/` | JPEG 信息读取、解码（RGB565/RGB888）、缩放 |
| wukong_ai_agent | `src/wukong/` | AI Agent 媒体回调（下行）、图片/文本上行接口 |
| tuya_ai_display | `src/miscs/gui/display/` | UI 消息分发（`TY_DISPLAY_TP_AI_IMAGE`） |

## 事件系统

图片模块发布以下事件：

| 事件 | 触发时机 | payload |
|------|----------|---------|
| `WUKONG_AI_EVENT_ACCEPT_PICTURE` | 云端图片接收并保存完成 | 文件名字符串（`char *`） |
| `WUKONG_AI_EVENT_SEND_PICTURE_END` | 设备侧图片队列发送完毕 | `NULL` |

## 支持

如果在使用图片模块过程中遇到问题，欢迎通过以下渠道获取帮助：

- **涂鸦开发者论坛**：[https://www.tuyaos.com/posting](https://www.tuyaos.com/posting) — 发帖时请附上日志输出、配置信息和复现步骤，社区和官方工程师会协助排查。
- **GitHub Issues**：如果确认是代码缺陷，可在仓库中提交 Issue 并附上最小复现用例。
