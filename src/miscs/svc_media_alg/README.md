# svc_media_alg

## 概述

`svc_media_alg` 是从涂鸦 IPC（摄像机）代码库移植过来的图像/视频算法工具集。当前 wukong 应用中实际接入的只有移动侦测（`tuya_ipc_video_proc.c`，由 `src/mode/wukong_ai_mode_detection.c` 调用 `tuya_ipc_motion_init`/`tuya_ipc_motion`）；图像处理（`tuya_ipc_img_proc.c`：YUV 缩放、格式转换、画框、OSD）与二维码图像增强（`tuya_ipc_qrcode_proc.c`，配合 zbar 等三方库解码）目前未被应用任何代码调用，是随移植保留的可用工具函数。

三个子模块相互独立、无共享状态，按需单独引用对应头文件即可，不需要整体初始化。

## 设计要点与坑

- **移动侦测是经典帧差法**：背景/当前帧做差、二值化、腐蚀膨胀去噪、连通域标记找最大运动区域，可选多帧/超时抑制抖动（`MULTI_FRAME_FILTER`/`NO_MOTION_TIMEOUT`）。灵敏度（`sensitivity`，1-10）与阈值（`y_thd`，默认 30，弱光场景建议 5-30）是主要调参入口；4 种检测区域模式（单区域/矩阵多区域/自定义多框/多边形）通过 `rect_type` 切换，互斥使用同一份配置结构。
- **`tuya_ipc_video_proc.c` 用全局静态状态 + 互斥锁**（`g_motion_spec`/`g_motion_ctrl`），一个进程内只支持一路检测；`tuya_ipc_motion_init` 只应调用一次，运行时调整参数要用 `tuya_ipc_set_motion` 而不是重新 init。
- **`tuya_ipc_img_proc.c` 并非完全无状态**：三次插值缩放使用全局 scratch 缓冲 `g_scale_spec`（`Cubic_Scale_Init` 每次缩放 malloc/free 一轮），**并发调用不安全**；其余画框/格式转换函数是调用方供缓冲的同步计算，但内部不做越界校验，调用方需保证缓冲区与图像宽高一致。
- **应用侧接线自带节流**：`wukong_ai_mode_detection.c` 先跳过前 `MD_SKIP_FRAMES`(50) 帧再启动检测，实际使用 `y_thd=50`、`sensitivity=3`、单区域全幅模式，两次运动上报之间有 `MD_COOLDOWN_MS`(10s) 冷却——调灵敏度前先确认现象不是被这层节流吃掉的。
- **已知实现缺陷（移植保留，未修）**：`tuya_ipc_img_osd_ARGB1555` 的循环上界误用宽/高而非 `right/bottom`，`osdRect.left/top > 0` 时少处理区域；`tuya_ipc_img_get_rect` 输出尺寸硬编码 36×108，入参形同虚设；`tuya_ipc_qrcode_enhance` 返回 `tal_malloc` 的缓冲区，调用方负责释放。
- **现有文档曾提到"音频重采样"，代码库中未找到对应实现**——本模块目前只有图像/视频算法，不含音频处理。若后续确有音频重采样需求，应作为独立子模块接入，不与图像算法混在一起。

## 改动指南

- 调整移动侦测灵敏度/区域策略：`TUYA_MOTION_TRACKING_CFG_T` 各字段 + `tuya_ipc_video_proc.c` 对应处理分支。
- 接入图像缩放/格式转换/OSD 能力：直接引用 `tuya_ipc_img_proc.h`，本身已是独立可用的工具函数，无需额外初始化。
- 接入二维码识别：先用 `tuya_ipc_qrcode_enhance` 做图像增强/二值化，再送 zbar 等三方库解码，本模块只负责增强这一步。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
