<p align="center"><a href="https://tuyaos.com" target="_blank"><img src="https://github.com/tuya/.github/raw/main/profile/site_logo.png" width="400"></a></p>

## Overview

The **Wukong AI Hardware Development Framework** is an innovation platform built on TuyaOS for commercial AI hardware development. It inherits TuyaOS strengths in cross-platform compatibility, modular and componentized design, and adds enhanced text, audio, and image processing. The framework supports multiple LLMs via Tuya Cloud (e.g. DeepSeek, Doubao, Qwen, Kimi, Yuanbao, as well as ChatGPT, Claude, Gemini), and combines with LLM agents to deliver text/voice dialogue, image generation, and video generation, targeting AI toys, AI robots, AI appliances, and AI consumer electronics. Developers can obtain the framework and build products via [Tuya Wind IDE](https://developer.tuya.com/cn/docs/iot-device-dev/ai-hardware?id=Kectwmx9isrgl).

This repo (**tuyaos_demo_wukong_ai**) is a reference application on top of that framework for developers building AI voice and multimodal devices on TuyaOS. It provides reference architecture and module layout. Main capabilities: multiple AI dialogue modes (hold, oneshot, wake word, free talk, P2P, translate), multimodal I/O (audio, video, image), on-device MCP (e.g. volume, photo recognition), AI skills (clock, emotion, music story, etc.), wake word and AEC/VAD. Source is organized by domain: **wukong** (AI core), **mode** (dialogue modes), **boards** (BSP and UI), **miscs** (GUI, audio player, etc.), **drivers** (camera, display, key, etc.).

## Applicable Scenarios

The Wukong AI Hardware Development Framework is widely used for **AI toys**, **AI robots**, **AI appliances**, and **AI consumer electronics**, among other product categories. This sample serves as a reference implementation for these scenarios, helping you understand the framework and extend it for your own products.

## Quick Start

For prerequisites, fetching source, configuration, build, flash, and run, follow **[Quick Start](docs/QUICKSTART.md)**.

## Source and Document Index

Below maps source directories to document entries. Use the links to open each module’s README.

| Document | Description |
|----------|-------------|
| [Quick Start](docs/QUICKSTART.md) | Prerequisites, get source, configure and build, flash and run |

### Functional Modules

| Domain | Document | Description |
|--------|----------|-------------|
| **wukong** | [Wukong AI core](src/wukong/README_CN.md) | AI dialogue, audio, mode, skills, MCP |
| ├─ audio | [Audio](src/wukong/audio/README_CN.md) | Audio I/O, AEC/VAD, player (CN) |
| ├─ kws | [KWS](src/wukong/kws/README_CN.md) | Keyword spotting (CN) |
| ├─ mcp | [MCP server](src/wukong/mcp/README.md) | Model Context Protocol on-device |
| ├─ assets | [Assets](src/wukong/assets/README_CN.md) | Prompts and assets (CN) |
| **mode** | [Mode](src/mode/README_CN.md) | Dialogue modes (CN) |
| **boards** | [Boards](src/boards/README_CN.md) | BSP and boards (CN) |
| **miscs** | — | GUI, audio player, uart_codec, etc. (see subdirs) |
| **drivers** | — | Camera, display, key, LED, TP, IMU (see subdirs) |

## Support

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
