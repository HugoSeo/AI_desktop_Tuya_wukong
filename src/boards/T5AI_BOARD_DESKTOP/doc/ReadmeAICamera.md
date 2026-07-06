# 桌面机器人 AI 摄像头与相册功能技术文档

## 1. 概述

桌面机器人（T5AI_BOARD_DESKTOP）内置 DVP 摄像头，提供相机预览、拍照、图片相册管理等能力。相册系统分为两条独立管道：

- **本地相册**：用户手动拍摄的照片，以 JPEG 文件形式存储于文件系统，最多保留 100 张。
- **AI 生图相册**：由 AI 生图模式生成的图片，存储于内存，最多保留 10 张，超出时自动淘汰最旧的。

**功能特性：**
- YUV 流实时预览，RGB565 转换后渲染至 LVGL 画布
- 支持一键拍照，JPEG 格式保存到本地相册
- 支持 AI 摄像头模式：拍照后直接上传 AI 服务端进行图像理解
- 相册支持图片浏览（上/下翻页）、网格缩略图视图、单张/批量删除
- 相册图片可选入 AI 请求队列，作为参考图发送给 AI（最多 3 张）
- AI 生图结果自动流式接收、拼装、保存至内存相册

**相关核心文件：**

| 文件 | 说明 |
|------|------|
| `src/boards/T5AI_BOARD_DESKTOP/tuya_device_camera.h/.c` | 摄像头硬件驱动（YUV/MJPEG 接口） |
| `src/boards/T5AI_BOARD_DESKTOP/ui/desk_func_camera.h/.c` | 相机预览屏 UI 与拍照逻辑 |
| `src/boards/T5AI_BOARD_DESKTOP/ui/desk_func_photo.h/.c` | 本地相册 UI 与文件管理 |
| `src/wukong/picture/wukong_picture.h/.c` | AI 生图相册管理 |
| `src/wukong/picture/wukong_picture_input.h/.c` | 图片输入队列（图生图） |
| `src/wukong/picture/wukong_picture_output.h/.c` | AI 生图结果接收与保存 |
| `src/miscs/image_album/image_album.h` | 通用相册引擎（保存/读取/删除/迭代） |
| `src/miscs/image_album/image_album_thumb.h` | 缩略图生成 |
| `include/tuya_app_config.h` | 配置参数定义 |

---

## 2. 编译配置

相机与相册功能由以下宏控制，定义于 `tuya_app_config.h`：

```c
#define ENABLE_TUYA_CAMERA             1    // 启用摄像头硬件支持
#define ENABLE_TUYA_PICTURE            1    // 启用图片处理模块
#define ENABLE_IMAGE_ALBUM             1    // 启用相册管理引擎
#define ENABLE_IMAGE_ALBUM_STORAGE_MEM 1    // 使用内存存储后端（AI 生图相册）
#define ENABLE_IMAGE_ALBUM_STORAGE_SD  0    // SD 卡存储（默认关闭）
```

---

## 3. 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `CAMERA_TYPE_DVP` | `1` | DVP 接口摄像头 |
| `TUYA_AI_TOY_ISP_WIDTH_VAL` | `480` | 摄像头 ISP 输出宽度（像素） |
| `TUYA_AI_TOY_ISP_HEIGHT_VAL` | `480` | 摄像头 ISP 输出高度（像素） |
| `TUYA_AI_TOY_ISP_FPS_VAL` | `10` | 摄像头帧率（fps） |
| `TUYA_PICTURE_ALBUM_NAME` | `"ai_picture"` | AI 生图相册名称 |
| `TUYA_PICTURE_ALBUM_MAX_IMAGE_CNT` | `10` | AI 生图相册最大图片数（超出自动删除最旧） |
| `TUYA_PICTURE_DEF_OUTPUT_WIDTH` | `320` | AI 生图默认宽度（像素） |
| `TUYA_PICTURE_DEF_OUTPUT_HEIGHT` | `240` | AI 生图默认高度（像素） |
| `WUKONG_PICTURE_INPUT_MAX_NUM` | `3` | 图片输入队列最大长度 |
| `WUKONG_PICTURE_OUTPUT_MAX_NUM` | `12` | 同时处理的输出流最大数量 |
| `WUKONG_PICTURE_NAME_MAX_LEN` | `64` | 图片文件名最大长度（字节） |
| `PHOTO_LIST_MAX`（本地相册） | `100` | 本地相册最大图片数 |

---

## 4. 摄像头硬件驱动（T5AI_BOARD_DESKTOP）

### 4.1 YUV 流用户标识

YUV 视频流采用引用计数管理，多个功能模块可同时持有，最后一个释放时才真正停流：

```c
typedef enum {
    YUV_USER_PREVIEW = (1 << 0),  // UI 预览
    YUV_USER_MD      = (1 << 1),  // 运动检测
} YUV_USER_E;
```

### 4.2 核心接口

```c
/* 初始化摄像头 */
OPERATE_RET tuya_desktop_camera_init(void);

/* YUV 流控制（UI 预览） */
OPERATE_RET tuya_device_camera_yuv_acquire(YUV_USER_E user);  // 引用计数加 1
OPERATE_RET tuya_device_camera_yuv_release(YUV_USER_E user);  // 引用计数减 1

/* JPEG 流控制（拍照） */
OPERATE_RET tuya_device_camera_jpeg_start(void);
OPERATE_RET tuya_device_camera_jpeg_stop(void);
OPERATE_RET tuya_device_camera_get_jpeg_frame(BYTE_T **data, UINT_T *len, VOID *user_data);

/* 回调类型 */
typedef VOID_T (*TUYA_YUV_FRAME_CB) (UINT8_T *data, UINT16_T width, UINT16_T height);
typedef VOID_T (*TUYA_MJPEG_FRAME_CB)(UINT8_T *data, UINT32_T len);
```

---

## 5. 相机预览屏（desk_func_camera.c）

### 5.1 内部状态

```c
typedef struct {
    lv_obj_t   *canvas_obj;           // LVGL 画布（YUV→RGB565 渲染目标）
    BOOL_T      ai_camera_on;         // AI 摄像头模式开关
    UINT8_T    *display_buf;          // YUV 显示缓冲
    UINT8_T    *display_rotate_buf;   // 旋转后缓冲
    MUTEX_HANDLE mutex;               // 帧缓冲互斥锁
    BOOL_T      camera_exit;          // 相机退出标志
    BOOL_T      jpeg_start;           // JPEG 流启动标志
    BOOL_T      jpeg_processing;      // JPEG 处理中（防重入）
    UINT8_T    *jpeg_data;            // JPEG 数据缓冲
    UINT32_T    jpeg_data_len;        // JPEG 数据长度
    BOOL_T      jpeg_save_to_album;   // 是否保存到相册
    lv_obj_t   *ai_icon_img;          // AI 模式指示图标
    lv_obj_t   *thumbnail_btn;        // 右下角相册缩略图按钮
} CAMERA_DISPLAY_T;
```

### 5.2 相机预览屏布局

```
┌────────────────────────────────┐
│ [←返回]                        │  ← 标题栏
├────────────────────────────────┤
│                                │
│     YUV 实时预览画面           │  ← 画布 (canvas_obj)
│                                │
├───────────┬────────────────────┤
│ [AI图标]  │  [拍照按钮] [相册] │  ← 底部操作区
└───────────┴────────────────────┘
```

### 5.3 拍照流程

```
用户点击拍照按钮
    │
    └─→ desk_camera_take_photo()
         │
         ├─ 检查 canvas_obj 就绪（desk_camera_is_canvas_ready()）
         ├─ jpeg_start = TRUE
         │
         └─→ tuya_device_camera_jpeg_start()
              │
              └─→ camera_jpeg_done_async_cb()  [JPEG 帧回调]
                   │
                   ├─ jpeg_save_to_album == TRUE
                   │    └─→ desk_photo_add(jpeg_data, jpeg_data_len)
                   │         └─→ 保存至 /t5_fs/tmp/picture/TUYA<NNNN>.jpeg
                   │
                   ├─ ai_camera_on == TRUE
                   │    └─→ wukong_ai_agent_send_image(jpeg_data, jpeg_data_len)
                   │         └─→ AI 图像理解请求
                   │
                   └─ 否则：仅写入临时文件 /t5_fs/tmp/take_photo.jpeg
```

---

## 6. 本地相册（desk_func_photo.c）

### 6.1 存储路径与命名

```c
#define PHOTO_STORE_DIR    "/t5_fs/tmp/picture"
#define PHOTO_NAME_PREFIX  "TUYA"
#define PHOTO_NAME_SUFFIX  ".jpeg"
#define PHOTO_PATH_MAX_LEN 64
#define PHOTO_LIST_MAX     100
```

文件命名格式：`TUYA<NNNN>.jpeg`（如 `TUYA0001.jpeg`），索引从 `0000` 递增至 `9999`。

### 6.2 核心接口

```c
/* 添加照片到本地存储 */
OPERATE_RET desk_photo_add(CONST UINT8_T *data, UINT32_T data_len);

/* 删除指定索引照片 */
OPERATE_RET desk_photo_delete(UINT32_T index);

/* 获取下一张照片的索引 */
OPERATE_RET desk_photo_get_next_index(UINT32_T *next_index);

/* 构建照片完整路径 */
OPERATE_RET desk_photo_build_path(UINT32_T index, CHAR_T *path, UINT32_T path_sz);

/* 在相册 UI 中展示最新照片 */
void desk_photo_set_show_latest(void);
```

---

## 7. AI 生图相册管理（wukong_picture.h）

AI 生成的图片统一由 `wukong_picture` 模块管理，底层使用 `image_album` 内存后端。

### 7.1 相册生命周期

```c
/* 初始化，创建 "ai_picture" 内存相册 */
OPERATE_RET wukong_picture_init(void);

/* 保存 JPEG 到相册（自动修剪至 TUYA_PICTURE_ALBUM_MAX_IMAGE_CNT） */
OPERATE_RET wukong_picture_save_to_album(uint8_t *picture, uint32_t len,
                                        char name[WUKONG_PICTURE_NAME_MAX_LEN + 1]);
```

### 7.2 相册浏览接口

```c
/* 打开相册（获取扫描锁） */
OPERATE_RET wukong_picture_open_album(void);

/* 关闭相册（释放扫描锁） */
OPERATE_RET wukong_picture_close_album(void);

/* 获取图片总数 */
uint32_t wukong_picture_get_count(void);

/* 定位到指定张（1-based） */
OPERATE_RET wukong_picture_seek_to_photo(uint32_t one_based);

/* 前/后翻页 */
OPERATE_RET wukong_picture_get_prev(WUKONG_PICTURE_INFO_T *pic);
OPERATE_RET wukong_picture_get_next(WUKONG_PICTURE_INFO_T *pic);

/* 删除当前图片（自动重扫描） */
OPERATE_RET wukong_picture_delete_current(void);

/* 批量删除（按文件名数组） */
OPERATE_RET wukong_picture_delete_batch(const char *names[], uint32_t count);

/* 获取当前图片名称 */
OPERATE_RET wukong_picture_get_current_name(char name[WUKONG_PICTURE_NAME_MAX_LEN + 1]);

/* 按名称直接读取图片数据 */
OPERATE_RET wukong_picture_get_by_name(const char *name, uint8_t **data, uint32_t *len);
```

### 7.3 缩略图接口

```c
/* 获取全部缩略图列表（RGB565，指定尺寸） */
OPERATE_RET wukong_picture_get_thumb_list(uint16_t thumb_w, uint16_t thumb_h,
                                          WUKONG_PICTURE_THUMB_LIST_T *list);
```

### 7.4 数据结构

```c
typedef struct {
    char     name[WUKONG_PICTURE_NAME_MAX_LEN + 1];
    uint16_t width;
    uint16_t height;
    uint32_t len;
    uint8_t *data;    // JPEG 原始数据
} WUKONG_PICTURE_INFO_T;

typedef struct {
    char     name[WUKONG_PICTURE_NAME_MAX_LEN + 1];
    uint16_t width;
    uint16_t height;
    uint32_t size;
    uint8_t *data;    // RGB565 缩略图数据
} WUKONG_PICTURE_THUMB_T;
```

### 7.5 容量管理

相册超出 `TUYA_PICTURE_ALBUM_MAX_IMAGE_CNT`（默认 10）时，`__album_trim_oldest()` 自动删除最早保存的图片，保持相册大小稳定。

---

## 8. 图片输入队列（图生图，wukong_picture_input.h）

在生图请求中附带已有图片，用于参考图、编辑等场景。

```c
/* 将相册图片加入待发队列（仅锁定引用，不立即读取数据） */
OPERATE_RET wukong_picture_input_add_from_album(char *filename, char *text);

/* 从队列移除指定图片 */
OPERATE_RET wukong_picture_input_del_from_album(char *filename);

/* VAD_START 时批量发送（逐张读取→发送→释放，节省内存峰值） */
OPERATE_RET wukong_picture_input_from_album(void);

/* 查询队列中待发图片数 */
uint32_t wukong_picture_input_get_num(void);
```

队列容量上限：`WUKONG_PICTURE_INPUT_MAX_NUM = 3`。

---

## 9. AI 生图输出接收（wukong_picture_output.h）

AI 返回的图片以 JPEG 分块流形式到达，由本模块累积后一次性保存到相册。

```c
/* 告知 AI 服务端期望的图片输出尺寸 */
OPERATE_RET wukong_picture_output_set_size(uint16_t width, uint16_t height);

/* 累积 JPEG 分块，全部到达后保存到相册 */
OPERATE_RET wukong_picture_output_save_to_album(uint8_t *data, uint32_t len, uint32_t total_len);
```

**图片尺寸推送键值（发送至 AI 代理参数）：**

```json
{
    "sys.device.img_resize.width": 320,
    "sys.device.img_resize.height": 240
}
```

**累积机制：**

```
第 1 块到达 → 按 total_len 分配缓冲区，is_start=true，offset=0
第 N 块到达 → 校验 total_len 一致性，追加写入 acc_buf[offset]
最后一块    → offset >= total_len
              → wukong_picture_save_to_album()（文件名：wukong_pic_{timestamp}）
              → 触发 WUKONG_AI_EVENT_ACCEPT_PICTURE
              → 释放累积缓冲区
```

---

## 10. 通用相册引擎（image_album.h）

`image_album` 是底层通用相册引擎，支持多种存储后端，`wukong_picture` 和本地相册均基于此构建。

### 10.1 初始化

```c
OPERATE_RET image_album_init(char *name, const IMAGE_ALBUM_INIT_CFG_T *cfg,
                            IMAGE_ALBUM_HANDLE *album_handle);
OPERATE_RET image_album_deinit(IMAGE_ALBUM_HANDLE album_handle);
IMAGE_ALBUM_HANDLE image_album_find_by_name(const char *name);
```

### 10.2 图片保存与读取

```c
/* 保存图片 */
OPERATE_RET image_album_save(IMAGE_ALBUM_HANDLE album_handle, ALBUM_IMAGE_SAVE_INFO_T *info);

/* 读取原始编码数据 */
OPERATE_RET image_album_read(IMAGE_ALBUM_HANDLE album_handle, const char *filename,
                            IMAGE_ALBUM_STORAGE_TP_E storage_tp,
                            uint8_t **file_data, size_t *file_size);
OPERATE_RET image_album_free_file_data(uint8_t *file_data);

/* 删除图片 */
OPERATE_RET image_album_delete(IMAGE_ALBUM_HANDLE album_handle, const char *filename);

/* 获取已提交图片总数 */
OPERATE_RET image_album_get_committed_count(IMAGE_ALBUM_HANDLE album_handle, UINT32_T *count);
```

### 10.3 扫描迭代（image_album_scan.h）

```c
/* 创建扫描会话（内部获取相册锁） */
int image_album_scan_init(char *name, IMAGE_ALBUM_STORAGE_TP_E storage_tp,
                         IMAGE_ALBUM_SCAN_HANDLE *scan_handle);

/* 设置排序方式（须在第一次 next 前调用） */
OPERATE_RET image_album_scan_set_sort(IMAGE_ALBUM_SCAN_HANDLE scan_handle,
                                      const IMAGE_ALBUM_SORT_OPT_T *sort_opt);

/* 前/后向迭代 */
OPERATE_RET image_album_scan_next(IMAGE_ALBUM_SCAN_HANDLE scan_handle, ALBUM_IMAGE_ITEM_T *item);
OPERATE_RET image_album_scan_prev(IMAGE_ALBUM_SCAN_HANDLE scan_handle, ALBUM_IMAGE_ITEM_T *item);

/* 定位到指定位置 */
OPERATE_RET image_album_scan_seek(IMAGE_ALBUM_SCAN_HANDLE scan_handle, uint32_t pos);

/* 关闭扫描（释放相册锁） */
OPERATE_RET image_album_scan_deinit(IMAGE_ALBUM_SCAN_HANDLE scan_handle);

/* 获取总数 */
uint32_t image_album_scan_get_count(IMAGE_ALBUM_SCAN_HANDLE scan_handle);
```

**排序选项：**

```c
typedef enum {
    IMAGE_ALBUM_SORT_NONE = 0,
    IMAGE_ALBUM_SORT_SAVE_SEQ,    // 按保存序列号
    IMAGE_ALBUM_SORT_SAVE_TIME,   // 按保存时间戳
    IMAGE_ALBUM_SORT_FILE_SIZE,   // 按文件大小
} IMAGE_ALBUM_SORT_KEY_E;

typedef enum {
    IMAGE_ALBUM_SORT_ASC = 0,     // 升序
    IMAGE_ALBUM_SORT_DESC,        // 降序
} IMAGE_ALBUM_SORT_ORDER_E;
```

### 10.4 引用计数与锁定

```c
/* 相册级锁（阻止列表修改，批量操作时使用） */
OPERATE_RET image_album_retain_locked(IMAGE_ALBUM_HANDLE album_handle);
OPERATE_RET image_album_release_locked(IMAGE_ALBUM_HANDLE album_handle);

/* 单文件引用（延迟删除保护） */
OPERATE_RET image_album_item_retain_locked(IMAGE_ALBUM_HANDLE album_handle,
                                           const char *filename,
                                           IMAGE_ALBUM_STORAGE_TP_E storage_tp,
                                           ALBUM_IMAGE_ITEM_T *item);
OPERATE_RET image_album_item_release_locked(IMAGE_ALBUM_HANDLE album_handle,
                                            const char *filename);
```

---

## 11. 缩略图生成（image_album_thumb.h）

### 11.1 单图缩略图

```c
OPERATE_RET album_thumb_get(IMAGE_ALBUM_HANDLE handle,
                           const char *filename,
                           IMAGE_ALBUM_STORAGE_TP_E storage_tp,
                           const ALBUM_THUMB_CFG_T *cfg,
                           ALBUM_THUMB_T *out);
OPERATE_RET album_thumb_free(ALBUM_THUMB_T *out);
```

### 11.2 批量缩略图迭代器

```c
/* 创建迭代器 */
OPERATE_RET image_album_thumb_iter_init(char *name,
                                       IMAGE_ALBUM_STORAGE_TP_E storage_tp,
                                       const IMAGE_ALBUM_SORT_OPT_T *sort_opt,
                                       ALBUM_THUMB_ITER_HANDLE *iter);

/* 分页获取（正向/反向） */
OPERATE_RET image_album_thumb_iter_next(ALBUM_THUMB_ITER_HANDLE iter,
                                       const ALBUM_THUMB_CFG_T *cfg,
                                       uint32_t n,
                                       ALBUM_THUMB_BATCH_T *batch);
OPERATE_RET image_album_thumb_iter_prev(ALBUM_THUMB_ITER_HANDLE iter,
                                       const ALBUM_THUMB_CFG_T *cfg,
                                       uint32_t n,
                                       ALBUM_THUMB_BATCH_T *batch);

/* 释放批次数据 */
OPERATE_RET image_album_thumb_batch_free(ALBUM_THUMB_BATCH_T *batch);

/* 关闭迭代器 */
OPERATE_RET image_album_thumb_iter_deinit(ALBUM_THUMB_ITER_HANDLE iter);

/* 获取总数 */
uint32_t image_album_thumb_iter_count(ALBUM_THUMB_ITER_HANDLE iter);
```

### 11.3 缩略图配置

```c
typedef struct {
    uint16_t          width;   // 目标宽度
    uint16_t          height;  // 目标高度
    ALBUM_THUMB_FMT_E fmt;     // 像素格式
    ALBUM_THUMB_FIT_E fit;     // 缩放模式
} ALBUM_THUMB_CFG_T;

typedef enum {
    ALBUM_THUMB_FMT_RGB565 = 0,  // 2 字节/像素
    ALBUM_THUMB_FMT_RGB888,      // 3 字节/像素
    ALBUM_THUMB_FMT_GRAY,        // 1 字节/像素
} ALBUM_THUMB_FMT_E;

typedef enum {
    ALBUM_THUMB_FIT_STRETCH = 0,  // 拉伸至精确尺寸
    ALBUM_THUMB_FIT_CONTAIN,      // 保持比例，加黑边
    ALBUM_THUMB_FIT_COVER,        // 保持比例，中心裁剪
} ALBUM_THUMB_FIT_E;
```

---

## 12. 存储后端

| 后端类型 | 宏标识 | 存储位置 | 持久化 | 容量 |
|----------|--------|---------|--------|------|
| 内存后端 | `IMAGE_ALBUM_STORAGE_TP_MEMORY` | 堆内存 | 不持久（重启丢失） | 受限于可用 RAM |
| SD 卡后端 | `IMAGE_ALBUM_STORAGE_TP_SD` | SD 卡文件系统 | 持久 | 受限于 SD 卡空间 |
| 自定义后端 | `IMAGE_ALBUM_STORAGE_TP_CUSTOM` | 用户实现 | 由实现决定 | — |

**内存后端实现原理：**

```c
// 保存时分配堆内存，复制数据，返回指针作为"路径句柄"
static OPERATE_RET __mem_save(void *handle, const char *filename,
                              const uint8_t *data, size_t size, void **path)
{
    uint8_t *buf = (uint8_t *)Malloc(size);
    memcpy(buf, data, size);
    *path = buf;    // path 即为数据缓冲指针
    return OPRT_OK;
}
// 删除时直接 Free(path)
// 无 recover 支持（重启后相册清空）
```

---

## 13. 显示与 UI 集成

### 13.1 相机/相册相关动作类型

通过 `tuya_ai_display_action_post()` 向 UI 层发送动作请求：

```c
typedef enum {
    TY_DISP_ACT_OPEN_CAMERA = 0,         // 打开相机预览屏
    TY_DISP_ACT_CLOSE_CAMERA,            // 关闭相机预览屏
    TY_DISP_ACT_CAMERA_OPEN_AI,          // 开启 AI 摄像头模式
    TY_DISP_ACT_CAMERA_CLOSE_AI,         // 关闭 AI 摄像头模式
    TY_DISP_ACT_TAKE_PHOTO,              // 触发拍照
    TY_DISP_ACT_OPEN_ALBUM,              // 打开相册浏览屏
    TY_DISP_ACT_CLOSE_ALBUM,             // 关闭相册
    TY_DISP_ACT_ALBUM_VIEW_PREV_PIC,     // 上一张
    TY_DISP_ACT_ALBUM_VIEW_NEXT_PIC,     // 下一张
    TY_DISP_ACT_ALBUM_DELETE_PIC,        // 删除当前图片
    TY_DISP_ACT_OPEN_ALBUM_GRID,         // 打开相册网格视图
    TY_DISP_ACT_CLOSE_ALBUM_GRID,        // 关闭相册网格视图
    TY_DISP_ACT_ALBUM_BATCH_DELETE,      // 批量删除
    TY_DISP_ACT_ALBUM_AI_RECOGNIZE,      // AI 识别当前图片
} TY_DISPLAY_ACTION_E;
```

### 13.2 屏幕 ID

```c
DHUI_SCREEN_ID_CAMERA       // 相机预览屏
DHUI_SCREEN_ID_ALBUM        // 相册图片浏览屏
DHUI_SCREEN_ID_ALBUM_GRID   // 相册网格视图屏
```

### 13.3 图片显示消息

AI 生图完成后，通过 `TY_DISPLAY_TP_AI_IMAGE` 消息携带文件名触发 UI 渲染：

```c
tuya_ai_display_msg(filename, strlen(filename), TY_DISPLAY_TP_AI_IMAGE);
```

UI 侧收到后，从相册读取 JPEG 数据，解码后渲染至 LVGL 图片组件（`lv_img_create`），同时在消息列表中添加"查看图片"链接。

---

## 14. 数据路径总览

| 功能 | 数据来源 | 存储位置 | 容量上限 |
|------|---------|---------|---------|
| 用户拍照（本地相册） | JPEG 流（摄像头） | `/t5_fs/tmp/picture/TUYA<NNNN>.jpeg` | 100 张 |
| 拍照临时文件 | JPEG 流（摄像头） | `/t5_fs/tmp/take_photo.jpeg` | 1 张（覆写） |
| AI 生图（内存相册） | AI 服务端 JPEG 流 | 堆内存（image_album 内存后端） | 10 张 |
| AI 摄像头上传 | JPEG 帧（摄像头） | 仅上传，不本地保存 | — |

---

## 15. 调试日志

```
# 相机与拍照（INFO 级别）
[camera] take photo, jpeg_data_len=65536
[camera] photo saved to album: TUYA0001.jpeg
[camera] ai camera mode on, send image to ai agent

# AI 生图接收（NOTICE 级别）
[pic_chain] start accumulating, total_len:76800
[pic_chain] chunk accumulated, offset:8192/76800
[pic_chain] all chunks received, total:76800, saving to album
[pic_chain] album saved, filename:wukong_pic_1714012345, size:76800

# 相册操作（DEBUG 级别）
[wukong_picture] open album, count=5
[wukong_picture] seek to 1
[wukong_picture] delete current: wukong_pic_1714012300
[wukong_picture] close album

# 缩略图生成
[album_thumb] generating thumb 80x80 for wukong_pic_1714012345
```

---

## 16. 常见问题

| 现象 | 可能原因 | 排查方向 |
|------|---------|---------|
| 相机预览黑屏 | YUV 流未启动 | 检查 `tuya_device_camera_yuv_acquire(YUV_USER_PREVIEW)` 返回值 |
| 拍照后无文件 | 存储路径不存在 | 确认 `/t5_fs/tmp/picture/` 目录已创建 |
| AI 生图相册图片消失 | 内存后端无持久化 | 重启后内存相册清空属正常，如需持久化请启用 SD 卡后端 |
| AI 生图相册只有 10 张 | 超出容量自动修剪 | 调整 `TUYA_PICTURE_ALBUM_MAX_IMAGE_CNT` |
| 图生图未上传参考图 | VAD_START 前未调用 `input_add` | 确认在启动语音输入前调用 `wukong_picture_input_add_from_album()` |
| 缩略图颜色异常 | 格式不匹配 | 确认 `ALBUM_THUMB_FMT_RGB565` 与 LVGL 显示格式一致 |
| 相册浏览卡死 | 未释放扫描锁 | 确认 `wukong_picture_close_album()` 在所有退出路径均被调用 |
