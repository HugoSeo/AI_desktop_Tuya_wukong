<!-- Auto-translated from ../architecture.md. Do not edit manually. -->

# Overall Architecture

This document is for developers who need to understand the overall structure of tuyaos_demo_wukong_ai: the six-domain split, the boot and initialization flow, low-power behavior, and the entry points to each domain's documentation. Read this document in full before changing code.

## The Six Domains

`src/` is split into six domains by responsibility: **wukong** (AI core), **mode** (dialogue mode state machine), **boards** (board-level BSP and product form factors), **ui** (LVGL UI framework), **miscs** (general-purpose capabilities), and **drivers** (application-layer drivers). Relationship between domains: `tuya_app_main.c` brings up the `tuya_ai_toy.c` main controller, which dispatches the wukong core capabilities and the mode state machine, while boards/drivers/miscs provide board-level and low-level support.

## Top-level C Files in src

The current top-level core `.c` files in `src/` are mainly as follows:

| File | Role | Description |
|------|------|------|
| `tuya_app_main.c` | Application entry point | Creates the application main thread and completes overall initialization of Tuya IoT, networking, authorization, board/UI, and the AI toy |
| `tuya_ai_toy.c` | AI toy main controller | Manages AI runtime state, event subscriptions, mode dispatch, the audio pipeline, `cron` / `alarm`, and the idle / lowpower timers |
| `tuya_ai_toy_key.c` | Key handling & quick-reset provisioning | Handles key drivers, reset-to-provisioning on power-cycle count, and simulated keys on Ubuntu |
| `tuya_ai_toy_led.c` | LED indicator wrapper | A thin wrapper around the LED driver providing on/off/blink capability |

> Camera capability does not live at the top level of `src/`; it lives in `src/miscs/camera/tuya_camera.c` (interface `tuya_camera.h`), brought up by `tuya_ai_toy.c` calling `tuya_camera_init()`.

The top-level relationship, simplified, can be understood as:

```text
tuya_app_main.c
    └─ System and IoT framework entry point
       └─ tuya_ai_toy.c
          ├─ key / led / camera top-level device wrappers
          ├─ wukong core capabilities
          ├─ mode state machine
          └─ board-level and low-level capabilities from boards / drivers / miscs
```

## Initialization Flow

### Application Entry Point

`tuya_app_main.c` is the outermost entry point of the whole application; the main flow is as follows:

```text
main() / tuya_app_main()
    -> creates tuya_app_thread
    -> tuya_base_utilities_init()
    -> user_main()
    -> __soc_device_init()
    -> tuya_ai_toy_init()
```

It is mainly responsible for:

- Initializing the base runtime environment
- Initializing Tuya IoT parameters and the local database
- Registering IoT callbacks for upgrade, DP, network status, reset, etc.
- Initializing Wi-Fi / BLE / wired / cellular capability based on the build configuration
- Initializing the board, UI, and the AI toy

### Device Initialization Stage

`__soc_device_init()` is where the application layer meets the TuyaOS / IoT framework; it is mainly responsible for:

- Subscribing to `EVENT_LINK_UP` / `EVENT_LINK_DOWN` / `EVENT_MQTT_CONNECTED`
- Handling software authorization or production-test flashing authorization
- Configuring Wi-Fi low-power parameters, e.g. DTIM
- Initializing board-level capabilities and the UI
- Finally calling `tuya_ai_toy_init()`

### AI Toy Initialization Stage

`tuya_ai_toy_init()` in `tuya_ai_toy.c` is the runtime-state control center; it mainly does the following:

- Allocates and initializes the `s_ai_toy` context
- Reads the local volume and trigger-mode configuration
- Initializes peripheral capabilities such as LED, keys, camera, battery, and cellular
- Creates the `idle_timer` and `lowpower_timer`
- Calls `__ai_toy_start()` to enter the AI working state

### AI Working-State Startup

`__ai_toy_start()` wires together the AI submodules that actually run:

- Subscribes to OTA, AI client, KWS, VAD, RESET, and other events
- Initializes `wukong_ai_agent`, audio input, the player, and KWS
- Registers the Wi-Fi network status callback
- Initializes MCP
- Initializes `wukong_cron`
- Initializes `wukong_time_manage`
- Calls `wukong_cron_time_ready_notify()` once time is available
- Initializes `wukong_ai_mode`
- Creates the `ai_toy_state` thread, which loops calling `wukong_ai_mode_dispatch(AI_MODE_OP_TASK, ...)`

## Low-Power Flow

### Low-Power Parameter Configuration at Startup

`tuya_app_main.c` performs a one-time Wi-Fi low-power parameter configuration inside `__soc_device_init()`:

- `tal_cpu_set_lp_mode(TRUE)`
- `tal_wifi_set_lps_dtim(3)`
- then, by default, first calls `tal_cpu_lp_disable()` / `tal_wifi_lp_disable()`

This step is more about pre-configuring the low-power parameters — the device does not go straight into low power on startup.

### idle / lowpower Timers at Runtime

The actual runtime low-power control lives in `tuya_ai_toy.c`:

- `idle_timer`
  - On timeout, if nothing is playing, notifies `mode` to enter the idle state
  - If still playing, restarts the idle timer
- `lowpower_timer`
  - On timeout, if nothing is playing, enters light sleep or deep sleep according to `TY_AI_DEFAULT_LOWP_MODE`
  - If still playing, restarts the lowpower timer

The flow can be simplified as:

```text
Normal operation
  -> idle_timer expires
     -> not playing: notify mode to enter idle
     -> playing: keep resetting the timer

idle persists for a while
  -> lowpower_timer expires
     -> deep sleep: configure the wakeup source, then enter deep sleep
     -> light sleep: stop the AI runtime state, power down peripherals, enable CPU/Wi-Fi LP
```

### Low-Power Wakeup Recovery

Once a key press triggers `__on_ai_toy_key_press_exit_lowpower()`, the recovery flow runs:

- Disables CPU / Wi-Fi low power
- Turns on peripherals such as the backlight, speaker, and LED
- Re-runs `__ai_toy_start()`
- Re-initializes the AI working state

Currently, light-sleep recovery is essentially "stop the AI working state, then bring it back up again."

## Feature Modules

The code is organized by domain: `src/wukong/` (the AI core — dialogue, audio, skills, MCP, etc.; a git submodule), `src/mode/` (dialogue modes), `src/ui/` (responsive LVGL UI framework), `src/boards/` (board-level BSP), `src/miscs/` (platform components: player, TEF animations, P2P calls, media algorithms, etc.), and `src/drivers/` (driver layer: camera, display, keys, touch, IMU, etc.).

Each module's one-line responsibility and README entry point are maintained in one place — the [Module Reference section of the documentation map](README.md#module-reference) — and are not duplicated here.

## Support

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
