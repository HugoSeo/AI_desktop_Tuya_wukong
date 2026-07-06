# 提示音资源目录说明

本文档介绍如何将 MP3 文件转换为 C 源文件，并在悟空 AI 项目中作为**本地提示音**或**云端提示音**使用。

## 目录结构

```
assets/
├── scripts/
│   └── gen_media_src.py    # MP3 转 C 文件生成脚本
├── media_src.h             # 媒体资源总头文件
├── media_src_zh.h          # 中文提示音声明
├── media_src_zh.c          # 中文提示音数据
├── media_src_en.h          # 英文提示音声明
├── media_src_en.c          # 英文提示音数据
└── README.md               # 本文档
```

## 一、MP3 文件命名规范

脚本按文件名后缀将 MP3 分为**中文**和**英文**两类：

| 后缀 | 语言 | 示例 |
|------|------|------|
| `*_zh.mp3` | 中文 | `dingdong_zh.mp3` |
| `*_en.mp3` | 英文 | `wakeup_en.mp3` |

**重要**：不以后缀 `zh.mp3` 或 `en.mp3` 结尾的 MP3 文件会被忽略。

## 二、生成 C 文件步骤

### 1. 准备 MP3 文件

将所有 MP3 文件放入同一目录（如 `scripts/mp3/` 或项目中的其他 mp3 目录）。

### 2. 运行生成脚本

```bash
# 从 assets/scripts 目录运行
cd src/wukong/assets/scripts
python gen_media_src.py <mp3文件所在目录路径>
```

**示例**：若 MP3 文件在 `scripts/mp3` 目录下：

```bash
python gen_media_src.py ./mp3
# 或使用绝对路径
python gen_media_src.py /path/to/your/mp3_folder
```

### 3. 生成结果

脚本会：

- 遍历指定目录下所有 `*.mp3` 文件
- 按 `zh`/`en` 后缀分类
- 在 `assets/` 目录生成：
  - `media_src_zh.h` / `media_src_zh.c`（中文）
  - `media_src_en.h` / `media_src_en.c`（英文）
  - `media_src.h`（统一包含上述头文件）

### 4. 变量命名规则

- 文件名：`dingdong_zh.mp3` → C 数组名：`media_src_dingdong_zh`
- 文件名：`wakeup_en.mp3` → C 数组名：`media_src_wakeup_en`
- 文件名中的 `-` 会替换为 `_`

## 三、在代码中使用提示音

### 1. 包含头文件

```c
#include "media_src.h"
```

### 2. 播放提示音

在 `wukong_audio_player.c` 中，提示音通过 `wukong_audio_play_data()` 播放：

```c
#include "media_src.h"

// 播放 dingdong_zh 提示音
audio_data = (CONST CHAR_T*)media_src_dingdong_zh;
audio_size = sizeof(media_src_dingdong_zh);
wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size);
```

### 3. 添加新的提示音类型

在 `wukong_audio_player_alert()` 的 `switch (type)` 中按 `TY_AI_TOY_ALERT_TYPE_E` 选择不同提示音：

1. 在 MP3 目录中放入新文件（以 `_zh.mp3` 或 `_en.mp3` 结尾）
2. 运行 `gen_media_src.py` 重新生成 C 文件
3. 在 `switch (type)` 中增加对应 case

**示例**：添加「网络配置」中文提示音：

```c
case AI_TOY_ALERT_TYPE_NETWORK_CFG:
    audio_data = (CONST CHAR_T*)media_src_network_config_zh;
    audio_size = sizeof(media_src_network_config_zh);
    break;
```

## 四、云端提示音使用说明

除本地 MP3 外，悟空 AI 支持**云端提示音**：由云端 TTS 生成语音并下发给设备播放，可动态更换文案、音色，无需重新烧录固件。

### 1. 启用云端提示音

在 `include/tuya_device_cfg.h` 中配置：

```c
#define ENABLE_CLOUD_ALERT 1   /* 启用云端提示音，默认 0 为关闭 */
```

### 2. 提示音优先级

当 `ENABLE_CLOUD_ALERT == 1` 时，以下类型的提示音会**优先请求云端**；云端成功时播放云端 TTS，失败或不支持时**回退到本地 MP3**：

| 提示音类型 | 说明 |
|------------|------|
| `AI_TOY_ALERT_TYPE_NETWORK_CONNECTED` | 联网成功 |
| `AI_TOY_ALERT_TYPE_WAKEUP` | 唤醒（你好我在） |
| `AI_TOY_ALERT_TYPE_LONG_KEY_TALK` | 长按键对话 |
| `AI_TOY_ALERT_TYPE_KEY_TALK` | 按键对话 |
| `AI_TOY_ALERT_TYPE_WAKEUP_TALK` | 唤醒对话 |
| `AI_TOY_ALERT_TYPE_RANDOM_TALK` | 任意对话 |

以下类型**仅使用本地**，不走云端（无网或云端不支持）：

- `AI_TOY_ALERT_TYPE_POWER_ON`（开机播报）
- `AI_TOY_ALERT_TYPE_BATTERY_LOW`（电量不足）
- `AI_TOY_ALERT_TYPE_PLEASE_AGAIN`（请再说一遍）
- `AI_TOY_ALERT_TYPE_NOT_ACTIVE`（未配网）
- `AI_TOY_ALERT_TYPE_NETWORK_CFG`（配网中）
- `AI_TOY_ALERT_TYPE_NETWORK_FAIL`（联网失败）
- `AI_TOY_ALERT_TYPE_NETWORK_DISCONNECT`（掉网）

### 3. 云端提示音流程

```
wukong_audio_player_alert(type)
        │
        ├─ ENABLE_CLOUD_ALERT==1 且 type 支持云端
        │       │
        │       └─► wukong_ai_agent_cloud_alert(type)
        │               │
        │               ├─ 成功 → 发送命令到云端 → 云端 TTS 流 → 设备播放
        │               └─ 失败 → 回退到本地 MP3
        │
        └─ 其他 → 直接播放本地 media_src_xxx
```

### 4. 主动请求云端提示音

应用层可直接调用 `wukong_ai_agent_cloud_alert()` 请求云端提示音（需先 `#include "wukong_ai_agent.h"`）：

```c
#include "wukong_ai_agent.h"

/* 请求「联网成功」云端提示音 */
wukong_ai_agent_cloud_alert(AT_NETWORK_CONNECTED);

/* 请求「唤醒」云端提示音 */
wukong_ai_agent_cloud_alert(AT_WAKEUP);
```

### 5. 使用建议

- **联网场景**：优先用云端提示音，便于远程更新文案和音色。
- **离线/配网场景**：必须使用本地 MP3，保证无网也能播报。
- **首次使用**：建议先关闭 `ENABLE_CLOUD_ALERT`，验证本地提示音正常后再开启云端。

## 五、MP3 文件要求（本地提示音）
- **音色**：尽量使用和产品`AI`角色的音色来生成 
- **格式**：标准 MP3，采样率16K、位宽16bit、声道为mono
- **码率**：建议 32 kbps，以减小固件体积
- **时长**：提示音宜控制在 1–3 秒
- **体积**：过大会显著增加 Flash 占用，需权衡

## 六、注意事项

1. **常量存储**：生成的数组声明为 `CONST BYTE_T`，存放在 Flash 中，不占用 RAM。
2. **依赖**：`media_src.h` 依赖 `tuya_cloud_types.h`（通过 `media_src_zh.h` / `media_src_en.h` 引入）。
3. **编译**：`media_src_zh.c`、`media_src_en.c` 需加入工程编译。
4. **修改 MP3 后**：每次增删或修改 MP3，需重新执行 `gen_media_src.py` 并重新编译工程。
