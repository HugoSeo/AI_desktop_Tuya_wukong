# 视频模块

## 概述

视频模块为 Wukong AI 提供视频输入输出能力，采用**输入源层 + 服务层**两层架构：

- **输入源层**（`wukong_video_input`）：帧的来源。单一输入源（当前为板载摄像头 `video_input_camera.c`，依赖 `tal_camera`）实现 `VIDEO_INPUT_OPS_T` 并注册；业务代码不直接包含这个头文件，除非要实现新的输入源。
- **服务层**（`wukong_video_service`）：帧的去向。相机预览、移动侦测、P2P 通话、AI agent 上行等消费者以对等方式在此订阅/退订。

核心设计：某格式**首个订阅者**触发输入源 `start(fmt)`，**最后一个退订者**触发 `stop(fmt)`，消费者之间无需互相协调引用计数。未配置输入源（`USING_NONE_VIDEO_INPUT`）时所有接口安全降级（返回不支持，不崩溃），调用方无需 `#ifdef`。

接入方式：`make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai` 的 `Video Configuration` 菜单里选输入源（`USING_NONE_VIDEO_INPUT` / `USING_CAMERA_VIDEO_INPUT`，后者依赖 `ENABLE_TUYA_CAMERA`）。

## 设计要点与坑

- **snapshot 与流订阅正交**：`wukong_video_snapshot()` 是直通输入源的一次性 JPEG 抓拍，可以和任意已订阅的流并行；相机实现内部用独立的 `CAM_USER_SNAPSHOT` 位与服务层的 `CAM_USER_SERVICE` 位分开计数，抓拍不会影响、也不受服务层订阅计数影响。
- **回调在派发线程上跑，不得阻塞**：服务层 `__dispatch()` 在锁内只拷贝活跃回调列表，解锁后再逐个调用，允许回调内部再次 subscribe/unsubscribe 而不死锁；但回调本身仍运行在输入源的采集线程上，阻塞会拖慢底层摄像头驱动。
- **坑：`VIDEO_FRAME_T.data` 仅在回调期间有效**——`wukong_video_input.h` 明确注释"Data is valid only for the duration of the callback"；`data` 指向输入源内部的帧缓冲区，回调 `VIDEO_FRAME_CB` 返回后该指针即可能被复用/失效。跨线程处理、异步编码、或需要保留原始帧数据的消费者（如另起线程做检测/编码）必须在回调内自行拷贝整帧数据，不能只存指针留到回调外使用。`is_key_frame` 字段仅 `H264` 格式下有意义，其余格式恒为 `FALSE`。
- **坑：DVP 双流互斥，H264 和 MJPEG/YUV 会打架**——板载摄像头是 DVP 类型时，`H264` 只能和 `YUV422` 同时跑（`H264+YUV` 模式），`MJPEG` 只能和 `YUV422` 同时跑（`JPEG+YUV` 模式），两者硬件互斥。当某个消费者订阅 `VIDEO_FMT_H264` 时，`video_input_camera.c` 的 `__cam_start()` 会**主动把摄像头切到 H264+YUV 模式**，如果此时已有消费者在用 MJPEG 流（如相册拍照/预览），MJPEG 会被静默中断——服务层本身不做跨格式互斥检查，只按格式各自计数。新增会用到 H264（如 P2P）的消费者时，要留意是否与现有 MJPEG 消费者冲突。
- **坑：模式切换要重新申请内存，失败过会让整个相机失效**——`__switch_output_mode()` 底下的 `tal_camera_switch_output_mode()` 是「先 deinit 再 init」，重新 init 时要重新申请 yuv pingpong buf（H264 模式 `width*32*2`，JPEG 模式 `width*16*2`）。P2P 通话挂断时这一步跑在 P2P 会话资源释放**之前**，内存吃紧时会申请失败（底层打 `__dvp_yuv_buf_module_init malloc pingpong buf failed`）。失败后 `ctx->drv` 为空，此后所有取流都会失败。当前的兜底行为：切换失败重试 3 次（间隔 100ms）→ 仍失败则回滚到原模式（仅当原模式不比目标模式更吃内存，否则跳过，H264 pingpong 是 JPEG 的两倍，回滚必然也失败）→ 再失败则相机标记为 down，但**下一次模式切换会从头重建 DVP 自愈**，不需要重启。排查时优先看切换前后的空闲堆。
- **坑：`get_caps()` 返回的是静态硬件能力位图，不反映当前运行时占用的子模式**——DVP 类型下 `fmt_mask` 总是同时包含 `YUV422 | MJPEG | H264` 三个位，`wukong_video_available()` 因此不会因为当前正跑在 MJPEG 模式而对 H264 返回不可用；真正的互斥表现是运行时的流打断（见上一条），不是 `available()` 检测得到的静态错误。
- **ROBOT / EVB_PRO 有一条历史遗留的直连 LCD 通路**：这两个 board 在摄像头流起来时，帧会直接经 DMA2D 转换后推给 LCD（`__on_camera_lcd_display`），并在流启停时调用 `tuya_ai_display_pause()/resume()` 暂停主显示总线；这条路径是从旧的各 board `tuya_device_camera.c` 原样迁移过来的，不走 `wukong_video_service` 的消费者回调机制，其它 board 没有这条路径。

## 改动指南

- **要新增一路视频输入源**（如外接屏摄像头、软件生成的测试源）：实现 `VIDEO_INPUT_OPS_T`（`init/deinit/start/stop/get_caps`，`snapshot` 可选），在合适的初始化时机调用 `wukong_video_input_register()`；第二次注册会返回 `OPRT_COM_ERROR`，当前只支持单一输入源。
- **要新增一类视频消费者**：在 `wukong_video_service.h` 的 `VIDEO_CONSUMER_E` 里加一个枚举值，调用 `wukong_video_subscribe(fmt, consumer, cb, ctx)` / `wukong_video_unsubscribe()`；如果新消费者要用 `VIDEO_FMT_H264`，先确认 DVP 板子上是否有并存的 MJPEG 消费者（见上面的互斥坑）。
- **要调整摄像头能力参数**（分辨率/帧率）：改板级 `tuya_board_get_camera_cfg()`（每个 board 自己的实现），`__cam_get_caps()` 直接读取 `TUYA_AI_TOY_ISP_WIDTH/HEIGHT/FPS` 这几个板级宏。
- **要调试 DVP 双模式切换问题**：看 `video_input_camera.c` 里的 `__switch_output_mode()`（含重试次数/间隔 `CAM_MODE_SWITCH_TRY_MAX/MS`）和 `__cam_start/__cam_stop` 中对 `VIDEO_FMT_H264` 的特判；切换失败后的回滚与自愈逻辑在 `tal_camera.c` 的 `tal_camera_switch_output_mode()`。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
