<!-- Auto-translated from ../../howto/use-tef-animation.md. Do not edit manually. -->
# Store and Play Animations Efficiently with TEF (Replacing GIF)

## Goal

Convert a set of animated GIFs (emotions, idle animations, etc.) to TEF (Tuya Emotion
Format), compile them into the firmware, and play them in one of three modes depending
on the scenario: a resident player pushing straight to GRAM (no LVGL), one-shot blocking
playback (boot splash), or the LVGL widget `lv_tef` (mode C). None of them does runtime
GIF decoding.

Gains over the "lv_gif + embedded GIF" approach (measured with 10 emotions on the EYES board):

| Dimension | lv_gif + embedded GIF | TEF |
|---|---|---|
| Asset flash | ~1.58MB (the LVGL asset set the old scheme actually embedded, plus the LVGL library itself) | **419KB** (~0.65× the 648KB total of the 10 same-source GIF byte streams, no LVGL dependency) |
| Decode CPU | 12.5% (of a 58ms frame budget) | **2.2%**, worst frame ≤7ms |
| Resident memory | 79.4KB (GIF decode peak, excluding LVGL itself) | **74.4KB** (PSRAM, including a full-frame shadow buffer) |

This page covers usage only; the authoritative description of the format, the player's
design constraints, and known pitfalls lives in
[`src/miscs/tef/README.md`](../../../src/miscs/tef/README.md) (required reading before
changing `src/miscs/tef/`), and the format spec is `src/miscs/tef/docs/TEF_技术规范.html`.

## Prerequisites

- The board has the display stack enabled: `CONFIG_ENABLE_TUYA_DISPLAY=y` (TEF does not
  require `ENABLE_TUYA_UI`; see the EYES "UI off, direct display" configuration).
- PSRAM is available: all player working buffers live in PSRAM — a full-frame shadow
  buffer (W×H×2 bytes) + a 16KB decompression dictionary + one frame of indices (W×H) +
  ~11KB for tinfl.
- Host-side Python 3 with `Pillow` + `numpy` (for the asset generation script).
- GIF canvas size: the resident player (mode A below) requires it to equal the
  display's logical resolution (EYES dual-panel special case: 128×128 single-eye assets
  are mirrored to both panels automatically, 128×256 dual-eye assets are routed by row
  bands); one-shot playback (mode B) only requires it to be no larger than the target
  window (smaller assets are centered automatically); the LVGL widget (mode C) accepts
  any size — the widget is exactly as large as the asset.

## Steps

### 1. Prepare the GIF folder

The file name becomes the asset name (must be a valid C identifier); when used as a
cloud emotion it must match the emoji names in `src/wukong/skills/skill_emotion.c`.
An asset named `neutral` is placed at registry index 0 and serves as the fallback for
unmatched names.

```bash
cd src/miscs/tef/tools
mkdir gif && cp <your assets>/*.gif gif/
```

### 2. Generate the C arrays and registry in one shot

```bash
python3 gen_assets.py                 # in-place mode: gif/ -> out/
# or with explicit directories (how EYES maintains its board assets):
python3 gen_assets.py <gif_dir> <out_dir>
```

Three kinds of output: one `tef_<name>.c` per GIF (the `.tef` byte stream as a constant
array), `tef_assets.h` (declarations + the `tef_asset_t` registry type), and
`tef_assets.c` (the `g_tef_assets[]` / `g_tef_asset_cnt` registry).

### 3. Playback integration

**Mode A: resident emotion player** (loop-and-switch scenarios like EYES emotions),
in your board init (after `tuya_display_hw_init()`):

```c
#include "tef_player.h"
#include "tef_assets.h"

tef_player_init();                                          /* create the playback thread */
tef_player_play(g_tef_assets[0].data, g_tef_assets[0].len); /* loops; switching is async */
```

For name-based switching (with fallback) see the EYES implementation in
[`src/boards/T5AI_BOARD_EYES/ui/eyes_app.c`](../../../src/boards/T5AI_BOARD_EYES/ui/eyes_app.c):
`eyes_emotion_find()` looks up the registry, falls back to entry 0 on a miss, and
emotion/chat-state messages drive `tef_player_play()`.

**Mode B: one-shot blocking playback** (play-once-and-hold scenarios like the boot
splash) — `tef_player_play_once()` decodes frame by frame into an RGB565 canvas you
provide (assets smaller than the window are centered automatically), calls you back
every frame so you can push it to the screen, returns after one full pass and frees
its working buffers immediately; `tef_player_init()` is not required. See the UI
boards' boot splash:
[`src/boards/common/boot_splash/tuya_boot_splash.c`](../../../src/boards/common/boot_splash/tuya_boot_splash.c)
(full-screen PSRAM canvas + `tal_display_flush` commit, last frame held until LVGL
takes over).

**Mode C: LVGL in-page widget** (play TEF animations on any page of an
`ENABLE_TUYA_UI` board) — use `src/ui/widgets/lv_tef.[ch]`, same usage as `lv_gif`:
create, then `set_src` to start looping; deleting the widget stops playback:

```c
#include "lv_tef.h"

lv_obj_t *tef = lv_tef_create(parent);
lv_obj_center(tef);
lv_tef_set_src(tef, tef_data, tef_len);   /* the constant array generated in step 2 */
```

Notes:

- Decoding runs frame by frame in an `lv_timer` on the UI thread (frame pacing =
  the asset's fps); multiple widgets can coexist, each holding its own PSRAM ≈
  canvas W×H×2 + indices W×H + 16KB dictionary + ~11KB tinfl (about 196KB for
  240×240, 75KB for 128×128) — budget the number of on-screen widgets accordingly.
- The canvas is native RGB565: do **not** add `TEF_RGB565_BYTE_SWAP` on UI boards
  (that switch belongs to the direct-to-GRAM path); byte swapping for SPI panels is
  handled by the UI flush layer's `CONFIG_UI_LCD_RGB565_BYTE_SWAP`.
- If the asset fails to load the widget stays blank; if the data turns out corrupt
  mid-playback it freezes on the last good frame (errors are logged with an
  `lv_tef:` prefix).

### 4. Build wiring (local.mk)

The TEF framework (`miscs/tef`, including the decoder) is compiled automatically with
`CONFIG_ENABLE_TUYA_DISPLAY`; your board block only needs two things:

```makefile
# 8-bit SPI panels (e.g. st7735s) need pixel byte swapping (done once at palette load);
# RGB and similar panels must not set this:
LOCAL_TUYA_SDK_CFLAGS += -DTEF_RGB565_BYTE_SWAP=1
# Asset directory (output of step 2):
LOCAL_TUYA_SDK_INC += $(LOCAL_PATH)/src/<your asset dir>
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/<your asset dir> -name "*.c")
```

### 5. Optional: encoder tuning

For special needs call the encoder directly (`gen_assets.py` uses it internally):

```bash
python3 tef_encoder.py in.gif out.tef --jpeg-q 0     # JPEG frames MUST stay disabled on device
    --window 14      # deflate window bits 9~15: +1 doubles decoder RAM for slightly less flash (default 14 = 16KB dictionary)
    --seg 16         # frames per segment: larger compresses better but raises the decode peak at segment boundaries
    --stabilize auto # temporal stabilization: preprocessing for AI-generated/dithered sources, see below
```

**`--stabilize` (temporal stabilization, preprocessing for AI/dithered sources)**:
AI generation tools and common GIF exporters bake dithering noise into the pixels
as inter-frame flicker, which inflates TEF's dirty-block ratio and ruins the
compression (the .tef can even end up larger than the GIF). When enabled, pixels
whose inter-frame difference is tiny (`max(ΔR,ΔG,ΔB) ≤ threshold`) are pinned back
to the previous frame's value before encoding — real motion is unaffected; only
flicker invisible to the eye is removed. `auto` picks a level (8/16/24/32) from the
Δ distribution or skips clean sources, and automatically falls back if stabilization
does not shrink the output, so it is safe to enable by default for AI-generated
assets (`gen_assets.py` accepts `--stabilize auto` too). Measured on the existing
EYES assets: 4 noisy emotions shrink by 8%–24%; clean assets are skipped with
byte-identical output. Three boundaries: (1) with stabilization on, "lossless" means
pixel-exact relative to the stabilized frames — relative to the original GIF it is
perceptually lossless (error ≤ the chosen threshold); (2) not recommended for slow
gradients (fade-in/out) — it causes visible stepping; (3) off by default, and it is
purely encoder-side preprocessing — format and on-device decoder are unchanged.

Desktop preview/verification: `tef_player.py` (windowed playback), `tef_decoder.py`
(decode dump).

## Verification

1. After flashing, the boot log should show `tef: load <W>x<H> <N> frames ...`, and during
   playback a `[TEF-STAT] frames=.. itv(avg/max)=..` line every 2s — `itv` should sit
   stably at the asset's frame interval (e.g. 17fps → 58ms).
2. To quantify against GIF: `make app APP_NAME=tuyaos_demo_wukong_ai TEF_BENCH=1` compiles
   in the benchmark, which prints a `[TEF-BENCH]` report at boot (flash / byte-exact
   memory / µs per frame and CPU%); see [`src/miscs/tef/bench/`](../../../src/miscs/tef/bench/).

## FAQ

- **`tef: unsupported version` on load**: the player supports v3 assets only; re-encode
  old files with `tef_encoder.py` (or just rerun `gen_assets.py` if you have the GIF sources).
- **Whole screen tinted wrong (e.g. pink/purple)**: RGB565 byte-order mismatch; 8-bit SPI
  panels need `-DTEF_RGB565_BYTE_SWAP=1` (see step 4).
- **`tef: canvas WxH mismatch display`**: the resident player (mode A) requires the
  asset canvas to equal the display's logical resolution — fix the source GIF and
  regenerate; one-shot playback (mode B) only requires the canvas to be no larger than
  the target window (smaller assets are centered).
- **`tef: jpeg seg not supported` warning, skipped frames**: the asset was encoded with
  JPEG frames; the device has no JPEG decoder, always use `--jpeg-q 0` (`gen_assets.py`
  disables it by default).
- **Playback thread crash / only the first frame shows**: usually the decompressor placed
  on a thread stack (the tinfl singleton is ~10KB) or a non-resident asset; see the
  "design constraints & pitfalls" section of
  [`src/miscs/tef/README.md`](../../../src/miscs/tef/README.md).

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
