<!-- Auto-translated from ../../howto/customize-wakeword.md. Do not edit manually. -->
# Changing the Wake Word / Tuning KWS

## Goal

Explain how, in the wukong demo, to:

1. Switch between the several precompiled TUTUClear wake-word models shipped in the repo (e.g. keep only "Ni Hao Tuya");
2. Switch the KWS engine (TUTUClear ↔ SNDX, a source-level change);
3. Understand the current state of sensitivity tuning and ways to mitigate false wakes / missed wakes;
4. Verify that the device wakes as expected after changing the word/engine or tuning parameters.

Training a brand-new wake-word acoustic model is out of scope for this guide (see Step 5); follow the model provider's documentation for that.

## Prerequisites

- You have built, flashed, and talked to any existing board by following the [Quick Start](../quickstart.md).
- Confirm the board captures audio via the **onboard microphone**: in `make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai`, `Audio input source` should be set to `Onboard microphone` (`CONFIG_USING_BOARD_AUDIO_INPUT=y`; the choice is defined in `src/miscs/audio_player/Kconfig`). The model/engine switch in this guide only applies to that mode. If `UART (external codec)` (`CONFIG_USING_UART_AUDIO_INPUT=y`) is selected instead, the wake word is decided by the external codec chip and the KWS code described here is not compiled in — see the FAQ.
- The board-side KWS engine (TUTUClear) that is active by default in this repo is only wired up on the T5 module (`CONFIG_TUYA_MODULE_T5=y`); see the `#if defined(TUYA_MODULE_T5)` branch in `wukong_kws_default_init()` inside `src/wukong/audio/frontend/kws/wukong_kws.c`.

## Steps

### 1. Understand the current wake-word model files

The precompiled wake-word models live in `src/wukong/audio/frontend/kws/tutuclear/`:

| File | Description |
| --- | --- |
| `libtutuClear.a` | **Currently active** model (`local.mk` copies it into the build output) |
| `libtutuClear_wakeup_nihaotuya_xiaozhitongxue_heytuya_hituya_20251024_small_model.a` | Byte-for-byte identical to `libtutuClear.a` (same md5): a combined 4-word model — Ni Hao Tuya / Xiao Zhi Tong Xue / hey tuya / hituya |
| `libtutuClear_wakeup_nihaotuya_chinese_20251227_200KB_model.a` | Single-word model: Ni Hao Tuya |
| `libtutuclear_wakeup_xiaozhitongxue_chinese_200KB_20260112.a` | Single-word model: Xiao Zhi Tong Xue |
| `libtutuClear_wakeup_heytuya_english_20251227_730KB_model.a` | English single-word model: hey tuya |

In other words, the demo's default firmware can be woken by all four words — "Ni Hao Tuya", "Xiao Zhi Tong Xue", "hey tuya", "hituya" (see the detect callback `TUTUClear_kws_detect()` in `src/wukong/audio/frontend/kws/tutuclear/tutuclear.c`, lines 66-86, which logs and reports based on `w32WakeWord` values 1-4).

### 2. Switch to a single wake-word model

Edit the line in the app-root `local.mk` that copies this `.a` file (inside the on-board-mic branch):

```makefile
# local.mk, original
INSTALL_TUTUKWS := $(shell mkdir -p  $(LOCAL_PATH)/../../libs/app_libs && cp $(LOCAL_PATH)/src/wukong/audio/frontend/kws/tutuclear/libtutuClear.a $(LOCAL_PATH)/../../libs/app_libs/ && echo "libtutuClear.a copied" >&2)
```

Change the `cp` source to the model you want, but **keep the destination filename `libtutuClear.a`** (the file under `libs/app_libs/` must keep this name — it's bound to the symbols declared in `tutuclear.c`):

```makefile
INSTALL_TUTUKWS := $(shell mkdir -p  $(LOCAL_PATH)/../../libs/app_libs && cp $(LOCAL_PATH)/src/wukong/audio/frontend/kws/tutuclear/libtutuClear_wakeup_nihaotuya_chinese_20251227_200KB_model.a $(LOCAL_PATH)/../../libs/app_libs/libtutuClear.a && echo "libtutuClear.a copied" >&2)
```

No changes are needed in the branches of `TUTUClear_kws_detect()` — a single-word model will simply only ever report the `w32WakeWord` value it recognizes.

### 3. Regenerate config and rebuild

```bash
make app_config APP_NAME=tuyaos_demo_wukong_ai
make app APP_NAME=tuyaos_demo_wukong_ai
```

### 4. Switch the KWS engine (TUTUClear ↔ SNDX)

Engine selection is currently **hardcoded in source**, with no corresponding Kconfig switch: in `wukong_kws_default_init()` (`src/wukong/audio/frontend/kws/wukong_kws.c`, lines 469-492), the four SNDX callback lines are commented out, and TUTUClear is active by default:

```c
// cfg.create = SNDX_kws_create;
// cfg.detect = SNDX_kws_detect;
// cfg.reset  = SNDX_kws_reset;
// cfg.deinit = SNDX_kws_deinit;
cfg.create = TUTUClear_kws_create;
cfg.detect = TUTUClear_kws_detect;
cfg.reset  = TUTUClear_kws_reset;
cfg.deinit = TUTUClear_kws_deinit;
```

To use SNDX instead, swap which set of four lines is commented out and rebuild. Note that the current SNDX implementation (`src/wukong/audio/frontend/kws/sndx/sndx.c`) only recognizes two words: `id==1001` → Ni Hao Tuya, `id==1002` → heytuya (lines 33-39); and `local.mk` always copies `sndx/libsndxasr.a` — the other three per-word `.a` files under `sndx/` (e.g. `libsndxasr-nihaotuya.a`) are not referenced by any build rule today.

### 5. Training / integrating a brand-new wake word

> **Needs verification**: Training a new wake-word acoustic model and producing a new `lib*.a` is a customization service provided by the model owner (Tuya or a solution partner) — it cannot be done inside this repo. Contact Tuya or your solution partner to obtain the model file; the exact request channel and any cloud tooling should follow the platform/business-side documentation — this guide makes no assumptions about it.

Once you have the new model file, the steps to wire it into the code (see the "Custom wake words" section of `src/wukong/audio/frontend/kws/README.md`) are:

1. If the new word reuses the TUTUClear/SNDX engine and needs a new index, pick/add a value in `WUKONG_KWS_INDEX_E` (`src/wukong/audio/frontend/kws/wukong_kws.h`, lines 30-39) — `WUKONG_KWS_UDF1/2/3` (values 6/7/8) are already reserved for custom use.
2. Place the model `.a` under `src/wukong/audio/frontend/kws/tutuclear/` or `sndx/`, and wire it into `local.mk` the same way as in Step 2.
3. If reusing an existing engine's create/detect callbacks, call `wukong_kws_event(index)` for the new index inside detect (follow the existing branches in `TUTUClear_kws_detect()` / `SNDX_kws_detect()`).
4. For a brand-new engine (neither TUTUClear nor SNDX), implement the four `WUKONG_KWS_CFG_T` callbacks — `create` / `detect` / `reset` / `deinit` — yourself and wire them in via `wukong_kws_init(&cfg)` (instead of `wukong_kws_default_init()`); the interface is defined in `wukong_kws.h`, lines 56-78.

### 6. Sensitivity and false-wake / missed-wake mitigation

Based on the current repo code, the knobs available at the KWS layer are limited:

- TUTUClear's underlying library exports `TUTUClear_SetWakeupThr(handle, thr)` / `TUTUClear_GetWakeupThr(handle)` (declared in `tutuclear.c`, lines 17-18), but `TUTUClear_kws_create()` (same file, lines 20-53) **does not currently call them** — i.e. there is no application-level sensitivity knob exposed today. To tune it, call `TUTUClear_SetWakeupThr()` yourself after the instance is created in `TUTUClear_kws_create()`, then rebuild.
  > **Needs verification**: The valid range of `Thr` and the direction of the effect (does a larger value make false wakes more or less likely) are not documented anywhere in this repo's source or headers — follow the model provider's documentation rather than guessing the direction.
- SNDX's `sndx_asr_init(silence_ms)` (`sndx.c`, line 10; currently called with `450`) controls the silence-detection duration, not a wake-up threshold.
- Many "false wake / missed wake" issues actually originate in the upstream AEC/VAD stage: KWS is throttled by the `vadflag` passed into `wukong_kws_feed_with_vad()` (with `is_detect_vad=1`, detect only runs while voice is active or right after it ends). VAD sensitivity levels and AEC residual-echo tuning are not repeated here — see the [Audio Algorithm Tuning Guide](../troubleshooting/audio-tuning.md).

## Verification

1. After making the changes above, rebuild and flash, and keep the serial console at debug log level (so `TAL_PR_DEBUG` prints are visible).
2. Say the target wake word at the device; the serial console should print something like:
   - TUTUClear engine: `TUTUClear_WakeWord -> 你好涂鸦` / `小智同学` / `heytuya` / `hituya` (`tutuclear.c`, lines 70-79).
   - SNDX engine: `sndx_WakeWord -> 你好涂鸦` / `heytuya` (`sndx.c`, lines 35, 37).
3. The wake event `EVENT_WUKONG_KWS_WAKEUP` is picked up by `__on_ai_toy_audio_kws()` (subscribed in `src/tuya_ai_toy.c`), which triggers `wukong_ai_mode_dispatch(AI_MODE_OP_WAKEUP, ...)`; the device should enter its dialogue/wake state — confirm via the actual interaction (prompt tone, UI transition, etc.).
4. After switching to a single-word model, no longer waking on the other three words is expected behavior. If **no word triggers a wake** after changing the model/engine, first confirm whether `make app_config` was rerun and whether `CONFIG_USING_BOARD_AUDIO_INPUT` / `CONFIG_TUYA_MODULE_T5` in `include/tuya_app_config.h` match your expectations (same approach as the FAQ in [Porting a New Board](porting-new-board.md)).

## FAQ

**I'm using UART external codec mode, and changing the wake-word model has no effect**
When `CONFIG_USING_BOARD_AUDIO_INPUT` is not enabled, `wukong_kws_default_init()` takes the `wukong_kws_uart_init()` branch (the `#else` branch at lines 469-491 of `wukong_kws.c`), and the TUTUClear/SNDX sources are only compiled in by `local.mk` when `CONFIG_USING_BOARD_AUDIO_INPUT=y`. In this mode the wake word is decided by the external codec chip, which is outside the scope of this guide.

**I'm on a non-T5 module (`CONFIG_TUYA_MODULE_T5` not enabled) and changing the model has no effect**
The board-side engine wiring in `wukong_kws_default_init()` only takes effect when `TUYA_MODULE_T5 == 1`; otherwise the function just returns `OPRT_OK` without wiring up any engine (`wukong_kws.c`, lines 471-489). On non-T5 platforms, wire up an engine yourself via `wukong_kws_init()` instead of relying on `wukong_kws_default_init()`.

**The build output doesn't seem to reflect my `local.mk` / Kconfig changes**
As with porting a new board, after changing Kconfig/APPconfig-related settings you must rerun `make app_config APP_NAME=tuyaos_demo_wukong_ai`, otherwise `include/tuya_app_config.h` won't be updated. Changes to the `.a` copy path in `local.mk` only require rerunning `make app`.

**How do I tell which model `libtutuClear.a` currently is?**
Run `md5sum` over the `.a` files in `src/wukong/audio/frontend/kws/tutuclear/` and compare against `libtutuClear.a` — whichever one matches is the model currently in effect.

## Support

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
