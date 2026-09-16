<!-- Auto-translated from ../../howto/porting-new-board.md. Do not edit manually. -->
# Porting a New Board

## Goal

Add a new board type to the wukong demo (this guide uses `T5AI_BOARD_NEW` as the example), covering the whole journey from creating the directory and wiring up the build system to compiling, flashing, and verifying. Once done, you can switch to the new board with a single `make app_config_choice`.

## Prerequisites

- You have built, flashed, and talked to any existing board by following the [Quick Start](../quickstart.md).
- You have the new board's hardware details at hand: function pins (audio trigger key, speaker enable, LED, pairing key, battery sensing), display model and resolution, camera model (if any).
- Pick the **closest existing board** as your template from the feature-module table in [Architecture](../architecture.md) and the board comparison table in `src/boards/README.md` (e.g. 240×240 SPI display + battery → start from `T5AI_BOARD_EVB`; screenless voice-only → start from `T3AI_BOARD`).

## Steps

### 1. Create the board directory

Copy the template board and rename it:

```bash
cd apps/tuyaos_demo_wukong_ai
cp -r src/boards/T5AI_BOARD_EVB src/boards/T5AI_BOARD_NEW
```

The minimum contents of a board directory are `tuya_device_board.c` / `tuya_device_board.h`, whose core job is implementing:

```c
OPERATE_RET tuya_device_board_init(VOID)
{
    // 1. GPIO / pinmux initialization
    // 2. Peripheral initialization: display, camera, keys, etc.
    // 3. UI hookup: default wukong UI or register a custom board UI (see step 6)
    return OPRT_OK;
}
```

### 2. Register the board in the board-type choice

Edit `build/APPconfig` and add to the `Board type` choice (the choice block with `default T5AI_BOARD`):

```kconfig
        config T5AI_BOARD_NEW
            bool "T5AI_BOARD_NEW"
```

### 3. Add hardware defaults for the new board

Edit `src/boards/Kconfig` and add `default ... if T5AI_BOARD_NEW` to each relevant option. Cover at least the pin options (a pin value of 64 means the feature is disabled):

```kconfig
    config TUYA_AI_TOY_AUDIO_TRIGGER_PIN_NUM
        int "Audio trigger GPIO pin number (64=disabled)"
        default 4  if T5AI_BOARD_NEW      # ← new line, fill in per your hardware
        ...

    config TUYA_AI_TOY_SPK_EN_PIN_NUM
        default 19 if T5AI_BOARD_NEW
        ...
```

For boards with a display or battery, likewise add `if T5AI_BOARD_NEW` defaults to the display / battery options (copy the template board's line and change the values).

### 4. Wire up the build system

Edit `local.mk` at the app root and add a board-selection block modeled on the existing ones:

```makefile
ifeq ($(CONFIG_T5AI_BOARD_NEW), y)
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/boards/T5AI_BOARD_NEW/
LOCAL_SRC_FILES := $(LOCAL_PATH)/src/boards/T5AI_BOARD_NEW/tuya_device_board.c
endif
```

If the board has custom UI source files (step 6, option B), add them to `LOCAL_SRC_FILES` in this block as well.

### 5. Generate the board preset config

`build/appconfig/<BOARD>` holds each board's preset configuration. Copy the template board's file and flip the board switches:

```bash
cp build/appconfig/T5AI_BOARD_EVB build/appconfig/T5AI_BOARD_NEW
# Edit build/appconfig/T5AI_BOARD_NEW:
#   CONFIG_T5AI_BOARD_EVB=y        becomes  # CONFIG_T5AI_BOARD_EVB is not set
#   add the line                    CONFIG_T5AI_BOARD_NEW=y
```

Then select and generate the config from the SDK root:

```bash
make app_config_choice APP_NAME=tuyaos_demo_wukong_ai   # pick T5AI_BOARD_NEW from the list
make app_config APP_NAME=tuyaos_demo_wukong_ai          # generates include/tuya_app_config.h
```

> **Note**: after changing `APPconfig` / `Kconfig` / appconfig files you must rerun `make app_config`, otherwise the config header is not regenerated and your changes take no effect.

### 6. Hook up the UI (boards with a display)

Pick one of two options (see the "UI Implementation" section of `src/boards/README.md` for details):

- **Default wukong UI**: don't create a `ui/` directory under the board; nothing else is needed — the framework's `ui_app_init()` automatically runs the full wukong pages with resolution adaptation.
- **Custom board UI**: implement `app_ui_init()` and `app_ui_msg_handler()` under `src/boards/T5AI_BOARD_NEW/ui/`, and register them in `tuya_device_board_init()`:

```c
ui_app_register_board_ui(app_ui_init, app_ui_msg_handler);
```

For screenless boards, simply disable `ENABLE_TUYA_UI` in menuconfig.

### 7. Build

```bash
make app APP_NAME=tuyaos_demo_wukong_ai
```

## Verification

1. Flash the firmware, power up, and watch the serial log: `tuya_device_board_init` reports no errors and the device enters pairing/idle normally.
2. Walk through the hardware features: pressing the audio trigger key starts a dialogue; LED status indication works; boards with a display show the boot screen and respond to touch; boards with a camera complete one photo/vision round.
3. On display boards, confirm page navigation and Chinese font rendering.
4. Run acoustic acceptance per the [Hardware Acoustic Structure Test Guide](../troubleshooting/acoustic-test.md); for voice-chain issues see the [Audio Algorithm Tuning Guide](../troubleshooting/audio-tuning.md).

## FAQ

**SPI display completely dark (T5 platform)**
T5's SPI0 defaults to pinmux on pins 14/15/16/17. If your display is wired to pins 44-47, you must explicitly remap the pinmux in board init, otherwise the display fails silently (no error at all). See how `src/boards/T5AI_BOARD_ROBOT/tuya_device_board.c` does it:

```c
tkl_io_pinmux_config(TUYA_IO_PIN_45, TUYA_SPI0_CS);
tkl_io_pinmux_config(TUYA_IO_PIN_44, TUYA_SPI0_CLK);
tkl_io_pinmux_config(TUYA_IO_PIN_46, TUYA_SPI0_MOSI);
tkl_io_pinmux_config(TUYA_IO_PIN_47, TUYA_SPI0_MISO);
```

**Wrong colors (pinkish tint / red-blue swapped)**
The UI framework renders native RGB565; panels on 8-bit SPI showing a pinkish background need byte swapping at the flush level: define `UI_LCD_RGB565_BYTE_SWAP=1` in the board's build options (takes effect in `src/ui/port/ui_port_disp_dma2d.c`).

**menuconfig changes take no effect**
You skipped `make app_config APP_NAME=tuyaos_demo_wukong_ai` — every `CONFIG_*` change must go through it to land in `include/tuya_app_config.h`.

**Custom UI not showing, default pages appear instead**
Check two things: `ui_app_register_board_ui()` is actually called inside `tuya_device_board_init()` (registration must happen before UI init); and the custom UI `.c` files are added to the board block in `local.mk`.

**Keys / LED unresponsive**
Check whether the pin's Kconfig default is 64 (disabled), and whether `make app_config_choice` really selected the new board (check that `CONFIG_T5AI_BOARD_NEW` is 1 in `include/tuya_app_config.h`).

## Support

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
