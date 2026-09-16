# 用 TEF 高效存储与播放动图（替代 GIF）

## 目标

把一组 GIF 动画（表情、待机动效等）转成 TEF（Tuya Emotion Format）编进固件，
按场景选三种播放形态：常驻播放器直推 GRAM（不经 LVGL）、一次性阻塞播放
（开机动画）、LVGL 页面控件 `lv_tef`（形态 C）。均不做运行时 GIF 解码。

相对「lv_gif 内嵌 GIF」方案的收益（EYES 板 10 表情实测）：

| 维度 | lv_gif + 内嵌 GIF | TEF |
|---|---|---|
| flash 素材 | ~1.58MB（旧方案实际内嵌的 LVGL 素材集，另需编入 LVGL 库） | **419KB**（同源 10 个 GIF 字节流合计 648KB 的 ~0.65×，且不依赖 LVGL） |
| 解码 CPU | 12.5%（相对 58ms 帧节拍） | **2.2%**，单帧峰值 ≤7ms |
| 常驻内存 | 79.4KB（GIF 解码峰值，不含 LVGL 本体） | **74.4KB**（PSRAM，含整屏影子帧缓存） |

本文只讲「怎么用」；格式原理、播放器设计要点与已知坑的权威说明在
[`src/miscs/tef/README.md`](../../src/miscs/tef/README.md)（改 `src/miscs/tef/` 前必读），
格式规范见 `src/miscs/tef/docs/TEF_技术规范.html`。

## 前置条件

- 板子已开显示栈：`CONFIG_ENABLE_TUYA_DISPLAY=y`（TEF 不要求开 `ENABLE_TUYA_UI`；
  参考 EYES 的「关 UI、直连显示」形态）。
- 屏幕可用 PSRAM：播放器工作缓冲全在 PSRAM——整屏影子帧缓存（W×H×2 字节）+
  解压字典 16KB + 一帧索引缓冲（W×H）+ tinfl 约 11KB。
- 主机侧 Python 3 + `Pillow` + `numpy`（跑素材生成脚本用）。
- 素材 GIF 的画布尺寸：常驻播放器（下文形态 A）须与屏幕逻辑分辨率一致
  （EYES 双屏特例：128×128 单眼素材自动镜像双屏，128×256 双眼素材按行带路由）；
  一次性播放（形态 B）只须不大于目标窗口，小于时自动居中；LVGL 控件
  （形态 C）任意尺寸，控件大小即素材大小。

## 步骤

### ① 准备 GIF 目录

文件名即素材名（须为合法 C 标识符）；用作云端表情时须与
`src/wukong/skills/skill_emotion.c` 的 emoji 名一致。名为 `neutral` 的素材会排在
注册表第 0 项，作为未命中名字时的回落项。

```bash
cd src/miscs/tef/tools
mkdir gif && cp <你的素材>/*.gif gif/
```

### ② 一键生成 C 数组与注册表

```bash
python3 gen_assets.py                 # 就地模式：gif/ -> out/
# 或指定目录（板级素材维护方式，EYES 即如此）：
python3 gen_assets.py <gif_dir> <out_dir>
```

输出三类文件：每个 GIF 一个 `tef_<name>.c`（`.tef` 字节流常量数组）、
`tef_assets.h`（声明 + `tef_asset_t` 注册表类型）、`tef_assets.c`
（`g_tef_assets[]` / `g_tef_asset_cnt` 注册表）。

### ③ 播放代码接入

**形态 A：常驻表情播放器**（EYES 表情这类"循环播放、随时切换"场景），在板级
初始化处（`tuya_display_hw_init()` 之后）：

```c
#include "tef_player.h"
#include "tef_assets.h"

tef_player_init();                                        /* 建播放线程 */
tef_player_play(g_tef_assets[0].data, g_tef_assets[0].len); /* 循环播放，异步切换 */
```

按名字切换（含回落）参考 EYES 的
[`src/boards/T5AI_BOARD_EYES/ui/eyes_app.c`](../../src/boards/T5AI_BOARD_EYES/ui/eyes_app.c)：
`eyes_emotion_find()` 查注册表、未命中回落第 0 项，表情/对话状态消息驱动
`tef_player_play()`。

**形态 B：一次性阻塞播放**（开机动画这类"播一遍定格"场景）——
`tef_player_play_once()` 逐帧解码到你提供的 RGB565 画布（素材画布小于窗口时自动
居中），每帧回调由你上屏，播完返回、工作缓冲即刻释放；不需要
`tef_player_init()`。参考 UI 板 boot splash：
[`src/boards/common/boot_splash/tuya_boot_splash.c`](../../src/boards/common/boot_splash/tuya_boot_splash.c)
（整屏 PSRAM 画布 + `tal_display_flush` 提交，末帧定格直到 LVGL 接管）。

**形态 C：LVGL 页面内控件**（`ENABLE_TUYA_UI` 板在任意页面播放 TEF 动画），
用 `src/ui/widgets/lv_tef.[ch]`，用法与 `lv_gif` 一致——create 后 `set_src`
即循环播放，删控件即停：

```c
#include "lv_tef.h"

lv_obj_t *tef = lv_tef_create(parent);
lv_obj_center(tef);
lv_tef_set_src(tef, tef_data, tef_len);   /* ② 生成的常量数组 */
```

注意事项：

- 解码在 UI 线程 `lv_timer` 里逐帧进行（帧节拍 = 素材 fps）；多个控件可并存，
  每个控件独立占 PSRAM ≈ 画布 W×H×2 + 索引 W×H + 字典 16KB + tinfl 11KB
  （240×240 约 196KB，128×128 约 75KB），同屏个数按内存预算控制。
- 画布是 native RGB565：**不要**给 UI 板加 `TEF_RGB565_BYTE_SWAP`（那是直推
  GRAM 通路的开关），SPI 面板字节交换由 UI flush 层的
  `CONFIG_UI_LCD_RGB565_BYTE_SWAP` 负责。
- 素材加载失败控件保持空白；播放中数据损坏则定格在最后一帧（日志有
  `lv_tef:` 前缀报错）。

### ④ 编译接线（local.mk）

TEF 框架（`miscs/tef`，含解码器）随 `CONFIG_ENABLE_TUYA_DISPLAY` 自动编入，
无需手动接线；你的板块里只需两件事：

```makefile
# 8bit SPI 面板（如 st7735s）像素需字节交换（调色板加载时一次完成）；RGB 等面板不加：
LOCAL_TUYA_SDK_CFLAGS += -DTEF_RGB565_BYTE_SWAP=1
# 素材目录（②的输出）：
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/<你的素材目录>
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/<你的素材目录> -name "*.c")
```

### ⑤ 可选：编码参数调优

素材有特殊需求时直接用编码器（`gen_assets.py` 内部即调它）：

```bash
python3 tef_encoder.py in.gif out.tef --jpeg-q 0     # 设备端必须禁用 JPEG 帧
    --window 14      # deflate 窗口位数 9~15：+1 内存翻倍换 flash 略降（默认 14 = 16KB 字典）
    --seg 16         # 成段帧数：越大压得越好，段边界解码峰值越高
    --stabilize auto # 时域整形：AI 生成/抖动量化素材前处理，见下
```

**`--stabilize`（时域整形，AI/抖动素材前处理）**：AI 生成工具与常见 GIF 导出的
抖动量化会把帧间闪烁噪声烤进像素，令 TEF 差分脏区虚高、体积达不到理想压缩比
（甚至反超 GIF）。开启后编码前把帧间微小色差（`max(ΔR,ΔG,ΔB) ≤ 阈值`）的像素
钉回上一帧值——真动作不受影响，清掉的是人眼不可见的闪烁。`auto` 按 Δ 分布
自动选档（8/16/24/32）或跳过干净素材，且整形后体积没变小会自动回退，可放心
默认给 AI 素材加上（`gen_assets.py` 同样支持 `--stabilize auto`）。
EYES 现有素材实测：4 个含噪表情 −8%~−24%，干净素材自动跳过、字节不变。三条边界：
① 开启后「无损」指对整形后帧逐像素无损，对原始 GIF 为感知无损（误差 ≤ 所选阈值）；
② 淡入淡出类缓慢渐变内容会产生台阶感，不建议开启；③ 默认关闭，仅编码端前处理，
格式与设备端解码器零改动。

桌面预览/校验：`tef_player.py`（窗口播放）、`tef_decoder.py`（解码转储）。

## 验证方法

1. 编译烧录后看启动日志有 `tef: load <W>x<H> <N> frames ...`，播放期间每 2s 一行
   `[TEF-STAT] frames=.. itv(avg/max)=..`——`itv` 应稳定在素材帧间隔（如 17fps → 58ms）。
2. 量化对比 GIF：`make app APP_NAME=tuyaos_demo_wukong_ai TEF_BENCH=1` 编入基准，
   启动时输出 `[TEF-BENCH]` 报告（flash / 内存精确字节 / 每帧 µs 与 CPU%），
   详见 [`src/miscs/tef/bench/`](../../src/miscs/tef/bench/)。

## 常见问题

- **`tef: unsupported version` 拒载**：播放器仅支持 v3 素材；旧文件用
  `tef_encoder.py` 重编（GIF 源在手时直接重跑 `gen_assets.py`）。
- **整屏颜色异常（如粉紫色调）**：RGB565 字节序不匹配，8bit SPI 面板需
  `-DTEF_RGB565_BYTE_SWAP=1`（见步骤④）。
- **`tef: canvas WxH mismatch display`**：常驻播放器（形态 A）要求素材画布等于
  屏幕逻辑分辨率，改 GIF 源重新生成；一次性播放（形态 B）只要求素材画布不大于
  目标窗口（小于时居中）。
- **`tef: jpeg seg not supported` 告警、画面跳帧**：素材编码时开了 JPEG 帧；
  设备端不带 JPEG 解码，务必 `--jpeg-q 0`（`gen_assets.py` 默认已禁用）。
- **播放线程 crash / 只见第一帧**：多为解码器被放上线程栈（tinfl 单例约 10KB）
  或素材未常驻；集成约束详见 [`src/miscs/tef/README.md`](../../src/miscs/tef/README.md)
  「设计要点与坑」。

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
