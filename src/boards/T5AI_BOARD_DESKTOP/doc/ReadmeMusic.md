# 桌面机器人音乐与故事播放功能技术文档

## 1. 概述

音乐/故事播放是桌面机器人的核心 AI 技能之一。用户通过语音指令请求播放音乐或故事，AI 服务端返回携带媒体 URL 的 Skill JSON，设备端解析后通过背景播放器流式播放。播放期间若有 TTS 语音回复，前景播放器（TTS）与背景播放器（音乐）自动混音，TTS 期间背景音量自动降至 50%，TTS 结束后恢复。

**功能特性：**
- 支持 `play`、`next`、`prev`、`resume`、`stop`、`replay`、`single_loop`、`sequential_loop`、`no_loop` 等播放控制动作
- 前景播放器（TTS/提示音）与背景播放器（音乐/故事）双路混音
- 支持 MP3 格式在线流播放
- 播放列表持久化存储到文件系统（`/t5_fs/music/music_list.json`），支持跨会话保留
- 桌面板型提供完整播放器 UI：歌曲/艺术家信息自动滚动、播放模式切换、列表管理

**相关核心文件：**

| 文件 | 说明 |
|------|------|
| `src/wukong/skills/skill_music_story.h/.c` | 音乐/故事 Skill：JSON 解析与动作分派 |
| `src/wukong/skills/wukong_ai_skills.c` | Skill 事件处理入口 |
| `src/wukong/audio/wukong_audio_player.h/.c` | 双路播放器初始化与控制 API |
| `src/wukong/audio/wukong_playback_ctrl.h/.c` | 播放列表管理与 MQTT 控制（可选） |
| `src/boards/T5AI_BOARD_DESKTOP/ui/desk_func_music.h/.c` | 桌面音乐播放器 UI |
| `include/tuya_app_config.h` | 配置参数定义 |

---

## 2. 编译配置

```c
#define ENABLE_TOOLKITS_PLAYBACK       1    // 启用播放列表工具集（MCP 工具）
#define AI_PLAYER_DECODER_OPUS_ENABLE  1    // 启用 OPUS 解码器
#define AI_PLAYER_DECODER_OGGOPUS_ENABLE 1  // 启用 OggOpus 解码器
#define USING_BOARD_AUDIO_OUTPUT       1    // 启用板载音频输出
#define TY_SPK_DEFAULT_VOL             70   // 默认音量（百分比）
```

---

## 3. 配置参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `AI_PLAYER_STACK_SIZE` | `6144`（含 OPUS 时 12 KB） | 播放器线程栈大小 |
| `AI_PLAYER_FRAMEBUF_SIZE` | `4096` | 帧缓冲大小 |
| `AI_PLAYER_DECODEBUF_SIZE` | `8192`（OggOpus 为 9830） | 解码缓冲大小 |
| `AI_PLAYER_RINGBUF_SIZE` | `16384` | 环形缓冲大小 |
| `AI_PLAYER_HTTP_TIMEOUT_S` | `180` | HTTP 流超时（秒） |
| `TY_SPK_DEFAULT_VOL` | `70` | 扬声器默认音量（%） |
| 播放列表容量（背景播放器） | `32` 条 | 一次最多加入播放列表的歌曲数 |
| 播放列表容量（前景播放器） | `2` 条 | TTS/提示音队列深度 |
| `DESKTOP_MUSIC_LIST_JSON_PATH` | `/t5_fs/music/music_list.json` | 播放列表持久化路径 |

---

## 4. 数据结构

### 4.1 音乐源信息

```c
typedef struct {
    UINT_T           id;          // 音乐 ID
    CHAR_T          *url;         // 在线播放 URL
    INT64_T          length;      // 文件大小（字节）
    INT64_T          duration;    // 时长（毫秒）
    AI_AUDIO_CODEC_E format;      // 音频格式（当前仅 MP3）
    CHAR_T          *artist;      // 艺术家名称
    CHAR_T          *song_name;   // 歌曲名称
    CHAR_T          *audio_id;    // 音频 ID
    CHAR_T          *img_url;     // 专辑封面 URL
} WUKONG_AI_MUSIC_SRC_T;
```

### 4.2 音乐任务描述

```c
typedef struct {
    CHAR_T                action[32];   // 动作：play/next/prev/resume/stop/replay/single_loop/sequential_loop/no_loop
    BOOL_T                has_tts;      // 是否有 TTS 先于音乐播放
    INT_T                 src_cnt;      // 音乐源数量
    WUKONG_AI_MUSIC_SRC_T *src_array;  // 音乐源数组（heap 分配）
} WUKONG_AI_MUSIC_T;
```

### 4.3 播放器 UI 消息

```c
typedef enum {
    MUSIC_PLAYER_STATE,   // 携带播放状态（PLAYING/STOPPED/PAUSED）
    MUSIC_PLAYER_DATA,    // 携带歌曲信息（名称/艺术家/URL）
} WUKONG_MUSIC_PLAYER_TYPE_E;

typedef struct {
    WUKONG_MUSIC_PLAYER_TYPE_E cmd;
    AI_PLAYER_STATE_T           state;
    char song_name[128];
    char artist[128];
    char song_url[512];
} WUKONG_MUSIC_PLAYER_T;
```

---

## 5. 双路播放器架构

| 特性 | 背景播放器（`AI_PLAYER_BG`） | 前景播放器（`AI_PLAYER_FG`） |
|------|--------------------------|--------------------------|
| 用途 | 音乐/故事等长音频 | TTS 回复/提示音 |
| 创建模式 | `AI_PLAYER_MODE_BACKGROUND` | `AI_PLAYER_MODE_FOREGROUND` |
| 播放列表容量 | 32 条 | 2 条 |
| 自动播放 | 是 | 是 |
| 支持暂停/恢复 | 是 | 否（只能停止） |

**混音规则：**

```
TTS 开始播放 → 背景音乐音量降至 50%
TTS 播放结束 → 背景音乐恢复原音量
```

### 5.1 播放器初始化

```c
OPERATE_RET wukong_audio_player_init(void)
{
    // 1. 初始化播放器服务（采样率 16000，16bit，单声道）
    tuya_ai_player_service_init(&cfg);

    // 2. 创建前景播放器（TTS）
    tuya_ai_player_create(AI_PLAYER_MODE_FOREGROUND, &__s_tone_player);
    tuya_ai_playlist_create(__s_tone_player, {.auto_play=TRUE, .capacity=2}, &__s_tone_playlist);

    // 3. 创建背景播放器（音乐）
    tuya_ai_player_create(AI_PLAYER_MODE_BACKGROUND, &__s_music_player);
    tuya_ai_playlist_create(__s_music_player, {.auto_play=TRUE, .capacity=32}, &__s_music_playlist);

    // 4. 订阅播放器状态事件，处理 TTS/音乐混音逻辑
    ty_subscribe_event(EVENT_AI_PLAYER_STATE, "wk_player", __player_event, ...);

    // 5. 启用混音模式
    tuya_ai_player_set_mix_mode(TRUE);
}
```

### 5.2 播放控制 API

```c
/* 播放音乐（清空旧列表，批量加入新列表） */
OPERATE_RET wukong_audio_play_music(WUKONG_AI_MUSIC_T *music);

/* 暂停/恢复/停止背景播放器 */
OPERATE_RET wukong_audio_player_pause(void);
OPERATE_RET wukong_audio_player_resume(void);
OPERATE_RET wukong_audio_player_stop(TY_AI_TOY_PLAYER_TYPE_E type);
//   type: AI_PLAYER_FG（前景）| AI_PLAYER_BG（背景）| AI_PLAYER_ALL（全部）

/* 播放本地/URL 文件 */
OPERATE_RET wukong_audio_play_local(const char *path, const char *name,
                                    const char *artist,
                                    AI_AUDIO_CODEC_E codec, INT_T position);

/* 播放提示音/告警音 */
OPERATE_RET wukong_audio_player_alert(AI_TOY_ALERT_TYPE_E type, BOOL_T repeat);
```

---

## 6. Skill 解析与事件处理

### 6.1 Skill 入口

AI 服务端返回带 `code="music"` 或 `code="story"` 的 Skill JSON，由 `wukong_ai_skills.c` 统一分发：

```c
/* wukong_ai_skills.c */
STATIC VOID __wukong_ai_skill_process(AI_TEXT_TYPE_E type, ty_cJSON *root, BOOL_T eof)
{
    node = ty_cJSON_GetObjectItem(root, "code");
    code = ty_cJSON_GetStringValue(node);

    if (strcmp(code, "music") == 0 || strcmp(code, "story") == 0) {
        WUKONG_AI_MUSIC_T *music = NULL;
        wukong_ai_parse_music(root, &music);        // 解析 JSON
        wukong_ai_parse_music_dump(music);           // 打印 + 通知 UI
        wukong_ai_play_music(music);                 // 执行播放动作
        wukong_ai_parse_music_free(music);           // 释放 heap
    } else if (strcmp(code, "PlayControl") == 0) {
        // 处理播放控制命令（来自 APP 面板操作）
    }
}
```

### 6.2 播放动作分派

```c
/* skill_music_story.c */
VOID wukong_ai_play_music(WUKONG_AI_MUSIC_T *music)
{
    if (strcmp(music->action, "play") == 0 && music->src_cnt > 0) {
        wukong_audio_play_music(music);                               // 播放（清旧列表）
    } else if (strcmp(music->action, "resume") == 0) {
        wukong_ai_event_notify(WUKONG_AI_EVENT_PLAY_CTL_RESUME, NULL);
    } else if (strcmp(music->action, "stop") == 0) {
        wukong_ai_event_notify(WUKONG_AI_EVENT_PLAY_CTL_PAUSE, NULL);
    } else if (strcmp(music->action, "replay") == 0) {
        wukong_ai_event_notify(WUKONG_AI_EVENT_PLAY_CTL_REPLAY, NULL);
    } else if (strcmp(music->action, "prev") == 0 ||
               strcmp(music->action, "next") == 0) {
        if (music->src_cnt > 0) wukong_audio_play_music(music);      // 上/下一首
    } else if (strcmp(music->action, "single_loop") == 0) {
        wukong_ai_event_notify(WUKONG_AI_EVENT_PLAY_CTL_SINGLE_LOOP, NULL);
    } else if (strcmp(music->action, "sequential_loop") == 0) {
        wukong_ai_event_notify(WUKONG_AI_EVENT_PLAY_CTL_SEQUENTIAL_LOOP, NULL);
    } else if (strcmp(music->action, "no_loop") == 0) {
        wukong_ai_event_notify(WUKONG_AI_EVENT_PLAY_CTL_SEQUENTIAL, NULL);
    }
}
```

### 6.3 JSON 结构示例

AI 服务端返回的 Skill JSON 格式：

```json
{
    "code": "music",
    "data": {
        "action": "play",
        "audios": [
            {
                "id": 1,
                "url": "https://example.com/song.mp3",
                "size": 3145728,
                "duration": 180000,
                "format": "mp3",
                "artist": "歌手名",
                "name": "歌曲名",
                "audioId": "abc123",
                "imageUrl": "https://example.com/cover.jpg"
            }
        ]
    }
}
```

---

## 7. 完整数据流

```
AI 服务端（Skill JSON）
    │
    └─→ wukong_ai_text_process(AI_TEXT_SKILL, json)
         │
         └─→ __wukong_ai_skill_process()
              │
              ├─→ wukong_ai_parse_music()         // 解析 JSON → WUKONG_AI_MUSIC_T
              │
              ├─→ wukong_ai_parse_music_dump()
              │    └─→ EVENT_MUSIC_PLAYER (MUSIC_PLAYER_DATA)
              │         └─→ music_player_event()  // 更新 UI 歌曲信息
              │
              └─→ wukong_ai_play_music()
                   │
                   └─→ wukong_audio_play_music()
                        │
                        └─→ tuya_ai_playlist_clear()
                            tuya_ai_playlist_add(URL, MP3)  // 批量加入
                            tuya_ai_player 自动开始播放
                                 │
                                 └─→ EVENT_AI_PLAYER_STATE (PLAYING/STOPPED)
                                      │
                                      └─→ __player_event()
                                           ├─→ TTS 期间降低背景音量至 50%
                                           └─→ EVENT_MUSIC_PLAYER (MUSIC_PLAYER_STATE)
                                                └─→ music_player_event()  // 更新 UI 状态
```

---

## 8. 播放列表管理（可选，`ENABLE_TOOLKITS_PLAYBACK`）

### 8.1 核心接口

```c
/* 添加歌曲到播放列表 */
OPERATE_RET wukong_playback_playlist_add(INT_T id, const char *song_name,
                                        const char *artist, const char *url,
                                        const char *audio_id, const char *channel_code);

/* 播放指定 ID 的歌曲 */
OPERATE_RET wukong_playback_playlist_play(INT_T id);

/* 本地切换上/下一首 */
OPERATE_RET wukong_playback_playlist_next(void);
OPERATE_RET wukong_playback_playlist_prev(void);

/* 获取播放列表（JSON 格式） */
OPERATE_RET wukong_playback_playlist_list(ty_cJSON **out);

/* 发送 MQTT 播放控制请求到云端 */
OPERATE_RET wukong_playback_ctrl_send_mqtt(const char *action);
```

### 8.2 持久化存储

```
/t5_fs/music/
└── music_list.json        // 播放列表持久化
```

---

## 9. 播放器 UI（T5AI_BOARD_DESKTOP）

### 9.1 主播放器屏幕布局

```
┌────────────────────────────────────┐
│ [←返回]                            │  ← 标题栏
├────────────────────────────────────┤
│                                    │
│   歌曲名（自动横向滚动）            │
│   艺术家（自动横向滚动）            │
│                                    │
│   [单循环] [列表循环] [随机]        │  ← 播放模式按钮
│                                    │
│   [上一首]  [播放/暂停]  [下一首]   │  ← 控制按钮
│                                    │
│   [播放列表]                        │  ← 切换到列表屏
└────────────────────────────────────┘
```

### 9.2 播放列表屏幕

```
┌────────────────────────────────────┐
│ [←返回]    播放列表                 │
├────────────────────────────────────┤
│ ┌──────────────────────────────┐   │
│ │▶ 歌曲名（正在播放，高亮背景）│   │  ← 当前播放条目
│ └──────────────────────────────┘   │
│ ┌──────────────────────────────┐   │
│ │  歌曲名                  [×] │   │  ← 可点击播放 / 右侧删除
│ └──────────────────────────────┘   │
│ ...                                │
└────────────────────────────────────┘
```

### 9.3 UI 事件处理

```c
/* 订阅播放器事件，更新 UI */
int music_player_event(void *data)
{
    WUKONG_MUSIC_PLAYER_T *msg = (WUKONG_MUSIC_PLAYER_T *)data;

    if (msg->cmd == MUSIC_PLAYER_STATE) {
        s_music_player.state = msg->state;
        music_state_update();    // 更新播放/暂停图标
    } else if (msg->cmd == MUSIC_PLAYER_DATA) {
        // 更新歌曲名、艺术家、URL
        music_info_update();     // 重置滚动动画，刷新标签
    }
}
```

---

## 10. 播放器状态事件

| 事件 | 触发时机 | 处理动作 |
|------|---------|---------|
| `WUKONG_AI_EVENT_PLAY_CTL_PLAY` | 背景音乐开始 | 通知 UI 更新状态 |
| `WUKONG_AI_EVENT_PLAY_CTL_PAUSE` | TTS 开始/用户暂停 | 背景音量降至 50% |
| `WUKONG_AI_EVENT_PLAY_CTL_RESUME` | TTS 结束/用户恢复 | 背景音量恢复 |
| `WUKONG_AI_EVENT_PLAY_CTL_SINGLE_LOOP` | AI 设置单曲循环 | 更新播放模式图标 |
| `WUKONG_AI_EVENT_PLAY_CTL_SEQUENTIAL_LOOP` | AI 设置列表循环 | 更新播放模式图标 |
| `WUKONG_AI_EVENT_PLAY_CTL_REPLAY` | AI 命令重播 | 重新播放当前曲目 |
| `EVENT_MUSIC_BREAK` | `AI_PLAYER_ALL` 停止 | 通知 UI 播放结束 |

---

## 11. 调试日志

```
# Skill 解析与播放
[skill] code: music, action: play, src_cnt: 2
[player] music url https://example.com/1.mp3
[player] music url https://example.com/2.mp3
[player] state: PLAYING, player: BG

# 混音
[player] TTS start, lower bg volume to 50%
[player] TTS stop, restore bg volume

# 播放列表持久化
[music_ui] playlist saved to /t5_fs/music/music_list.json
[music_ui] playlist loaded, item_count=5
```

---

## 12. 常见问题

| 现象 | 可能原因 | 排查方向 |
|------|---------|---------|
| 音乐无声 | 扬声器静音或默认音量为 0 | 检查 `TY_SPK_DEFAULT_VOL`，确认 `USING_BOARD_AUDIO_OUTPUT=1` |
| 只播放第一首就停止 | 播放列表未正确加入 | 检查 `tuya_ai_playlist_add` 返回值 |
| TTS 和音乐同时满音量 | 混音模式未启用 | 确认 `tuya_ai_player_set_mix_mode(TRUE)` 已调用 |
| 播放列表重启后丢失 | JSON 文件未保存 | 确认 `ENABLE_TOOLKITS_PLAYBACK=1`，检查文件系统路径 |
| MP3 无法解码 | 解码器未编译 | 确认 `AI_PLAYER_DECODER_OPUS_ENABLE`（MP3 解码器需单独确认） |
