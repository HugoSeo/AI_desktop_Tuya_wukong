# 图片模块

## 概述

图片模块为 Wukong AI 提供 AI 生图（云端下行）、设备侧图片上行（多模态输入）、相册管理（浏览/缩略图/删除）能力，构建在 `image_album` 存储组件与 `tal_image` 图像处理库之上。模块拆成三个职责明确的文件：

- `wukong_picture`：封装 `image_album` 之上的相册生命周期（初始化、保存、顺序浏览、缩略图、批量删除）。
- `wukong_picture_output`：接收云端下行的 JPEG 分片，累加后落盘到相册。
- `wukong_picture_input`：把相册中的图片发送给 AI Agent，支持图文多模态对话。

接入方式：在 `AI Mode Configuration → Enabled AI Mode` 中开启 `Picture generation`（`ENABLE_AI_MODE_PICTURE`），该菜单展开后才会出现 `ENABLE_TUYA_PICTURE`（默认 y，自动 select `ENABLE_IMAGE_ALBUM`）。

## 设计要点与坑

- **下行分片累加**：云端以 `AI_PT_IMAGE` 类型流式下发 JPEG；首片到达按 `total_len` 分配累加缓冲区，末片（`offset >= total_len`）落盘后触发 `WUKONG_AI_EVENT_ACCEPT_PICTURE` 并释放缓冲区。分片间 `total_len` 不一致或写入越界都会重置累加区、返回错误，不会残留半截缓冲。
- **尺寸同步给云端**：初始化时订阅 `EVENT_AI_CLIENT_RUN`，把设备期望的输出尺寸（`TUYA_PICTURE_DEF_OUTPUT_WIDTH/HEIGHT`，默认等于 LCD 分辨率）推送给云端，让云端按设备分辨率生成图片。
- **上行延迟读取**：`wukong_picture_input_add_from_album()` 入队时只做 `retain-lock`，不读文件数据；真正发送（VAD 触发后）才逐张 `image_album_read → tuya_ai_image_input → 可选 tuya_ai_text_input → release`，目的是把上行内存峰值控制在单张图片大小，而不是队列里全部图片之和（队列容量 `WUKONG_PICTURE_INPUT_MAX_NUM`，默认 3）。
- **相册容量**：超过 `TUYA_PICTURE_ALBUM_MAX_IMAGE_CNT`（默认 10）时，`wukong_picture_save_to_album()` 会自动删除最旧的一张，无需上层维护容量。
- **上行完成事件 `WUKONG_AI_EVENT_SEND_PICTURE_END`**：`wukong_picture_input_from_album()`（`wukong_picture_input.c:190`）把入队图片逐张读取、通过 `tuya_ai_image_input()`（及可选的 `tuya_ai_text_input()`）发送完毕后，只要本轮**至少发送成功一张**（`has_data == TRUE`），就会触发该事件，`payload` 为 `NULL`；这是模块对外发布的第二个事件（第一个是下行的 `WUKONG_AI_EVENT_ACCEPT_PICTURE`），用于让上层知道"设备侧图片队列已发送完毕"，可据此收尾 UI 状态或释放相关资源。若本轮队列为空或全部读取失败，则不会触发。
- **坑：ACCEPT_PICTURE 需要各对话模式自行订阅**——目前 `hold / oneshot / wakeup / free / detection / picture` 六个 `wukong_ai_mode_*.c` 都各自订阅了 `WUKONG_AI_EVENT_ACCEPT_PICTURE` 并调用 `tuya_ai_display_msg()` 显示；本模块只负责发出事件，不负责分发到所有模式。新增一个对话模式时，若要支持下行生图展示，必须在该模式自己的事件分发里补上这个 case。
- **坑：显示管线已搬家**——`tuya_ai_display_msg()` / `TY_DISPLAY_TP_AI_IMAGE` 现在实现在 `src/miscs/display/`（UI 无关的消息总线），由新 UI 框架（`src/ui/ui_app.c` 或无 UI 时的 `src/ui/port/tuya_ai_display_stub.c`）注册 handler 消费；旧的 `src/miscs/gui/` 已随老 UI 一起删除，不要再按旧路径找。
- **坑：`ENABLE_TUYA_PICTURE` 不是所有 T5AI_BOARD 变体都默认开**——它挂在 `ENABLE_AI_MODE_PICTURE` 菜单下面。`T5AI_BOARD`、`T5AI_BOARD_DESKTOP` 默认打开；`T5AI_BOARD_EVB`、`T5AI_BOARD_EVB_PRO`、`T5AI_BOARD_ROBOT`、`T5AI_BOARD_EYES` 默认不开，需要在对应 board 的 appconfig 里显式打开。

## 改动指南

- **下行图片要换展示方式**（外接屏/串口转发/存到外部存储而不是默认 LCD）：在对应 `wukong_ai_mode_*.c` 的 `WUKONG_AI_EVENT_ACCEPT_PICTURE` 分支里，用 `wukong_picture_get_by_name()` 取到 JPEG 后接自定义逻辑，参考 `wukong_ai_mode_picture.c` / `wukong_ai_mode_detection.c` 现有写法。
- **要换相册存储后端**（内存 ↔ SD 卡掉电保存）：改 `src/wukong/picture/Kconfig` 里的 `ENABLE_IMAGE_ALBUM_STORAGE_MEM` / `ENABLE_IMAGE_ALBUM_STORAGE_SD`；两者可同时打开，实际使用哪个由 `image_album` 组件的存储类型参数决定。
- **要调相册容量 / 输出分辨率 / 上下行队列上限**：改 `src/wukong/picture/Kconfig` 里的 `TUYA_PICTURE_ALBUM_MAX_IMAGE_CNT`、`TUYA_PICTURE_DEF_OUTPUT_WIDTH/HEIGHT`、`WUKONG_PICTURE_INPUT_MAX_NUM`、`WUKONG_PICTURE_OUTPUT_MAX_NUM`，执行 `make app_config APP_NAME=tuyaos_demo_wukong_ai` 使其生效。
- **要让新对话模式支持图文上行**：调用 `wukong_picture_input_add_from_album()` 入队、`wukong_picture_input_from_album()` 在 VAD 触发后发送；若图片来源不是相册（如摄像头实时拍照），跳过队列直接调用 `wukong_ai_agent_send_image()` / `wukong_ai_agent_send_text()`（参考 `wukong_picture_input_recognize()`）。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
