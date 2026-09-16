# AVI 媒体服务

## 概述

本模块为 Wukong Demo 提供 MJPEG AVI 录制和视频解码，并在麦克风可用时附带 16kHz/16bit/单声道 PCM 音轨。录像保存到 `/sdcard/tuyaos/video/`；应用中心的“视频”页通过 Demo 文件服务列出录像，并把解码后的 RGB565 帧交给 `ui_comp_picture` 显示。

能力由 `ENABLE_TY_AVI_MEDIA` 和 `UI_FEATURE_VIDEO` 控制，仅支持使用 `libavi.a` 的 T5/BK7258 平台及已就绪的应用存储。

## 资源边界

- 存储挂载由 `wukong_storage` 统一负责。AVI 模块不挂载、卸载或扫描目录。
- 目录枚举和绝对路径拼接由 `ui_svc_fs` 的 `ui_fs_list_app_async()` / `ui_fs_path()` 完成；播放器只打开调用者传入的绝对文件路径。
- 摄像头帧经 `wukong_video_service` 的 `VIDEO_CONSUMER_AVI_RECORD` 订阅，禁止重复初始化 DVP/UVC。
- 播放器内核不直接访问 LCD、LVGL、DMA2D、ADC 或 DAC；视频帧和可选 PCM 音频均通过回调交给 Demo 服务，分别接入 `ui_comp_picture` 与共享音频输出。硬解需要的 YUV422→RGB565 转换同样不在内核里做：由 `TY_AVI_PLAYER_CFG_T.yuv422_to_rgb565` 注入，`ui_svc_video_playback.c` 传入 `tuya_dma2d_yuv422_to_rgb565`，DMA2D 引擎的唯一归属仍在 `miscs/display`。
- 构建参数 `TY_AVI_ENABLE_PLAYBACK=0` 仅关闭旧版直连硬件的 port 绑定；`ty_avi_player.c` 的回调式播放器仍会编译。
- T5 SDK 自带旧版单体 `libavi.a`。`local.mk` 会把本模块新版归档的对象提取到应用组件库，使其先于 SDK 旧库解析；不要再把新版归档复制到全局 `libs/app_libs/`，否则会产生重复 `AVI_*` 符号。

## 线程与缓冲所有权

- 录像上限为 15fps，JPEG 使用板级默认目标（T5AI_BOARD_DESKTOP 当前为 10–25KB）；回调只把带原始采集时间戳的 MJPEG 复制到最多 144 帧、总量 1024KB 的有界队列，写盘在 `ty_avi_rec` 线程完成。写盘拥塞时有效帧率会快速降到可持续区间，连续健康 3 秒后才逐级回升。
- 开始和停止录像不动态调整 JPEG 编码参数，也不重初始化 DVP；AVI 帧率按首末已保存帧的采集时间戳计算。
- PCM 先聚合为 100ms/3.2KB 音频块，最多排队 400 块并受 256KB 字节预算限制；队列拥塞导致 PCM 无法入队时写入等时长静音占位，避免后续声音向前压缩并累积音画偏差。存储故障触发安全停止后仍会有限地排空已缓存 PCM/静音，真实 I/O 写入失败则立即中止收尾。AVI 文件适配层再以 32KB PSRAM 顺序缓存合并块头和媒体数据，减少高频小写和 SD 恢复后的突发补写；所有 seek/close 都会先刷新缓存。
- 单次写入达到 2 秒才按严重慢写处理：返回后立即降帧并丢弃陈旧队列、仅保留最新帧；5 秒内第二次严重慢写重新形成高水位或累计第三次严重慢写，以及任意底层 I/O 失败，都会停止接收新帧并通知 `ui_svc_video` 异步安全收尾。
- 播放打开、关闭和控制运行于 `WORKQ_SYSTEM`，JPEG 解码运行于 `ty_avi_play` 线程；UI 回调统一封送到 UI 线程。
- 播放先解码首帧并准备音频输出，再启动媒体时钟；PCM 提前一个 20ms DAC 帧预缓冲，UI 音频队列限制在约 200ms。暂停会清空 UI 与 TAL 音频缓存，恢复时按精确音频字节位置重建两路时间轴。后续视频帧在解码完成后等待 PTS 显示，已经落后超过一个完整帧周期的帧会跳过。每 2 秒输出一次 `avi play stat`，包含实际显示 fps、平均/最大 JPEG 解码耗时、硬解/软解帧数和迟到帧数。
- JPEG 解码优先走硬件：`tkl_jpeg_codec_convert()` 只在输出 YUV422 且宽 32、高 8 对齐时才调度硬件块，且只能按原始尺寸解码，480x480 的录像正好满足。因此 YUV422 与 RGB565 两块源尺寸暂存缓冲在 `ty_avi_player_start()` 里一次性申请（480x480 各 450KB）、整段复用，只有交给回调的目标尺寸帧才逐帧申请。缺少转换钩子、几何不对齐、codec init 或暂存申请失败都只是回落到软解，不影响播放。孤立的单帧硬解失败只对该帧回落；连续 3 帧失败则整段关闭硬解，因为硬件转换要等满 500ms 信号量才报错，逐帧重试比直接软解更慢。纯软件 libjpeg-turbo 在 Cortex-M33 上没有 SIMD，480x480 单帧约 100ms，只靠软解无法跟上录像帧率。
- `TY_AVI_PLAYER_FRAME_T.data` 的所有权随帧回调转移，接收方必须调用 `ty_avi_player_frame_free()`。Demo 服务最多保留一个待投递帧，新帧会释放旧帧。
- 页面退出时先清空 `ui_comp_picture`、解绑服务回调，再异步停止播放器，避免迟到帧访问已销毁对象。
- 当前单段录像限制为 110 秒，为关闭文件和写入索引预留时间。

## 主要文件

- `src/miscs/tuya_avi_service/src/ty_avi_player.c`：AVI 读取、节流、暂停/跳转和 RGB565 帧回调。
- `src/miscs/tuya_avi_service/src/ty_avi_recorder_adapter.c`、`src/miscs/tuya_avi_service/recorder/`：共享相机流接入与 AVI 写入。
- `src/ui/services/ui_svc_video.c`：Demo 录像状态机与文件命名。
- `src/ui/services/ui_svc_video_playback.c`：录像枚举、播放状态机和 UI 线程封送。
- `src/ui/pages/ui_page_video.c`：视频列表和播放界面。

启用新板型时，同时配置摄像头、存储、`ENABLE_TY_AVI_MEDIA`、`ENABLE_TAL_IMAGE` 和 `UI_FEATURE_VIDEO`，再运行 `make app_config APP_NAME=tuyaos_demo_wukong_ai`。
