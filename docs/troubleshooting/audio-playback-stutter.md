# 音乐/TTS 播放卡顿排查

本文排查**播放侧**问题：音乐（URL 流）或 TTS（云端下发语音流）播放中出现周期性断音、碎卡、起播慢。上行链路（识别/回声/VAD）问题见[语音算法调试](audio-tuning.md)。

## 先认识链路：卡顿=输出环空转

播放数据流（实现在 `src/miscs/audio_player/` 子模块，模块说明见其 README）：

```
云端 ──► datasink（mem=TTS 流式 / url=HTTP 音乐 / file）
          │  播放线程单线程拉取：URL 下载不是独立线程，
          │  就在播放线程里 http_read_content（10ms 让出）
          ▼
     解码（MP3/OPUS…）→ 重采样 → 混音
          ▼
     tal_audio_output 环形缓冲 ──► 喇叭
```

**关键事实**：输出环形缓冲被放空时，底层补零静音——这就是听感上的"卡一下"，且**不产生任何日志**。所以排查不能靠找报错，要靠 AP-STAT 统计判断是链路哪一环供不上数。

## 第一步：打开 AP-STAT 诊断

AP-STAT 是两级门：

1. **编入（编译期）**：`make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai` → `Audio Configuration` → `Audio Player` → 勾选 `Enable [AP-STAT] playback diagnostics`（`CONFIG_AI_PLAYER_DEBUG_STATS`，默认关闭），然后 `make app_config` + `make app` 重编烧录。
2. **打开（运行时）**：设备上进入「应用中心 → 诊断」，打开「播放统计」开关（该行仅在统计代码已编入时显示）。开关是内存态、默认关，重启后需重新打开；也可代码调用 `tuya_ai_player_debug_stats_set(TRUE)`。

排查版固件建议直接带上 `CONFIG_AI_PLAYER_DEBUG_STATS=y`——编入后不打开开关没有日志输出，现场要抓数据时开一下即可，不用再重编烧录。

开启后播放期间每 2 秒输出一行（FG=前台 TTS/提示音，BG=后台音乐）：

```
[AP-STAT FG] loop=.. sink(r=.. B=.. zero=.. max=..ms) dec(n=.. B=.. max=..ms) rs_max=..ms wr(B=.. audio=..ms blkmax=..ms) win=..ms
```

| 字段 | 含义 |
|---|---|
| `sink(r/B/zero/max)` | 数据源读取：次数 / 字节 / **零读次数**（拉不到数据） / 单次最大耗时 |
| `dec(n/B/max)` | 解码：次数 / 产出字节 / 单次最大耗时 |
| `rs_max` | 重采样单次最大耗时 |
| `wr(B/audio/blkmax)` | 写出：字节 / **折算成音频时长(ms)** / 单次最大阻塞 |
| `win` | 本统计窗口实际时长（名义 2000ms） |

## 第二步：三句判读口诀

**① `audio` 明显小于 `win`，且 `sink` 的 `zero`/`max` 大 → 上游供数不足（网络饿）。**
每 2 秒窗口要写出 ≈2000ms 音频才算供给充足；`audio=1850` 意味着每 2 秒欠 150ms，必然周期性卡。转去看 URL 数据源日志（`datasink_url.c`，NOTICE 级常开）：

- `url sink connected: cost N ms` —— 建联耗时（首次 TLS 握手可达秒级，表现为**起播慢**而非卡顿）。
- `url sink reconnect #n at offset ...` —— **断线重连**，一次 https 重连几百 ms~秒级，必可闻。反复 reconnect = 网络/CDN 侧问题（限速、吞吐塌陷），设备端只能靠加大缓冲缓解，治本在云侧。
- `url sink first data N bytes, M ms after start` —— 首包延迟。

**② `dec`/`rs_max` 耗时大，而 `wr blkmax≈0` → CPU 饿（解码被高负载线程抢占）。**
`blkmax≈0` 说明输出环长期不满（写入从不需要等），解码产出追不上消耗。排查同期启动的高 CPU 业务——实测案例：开启摄像头预览（DVP 17fps）后解码单帧耗时 22→60ms、供给率 99.8%→92%，持续 underrun。播放线程优先级已提至 `THREAD_PRIO_0`（`svc_ai_player.c`，该线程环满自阻塞、不会饿死他人）；若你的场景仍复现，用同样思路核查是谁在抢核。

**③ `wr blkmax` 大 → 输出环写满被阻塞，属健康状态。**
下游满说明供给充足，此时的卡顿另找原因（如上层主动 stop/start、切歌逻辑）。

## TTS 碎卡与蓄水机制

TTS 走内存数据源（mem sink），云端流式下发遇网络抖动时，若边到边播会形成高频"碎卡"。播放器内置**蓄水**机制（`datasink_mem.c`）：起播及每次断流后先攒数据，攒够水位（或 EOF、或超时兜底）才放行解码，把多次碎卡合并成一次可感知的停顿。

Kconfig（`Audio Player` 菜单 → `Streaming pre-buffer`）：

| 配置 | 默认 | 说明 |
|---|---|---|
| `AI_PLAYER_MEM_PREBUF_BYTES` | 2048 | 蓄水水位（字节），0=关闭。**须显著小于** `AI_PLAYER_RINGBUF_SIZE`（16384），配到接近容量将永远攒不满、只能靠超时放行 |
| `AI_PLAYER_MEM_PREBUF_TIMEOUT_MS` | 500 | 超时兜底：上游卡死/码率过低时照常放行 |

蓄水完成时打印 `mem sink prebuf done: N bytes in M ms`（空超时放行不打印，避免刷屏）。调参思路：

- **水位 ≈ 码率 × 想吸收的抖动时长**。先开 AP-STAT 实测 TTS 实际码率（`sink B` ÷ `win`），再按目标抖动窗口换算；盲目加大水位只会线性增加起播/续播延迟。
- 碎卡仍频繁 → 加大水位；起播/打断响应变慢明显 → 减小水位或缩短超时。
- LITE 播放器（`AI_PLAYER_LITE=y`，如 T3 板）无 datasink 层，蓄水与 AP-STAT 均不适用。

## 起播慢（区别于播放中卡顿）

起播慢的账要单独算，判据都在 url sink 日志里：

1. `connected: cost` 大（秒级）——首次 TLS 建联开销，属网络环境问题。
2. 连上后 `dec n>0` 但 `B=0` 持续一段时间——常见于 mp3 头部带大体积 ID3 封面（上百 KB），解码器要先读完跳过才出声；治本靠服务端去封面或 Range 跳过。

## 常见问题

**开了 AP-STAT 没有任何输出**
LITE 模式下该诊断不编译（`depends on !AI_PLAYER_LITE`）；另确认改完 Kconfig 跑过 `make app_config`。

**卡顿只在特定页面/功能开启时出现**
典型的口诀②场景：对照卡顿起点前后的 AP-STAT 各环节耗时变化，找同一时刻启动的高负载业务（摄像头、算法、UI 动画），而不是先怀疑网络。

**加大了输出环缓冲还是周期性卡**
供给率不足（如持续 92%）时任何容量的缓冲都只是拉长卡顿间隔，不能消除——先按口诀①②把供给率修到 ≈100%。

**TTS 每句开头丢半个字**
不是蓄水问题——蓄水只延后出声不丢数据；去查上行 VAD 切音（[语音算法调试](audio-tuning.md)第 4 节）。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
