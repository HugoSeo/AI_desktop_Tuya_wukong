# 音频上行 5 路抓流（SD 卡）

把语音上行链路的 5 路音频流（MIC / REF / AEC / KWS / VAD）**整段录到 SD 卡**，拷到 PC 转成 WAV 分析，用于回答"AEC 为什么没消干净""唤醒为什么不灵""云端听到的到底是什么"这类问题。

与串口抓取（tyuTool `dump 0~4`，见[硬件声学结构测试](acoustic-test.md)）的分工：串口通路适合配合测试音做**短会话、逐路**抓取；本通路在设备 UI 上一键开关、**5 路同步、长时段、真实使用场景**（边放音乐边对话）抓取，两者数据口径一致（同一组 `AUDIO_DUMP_*` 通道，见[语音算法调试](audio-tuning.md)第 2 节的通道表）。

## 前置条件

两个配置项（**T5AI_BOARD 预置全开**，其它板型需自行打开后 `make app_config` 重编）：

| 配置 | 位置 | 默认 | 作用 |
|---|---|---|---|
| `ENABLE_AUDIO_ANALYSIS` | `build/APPconfig` | n（T5AI_BOARD 预置 y） | 音频分析功能总开关：不开时 `audio_dump_write()` 为空操作，整条抓流通路不工作 |
| `UI_FEATURE_AUDIO_DIAG` | `src/ui/Kconfig` | y | UI「音频诊断」页面入口（依赖 wukong 页面集，`UI_WUKONG_PAGES=y`） |

另需：

- SD 卡已插入且可挂载（落盘目录在 SD 卡上）。
- 板载麦克风模式（`USING_BOARD_AUDIO_INPUT`，各路数据的写入点都在板载音频管线里）。
- SD/UART 抓取通道还依赖 `ENABLE_EXT_RAM`（会话缓冲在 PSRAM，见下文内存说明）。

## 操作步骤

1. **开始抓取**：设备上进入「应用中心 → 诊断 → 音频诊断」，选中「SD 卡」，然后**离开页面**——选择在离开页面时才生效（页面为 apply-on-leave 模式，避免中间误触反复起停会话）。
2. **复现问题场景**：正常使用设备——播放 TTS/音乐时喊话（查 AEC/打断）、说唤醒词（查 KWS）、正常对话（查上传音质）。抓取期间 5 路持续写卡。
3. **结束抓取**：回到「音频诊断」页选「关闭」，再离开页面——剩余缓存全部落盘，会话文件收尾。结束时日志会打印 `audio_dump sd: deinit (overrun=N)`，`N` 非 0 表示写卡速度跟不上产生过丢帧（见常见问题）。
4. **取数据**：文件在 SD 卡 `/sdcard/tuyaos/audio_dump/` 下，每次会话 5 个文件共用同一时间戳前缀、互不覆盖：

   ```text
   0625_143000_mic.pcm  0625_143000_ref.pcm  0625_143000_aec.pcm
   0625_143000_kws.pcm  0625_143000_vad.pcm
   ```

   墙钟未同步（未配网等）时前缀退化为自增序号（`0000_mic.pcm`）。格式为**裸 PCM，16000 Hz / 16-bit / 单声道**。
5. **PC 端转换/分析**：把 `.pcm` 拷到 PC 同一目录，用仓库脚本批量转 WAV：

   ```bash
   python scripts/audio_diag/pcm_analyze.py <目录>          # 所有 *.pcm 转同名 .wav
   python scripts/audio_diag/pcm_analyze.py <目录> --plot   # 额外按会话叠画 5 路波形 PNG（需 numpy + matplotlib）
   ```

   也可以直接把裸 PCM 导入 Audition / ocenaudio（导入参数按上述格式填）。脚本详情见 [`scripts/audio_diag/README.md`](../../scripts/audio_diag/README.md)。

## 5 路怎么读

| 文件 | 内容 | 典型用途 |
|---|---|---|
| `*_mic.pcm` | 麦克风原始信号 | 查削波、增益、底噪 |
| `*_ref.pcm` | 喇叭回采参考 | 查回采削波、回采通路是否正常 |
| `*_aec.pcm` | 回声消除后输出 | 与 mic/ref 对比看 AEC 效果 |
| `*_kws.pcm` | 送入唤醒词引擎的信号 | 查唤醒不灵 |
| `*_vad.pcm` | VAD 判定后实际上传云端的切片 | 查"云端听到了什么"、切音边界 |

**典型判读**：

- **AEC 没消干净**：三路对齐比对（`--plot` 叠图先看全局）——`aec` 里仍有与 `ref` 强相关的残留 → 先看 `ref`/`mic` 是否削波（结构问题，按[声学测试](acoustic-test.md)处理），排除削波后再调残留回声抑制参数（[语音算法调试](audio-tuning.md)第 3 节）。
- **喊了唤醒词没反应**：`kws` 路里人声是否清晰完整；`aec` 正常而 `kws` 明显异常，问题在前端到 KWS 之间。
- **本地听感正常但云端识别差**：看 `vad` 路——首尾字被切掉是 VAD 断句参数问题（[语音算法调试](audio-tuning.md)第 4 节）；`vad` 路本身干净则往云端侧查。
- 播放中对话的场景，`mic` 与 `ref` 的相对幅度直接反映回声路径强弱——回声远大于人声时，任何 AEC 参数都救不回来，先降音量/改结构。

## 常见问题

**诊断页里没有「音频诊断」入口**
入口只在**至少存在一个真实抓取通道**时才显示（`ui_svc_audio_diag_available()`：可用通道只剩「关闭」即隐藏入口），按下列顺序排查：

1. `ENABLE_AUDIO_ANALYSIS` 未开——所有通道都不编译，入口隐藏；
2. `ENABLE_AUDIO_ANALYSIS` 已开但 `ENABLE_EXT_RAM` 未开且局域网通道（`ENABLE_APP_AI_MONITOR`）也未开——SD/UART 通道都不可用，入口同样隐藏；
3. `UI_FEATURE_AUDIO_DIAG` 未开，或当前板型走板级自定义 UI（非 wukong 页面集）——页面本身没编译。

**选了「SD 卡」但没生成文件**
按顺序查：SD 卡是否插好、能否挂载；选择后是否**离开了页面**（不离开不生效）；结束时是否回到页面选「关闭」并再次离开（不收尾则文件不完整）；目录是否找对（`/sdcard/tuyaos/audio_dump/`）。

**文件比实际抓取时长短 / 波形有跳变**
写卡速度跟不上时会整块丢帧，结束日志 `overrun=N` 非 0 即发生过。换高速 SD 卡，或缩短单次会话时长。

**内存**：SD 抓取会话期间占用约 **1.6MB PSRAM**（5 路 × 双缓冲 × 160KB，`src/miscs/audio_analysis/audio_dump.c`），会话关闭后释放。内存紧张的配置注意避开与其它大内存功能（如生图）同时使用。

**页面里的「UART」「局域网」选项是什么**
同一套抓流框架的另外两个输出通道：UART 配合 tyuTool 串口抓取（[声学测试](acoustic-test.md)的命令通路），局域网为实时网络传输（额外依赖 `ENABLE_APP_AI_MONITOR`）。排查算法问题优先用 SD 卡通路，数据最完整。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
