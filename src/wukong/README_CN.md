# 🐵 Wukong AI 核心模块

## 📋 概述

`wukong` 目录包含了 Wukong AI 智能交互系统的核心功能模块，负责 AI 对话、音频处理、模式管理、技能系统等核心功能。

## 📁 目录结构

```
wukong/
├── 🔊 [audio/](audio/README_CN.md)            # 音频处理模块（输入输出、前端、KWS）
├── 🎯 [mode/](mode/README.md)                # AI 对话模式实现
├── 🎨 [skills/](skills/README.md)             # AI 技能系统
├── 🔧 [mcp/](mcp/README.md)                  # MCP (Model Context Protocol) 服务器
├── 🎥 [video/](video/README.md)               # 视频输入输出处理
├── 🖼️  [picture/](picture/README.md)           # 图像输入输出处理
├── 🛠️  utility/                                # 工具函数
├── 📦 assets/                                 # 资源文件（提示音等）
├── 🤖 wukong_ai_agent.c                       # AI 代理核心实现
└── 📄 wukong_ai_agent.h                       # AI 代理接口定义
```

## 🤖 核心模块: wukong_ai_agent

### 📖 简介

`wukong_ai_agent` 是 Wukong AI 系统的核心模块，作为所有 AI 交互的中心枢纽。它管理与云端 AI 服务的通信，处理事件通知，并协调不同子系统之间的交互。

### ✨ 核心特性

- **🔄 事件驱动架构**: 基于回调的事件通知系统
- **📱 多媒体支持**: 支持文本、音频、视频和图像数据传输
- **☁️ 云端集成**: 与涂鸦云端 AI 服务集成
- **🎨 技能管理**: 将文本流路由到技能处理系统
- **🔊 TTS 播放**: 管理文本转语音音频流播放
- **👤 角色切换**: 支持动态 AI 角色/人设切换

### 🔄 业务流程图

```
┌─────────────────────────────────────────────────────────────┐
│                    Wukong AI 系统流程                         │
└─────────────────────────────────────────────────────────────┘

1. 初始化阶段
   ┌─────────────────┐
   │ 系统启动        │
   └────────┬────────┘
            │
            ▼
   ┌─────────────────────────┐
   │ wukong_ai_agent_init()  │ ──► 注册事件回调
   │                         │ ──► 配置音频/视频编解码器
   │                         │ ──► 注册云端回调
   └────────┬────────────────┘
            │
            ▼
   ┌─────────────────────────┐
   │ 音频/KWS/模式初始化      │
   └─────────────────────────┘

2. 输入处理阶段
   ┌─────────────────┐
   │ 用户输入        │ ──► 语音/音频/文本/图像/视频
   └────────┬────────┘
            │
            ├──► [音频输入](audio/README_CN.md) ──► 前端(AEC/VAD) ──► [KWS](audio/frontend/kws/README_CN.md) ──► ASR
            │
            ├──► 文本输入 ──► 直接处理
            │
            ├──► [图像输入](picture/README.md) ──► 图像识别
            │
            └──► [视频输入](video/README.md) ──► 视频分析
            │
            ▼
   ┌─────────────────────────┐
   │ wukong_ai_agent_send_*  │ ──► 发送到云端 AI 服务
   └────────┬────────────────┘

3. 云端处理阶段
   ┌─────────────────────────┐
   │ 云端 AI 服务            │
   │ - ASR 处理              │
   │ - LLM 处理              │
   │ - 技能识别              │
   │ - TTS 生成              │
   └────────┬────────────────┘
            │
            ▼
   ┌─────────────────────────┐
   │ 云端回调                │
   │ - 文本流回调            │
   │ - 媒体数据回调          │
   │ - 事件回调              │
   │ - MCP request           │
   └────────┬────────────────┘

4. 输出处理阶段
   ┌─────────────────────────┐
   │ [技能处理](skills/README.md) │ ──► 解析 JSON，路由到技能
   │ - 音乐/故事             │
   │ - 时钟/定时器           │
   │ - 情绪                  │
   │ - 云端事件              │
   │ - MCP request           │
   └────────┬────────────────┘
            │
            ▼
   ┌─────────────────────────┐
   │ 事件通知                │ ──► wukong_ai_event_notify()
   │ - ASR 事件              │
   │ - TTS 事件              │
   │ - 技能事件              │
   │ - 播放控制事件          │
   │ - MCP request          │
   └────────┬────────────────┘
            │
            ▼
   ┌─────────────────────────┐
   │ [模式处理器](mode/README.md) │ ──► 根据模式处理事件
   │ - 状态管理              │
   │ - LED 控制              │
   │ - 定时器管理            │
   └────────┬────────────────┘
            │
            ▼
   ┌─────────────────────────┐
   │ [MCP、文本、音频输出](audio/README_CN.md) │ ──► TTS/音乐/提示音播放
   └─────────────────────────┘
```

### 📚 核心 API

#### 初始化
- `wukong_ai_agent_init()` - 使用事件回调初始化 AI 代理
- `wukong_ai_agent_deinit()` - 反初始化并释放资源

#### 数据传输
- `wukong_ai_agent_send_text()` - 发送文本内容到 AI
- `wukong_ai_agent_send_image()` - 发送图像进行识别
- `wukong_ai_agent_send_video()` - 发送视频流
- `wukong_ai_agent_send_file()` - 发送文件数据

#### 控制
- `wukong_ai_agent_cloud_alert()` - 请求云端提示音
- `wukong_ai_agent_role_switch()` - 切换 AI 角色/人设
- `wukong_ai_event_notify()` - 通知 AI 事件

### 📖 子模块文档

各子模块的详细文档请参考：

- 🔊 [音频模块](audio/README_CN.md) - 音频输入输出、前端(AEC/VAD/KWS)、播放器
- 🎯 [模式模块](mode/README.md) - 对话模式管理
- 🎨 [技能模块](skills/README.md) - AI 技能系统
- ⏰ [时间管理模块](tm/README_CN.md) - 闹钟、提醒、倒计时、正计时、番茄时钟
- 🔧 [MCP 模块](mcp/README.md) - 模型上下文协议服务器
- 🎥 [视频模块](video/README.md) - 视频输入输出处理
- 🖼️  [图像模块](picture/README.md) - 图像输入输出处理

### 💡 使用示例

#### 基础初始化

```c
#include "wukong_ai_agent.h"
#include "wukong_ai_mode.h"

/* 事件回调函数 */
VOID my_event_handler(WUKONG_AI_EVENT_T *event)
{
    switch (event->type) {
    case WUKONG_AI_EVENT_ASR_OK:
        {
            WUKONG_AI_TEXT_T *text = (WUKONG_AI_TEXT_T *)event->data;
            printf("用户说: %s\n", text->data);
        }
        break;
        
    case WUKONG_AI_EVENT_TEXT_STREAM_DATA:
        {
            WUKONG_AI_TEXT_T *text = (WUKONG_AI_TEXT_T *)event->data;
            printf("AI 回复: %s\n", text->data);
        }
        break;
        
    default:
        break;
    }
}

/* 初始化 AI 系统 */
OPERATE_RET init_ai_system(VOID)
{
    /* 初始化 AI 代理 */
    wukong_ai_agent_init(my_event_handler);
    
    /* 初始化模式系统 */
    wukong_ai_mode_init();
    
    return OPRT_OK;
}
```

#### 发送数据到 AI

```c
/* 发送文本查询 */
wukong_ai_agent_send_text("今天天气怎么样？");

/* 发送图像进行识别 */
wukong_ai_agent_send_image(image_data, image_len);

/* 请求云端提示音 */
wukong_ai_agent_cloud_alert(AT_NETWORK_CONNECTED);

/* 切换 AI 角色 */
wukong_ai_agent_role_switch("teacher");
```

#### 完整集成示例

```c
#include "wukong_ai_agent.h"
#include "wukong_ai_mode.h"
#include "wukong_audio_input.h"
#include "wukong_audio_output.h"
#include "wukong_kws.h"

/* 事件处理器 */
VOID ai_event_handler(WUKONG_AI_EVENT_T *event)
{
    /* 根据事件类型处理 */
    switch (event->type) {
    case WUKONG_AI_EVENT_ASR_OK:
        printf("[ASR] 用户: %s\n", ((WUKONG_AI_TEXT_T *)event->data)->data);
        break;
    case WUKONG_AI_EVENT_TEXT_STREAM_DATA:
        printf("[AI] %s", ((WUKONG_AI_TEXT_T *)event->data)->data);
        break;
    default:
        break;
    }
}

/* 系统初始化 */
OPERATE_RET wukong_system_init(VOID)
{
    /* 1. 初始化 AI 代理 */
    wukong_ai_agent_init(ai_event_handler);
    
    /* 2. 初始化音频（参见 [音频模块](audio/README_CN.md)） */
    wukong_audio_input_init(&input_cfg);
    wukong_audio_output_init(&output_cfg);
    
    /* 3. 初始化 KWS（参见 [KWS 模块](audio/frontend/kws/README_CN.md)） */
    wukong_kws_init(&kws_cfg);
    
    /* 4. 初始化模式系统（参见 [模式模块](mode/README.md)） */
    wukong_ai_mode_init();
    
    return OPRT_OK;
}
```
