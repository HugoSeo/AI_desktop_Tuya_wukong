<!-- Auto-translated from ../../howto/connect-uart-voice-chip.md. Do not edit manually. -->
# Connecting a UART Offline Voice Chip (GX8006 / CI1302)

## Goal

Attach an offline voice chip (Nationalchip GX8006, Chipintelli CI1302, etc.) to a wukong board over UART: the chip handles sound pickup, the acoustic front end, and **offline wake word detection**, while the Tuya module handles cloud AI dialogue and playback. This suits products where the module has no microphone/speaker of its own, or where the wake word should live in an external chip.

```
Offline voice chip (MCU)  ── UART 1M/8N1, Tuya offline voice protocol ──  Tuya module (T5 etc.)
  pickup + AEC/denoise + offline wake                     cloud dialogue + TTS/music playback
```

The protocol implementation is encapsulated in the `src/miscs/uart_codec/` component (see its README for layering and design notes); the app-side reference implementation is `src/wukong/audio/input/wukong_audio_input_uart.c` (input and callback orchestration), `src/wukong/audio/output/wukong_audio_output_uart.c` (playback), and `src/wukong/audio/frontend/kws/uart/uart.c` (wake event hookup). For the protocol itself (frame format, full command set), see "Tuya Offline Voice Protocol" on the Tuya Developer Platform (coming to the documentation center soon).

## Prerequisites

- You have built and flashed any board by following the [Quick Start](../quickstart.md).
- **Hardware wiring** (all pin numbers configurable in menuconfig):
  | Signal | Required | Notes |
  |---|---|---|
  | UART TX/RX | Required | Module UART0 or UART2, 1M baud, 8N1, cross-connected to the chip |
  | POWER IO | Required | Module controls chip power; high level = powered |
  | BOOT IO | GX8006 only | Needed for its power-up sequence; ignored by CI1302 |
  | SPK flow-control IO | Recommended | Chip pulls it high = module may send playback data (active level configurable); without it long playback may overflow |
- **Chip-side firmware**: the chip must run firmware that **speaks the Tuya offline voice protocol** (with the wake-word model built in). Sources: GX8006 [forum post](https://tuyaos.com/viewtopic.php?t=9147), CI1302 [forum post](https://tuyaos.com/viewtopic.php?t=9148); `src/miscs/uart_codec/tool/CI1302/` ships a reference firmware and an offline flashing tool (with a step-by-step flashing guide). The wake word is determined by the chip firmware — changing the wake word means changing the chip firmware.

## Steps

### 1. Switch audio input/output to UART

`make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai` → `Audio Configuration`:

- `Audio input source` → **UART (external codec)** (`CONFIG_USING_UART_AUDIO_INPUT=y`)
- `Audio output target` → **UART (external codec)** (`CONFIG_USING_UART_AUDIO_OUTPUT=y`)

The two are usually switched together (the chip takes over both microphone and speaker). In UART mode the onboard KWS code (TUTUClear etc.) is no longer compiled — wake detection is fully delegated to the chip.

### 2. Configure the UART Codec submenu

Enabling UART input reveals the `UART Codec` menu:

| Option | Notes |
|---|---|
| `UART codec chip vendor` | GX8006 / CI1302; determines the power-up sequence and default downlink playback format (8006=PCM, 1302=MP3) |
| `UART port number` | Module UART port wired to the chip (0 or 2) |
| `Codec BOOT mode GPIO` | GX8006 only |
| `Codec power control GPIO` | Chip power-control pin |
| `Speaker flow-control GPIO` + active level | SPK downlink flow control |
| `UART codec upload audio format` | Uplink audio format: SPEEX (default, low CPU) / OPUS (better compression) |

Then run `make app_config APP_NAME=tuyaos_demo_wukong_ai` to regenerate config headers, and rebuild/flash.

### 3. (Optional) Tune session parameters

Runtime parameters such as maximum pickup time (10–60s) and silence timeout (30–180s), and the mic on/off policy after wake, live in the callback orchestration of `wukong_audio_input_uart.c` — adjust per product needs. Parameter restoration after a chip reset is also handled in that file's reset callback.

## Verification

Power-up sequence: the module powers the chip first (the component already handles the POWER/BOOT sequencing), then the serial log shows, in order:

| Log | Meaning |
|---|---|
| `[HL] init success` | Chip version report received — UART link and protocol handshake OK |
| `[HL] init failed` | Timed out waiting for the version report; see FAQ item 1 |
| `wake up cb` | Chip reported a wake event after you said the wake word |

Say the wake word to the chip → the device enters listening and holds a normal conversation → TTS/music plays from the chip-side speaker: the full chain works.

## FAQ

**`[HL] init failed` — no chip version report ever arrives**
Check in order: ① TX/RX not cross-connected or wrong port number vs. menuconfig; ② the chip firmware isn't a build that **speaks the Tuya offline voice protocol** (stock firmware never reports a version); ③ POWER/BOOT pin numbers don't match the actual wiring (8006 requires BOOT); ④ baud rate is fixed at 1M — a chip firmware using another baud rate fails the handshake.

**No response to the wake word**
The wake word is determined by the **chip firmware**, not by any module-side setting; confirm the flashed firmware's wake word matches what you're saying. Module-side KWS is not compiled in UART mode, so the model-replacement flow in [Customize Wake Word](customize-wakeword.md) does not apply here (see that guide's FAQ).

**No sound or choppy playback**
① SPK flow-control IO not wired or active level inverted — the chip must pull it high before the module may send data; ② downlink format mismatched with the chip firmware (picking the right vendor uses the right default — no manual change needed); ③ confirm `tdl_comm_audio_spk_volume_set()` is being called (wukong already wires it to the system volume).

**How do I upgrade the chip firmware**
Two paths: Tuya cloud OTA (the chip firmware is pushed as an independent TP channel; the component's `gfw_mcu_ota` is already hooked up), or flash offline with the tool under `src/miscs/uart_codec/tool/` during production/debugging.

**Can I attach other protocol-compatible chip models**
Yes. Models speaking the same protocol (e.g. other chips in the Chipintelli family) follow the CI1302 path as-is; where behavior differs (power-up sequence, default format), extend per "Modification Guide · Adding a chip model" in `src/miscs/uart_codec/README.md`.

## Support

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
