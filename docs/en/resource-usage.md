<!-- Auto-translated from ../resource-usage.md. Do not edit manually. -->

# Resource Statistics

This page answers two kinds of questions: **which threads run** (priority / stack size / under what conditions each is created), and **how to account for static firmware size** (DATA/BSS/ROM broken down per component). Runtime observation methods are collected at the end.

> **Baseline**: master branch code as of 2026-08-14 (1.1.0); the thread inventory is verified against that build's ELF. Thread stack sizes/priorities are the code and Kconfig defaults; whether a thread exists depends on board type and config switches — see the "creation condition" column. MRs that change thread configuration must update this table in the same MR (see the maintenance note at the end).

## 1. Thread Priority Semantics

The `THREAD_PRIO_x` enum is defined in the SDK's `components/tal_system/include/tal_thread.h`. **A smaller enum number means higher priority** (the mapping to FreeRTOS values is inverted, which is easy to get backwards):

| Enum | FreeRTOS value | Meaning |
|---|---|---|
| `THREAD_PRIO_0` | 5 | highest (latency-sensitive threads: audio output, keys, display flush) |
| `THREAD_PRIO_1` | 4 | high (audio frontend processing, UI main loop) |
| `THREAD_PRIO_2` | 3 | medium |
| `THREAD_PRIO_3` | 2 | medium-low |
| `THREAD_PRIO_5`/`_6` | 0 | lowest |

For the real-world cost of a mis-assigned priority, see the case in [Playback Stutter Troubleshooting](troubleshooting/audio-playback-stutter.md): the player thread used to run at `THREAD_PRIO_1`, got preempted by camera preview causing sustained stutter, and was fixed by raising it to `THREAD_PRIO_0`.

## 2. Thread Inventory

Notes:

- "Stack" is in bytes; entries marked `Kconfig:` are adjustable via `make app_menuconfig`.
- With `ENABLE_EXT_RAM` on (T5 default), most application threads set `psram_mode=1`, placing their stacks in PSRAM instead of SRAM.
- **SDK/components-layer threads are not listed** (`sys_timer`, `wq_system`/`wq_highpri`, `health_monitor`, `mqtt_thread`, `ai_client`/`ai_agent_input`/`ai_agent_output`, etc. are created by the TuyaOS SDK and belong to the SDK's domain; they do appear alongside application threads in the stack watermark log described in section 4).

### 2.1 Core Resident (default T5AI_BOARD onboard-audio config)

| Thread | Priority | Stack | Role | Source |
|---|---|---|---|---|
| `tuya_app_main` | PRIO_2 | 4096 | application startup flow | `src/tuya_app_main.c` |
| `ai_toy_state` | PRIO_5 | 3072 | AI toy state machine | `src/tuya_ai_toy.c` |
| `wk_provider` | PRIO_3 | 4096 | AI provider event dispatch | `src/wukong/provider/wukong_ai_provider.c` |
| `record_task` | PRIO_1 | Kconfig: `INPUT_BOARD_STACK_SIZE` (Kconfig default 2560; T5AI_BOARD / DESKTOP / EVB / EYES / QEMU presets use 40960, while EVB_PRO and all other boards stay at 2560) | recording slice delivery/upload | `src/wukong/audio/input/wukong_audio_input_board.c` |
| `wukong_kws` | PRIO_1 | 6144 | wake word detection (onboard KWS) | `src/wukong/audio/frontend/kws/wukong_kws.c` |
| `ai_player` | PRIO_0 | Kconfig: `AI_PLAYER_STACK_SIZE` (default 12288 with OPUS-family decoders, else 6144) | audio playback pipeline (absent in LITE mode) | `src/miscs/audio_player/src/svc_ai_player.c` |
| `key_handle` | PRIO_0 | 4096 | key scanning and event dispatch | `src/drivers/app_tuya_key/src/tuya_key.c` |

> The default T5AI_BOARD captures via the **platform mechanism** (`ENABLE_WUKONG_AUDIO_PIPELINE=n`): the audio front end (AEC/VAD) runs inside the vendor driver's voice task, so there is no separate app-side frontend thread; `audio_proc` is only created under the wukong pipeline mechanism — see 2.4.

### 2.2 Display / UI (boards with the respective switches on)

| Thread | Priority | Stack | Creation condition | Source |
|---|---|---|---|---|
| `spi_task` / `rgb_task` / `display_qspi_task` | PRIO_0 | 2048 / 4096 / 4096 | `ENABLE_TUYA_DISPLAY`, one of three per panel interface | `src/drivers/app_tuya_display/tal_display/` |
| `tp_task` | PRIO_1 | 4096 | touch-panel boards | `src/drivers/app_tuya_tp/tal_tp/src/tal_tp_service.c` |
| `ui_loop` | PRIO_1 | 12288 | `ENABLE_TUYA_UI`, LVGL main loop | `src/ui/port/ui_port.c` |
| `boot_splash` | PRIO_0 | 8192 | `ENABLE_TUYA_UI`, boot animation (exits when done) | `src/boards/common/boot_splash/tuya_boot_splash.c` |
| `tef_player` | PRIO_1 | 8192 | board calls `tef_player_init()` (EYES resident emotion player) | `src/miscs/tef/tef_player.c` |
| `rec_sd_writer` | PRIO_2 | 4096 | wukong-pages recording feature, on demand while recording | `src/ui/services/ui_svc_recording.c` |
| `rec_transcribe` | PRIO_2 | 20480 | same, on demand during transcription polling | `src/ui/services/ui_svc_transcribe.c` |
| `ty_avi_rec` | PRIO_1 | 8192 | `ENABLE_TY_AVI_MEDIA`, on demand during local AVI recording | `src/miscs/tuya_avi_service/recorder/ty_video_recorder_ctlr.c` |
| `ty_avi_play` | PRIO_2 | 8192 | `ENABLE_TY_AVI_MEDIA`, on demand during local AVI playback | `src/miscs/tuya_avi_service/src/ty_avi_player.c` |
| `avi_cleanup` | PRIO_2 | 4096 | created briefly only when a playback finish/stop task cannot enter a full `WORKQ_SYSTEM` queue; exits after cleanup | `src/ui/services/ui_svc_video_playback.c` |
| `avi_rec_stop` | PRIO_2 | 4096 | created briefly only when a running recorder's stop task cannot enter a full `WORKQ_SYSTEM` queue; exits after cleanup | `src/ui/services/ui_svc_video.c` |

### 2.3 Board Peripherals

| Thread | Priority | Stack | Creation condition | Source |
|---|---|---|---|---|
| `imu_task` | PRIO_5 | 3072 | DESKTOP board | `src/drivers/app_tuya_imu/src/tuya_imu.c` |
| `motion_heart` + `motion_ctrl` | PRIO_5 | 3072 ×2 | DESKTOP board motion control | `src/miscs/motion/tuya_motion_ctrl.c` |
| `gesture_monitor` | PRIO_1 | 2048 | ROBOT board gesture sensor | `src/miscs/gesture/app_gesture.c` |
| `action_task` | PRIO_2 | 4096 | ROBOT board servo action queue | `src/miscs/servo_ctrl/servo_ctrl.c` |
| `pn532_poll` | PRIO_3 | 2048 | on demand during NFC polling | `src/drivers/app_tuya_nfc/src/tuya_pn532_hsu.c` |
| `conv` | PRIO_2 | 2048 | `ENABLE_BATTERY`, battery sampling | `src/miscs/battery/tuya_ai_battery.c` |
| `gfw_core` | PRIO_1 | 4096 | UART external-codec mode (`USING_UART_AUDIO_INPUT`) | `src/miscs/uart_codec/src/gfw_core/gfw_core.c` |

### 2.4 Feature Switches

| Thread | Priority | Stack | Creation condition | Source |
|---|---|---|---|---|
| `audio_proc` | PRIO_1 | Kconfig: `INPUT_BOARD_STACK_SIZE` (same as `record_task`) | `ENABLE_WUKONG_AUDIO_PIPELINE=y` (wukong pipeline capture mechanism, required by the SPRS dual-mic front end); audio frontend processing (AEC/VAD) | `src/wukong/audio/input/wukong_audio_pipeline.c` |
| `pb_auto_next` | PRIO_3 | 8192 | `ENABLE_TOOLKITS_PLAYBACK`, auto next-track | `src/wukong/audio/wukong_playback_ctrl.c` |
| `wk_wechat` | PRIO_1 | 6144 | `ENABLE_CHAN_WECHAT`, WeChat message polling | `src/wukong/channel/im_wechat/im_wechat_channel.c` |
| `cube_engine` | PRIO_2 | 12288 (UART passthrough) / 30720 (onboard OPUS encode) | `ENABLE_PROVIDER_CUBE` | `src/wukong/provider/cube/wukong_provider_cube.c` |
| `xiaozhi_ws_recv` | PRIO_2 | 4096 | `ENABLE_PROVIDER_CUBE`, WebSocket receive | `src/wukong/provider/cube/xiaozhi/xiaozhi_protocol_websocket.c` |
| `jd_client` | PRIO_1 | 8192 | `ENABLE_PROVIDER_JD` | `src/wukong/provider/jd/joyinside/joyinside_client.c` |
| `jd_input` | PRIO_1 | 40960 | `ENABLE_PROVIDER_JD`, audio uplink | `src/wukong/provider/jd/wukong_provider_jd.c` |
| `tuya_ipc_thread` | PRIO_2 | 8192 | `ENABLE_AI_MODE_P2P`, P2P service | `src/miscs/p2p/tuya_p2p_app.c` |
| `tuya_tmm_control` | PRIO_1 | 8192 | on demand during a P2P call | `src/miscs/p2p/tuya_tmm_control/src/tuya_tmm_control.c` |
| `tuya_tmm_stream` | PRIO_1 | 10240 | same | `src/miscs/p2p/tuya_tmm_stream/src/tuya_tmm_stream.c` |
| `tuya_tmm_manager` | PRIO_1 | 10240 | same (P2P call management main thread) | `src/miscs/p2p/tuya_tmm_manager/src/tuya_tmm_manager.c` |
| `tmm_audio_send` | PRIO_1 | 4096 | same | `src/miscs/p2p/tuya_tmm_manager/src/tuya_tmm_manager.c` |

### 2.5 Debug / Factory Test (not created in normal firmware)

| Thread | Priority | Stack | Trigger | Source |
|---|---|---|---|---|
| `audio_dump` | PRIO_1 | 4096 | lazily initialized on the first audio capture session (tooling of the [acoustic test](troubleshooting/acoustic-test.md)) | `src/miscs/audio_analysis/audio_dump.c` |
| `mf_thread` / `mf_factory_thread` / `record` / `ds` | PRIO_3 | 4096 ×4 | factory (MF) test mode | `src/miscs/mftest/tuya_ai_toy_mf_test.c` |
| `codec_bench` | PRIO_1 | 25600 | `CODEC_BENCH_TEST` benchmark mode | `src/miscs/codec_bench/codec_bench_encoder.c` |
| `taudio_*` ×3 | PRIO_2 | 8192 ×3 | tal_audio test commands | `src/drivers/app_tuya_audio/tal_audio/src/test/test_audio_onboard.c` |
| `test_task` | PRIO_2 | 2048 | `SERVO_ACTION_TEST` | `src/miscs/servo_ctrl/servo_ctrl.c` |
| `ubuntu_key` | PRIO_3 | 2048 | PC simulation board (`WUKONG_BOARD_UBUNTU`) | `src/tuya_ai_toy_key.c` |

## 3. Static Storage Accounting (DATA/BSS/ROM)

`tuyaos_statistics_ai.sh` in the repo root uses the `nm` symbol table to break the firmware's static footprint down **per component directory**. Usage (requires a completed build):

```bash
cd apps/tuyaos_demo_wukong_ai
./tuyaos_statistics_ai.sh output/<version>/debug/bk7258/app.elf
./tuyaos_statistics_ai.sh output/<version>/debug/bk7258_ap/app.elf   # T5 is dual-core: run once per elf
```

The output is a table grouped by component, one row per group:

```
rom-----ro------ram-----data----bss-----weak----components
...     ...     ...     ...     ...     ...     tal_system
...     ...     ...     ...     ...     ...     audio_player
...     ...     ...     ...     ...     ...     wukong
```

- **Grouping**: each SDK component under `components/`, each module directory under `src/miscs/` and `src/drivers/`, `wukong` (the application body) and `vendor` (the platform) get one row each; the trailing `no-path` row covers symbols without a source path (assembly/prebuilt libraries), and `all` is the grand total.
- **Columns**: `rom` = code + constants + initialized data (flash footprint); `ro` = read-only constants; `ram` = data+bss (static RAM footprint); `data`/`bss` = initialized/uninitialized variables; `weak` = weak symbols.
- **Symbol type legend** (third column of `nm` output, which the script classifies by):

| Symbol | Meaning | Belongs to |
|---|---|---|
| `t` / `T` | static / global function (code) | ROM |
| `r` / `R` | static / global constant | ROM (read-only) |
| `d` / `D` | static / global initialized variable | RAM + ROM (initial value) |
| `b` / `B` | static / global uninitialized variable | RAM (BSS) |

Note: this accounts for **static** footprint only; thread stacks (PSRAM/heap-allocated) and runtime heap allocations are not included. If your host `nm` mishandles the cross-compiled elf, use the toolchain's `arm-none-eabi-nm` instead (path in the toolchain section of [Crash Log Analysis](troubleshooting/crash-analysis.md)).

## 4. Runtime Observation

- **Thread stack watermark (most important)**: the SDK health monitor periodically (roughly every 10 minutes) prints the stack watermark of every `tal_thread` thread to the DEBUG log, via `tal_thread_dump_watermark()` (`components/tal_system/src/os/tal_thread.c`, scheduled by the `svc_devos` health monitor). Format:

  ```text
  [tal_thread.c:336] thread[ai_player       ] stack[12288] free[10228]
  [tal_thread.c:336] thread[ai_toy_state    ] stack[ 3072] free[ 1716]
  ```

  `stack` is the configured stack size; `free` is the FreeRTOS stack high-water mark — the **historical minimum remaining since boot**, not the instantaneous value. A thread whose `free` keeps approaching 0 is about to overflow — grow its stack first; conversely, a thread with a persistently large surplus is a candidate for shrinking. This log covers SDK and application threads alike, and is also the most direct way to verify the actual effective values against the table in section 2.
- **Free heap**: `mem left:<N>` in the logs (`tal_system_get_free_heap_size()`), printed on events such as key presses.
- **Playback pipeline latency/throughput**: `[AP-STAT]` diagnostics, see [Playback Stutter Troubleshooting](troubleshooting/audio-playback-stutter.md).
- **TEF animation decode cost**: the `TEF_BENCH=1` benchmark report, see [TEF animations](howto/use-tef-animation.md).
- For locating a stack that has already overflowed, see the stack-overflow pattern in [Crash Log Analysis](troubleshooting/crash-analysis.md).

## Maintenance

- Any MR that adds or changes a thread (priority, stack, creation condition) must update the corresponding row in section 2 within the same MR; the "Source" column paths are existence-checked by `scripts/docs_check.py`.
- When archiving the changelog at release time, re-verify this table and the baseline statement.

## Support

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
