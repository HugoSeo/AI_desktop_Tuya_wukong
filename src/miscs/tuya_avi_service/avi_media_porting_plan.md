# AVI 录制/播放功能移植方案

> 基于 `video_player_example`（bk_avdk_smp）向 `bk7258_tuya_smp` 移植  
> 版本：**v2.1（已实现）** | 日期：2026-07-24  
> 组件目录：`application_components/tuya_avi_service`

> **历史方案说明（Wukong Demo 集成以 `README.md` 和当前代码为准）**：下文记录模块原始的独占硬件方案。当前 Demo 已裁剪 ADC/DAC、LCD/DMA2D、存储挂载和目录扫描：录制使用共享相机流，目录与路径统一经 `ui_svc_fs`，播放改为无音频 RGB565 帧回调，由 `ui_page_video.c` 渲染。不要按下文的直连 LCD、音频或 `/sdcard` 扫描设计恢复资源所有权。

**当前状态（已完成）**：

| 能力 | 状态 |
|------|------|
| AVI 录制（DVP MJPEG + MIC PCM） | ✅ |
| 录制预览（LCD / 耳机监听，可独立开关） | ✅ |
| AVI 播放（LCD + 扬声器） | ✅ |
| 音画同步（录制写盘 + 播放节流） | ✅ |
| 播放控制（暂停/继续/跳转/快进/快退） | ✅ |
| 播放列表（扫描 SD 卡 / 按索引 / 上下首） | ✅ |
| 播放进度日志（1s 进度条） | ✅ |
| GFW 上位机测试（dev=8/9） | ✅ |
| Port 抽象层（`avi_port`） | ✅ |

音频路径：**仅** `tkl_aud_adc` / `tkl_aud_dac`（经 `audio_port` 绑定），不再使用 `tkl_mic` / `tkl_speaker`，非 `tkl_ai`。

---

## 1. 背景与目标

### 1.1 参考工程

- **路径**：`/bk_avdk_smp/projects/video_player_example`
- **功能**：
  1. 录制 AVI 文件到 SD 卡（DVP 视频 + MIC 音频）
  2. 从 SD 卡读取 AVI 并播放到 LCD + 扬声器
  3. 播放列表、暂停/继续、快进/快退、循环播放等

### 1.2 当前项目已有能力

| 能力 | 现有实现 | 相关文件 |
|------|----------|----------|
| AVI 录制 | DVP MJPEG + ADC PCM → `libavi.a` | `ty_avi_recorder_adapter.c`, `ty_avi_audio.c` |
| AVI 播放 | `libavi.a` + JPEG 硬解 + DAC | `ty_avi_player.c` |
| 音频采集（AVI） | `audio_port.mic_*` → `tkl_aud_adc`（16k/mono/PCM，DMA 回调） | `avi_port.c`, `ty_avi_audio.c` |
| 音频播放（AVI） | `audio_port.spk_*` → `tkl_aud_dac`（含 PA GPIO） | `avi_port.c`, `ty_avi_player.c`, `ty_avi_audio.c` |
| DVP 视频 | JPEG/YUV422 帧回调 | `tal_dvp.c`, `ty_avi_recorder_adapter.c` |
| LCD 显示 | YUV422 → `media_opt_port.dma2d_*` → RGB565 → flush | `ty_avi_recorder_adapter.c`, `ty_avi_player.c` |
| JPEG 编解码 | `media_opt_port.jpeg_*` → `tkl_jpeg_codec` | `avi_port.c`, `tkl_jpeg_codec.h` |
| SD 卡文件 | `file_opt_port.*` → `tkl_fs` + `ty_video_osi_wrapper` | `avi_port.c`, `ty_video_osi_wrapper.c` |
| Port / 系统 | `sys_port`（tick / sleep / PSRAM / GPIO） | `avi_port.h`, `avi_port.c` |
| Ringbuffer | `avi_rb_*`（组件内实现） | `rb.c`, `rb.h` |
| 测试入口 | GFW `dev=8/9` | `test_media.c`, `test_media_avi_*.c` |

### 1.3 移植目标（裁剪后）— 均已实现

| 能力 | 状态 |
|------|------|
| AVI 容器 + MJPEG + PCM 16k/mono | ✅ |
| 录制 + 独立音视频预览 | ✅ |
| 播放 + pause/resume/seek/ff/rewind | ✅ |
| SD 卡播放列表 scan/play/next/prev | ✅ |
| MP4 / H264 / AAC 等 | ❌ 不移植 |

### 1.4 已落地模块一览

| 模块 | 路径 | 说明 |
|------|------|------|
| Port 抽象 | `tuya_avi_service/include/avi_port.h`, `src/miscs/tuya_avi_service/src/avi_port.c` | 收敛 TKL 绑定：`audio_port` / `file_opt_port` / `media_opt_port` / `sys_port` |
| 录制核心 | `tuya_avi_service/recorder/` | `ty_video_recorder_ctlr` 写线程 + `libavi` |
| 录制适配 | `tuya_avi_service/src/ty_avi_recorder_adapter.c` | DVP 队列、Bresenham 抽帧、LCD 预览 |
| 音频 | `tuya_avi_service/src/ty_avi_audio.c` | ADC DMA 回调 → capture RB → 采集线程 → AVI RB + 可选 DAC 监听 |
| 播放 | `tuya_avi_service/src/ty_avi_player.c` | 视频/音频/进度 三线程 + 墙钟同步 |
| 播放列表 | `tuya_avi_service/src/ty_avi_playlist.c` | 扫描 `/sdcard/*.avi`，最多 32 个 |
| OSI 适配 | `tuya_avi_service/src/ty_video_osi_wrapper.c` | `file_opt_port` / `sys_port` + PSRAM 索引缓冲 |
| Ringbuffer | `tuya_avi_service/src/rb.c` | `avi_rb_*`，PSRAM 分配经 `sys_port` |
| 测试 | `ty_gfw_testsuites/.../test_media_avi_*.c` | GFW dev=8 录制 / dev=9 播放 |

**关键修复记录**：

- **libavi 升级**：对齐 `video_player_example` 新版 API（`AVI_open_input_file(path, idx, mem)`、`AVI_read_next_video_frame` / `AVI_read_next_audio_chunk`、`AVI_open_output_file`）
- **录制帧率瓶颈**：写线程改为视频优先 + 音频非阻塞拉取，避免每帧等 8×640B 音频
- **录制目标 15fps**：DVP 硬件 ~20fps，Bresenham 抽帧入队，避免时间节流与 DVP 不同步，并匹配当前 JPEG 解码能力
- **录制音画同步**：音频按墙钟 `elapsed_ms × 32B/ms` 写入；关文件时 `AVI_update_video_frame_rate_by_duration`
- **播放音画同步**：时长以音频 PCM 字节数为准，视频帧均匀铺在 `duration_ms` 上；音频先等 PTS 再写 DAC
- **暂停恢复**：先冻结 `playback_base_ms` 再置 `paused`，并同步 AVI 视频读指针
- LCD 预览：`static s_lcd_frame` 避免异步 flush 花屏
- **Port 抽象（v2.1）**：业务代码经 `avi_port` 调用 TKL；音频仅保留 ADC/DAC 路径；ringbuffer 接口由 `tkl_rb_*` 更名为 `avi_rb_*`

## 2. video_player_example 架构分析

### 2.1 整体分层

```
┌─────────────────────────────────────────────────────────┐
│  CLI 层（video_recorder_cli / video_play_playlist_cli） │  ← 需重写为 Tuya 适配
├─────────────────────────────────────────────────────────┤
│  Port 层（avi_port：audio / file / media / sys）         │  ← 绑定 tkl_aud_adc/dac 等
├─────────────────────────────────────────────────────────┤
│  核心层（ty_video_recorder / ty_avi_player）             │  ← 裁剪后移植，文件 ty_ 前缀
├─────────────────────────────────────────────────────────┤
│  基础库（libavi.a / avilib.h）                          │  ← 直接拷贝
└─────────────────────────────────────────────────────────┘
```

### 2.2 录制链路

```
DVP 回调(MJPEG帧)
    │ 非阻塞入队（满则丢帧）
    ▼
帧队列 ──────────────────────────────┐
                                     ▼
MIC 采集 ──► audio_recorder_device ──► get_audio_cb
                                     │
                            ty_video_recorder 写线程
                                     │
                            avi_write_video/audio
                                     │
                            libavi.a → SD卡 .avi
```

**关键设计**：
- DVP 回调必须快速返回，编码帧通过队列转交录制线程
- 录制线程通过 `get_frame_cb` / `get_audio_cb` 拉取数据
- 写完后通过 `release_frame_cb` / `release_audio_cb` 释放缓冲

### 2.3 播放链路

```
SD卡 .avi
    │
    ▼
avi_parser（解复用，读 MJPEG 帧 + PCM 块）
    │
    ├──► JPEG 解码器 ──► video_decode_complete_cb ──► LCD flush
    │
    └──► PCM 直通（无需解码）──► audio_decode_complete_cb ──► 扬声器

ty_video_player_engine / playlist
    ├── pause / resume / seek / ff / rewind
    ├── 音画同步（audio clock 驱动）
    └── 播放列表管理
```

**PCM 零拷贝**：播放器检测到 `WAVE_FORMAT_PCM` 时，不解码，直接将 parser 读出的 PCM 块交给 `audio_decode_complete_cb`。

### 2.4 核心 SDK 组件清单

| 组件 | 源路径（leding SDK） | 大小 | 说明 |
|------|---------------------|------|------|
| `libavi.a` | `ap/components/bk_libs/bk7258_ap/libs/` | 预编译库 | AVI 读写封装 |
| `avilib.h` | `ap/include/modules/` | 头文件 | AVI API |
| `ty_video_recorder`（源自 `bk_video_recorder`） | `ap/components/bk_video_recorder/` | ~104KB, 4个.c | 录制器（含写线程） |
| `ty_video_player`（源自 `bk_video_player`） | `ap/components/bk_video_player/` | ~572KB, 20个.c | 播放器引擎+列表 |

---

## 3. 数据源适配对照表

| 能力 | video_player_example | bk7258_tuya_smp | 适配方式 |
|------|---------------------|-----------------|----------|
| 视频采集 | `bk_camera_ctlr` DVP MJPEG | `tal_dvp` → `TUYA_FRAME_FMT_JPEG` | `dvp_frame_handle` 非阻塞入队 |
| 录制预览 | YUV → `bk_display_flush` | YUV422 → `tkl_dma2d` → `tal_display_flush` | 复用 `test_media_camera.c` |
| 音频采集 | `audio_recorder_device`（bk_audio_pipeline） | `tkl_aud_adc`（DMA 回调）→ capture RB | `audio_port` + `ty_avi_audio.c`，16k/mono/640B@20ms |
| 音频播放 | `audio_player_device`（raw_stream → speaker） | `tkl_aud_dac` + PA GPIO | `audio_port`；录制预览 / 播放线程写 DAC |
| 显示输出 | `bk_display_flush` + `frame_buffer` | `tal_display_flush` + `ty_frame_buffer_t` | 回调中做格式转换 |
| JPEG 解码 | `bk_jpeg_decoder` hw/sw | `media_opt_port.jpeg_*` → `tkl_jpeg_codec` | 播放线程内硬解 |
| 文件系统 | `bk_vfs` `/sd0` | `file_opt_port.*` → `tkl_fs` `/sdcard` | 路径映射 |
| SD 卡挂载 | 示例内 mount | `test_fs_mount("/sdcard")` | 复用现有逻辑 |

---

## 4. 推荐方案

### 4.1 策略

**移植核心组件 + 新建 Tuya 适配层**，不引入 Beken 专有框架（`bk_audio_pipeline`、`bk_display`、`frame_buffer` 等）。

### 4.2 目标目录结构

```
application_components/tuya_avi_service/      # 当前实际目录
├── include/
│   ├── avi_port.h                  # Port 抽象（audio/file/media/sys）
│   ├── ty_avi_recorder.h
│   ├── ty_avi_player.h
│   ├── ty_avi_audio.h
│   ├── ty_avi_playlist.h
│   └── ty_video_recorder.h
├── src/
│   ├── avi_port.c                  # TKL 函数指针绑定表
│   ├── ty_avi_recorder_adapter.c
│   ├── ty_avi_player.c
│   ├── ty_avi_playlist.c
│   ├── ty_avi_audio.c
│   ├── ty_video_osi_wrapper.c
│   ├── rb.c / rb.h                 # avi_rb_* ringbuffer
│   └── ty_video_osi_wrapper.h
├── recorder/
│   ├── include/ty_video_recorder_*.h
│   ├── ty_video_recorder.c
│   ├── ty_video_recorder_ctlr.c
│   └── ty_video_recorder_avi.c
├── third_party/
│   ├── avilib.h
│   ├── avilib_adp.h
│   └── libavi.a
├── local.mk
└── avi_media_porting_plan.md

application_components/ty_gfw_testsuites/src/test_media/
├── test_media_avi_record.c
├── test_media_avi_play.c
└── test_media.c                    # GFW dev 分发
```

### 4.3 Kconfig 裁剪

```kconfig
# 保留
CONFIG_TY_VIDEO_PLAYER_ENABLE_AVI_PARSER=y

# 关闭（不编译）
CONFIG_TY_VIDEO_PLAYER_ENABLE_MP4_PARSER=n
CONFIG_TY_VIDEO_PLAYER_ENABLE_HW_JPEG_DECODER=n   # 用 tkl_jpeg_codec 替代
CONFIG_TY_VIDEO_PLAYER_ENABLE_SW_JPEG_DECODER=n
CONFIG_TY_VIDEO_PLAYER_ENABLE_AAC_DECODER=n
CONFIG_TY_VIDEO_PLAYER_ENABLE_G711_DECODER=n
CONFIG_TY_VIDEO_PLAYER_ENABLE_G722_DECODER=n
# TY_VIDEO_RECORDER_TYPE_MP4 相关代码 #ifdef 剔除
```

> **命名约定**：从 leding SDK 拷贝源码后，文件名、头文件、结构体/函数前缀统一改为 `ty_`（如 `bk_video_recorder_open` → `ty_video_recorder_open`），仅 `third_party/` 下的 `avilib` 保持原名。

### 4.4 Port 抽象（`avi_port`）

业务模块不直接调用 `tkl_*`，统一经全局 port 结构体：

| 全局对象 | 职责 | 典型绑定 |
|----------|------|----------|
| `audio_port` | MIC/SPK | `tkl_aud_adc_*` / `tkl_aud_dac_*` |
| `file_opt_port` | 文件系统 / 目录 | `tkl_fopen` / `tkl_dir_*` / `tkl_fs_mount` 等 |
| `media_opt_port` | DMA2D / JPEG | `tkl_dma2d_*` / `tkl_jpeg_codec_*` |
| `sys_port` | tick / sleep / PSRAM / GPIO | `tkl_system_*` / `tkl_gpio_*` |

**音频约定（v2.1）**：只支持 ADC/DAC 路径；已移除 `tkl_mic` / `tkl_speaker` 及 `TY_AVI_USE_TKL_AUD_ADC_DAC` 条件编译。更换平台时优先改 `avi_port.c` 绑定表。

---

## 5. AVI 文件格式约定

| 字段 | 值 | 说明 |
|------|-----|------|
| 容器 | AVI | `libavi.a`（新版 API，带 index） |
| 视频编码 | MJPEG（`MJPG`） | DVP 硬件 JPEG 直出 |
| 音频编码 | PCM（`0x0001`） | 16 kHz / 16 bit / mono |
| 分辨率 | 480 × 480 | GC2145 DVP |
| DVP 输出 | ~20 fps | 硬件帧率 |
| **录制目标** | **15 fps** | Bresenham 抽帧后写入 AVI |
| 音频输入/写盘块 | 640 B / 20 ms；聚合为 3200 B / 100 ms | 减少 AVI 小块写 |
| 存储路径 | `/sdcard/xxx.avi` | `tkl_fs` 挂载点 |

关文件时按**实际已保存帧时间戳跨度**回写头部 fps（`AVI_update_video_frame_rate_by_duration`），避免头里写死目标 fps 与真实帧密度不符。

---

## 6. 已实现方案总览与技术细节

### 6.1 端到端流程

```
┌──────────── 录制 ────────────┐     ┌──────────── 播放 ────────────┐
│ DVP JPEG ──► 有界缓存(18帧)  │     │ SD .avi                      │
│      YUV ──► LCD预览(可选)    │     │   ├─ avi_v: JPEG→YUV→RGB→LCD │
│ ADC DMA ──► capture RB       │     │   ├─ avi_a: PCM→DAC          │
│   └─► avi_audio ──► AVI RB   │     │   └─ avi_prog: 1s进度条日志   │
│         └──► DAC 监听(可选)  │     │ ty_avi_playlist: 扫描/切歌    │
│ 写线程 ──► libavi ──► SD卡   │     │                              │
└──────────────────────────────┘     └──────────────────────────────┘
         GFW dev=8                              GFW dev=9
```

**线程一览**：

| 场景 | 线程名 | 职责 |
|------|--------|------|
| 录制 | `ty_avi_rec` | 从队列取 JPEG 写视频 + 墙钟同步写音频 |
| 录制 | `avi_audio` | 从 capture RB 取 PCM → AVI RB（+ 可选 DAC 预览） |
| 录制 | ADC DMA 回调 | `tkl_aud_adc` ISR → 写入 capture RB（须快速返回） |
| 录制 | DVP 回调 | JPEG 入队 / YUV 预览（同线程，须快速返回） |
| 播放 | `avi_v` | 读帧 → JPEG 硬解 → DMA2D → LCD |
| 播放 | `avi_a` | 读 PCM → 按 PTS 节流 → `audio_port.spk_write`（DAC） |
| 播放 | `avi_prog` | 每 1s 打印进度条日志 |

---

### 6.2 录制：视频流处理

**源**：`tal_dvp`，模式 `TUYA_CAMERA_OUTPUT_JPEG_YUV422_BOTH`（录像与预览共用一路 DVP）。

**回调 `ty_avi_video_frame_cb`**（`ty_avi_recorder_adapter.c`）：

1. 共享相机服务投递 **JPEG 帧**，预览仍由相机页独立订阅同一路 DVP 流。
2. JPEG 经 Bresenham 抽帧（目标 15fps，源 fps 由统计动态估计约 18–20）后复制到 PSRAM，再通过**非阻塞** `tal_queue_post` 交给写线程。
3. 视频缓存同时受 144 帧槽位和 1024KB 字节预算限制，用于吸收数秒级写盘抖动；单次写入达到 2 秒才按严重慢写处理，返回后立即降帧并丢弃旧队列、仅保留最新帧。第二次严重慢写重新形成高水位或 5 秒内累计第三次严重慢写时停止录像。音频以 100ms 聚合块进入独立 256KB 缓存。

**抽帧算法**（`ty_avi_should_enqueue_frame`）：

```
每来一帧 DVP：decim_acc += target_fps
若 decim_acc >= src_fps：decim_acc -= src_fps，允许入队
否则：丢弃（不计入 AVI，省 malloc）
```

**写线程取帧**（`get_frame_cb`）：`tal_queue_fetch` 弹出 JPEG → 交给 `ty_avi_write_video_frame`。

**统计日志**（约 1s 一次）：

```
avi rec fps tgt=15 eff=15 src=20 enq=15 avi=15 drop=0 skip=5 q=0/144 cache=0/1024KB wr=90KB/s v=8ms
```

---

### 6.3 录制：音频流处理

**采集**：`ty_avi_audio_rec_start()` → `audio_port.mic_init/start`（`tkl_aud_adc`）+ 启动 `avi_audio` 线程。

```
tkl_aud_adc DMA 回调(640B)
        │
        ▼
capture RB(16KB) ──► avi_audio 线程
                        ├─► [可选] audio_port.spk_write（DAC 预览 + PA GPIO）
                        └─► AVI RB(256KB) ──► get_audio_cb 非阻塞读取
```

- 采样：16 kHz / mono / 16 bit PCM
- 块大小：640 B = 20 ms（`frame_time_ms=20`，与播放侧 chunk 对齐）
- **预览独立**：`enable_audio_preview` 仅控制 DAC 监听，不影响 ADC → AVI RB
- `get_audio_cb` → `ty_avi_audio_rec_try_read()`，AVI RB 不够则本圈不写音频（不阻塞视频）
- 增益：UI volume 经 `ty_avi_audio_map_gain()` 映射到 ADC/DAC 硬件增益

---

### 6.4 录制：AVI 打包与写盘同步

**写线程**（`ty_video_recorder_ctlr.c`，`ty_avi_rec`）主循环：

```
while (recording) {
    1. get_frame_cb → 有 JPEG 则 AVI_write_frame（视频优先）
    2. ty_recorder_sync_audio(elapsed_ms)  // 每圈都跑，队列空时也追音频
    3. 无视频则 sleep(5ms)
}
```

**音频墙钟同步**（核心）：

```
bytes_per_ms = 16000 × 2 / 1000 = 32
target_bytes = elapsed_ms × 32
while audio_bytes_written + 3200 <= target_bytes:
    从队列读 100ms/3200B 聚合块 → AVI_write_audio
```

保证：**写入 AVI 的 PCM 总字节数 ≈ 录制经过的毫秒数 × 32**，不跟“每帧视频配 N 块音频”绑定，避免视频队列延迟导致音频超前。

**关文件**（`ty_avi_record_stop`）：

1. 最后一次 `sync_audio(duration_ms)` 补齐尾部 PCM
2. `AVI_update_video_frame_rate_by_duration(avi, duration_ms)`  
   → 头部 fps = `frame_count × 1000 / duration_ms`
3. `AVI_close`

**日志示例**：

```
avi record close frames=180 dur=12050ms fps=14.94 audio=385600B
```

（`audio` 应 ≈ `dur_ms × 32`，即 12050×32 ≈ 385600）

---

### 6.5 播放：视频流处理

**线程 `avi_v`**（`ty_avi_player.c`）：

```
loop:
  暂停 → sleep
  等待 frame_pts = video_frame_idx × duration_ms / total_frames
  AVI_read_next_video_frame → JPEG 校验(FF D8)
  media_opt_port.jpeg_img_info_get + jpeg_convert → YUV422
  media_opt_port.dma2d_convert YUV422→RGB565（居中裁剪到 LCD）
  tal_display_flush
  video_frame_idx++
```

- JPEG 缓冲：PSRAM 256 KB（`sys_port.psram_malloc`）
- `avi_mutex` 保护 `libavi` 读操作（与音频线程、seek 互斥）

---

### 6.6 播放：音频流处理

**线程 `avi_a`**：

```
audio_port.spk_init/start（tkl_aud_dac）+ PA GPIO 拉高
loop:
  暂停 → sleep
  seek 时：sleep(80ms) + 写一帧静音（DAC 无 pause/flush API）
  AVI_read_next_audio_chunk(pcm, 4096)
  chunk_start_pts = played_bytes × 1000 / byterate
  wait_until(elapsed_ms >= chunk_start_pts)   // 先等 PTS
  ty_avi_player_dac_write(pcm, n)             // 按 frame_size 切块 + BUSY 重试
  audio_played_bytes += n
audio_port.spk_stop/deinit + PA 拉低
```

**要点**：先节流、后写入；DAC 写经 `audio_port.spk_get_frame_size` 切块并处理 `OPRT_OS_ADAPTER_DAC_BUSY`，避免缓冲堆积导致“听起来音频落后”。

---

### 6.7 播放：音画同步策略

| 项目 | 策略 |
|------|------|
| **主时长** | 有音频时 `duration_ms = audio_bytes × 1000 / 32000`；无音频则用 `frames/fps` |
| **视频 PTS** | `frame_idx × duration_ms / total_frames`（按音频时长均匀铺帧，**不用** header fps） |
| **音频 PTS** | `played_bytes × 1000 / byterate` |
| **墙钟** | `elapsed = playback_base_ms + (now - play_start_ms)`；暂停时冻结 `playback_base_ms` |
| **打开日志** | `header_fps=15.0 eff_fps=11.8` — 若二者差大，旧方案会视频超前；现已按 `eff_fps` 铺帧 |

**暂停 / 继续**：

- pause：先读 `elapsed` → 写入 `playback_base_ms` → `AVI_set_video_read_index` 对齐帧号 → 再 `paused=1`
- resume：重置 `play_start_ms`，保持 `playback_base_ms`

**Seek / 快进 / 快退**：

- `AVI_set_video_read_index(frame)`，`frame = time_ms × total_frames / duration_ms`
- 音频：`AVI_set_audio_read_chunk`，按字节偏移二分查找 chunk
- 暂停时 seek：解码显示一帧预览，读指针回退，resume 从该帧继续

---

### 6.8 播放：进度条日志

**线程 `avi_prog`**，每 **1 秒** `PR_NOTICE`：

```
avi play 00:12/01:05 [========>-----------] 18%
avi play 00:12/01:05 [========>-----------] 18% PAUSED
```

- 格式：`当前分:秒 / 总分:秒 [20格进度条] 百分比`
- 打开 / 暂停 / 继续 / seek 时也会立即打一条

---

### 6.9 播放列表

`ty_avi_playlist.c`：

- `ty_avi_playlist_scan("/sdcard")`：`file_opt_port.dir_*` 枚举 `.avi`，排序，最多 32 个
- `ty_avi_playlist_play(index, vol)`：停当前 → `ty_avi_player_start`
- `play_next` / `play_prev`：循环切歌

---

## 7. 分阶段实施记录（均已完成）

| 阶段 | 内容 | 状态 |
|------|------|------|
| P0 | libavi + ty_video_recorder 核心 | ✅ |
| P1 | 录制适配（DVP + MIC + 预览 + 墙钟音频同步） | ✅ |
| P2 | 精简播放器（双线程 + JPEG 解码 + LCD/喇叭） | ✅ |
| P3 | 播放控制 + seek/ff/rewind + 进度条 | ✅ |
| P4 | 播放列表 scan/play/next/prev | ✅ |
| P5 | GFW 测试入口 dev=8/9 | ✅ |
| P6 | `avi_port` 抽象 + 音频收敛为 ADC/DAC + `avi_rb_*` | ✅ |

---

## 8. 风险与对策

| 风险 | 影响 | 对策 |
|------|------|------|
| DVP 回调阻塞 | 丢帧、画面卡顿 | 18 帧/128KB 有界缓存 + 非阻塞 push；内存达到上限时主动丢帧 |
| SD 卡写入慢 | 录制卡顿、文件损坏 | 音视频写盘与采集解耦；PCM 100ms 聚合 + AVI 32KB 顺序缓存；秒级慢写时降帧并仅保留最新帧，连续异常或 I/O 失败时安全关闭 AVI |
| 音画不同步 | 观感差 | 录制：墙钟音频字节同步 + 关文件修正 fps；播放：以音频时长铺视频帧 + 先等 PTS 再写 DAC |
| 录制/播放资源冲突 | 崩溃或功能异常 | 全局状态机 `IDLE → RECORDING → PLAYING`，切换前先 stop |
| avilib 与 FS 不兼容 | 文件读写失败 | `ty_video_osi_wrapper` + `file_opt_port` 封装；检查 `avilib_adp.h` |
| JPEG 硬解宽度对齐 | 画面错位 | `tkl_jpeg_codec` 自动 32 对齐；显示按实际宽高 flush |
| PSRAM 内存不足 | 分配失败 | 视频最多 18 帧且不超过 128KB，音频不超过 32KB；分配失败只丢当前帧，停止时排空并成对释放 |
| 与 AI 语音服务冲突 | ADC/DAC 被占用 | 录制/播放前检查并停止 AI 服务；或互斥锁 |
| DAC BUSY / 无 flush | 播放破音或 seek 残留 | `spk_write` BUSY 重试；seek 时 sleep + 写静音冲刷 |

---

## 9. 与示例工程差异总结

### 已移植（精简实现）

- AVI 封装/解封装（新版 `libavi.a`）
- 录制写线程（`ty_video_recorder_ctlr.c`）+ 墙钟音频同步
- 精简播放器（`ty_avi_player.c`，三线程）
- 播放列表（`ty_avi_playlist.c`，扫描 SD 卡）
- 文件 IO 适配（`ty_video_osi_wrapper.c`）
- Port 抽象（`avi_port.h` / `avi_port.c`）

### 设备适配层（已重写）

| 示例组件 | Tuya 替代 |
|----------|-----------|
| `audio_recorder_device` | `audio_port` → `tkl_aud_adc` + `ty_avi_audio.c` |
| `audio_player_device` | `audio_port` → `tkl_aud_dac` + 播放线程节流 / PA GPIO |
| `bk_display_flush` | `tal_display_flush` + `media_opt_port.dma2d_*` |
| `bk_camera_ctlr` DVP | `tal_dvp` |
| `bk_jpeg_decoder` | `media_opt_port.jpeg_*` → `tkl_jpeg_codec` |
| `bk_video_player_engine` 多线程 pipeline | `ty_avi_player` 三线程精简版 |
| 直接 `tkl_*` 散落调用 | 收敛到 `avi_port.c` 绑定表 |

### 未移植（有意裁剪）

- MP4 容器、`bk_video_recorder_mp4.c`
- AAC / G711 / G722 音频解码
- H264 视频解码
- `bk_audio_pipeline` 全框架
- Legacy `avi_player.c`

---

## 10. 上位机 GFW 测试命令（完整说明）

测试入口：`ty_gfw_testsuites` → `TEST_SUITE_MEDIA_TEST (0x58)` → `test_suites_media_test()`。

编译开关：`ENABLE_GFW_MEDIA_TEST=1` 且 `ENABLE_TY_AVI_MEDIA=1`（`tuya_avi_service/local.mk` 已定义）。

### 10.1 帧格式

```
55 AA 00 EE [len_hi len_lo] [payload...] [crc]
```

| 字段 | 说明 |
|------|------|
| `55 AA` | 帧头 |
| `00 EE` | GFW 测试套件主命令 |
| `len_hi len_lo` | **payload 长度**（大端），从 `0x58` 到 `data[]` 末尾 |
| `payload` | `MEDIA_CMD_S` 结构体 |
| `crc` | 从 `0x55` 到 payload 最后一字节累加，取低 8 位 |

**CRC 计算示例（Python）**：

```python
frame = [0x55, 0xAA, 0x00, 0xEE, 0x00, 0x05, 0x58, 0x01, 0x09, 0x01, 0x32]
crc = sum(frame) & 0xFF  # → 0x87
```

### 10.2 MEDIA_CMD_S（payload 结构）

```c
typedef struct {
    uint8_t cmd;   // 固定 0x58
    uint8_t opt;   // 操作码
    uint8_t dev;   // 8=录制, 9=播放
    uint8_t len;   // data[] 有效字节数
    uint8_t data[];// 参数（可选）
} MEDIA_CMD_S;
```

**设备号**：

| dev | 宏 | 功能 |
|-----|-----|------|
| `0x08` | `DEV_AVI_RECORD` | AVI 录制 |
| `0x09` | `DEV_AVI_PLAY` | AVI 播放 |

**文件路径规则**（`test_media_build_avi_path`）：

| 传入 | 实际路径 |
|------|----------|
| 不传 / 空 | `/sdcard/record.avi` |
| `test.avi` | `/sdcard/test.avi` |
| `/sdcard/foo.avi` | 原样 |

**ASCII 文件名编码**：字符串逐字节转 hex，如 `record1.avi` →  
`72 65 63 6F 72 64 31 2E 61 76 69`

---

### 10.3 AVI 录制（dev = 8）

| opt | 含义 | len / data | 行为 |
|-----|------|------------|------|
| `0` | 关闭 | `0` | 停止录制，释放 LCD |
| `1` | 开启 | 见下表 | 开始录制 |
| `2` | 视频预览 | `1`，`data[0]`=0/1 | 录制中开关 LCD（DVP 不停） |
| `3` | 音频预览 | `1`，`data[0]`=0/1 | 录制中开关耳机监听（MIC 不停） |

**opt=1 开启时 data 布局**：

| len | data 含义 |
|-----|-----------|
| `0` | 无预览，默认 `record.avi` |
| `1` | `data[0]` 同时控制视频+音频预览（兼容旧协议） |
| `2` | `data[0]`=视频预览，`data[1]`=音频预览，默认文件名 |
| `≥3` | `data[0]`=视频，`data[1]`=音频，`data[2..]`=文件名 ASCII |

**固定录制参数**（上位机不可改）：480×480 MJPEG，目标 15fps，PCM 16k/mono/16bit。

#### 录制命令 demo

| 场景 | payload（hex） | 完整帧（含 CRC） |
|------|----------------|------------------|
| 默认文件，无预览 | `58 01 08 00` | `55 AA 00 EE 00 04 58 01 08 00 51` |
| 默认文件，音视频预览 | `58 01 08 02 01 01` | `55 AA 00 EE 00 06 58 01 08 02 01 01 57` |
| 仅视频预览 | `58 01 08 02 01 00` | `55 AA 00 EE 00 06 58 01 08 02 01 00 56` |
| 仅音频预览 | `58 01 08 02 00 01` | `55 AA 00 EE 00 06 58 01 08 02 00 01 56` |
| **`record1.avi` 仅视频预览** | `58 01 08 0D 01 00 72..69` | `55 AA 00 EE 00 11 58 01 08 0D 01 00 72 65 63 6F 72 64 31 2E 61 76 69 8B` |
| **`record1.avi` 无预览** | `58 01 08 0D 00 00 72..69` | `55 AA 00 EE 00 11 58 01 08 0D 00 00 72 65 63 6F 72 64 31 2E 61 76 69 8A` |
| `test.avi` 音视频预览 | `58 01 08 0A 01 01 74..69` | `55 AA 00 EE 00 0E 58 01 08 0A 01 01 74 65 73 74 2E 61 76 69 95` |
| 关闭录制 | `58 00 08 00` | `55 AA 00 EE 00 04 58 00 08 00 51` |

`record1.avi` payload 拆解：`0D`=13 = 2 预览字节 + 11 文件名字节；`01 00` = 仅视频预览。

**预期日志**：`avi record open ret=0 file=/sdcard/record1.avi video_preview=1 audio_preview=0`  
关录制：`avi record close frames=xxx dur=xxxms fps=xx.xx audio=xxxB`

---

### 10.4 AVI 播放（dev = 9）

| opt | 含义 | len / data | 行为 |
|-----|------|------------|------|
| `0` | 关闭 | `0` | `ty_avi_player_stop()` |
| `1` | 开启 | `≥1` | `data[0]`=音量；`data[1..]`=可选文件名 |
| `2` | 暂停 | `0` | 冻结进度，停音视频输出 |
| `3` | 继续 | `0` | 从冻结位置恢复 |
| `4` | 跳转 | `2` | `data[0..1]`=目标时间 ms（**大端**） |
| `5` | 快进 | `0` 或 `2` | 默认 +5000ms；或 `data[0..1]`=步进 ms |
| `6` | 快退 | `0` 或 `2` | 默认 -5000ms |
| `7` | 扫描列表 | `0` | 枚举 `/sdcard/*.avi`，日志打印索引 |
| `8` | 按索引播 | `≥1` | `data[0]`=索引；`data[1]`=音量（可选） |
| `9` | 下一首 | `≥0` | `data[0]`=音量（可选，默认 50） |
| `10` | 上一首 | `≥0` | 同上 |

**时间 ms 大端编码**：5 秒 = 5000 = `0x1388` → `data[0]=0x13, data[1]=0x88`

#### 播放命令 demo

| 场景 | 完整帧（hex，含 CRC） |
|------|------------------------|
| 默认 `record.avi`，音量 50 | `55 AA 00 EE 00 05 58 01 09 01 32 87` |
| **`record1.avi`，音量 50** | `55 AA 00 EE 00 10 58 01 09 0C 32 72 65 63 6F 72 64 31 2E 61 76 69 BB` |
| `test.avi`，音量 50 | `55 AA 00 EE 00 0D 58 01 09 09 32 74 65 73 74 2E 61 76 69 C5` |
| 关闭 | `55 AA 00 EE 00 04 58 00 09 00 52` |
| 暂停 | `55 AA 00 EE 00 04 58 02 09 00 54` |
| 继续 | `55 AA 00 EE 00 04 58 03 09 00 55` |
| 跳到 5s | `55 AA 00 EE 00 06 58 04 09 02 13 88 F5` |
| 快进 5s（默认） | `55 AA 00 EE 00 04 58 05 09 00 57` |
| 快退 5s（默认） | `55 AA 00 EE 00 04 58 06 09 00 58` |
| 扫描 SD 卡 | `55 AA 00 EE 00 04 58 07 09 00 59` |
| 播列表第 0 首 | `55 AA 00 EE 00 06 58 08 09 02 00 32 90` |
| 下一首 | `55 AA 00 EE 00 05 58 09 09 01 32 8F` |
| 上一首 | `55 AA 00 EE 00 05 58 0A 09 01 32 90` |

**预期日志**：

```
avi play open ... header_fps=14.9 eff_fps=12.1 frames=180 dur=14876ms
avi play 00:05/00:14 [======>--------------] 35%
avi playlist [0] /sdcard/record.avi
avi playlist [1] /sdcard/record1.avi
```

---

### 10.5 典型测试场景（step-by-step）

#### 场景 A：单文件录制 → 播放验证

```
1. 录制  record1.avi（仅视频预览）
   → 55 AA 00 EE 00 11 58 01 08 0D 01 00 72 65 63 6F 72 64 31 2E 61 76 69 8B
2. 等待 10~30s，观察 LCD 预览
3. 关录制
   → 55 AA 00 EE 00 04 58 00 08 00 51
4. 播放 record1.avi
   → 55 AA 00 EE 00 10 58 01 09 0C 32 72 65 63 6F 72 64 31 2E 61 76 69 BB
5. 观察进度条日志 + 音画同步
6. 关播放
   → 55 AA 00 EE 00 04 58 00 09 00 52
```

#### 场景 B：多文件 + 播放列表

```
1. 分别录制 record.avi、record1.avi、test.avi（不同 opt=1 命令）
2. 扫描列表 → opt=7
3. 日志确认 [0][1][2] 索引
4. 播第 1 首 → opt=8, data[0]=1, data[1]=50
5. 播放中下一首 → opt=9
```

#### 场景 C：播放控制

```
1. 开始播放（opt=1）
2. 暂停（opt=2）→ 进度条带 PAUSED
3. 跳到 10s（opt=4, 27 10）→ LCD 显示该帧预览
4. 继续（opt=3）
5. 快进 5s（opt=5）→ 检查同步
```

#### 场景 D：录制中动态开关预览

```
1. 开始录制（无预览） opt=1 len=0
2. 中途开 LCD → opt=2 dev=8 data[0]=01
3. 中途开监听 → opt=3 dev=8 data[0]=01
4. 关录制
```

---

### 10.6 payload 拼装速查

**播放指定文件**（dev=9, opt=1）：

```
len = 1 + strlen(filename)
data[0] = volume
data[1..] = filename ASCII
```

**录制指定文件 + 独立预览**（dev=8, opt=1）：

```
len = 2 + strlen(filename)
data[0] = video_preview  (0/1)
data[1] = audio_preview  (0/1)
data[2..] = filename ASCII
```

**跳转 / 快进 / 快退**（dev=9, opt=4/5/6）：

```
len = 2
data[0] = (time_ms >> 8) & 0xFF
data[1] = time_ms & 0xFF
# opt=5/6 且 len=0 时步进默认 5000ms
```

---

### 10.7 推荐验收流程

1. 插 SD 卡，确认可挂载 `/sdcard`
2. 录制 `record1.avi`（仅视频预览）10s+ → 关录制
3. PC 上 `ffprobe /sdcard/record1.avi` 确认 MJPEG + PCM
4. 播放 `record1.avi`，观察 1s 进度条日志
5. 暂停 → 跳转 → 继续，确认画面不卡死、进度正确
6. 扫描列表 → 按索引 / 上下首切歌
7. 拷 AVI 到 PC 播放，对比设备端同步

---

## 11. 编译验证

- 默认工程：`t5_common_user_config_ai_baseline_ty` / 版本 `2.0.6`
- 2026-06-12 编译已通过；尾部 `arm-none-eabi-readelf not found` 仅影响 `app.sym`，不影响固件产出
- v2.1：`tuya_avi_service` 引入 `avi_port` 后需整编验证 ADC/DAC 与 FS/JPEG 链接

---

## 12. 参考资料

- 示例工程 README：`video_player_example/README_CN.md`
- 示例工程源码：`video_player_example/ap/video_player_cli/src/`
- leding SDK 组件：
  - `ap/components/bk_video_recorder/`
  - `ap/components/bk_video_player/`
  - `ap/components/bk_libs/bk7258_ap/libs/libavi.a`
- 当前项目参考：
  - `application_components/tuya_avi_service/include/avi_port.h`
  - `application_components/tuya_avi_service/src/avi_port.c`
  - `application_components/tuya_avi_service/src/ty_avi_audio.c`
  - `application_components/tuya_avi_service/src/ty_avi_player.c`
  - `application_components/tuya_avi_service/src/ty_avi_playlist.c`
  - `application_components/ty_gfw_testsuites/src/test_media/test_media_avi_record.c`
  - `application_components/ty_gfw_testsuites/src/test_media/test_media_avi_play.c`
  - `application_components/ty_gfw_testsuites/src/test_media/test_media.c`
  - `vendor/T5/tuyaos/tuyaos_adapter/include/audio/tkl_aud_adc.h`
  - `vendor/T5/tuyaos/tuyaos_adapter/include/audio/tkl_aud_dac.h`
  - `vendor/T5/tuyaos/tuyaos_adapter/include/jpeg_codec/tkl_jpeg_codec.h`
