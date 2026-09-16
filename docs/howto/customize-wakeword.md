# 更换唤醒词 / 调整 KWS

## 目标

说明如何在 wukong demo 中：

1. 在仓库自带的几个预编译 TUTUClear 唤醒词模型之间切换（如只保留"你好涂鸦"）；
2. 了解如何切换 KWS 引擎（TUTUClear ↔ SNDX，源码级改动）；
3. 了解当前灵敏度调节的现状与误唤醒/漏唤醒的缓解思路；
4. 验证换词/调参后设备确实按预期唤醒。

训练全新唤醒词声学模型不在本文范围内（见步骤 5），需以模型提供方文档为准。

## 前置条件

- 已按[快速开始](../quickstart.md)跑通任意一块板子的编译、烧录、对话。
- 确认板子采集音频的方式是**板载麦克风**：`make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai` 中 `Audio input source` 选择 `Onboard microphone`（对应 `CONFIG_USING_BOARD_AUDIO_INPUT=y`，choice 定义见 `src/miscs/audio_player/Kconfig`）。本文的模型/引擎切换仅在此模式下生效；若选的是 `UART (external codec)`（`CONFIG_USING_UART_AUDIO_INPUT=y`），唤醒词由外接语音芯片决定，KWS 相关代码不会被编译进固件，见常见问题。
- 当前仓库里默认生效的板载 KWS 引擎（TUTUClear）只在 T5 模组（`CONFIG_TUYA_MODULE_T5=y`）上被接入，见 `src/wukong/audio/frontend/kws/wukong_kws.c` 中 `wukong_kws_default_init()` 的 `#if defined(TUYA_MODULE_T5)` 分支。

## 步骤

### 1. 认识当前的唤醒词模型文件

预编译的唤醒词模型放在 `src/wukong/audio/frontend/kws/tutuclear/`：

| 文件 | 说明 |
| --- | --- |
| `libtutuClear.a` | **当前实际生效**的模型（`local.mk` 会把它拷进编译产物） |
| `libtutuClear_wakeup_nihaotuya_xiaozhitongxue_heytuya_hituya_20251024_small_model.a` | 与 `libtutuClear.a` 内容完全一致（md5 相同）：你好涂鸦 / 小智同学 / heytuya / hituya 四词合一 |
| `libtutuClear_wakeup_nihaotuya_chinese_20251227_200KB_model.a` | 单词模型：你好涂鸦 |
| `libtutuclear_wakeup_xiaozhitongxue_chinese_200KB_20260112.a` | 单词模型：小智同学 |
| `libtutuClear_wakeup_heytuya_english_20251227_730KB_model.a` | 英文单词模型：hey tuya |

即：demo 默认烧出来的固件同时能被"你好涂鸦""小智同学""hey tuya""hituya"四个词唤醒（对应检测回调 `TUTUClear_kws_detect()`，`src/wukong/audio/frontend/kws/tutuclear/tutuclear.c` 第 66-86 行，按 `w32WakeWord` 取值 1~4 打印并上报）。

### 2. 切换为单一唤醒词模型

编辑应用根目录 `local.mk` 中拷贝该 `.a` 的一行（板载麦克风分支内）：

```makefile
# local.mk，原样
INSTALL_TUTUKWS := $(shell mkdir -p  $(LOCAL_PATH)/../../libs/app_libs && cp $(LOCAL_PATH)/src/wukong/audio/frontend/kws/tutuclear/libtutuClear.a $(LOCAL_PATH)/../../libs/app_libs/ && echo "libtutuClear.a copied" >&2)
```

把 `cp` 的源文件换成目标模型，但**目标文件名仍保持 `libtutuClear.a`**（拷贝目标目录 `libs/app_libs/` 下的文件名固定为 `libtutuClear.a`，与 `tutuclear.c` 里声明的符号绑定）：

```makefile
INSTALL_TUTUKWS := $(shell mkdir -p  $(LOCAL_PATH)/../../libs/app_libs && cp $(LOCAL_PATH)/src/wukong/audio/frontend/kws/tutuclear/libtutuClear_wakeup_nihaotuya_chinese_20251227_200KB_model.a $(LOCAL_PATH)/../../libs/app_libs/libtutuClear.a && echo "libtutuClear.a copied" >&2)
```

换成单词模型后，`TUTUClear_kws_detect()` 里的分支代码不用改——模型只会上报它认识的词对应的 `w32WakeWord` 取值。

### 3. 重新生成配置并编译

```bash
make app_config APP_NAME=tuyaos_demo_wukong_ai
make app APP_NAME=tuyaos_demo_wukong_ai
```

### 4. 切换 KWS 引擎（TUTUClear ↔ SNDX）

引擎选择目前是**源码硬编码**，没有对应的 Kconfig 开关：`wukong_kws_default_init()`（`src/wukong/audio/frontend/kws/wukong_kws.c` 第 469-492 行）里 SNDX 的四行回调被注释掉，默认走 TUTUClear：

```c
// cfg.create = SNDX_kws_create;
// cfg.detect = SNDX_kws_detect;
// cfg.reset  = SNDX_kws_reset;
// cfg.deinit = SNDX_kws_deinit;
cfg.create = TUTUClear_kws_create;
cfg.detect = TUTUClear_kws_detect;
cfg.reset  = TUTUClear_kws_reset;
cfg.deinit = TUTUClear_kws_deinit;
```

如需改用 SNDX 引擎，把上面四行的注释关系互换后重编即可。注意 SNDX 引擎当前实现（`src/wukong/audio/frontend/kws/sndx/sndx.c`）只识别两个词：`id==1001`→你好涂鸦、`id==1002`→heytuya（第 33-39 行）；且 `local.mk` 里固定拷贝的是 `sndx/libsndxasr.a`，`sndx/` 目录下另外三个按词命名的 `.a`（`libsndxasr-nihaotuya.a` 等）当前未被任何编译规则引用。

### 5. 训练/接入全新唤醒词

> **说明**：训练新的唤醒词声学模型、生成新的 `lib*.a` 属于模型提供方（涂鸦或方案商）的定制服务，无法在本仓库内完成，请联系涂鸦或对应方案商获取模型文件；具体的申请渠道、云端工具以平台/商务侧文档为准，本文不做假设。

拿到新模型文件后，接入到代码里的步骤（参照 `src/wukong/audio/frontend/kws/README.md` 的「自定义唤醒词」一节）：

1. 如果新词沿用 TUTUClear/SNDX 引擎，且需要新的索引号，在 `WUKONG_KWS_INDEX_E`（`src/wukong/audio/frontend/kws/wukong_kws.h` 第 30-39 行）里挑选/新增枚举值——已预留 `WUKONG_KWS_UDF1/2/3`（值 6/7/8）供自定义使用。
2. 把模型 `.a` 放进 `src/wukong/audio/frontend/kws/tutuclear/` 或 `sndx/`，参照步骤 2 的方式在 `local.mk` 里接入。
3. 若沿用现有引擎的 create/detect 回调，在 detect 里对新增索引调用 `wukong_kws_event(index)`（写法参照 `TUTUClear_kws_detect()` / `SNDX_kws_detect()` 现有分支）。
4. 若是全新引擎（非 TUTUClear/SNDX），需要自行实现 `WUKONG_KWS_CFG_T` 的 `create` / `detect` / `reset` / `deinit` 四个回调，通过 `wukong_kws_init(&cfg)`（而非 `wukong_kws_default_init()`）接入，接口定义见 `wukong_kws.h` 第 56-78 行。

### 6. 灵敏度与误唤醒/漏唤醒缓解

以当前仓库代码为准，KWS 层面能动的旋钮很有限：

- TUTUClear 底层导出了 `TUTUClear_SetWakeupThr(handle, thr)` / `TUTUClear_GetWakeupThr(handle)`（声明见 `tutuclear.c` 第 17-18 行），但 `TUTUClear_kws_create()`（同文件第 20-53 行）**当前没有调用它们**，即应用层暂未暴露灵敏度调节入口。如需调节，需要在 `TUTUClear_kws_create()` 创建实例成功之后自行调用 `TUTUClear_SetWakeupThr()` 并重编。
  > **说明**：`Thr` 的取值范围、数值方向（越大越不易/越容易误唤醒）在本仓库源码与头文件中均未说明，需以模型提供方文档为准，不要凭经验假设方向。
- SNDX 的 `sndx_asr_init(silence_ms)`（`sndx.c` 第 10 行，当前传参 `450`）控制的是静音判定时长，不是唤醒阈值。
- 很多"误唤醒/漏唤醒"问题其实出在前级 AEC/VAD：KWS 依赖 `wukong_kws_feed_with_vad()` 传入的 `vadflag` 做节流（`is_detect_vad=1` 时只在有声/刚转静音时触发 detect），VAD 灵敏度档位、AEC 残留回声抑制等调参不在本文重复，参见[语音算法调试指南](../troubleshooting/audio-tuning.md)。

## 验证方法

1. 按上文改动后重新编译烧录，串口保持在调试日志级别（可看到 `TAL_PR_DEBUG` 打印）。
2. 对着设备说出目标唤醒词，串口应出现类似打印：
   - TUTUClear 引擎：`TUTUClear_WakeWord -> 你好涂鸦` / `小智同学` / `heytuya` / `hituya`（`tutuclear.c` 第 70-79 行）。
   - SNDX 引擎：`sndx_WakeWord -> 你好涂鸦` / `heytuya`（`sndx.c` 第 35、37 行）。
3. 唤醒事件 `EVENT_WUKONG_KWS_WAKEUP` 会经由 `src/tuya_ai_toy.c` 中订阅的 `__on_ai_toy_audio_kws()` 触发 `wukong_ai_mode_dispatch(AI_MODE_OP_WAKEUP, ...)`，设备应随之进入对话/唤醒态，可结合实际交互（提示音、界面切换等）确认。
4. 如果换了单词模型后，说其余三个词不再触发——属预期行为；如果换了模型/引擎后**所有词都不触发**，先确认 `make app_config` 是否重新跑过、`include/tuya_app_config.h` 里 `CONFIG_USING_BOARD_AUDIO_INPUT`、`CONFIG_TUYA_MODULE_T5` 是否符合预期（做法同[适配新板](porting-new-board.md)常见问题一节）。

## 常见问题

**选的是 UART 外接语音芯片模式，改了唤醒词模型没反应**
`CONFIG_USING_BOARD_AUDIO_INPUT` 未开启时，`wukong_kws_default_init()` 走的是 `wukong_kws_uart_init()` 分支（`wukong_kws.c` 第 469-491 行的 `#else` 分支），TUTUClear/SNDX 相关源码在 `local.mk` 里也只在 `CONFIG_USING_BOARD_AUDIO_INPUT=y` 时才会被编译进去。这种模式下唤醒词由外接 CODEC 芯片决定，不是本文的调整范围。

**非 T5 模组（`CONFIG_TUYA_MODULE_T5` 未开启）改了模型没反应**
`wukong_kws_default_init()` 里板载引擎的接入代码只在 `TUYA_MODULE_T5 == 1` 时生效，其余情况下该函数直接返回 `OPRT_OK` 且不接入任何引擎（`wukong_kws.c` 第 471-489 行）。非 T5 平台如需板载 KWS，需要自行调用 `wukong_kws_init()` 接入引擎，而不是依赖 `wukong_kws_default_init()`。

**改了 `local.mk` / Kconfig 后编译产物看起来没变**
和适配新板一样，改动 Kconfig/APPconfig 相关配置后必须重跑 `make app_config APP_NAME=tuyaos_demo_wukong_ai`，否则 `include/tuya_app_config.h` 不更新；`local.mk` 里 `.a` 拷贝路径的改动只需要重新 `make app` 即可生效。

**想知道当前 `libtutuClear.a` 到底是哪个模型**
可以用 `md5sum` 对比 `src/wukong/audio/frontend/kws/tutuclear/` 目录下各 `.a` 文件，与 `libtutuClear.a` 的 md5 一致的即为当前生效的模型来源。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
