<!-- Auto-translated from ../README.md. Do not edit manually. -->

# Documentation Map

Find documentation by "what you want to do." This page is the single entry point for all documentation.

## Getting Started

| You want to… | See |
|---|---|
| Build, flash, and run from scratch | [Quick Start](quickstart.md) |
| Understand the overall architecture, init flow, and low-power behavior | [Architecture](architecture.md) |
| Learn about version changes and upgrade notes | [Changelog](../../CHANGELOG.md) |

## Task Guides (howto/)

Organized around real development tasks; extended continually based on high-frequency customer questions.

| You want to… | Read this |
|---|---|
| Port a new board | [Porting a New Board](howto/porting-new-board.md) |
| Add an AI skill | [Adding an AI Skill](howto/add-ai-skill.md) |
| Change the wake word / tune KWS sensitivity | [Changing the Wake Word](howto/customize-wakeword.md) |
| Attach an external offline voice chip (GX8006/CI1302 — chip handles wake & pickup, module handles AI dialogue) | [Connecting a UART Offline Voice Chip](howto/connect-uart-voice-chip.md) |
| Add or trim a dialogue mode | [Adding or Trimming a Dialogue Mode](howto/add-dialog-mode.md) |
| Add an MCP tool so the AI can call new capabilities | [Adding an MCP Tool](howto/add-mcp-tool.md) |
| Customize the UI (tweak / add pages, board-custom UI) | [Customizing the UI](howto/customize-ui.md) |
| Store and play animations efficiently with TEF instead of GIF (emotions/motion graphics) | [TEF animations](howto/use-tef-animation.md) |
| Trigger the whole-product factory test in trial/mass production (hotspot trigger, custom test SSIDs) | [Factory Test](howto/factory-test.md) |
| Replace/add local prompt tones (ding-dong etc.) | [Prompt tone assets](../../src/wukong/assets/README.md) (module README) |

## Troubleshooting (troubleshooting/)

| You're seeing… | See |
|---|---|
| Speech recognition / interruption / echo issues that require audio algorithm tuning | [Audio Algorithm Tuning](troubleshooting/audio-tuning.md) |
| Music/TTS playback dropouts, choppiness, slow start | [Playback Stutter Troubleshooting](troubleshooting/audio-playback-stutter.md) |
| Capture the 5 uplink audio streams to SD card for analysis (AEC residue, wake word, upload quality) | [Capturing the 5 Uplink Audio Streams](troubleshooting/audio-stream-capture.md) |
| Hardware acoustic structure (microphone/speaker layout) testing and acceptance | [Hardware Acoustic Structure Testing](troubleshooting/acoustic-test.md) |
| Device crashes/reboots and you need to decode the serial log to a source line | [T5 Crash Log Analysis Primer](troubleshooting/crash-analysis.md) |
| Can't enter pairing, app can't find the device, activation fails, re-pairing after unbind/factory reset | [Network Pairing / Activation Troubleshooting](troubleshooting/network-pairing.md) |

## Module Reference

Before changing a module, read the README inside its directory (responsibilities, design points & pitfalls, and modification guide all live there). This table is the complete set of entry points.

**AI Core (src/wukong/)**

| Module | Description |
|---|---|
| [wukong overview](../../src/wukong/README.md) | Top entry for the AI core: dialogue, audio, skills, MCP, etc. |
| [audio](../../src/wukong/audio/README.md) | Audio input/output, AEC/VAD, playback control |
| [kws](../../src/wukong/audio/frontend/kws/README.md) | Keyword wake-up |
| [mcp](../../src/wukong/toolkits/mcp/README.md) | On-device MCP tools that let the AI call device capabilities |
| [skills](../../src/wukong/toolkits/fc/README.md) | AI skills such as music/stories, emotion, cloud events |
| [picture](../../src/wukong/picture/README.md) | AI image generation/recognition, album management |
| [video](../../src/wukong/video/README.md) | Video input/output processing |
| [cron](../../src/wukong/cron/README.md) | Local scheduled tasks and JSON-RPC dispatch |
| [tm](../../src/wukong/tm/README.md) | Unified entry for alarms, reminders, countdown/count-up, Pomodoro |
| [storage](../../src/wukong/storage/README.md) | Storage layer: unified mounting of external media (SD card / QSPI flash) + record I/O |
| [assets](../../src/wukong/assets/README.md) | Built-in assets such as prompt tones |

**Interaction**

| Module | Description |
|---|---|
| [ui](../../src/ui/README.md) | Vue-inspired responsive LVGL UI framework (`ENABLE_TUYA_UI`) |
| [mode](../../src/mode/README.md) | Dialogue modes (hold/oneshot/wakeup/free + translate/p2p/record/picture/detection) |
| [boards](../../src/boards/README.md) | Board-level BSP and per-board notes |

**Platform Components (src/miscs/)**

| Module | Description |
|---|---|
| [tef](../../src/miscs/tef/README.md) | GIF→TEF animation encoding toolchain + LVGL-free player (usage: [TEF animations](howto/use-tef-animation.md)) |
| [audio_player](../../src/miscs/audio_player/README.md) | AI player svc_ai_player: mem/url/file sources → decode → resample/mix → output |
| [uart_codec](../../src/miscs/uart_codec/README.md) | UART protocol and business logic for offline voice modules (GX8006, CI1302, and protocol-compatible models) (integration: [Connecting a UART Offline Voice Chip](howto/connect-uart-voice-chip.md)) |
| [svc_media_alg](../../src/miscs/svc_media_alg/README.md) | Image/video algorithm set ported from IPC; currently wired in for motion detection |
| [p2p/tuya_tmm_manager](../../src/miscs/p2p/tuya_tmm_manager/README.md) | TMM VoIP orchestration layer, the single external entry (three-layer split explained there) |
| [p2p/tuya_tmm_control](../../src/miscs/p2p/tuya_tmm_control/README.md) | TMM VoIP signaling layer (MQTT protocol 308) |
| [p2p/tuya_tmm_stream](../../src/miscs/p2p/tuya_tmm_stream/README.md) | TMM VoIP media transport layer (P2P streaming) |

**Hardware Drivers (src/drivers/)**

| Module | Description |
|---|---|
| [app_tuya_key](../../src/drivers/app_tuya_key/README.md) | Key detection and event reporting (click/multi-click/long-press) with software debounce |
| [app_tuya_led](../../src/drivers/app_tuya_led/README.md) | Multi-channel LED management: solid on/off and blinking |
| [app_tuya_imu](../../src/drivers/app_tuya_imu/README.md) | SC7A20 accelerometer driver + posture (tilt) detection |
| [app_tuya_driver](../../src/drivers/app_tuya_driver/README.md) | TAL-layer GPIO/UART implementation and touch TKL wrapper |
| [tal_audio](../../src/drivers/app_tuya_audio/tal_audio/README.md) | Unified audio capture/playback interfaces |
| [tal_uvc](../../src/drivers/app_tuya_camera/tal_uvc/README.md) | UVC camera abstraction layer |

## Resource Statistics

For the thread inventory (priority / stack size / creation conditions), the static firmware DATA/BSS/ROM accounting method (`tuyaos_statistics_ai.sh`), and runtime observation methods, see [Resource Statistics](resource-usage.md).

## Documentation Conventions

Read [DOC_POLICY](DOC_POLICY.md) before writing or editing documentation.

## Support

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
