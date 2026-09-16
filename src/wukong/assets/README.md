# 提示音资源目录

## 概述

本目录提供 MP3 → C 数组的转换脚本（`scripts/gen_media_src.py`）与生成产物（`media_src.h/c`），用于悟空 AI 的**本地提示音**；同时说明本地提示音与**云端提示音**（云端 TTS 下发播放）之间的选择逻辑，二者由 `wukong_audio_player_alert()`（`src/wukong/audio/wukong_audio_player.c`）统一驱动。当前实际编译进固件并会被播放到的本地音频只有一个：`media_src_dingdong_zh`（源文件 `media/dingdong_zh.mp3`）。

## 设计要点与坑

- **生成规则**：MP3 按文件名后缀分类（`*_zh.mp3` 中文 / `*_en.mp3` 英文，其它后缀的文件会被脚本忽略），在 `assets/scripts/` 下执行 `python gen_media_src.py <mp3目录>` 后，会在 `assets/` 生成 `media_src_zh.h/c`、`media_src_en.h/c` 及统一包含二者的 `media_src.h`；数组名规则是文件名去掉 `.mp3` 后缀、`-` 替换为 `_`（如 `dingdong_zh.mp3` → `media_src_dingdong_zh`）。
- **常量占 Flash 不占 RAM**：生成的数组声明为 `CONST BYTE_T`，但一旦对应 `.c` 加入工程编译就会计入固件体积。
- **坑（重要）：本地兜底目前只有一种声音，不按类型区分**——不论 `wukong_audio_player_alert(type, ...)` 传入的 `TY_AI_TOY_ALERT_TYPE_E` 是哪一种，只要没有成功走云端播报，最终都会播放同一个 `media_src_dingdong_zh`。`media_src_en.h` 里声明的 9 个英文提示音（`key_dialogue_en` / `wakeup_en` / `network_config_en` 等，对应源 mp3 已不在 `media/` 目录下）目前**没有任何代码路径引用**，属于生成过但已死掉的资源，纯粹占用约 620KB Flash。想要真正按类型/语言播放不同本地提示音，需要自己在 `wukong_audio_player_alert()` 的 `switch` 里给对应 `case` 显式赋值 `audio_data = media_src_xxx`，不要假设现有代码已经这样做了。
- **哪些类型会先尝试云端**：当前 switch 的 fall-through 结构里，`POWER_ON / NETWORK_CONNECTED / BATTERY_LOW / PLEASE_AGAIN / LONG_KEY_TALK / KEY_TALK / WAKEUP_TALK / RANDOM_TALK / WAKEUP` 这 9 个类型都会先落到 `#if ENABLE_CLOUD_ALERT` 判断，尝试 `wukong_ai_agent_cloud_alert(type)`，成功即 `break`；只有 `NOT_ACTIVE / NETWORK_CFG / NETWORK_FAIL / NETWORK_DISCONNECT` 这 4 个类型的 `case` 写在该判断之后，永远不会尝试云端。云端失败或未使能时，前 9 个类型和后 4 个类型一样，都落到同一份 `media_src_dingdong_zh`。
- **云端开关是 Kconfig 项，不是手改宏**：Kconfig 声明在**应用仓库**（tuyaos_demo_wukong_ai）的 `build/APPconfig`（"Enable cloud alert sounds"，默认 n），本仓库（wukong_ai）内检索不到属正常。命名约定：`.config` 快照里是 `CONFIG_ENABLE_CLOUD_ALERT`，而生成到 `include/tuya_app_config.h` 的 C 宏是**裸名** `#define ENABLE_CLOUD_ALERT 1`（该头文件所有宏均无 `CONFIG_` 前缀），代码里 `#if defined(ENABLE_CLOUD_ALERT)` 判断的正是它。开启方式：`make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai` 勾选后 `make app_config APP_NAME=tuyaos_demo_wukong_ai` 生成；不要手写 `#define`。
- **MP3 素材要求**：采样率 16K、16bit、单声道；建议码率 32kbps、时长控制在 1~3 秒，避免生成文件过大挤占 Flash。

## 改动指南

- **增删改 MP3 后**：把新文件放进素材目录（按 `_zh`/`_en` 后缀命名），执行 `cd src/wukong/assets/scripts && python gen_media_src.py <mp3目录所在路径>` 重新生成，并确认改动过的 `media_src_*.c` 已加入工程编译，然后重新编译整个工程。
- **想让某个 `AI_TOY_ALERT_TYPE_E` 播放专属本地提示音**（而不是统一的 dingdong）：在 `wukong_audio_player_alert()` 的 `switch` 里为该 `case` 显式赋值 `audio_data`/`audio_size` 为对应的 `media_src_xxx` 数组（可以是 `media_src_en.h` 里现成但目前未接线的英文数组），别指望它已经按类型自动分流。
- **想让某个类型也支持云端优先播报**：把该类型的 `case` 移到当前 9 个已接入云端判断的 `case` 那一组里（`#if ENABLE_CLOUD_ALERT` 判断之前），并确认 `wukong_ai_agent_cloud_alert()`（实现见 `src/wukong/provider/wukong_ai_provider.c`）对该类型有对应处理。
- **想默认开启云端提示音**：`make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai` 打开 "Enable cloud alert sounds" 后 `make app_config APP_NAME=tuyaos_demo_wukong_ai` 使其生效；建议先在断网场景下确认本地兜底（dingdong）能正常播放，再开启云端。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
