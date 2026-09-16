<!-- Auto-translated from ../../troubleshooting/audio-tuning.md. Do not edit manually. -->

# Audio Algorithm Tuning Guide

This guide troubleshoots **algorithm-level issues on the voice uplink**: incomplete echo cancellation, broken barge-in (double-talk), insufficient or excessive noise suppression, and VAD misses/false triggers. For hardware acoustic structure testing (mic/speaker layout, clipping, harmonic distortion) see [Acoustic Structure Test](acoustic-test.md); for wake word / KWS sensitivity see [Customize Wake Word](../howto/customize-wakeword.md); for playback-side (music/TTS) stutter see [Playback Stutter Troubleshooting](audio-playback-stutter.md).

**Scope**: onboard microphone mode (`CONFIG_USING_BOARD_AUDIO_INPUT=y`) on T5 modules. In UART external-codec mode the 3A algorithms run inside the external chip and this guide does not apply. There is currently no online serial tuning; the workflow is **edit source/Kconfig parameters and rebuild**, combined with audio data capture for offline analysis.

## Symptom Quick Reference

| You are seeing… | Go to |
|---|---|
| Device triggers itself on echo while playing TTS/music, recognition picks up playback | [3. Tuya frontend: residual echo suppression](#3-tuya-frontend-speex-aecaesns--rnn-vad) |
| Cannot barge in with wake word/speech during playback (double-talk broken) | [3. Tuya frontend: residual echo suppression](#3-tuya-frontend-speex-aecaesns--rnn-vad) |
| Noisy environment, low recognition rate, or over-suppression cutting speech | [3. Tuya frontend: noise suppression](#3-tuya-frontend-speex-aecaesns--rnn-vad) |
| Speech not detected (VAD miss) / silence detected as speech (false trigger) | [4. VAD tuning](#4-vad-sensitivity-and-segmentation) |
| Dual-mic board (SPRS frontend) performing poorly | [5. SPRS dual-mic frontend](#5-sprs-dual-mic-frontend) |

## 1. First, Identify Which Frontend Is In Use

The audio frontend (AEC/NS/VAD) is a compile-time choice. The Kconfig choice is under `make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai` → `Audio Configuration` → `Audio Input` → `Audio frontend engine` (defined in `src/miscs/audio_player/Kconfig`):

| Option | Engine | Mic configuration | Implementation |
|---|---|---|---|
| `USING_TUYA_AUDIO_FRONTEND` (default) | Speex AEC + AES + NS, RNN VAD | single mic + REF loopback | `src/wukong/audio/frontend/aec_vad/tuya/wukong_audio_aec_vad.c` |
| `USING_SPRS_AUDIO_FRONTEND` | SPRS ESNR (integrated AEC/NR/VAD) | dual mic (DMIC LR) + REF (ADC) | `src/wukong/audio/frontend/aec_vad/sprs/wukong_audio_sprs.c` |
| `USING_3RD_AUDIO_FRONTEND` | placeholder, not implemented | — | — |

Both frontends implement the `WUKONG_AUDIO_FRONTEND_OPS_T` callback set and are dispatched by `src/wukong/audio/frontend/wukong_audio_frontend.c`; registration happens in the pipeline init in `src/wukong/audio/input/wukong_audio_input_board.c` (selected by the macros above). The legacy `wukong_aec_vad_*` API (header `src/wukong/audio/frontend/aec_vad/tuya/wukong_audio_aec_vad.h`) still works as a thin wrapper around the dispatcher.

After switching frontends you must rerun `make app_config APP_NAME=tuyaos_demo_wukong_ai` and then `make app`.

## 2. Capturing Data: Dump Channels and Tools

Use the official Tuya serial tool [tyuTool](https://github.com/tuya/tyutool/tree/master) (debug ser / ser_auto mode) to send control commands and capture per-channel PCM (16k/16bit). For the command table, test tones (`bg 0/1/…`) and automated testing see [Acoustic Structure Test](acoustic-test.md) — both guides share the same command set, and **channel definitions follow the table below** (matching the `AUDIO_DUMP_*` macros in `src/miscs/audio_analysis/audio_dump.h`):

| Command | Channel | Content | Write point |
|---|---|---|---|
| `dump 0` | MIC | raw microphone data | inside frontend `process` (pre-AEC) |
| `dump 1` | REF | speaker loopback reference | same as above |
| `dump 2` | AEC | post-echo-cancellation output | same as above |
| `dump 3` | KWS | data fed to the wake word engine | `src/wukong/audio/frontend/kws/wukong_kws.c` |
| `dump 4` | VAD | speech slices **actually uploaded to the cloud** after VAD | `src/wukong/audio/input/wukong_audio_input_board.c` |

Typical flow: `reset` → `start` → play test tone or speak → `stop` → capture `dump 0`/`dump 1`/`dump 2` in turn, import into Audition / ocenaudio and compare pre/post-AEC waveforms and spectra. Comparing `dump 2` with `dump 4` reveals whether the VAD segmentation clips leading/trailing syllables.

For **long, 5-stream-synchronized, real-usage** captures (e.g. AEC residue while talking over music), use the SD card capture path instead: toggled from the device UI, recorded as full sessions, converted to WAV with a PC script — see [Capturing the 5 Uplink Audio Streams](audio-stream-capture.md).

**Fix the environment before tuning** to avoid confounding variables: microphone model and `micgain`, speaker model and `volume`, enclosure/acoustic cavity — lock all three down before evaluating the algorithms.

## 3. Tuya Frontend (Speex AEC/AES/NS + RNN VAD)

Parameters are hard-coded in `__speex_rnn_init()` in `src/wukong/audio/frontend/aec_vad/tuya/wukong_audio_aec_vad.c` (lines 38–39); edit and rebuild to take effect:

**Residual echo suppression**: `speex_aes_set_param(handle, 5)`

- **Larger** value: residual echo suppressed harder, but near-end speech gets suppressed too during double-talk — **barge-in degrades**.
- **Smaller** value: more near-end speech preserved, better barge-in, but more residual echo.
- Method: audible residual echo / self-triggering during TTS playback → increase; barge-in unresponsive during playback → decrease. Change only this one value at a time and compare with `dump 2`.

**Noise suppression**: `speex_ns_set_param(handle, 8, 10)`

- `level1` = suppression strength, larger is stronger; `level2` = noise floor, smaller allows more noise to be suppressed.
- High-SNR environment: large `level1`, small `level2`; low-SNR environment: small `level1`, large `level2` (avoid cutting speech together with noise).

Beyond algorithm tuning, structural measures — lowering speaker volume, increasing mic-to-speaker distance — are often more effective. Rule out structural issues first via the [Acoustic Structure Test](acoustic-test.md).

## 4. VAD (Sensitivity and Segmentation)

Three tunables of the RNN VAD:

**Sensitivity level**: `wukong_vad_set_threshold(level)`, adjustable at runtime, mapped to RNN thresholds:

| Level | Threshold | Effect |
|---|---|---|
| `WUKONG_AUDIO_VAD_HIGH` | -40 dB | less prone to false triggers |
| `WUKONG_AUDIO_VAD_MID` (default) | -50 dB | — |
| `WUKONG_AUDIO_VAD_LOW` | -60 dB | triggers more easily |

**Segmentation parameters**: initial values live in `audio_cfg.board` in `src/tuya_ai_toy.c` (`vad_active_ms = 500`, `vad_off_ms = 1000`), passed to the frontend via `wukong_audio_frontend_init()`:

- `vad_active_ms` → minimum valid speech length (ms): too small and coughs/knocks get reported as speech.
- `vad_off_ms` → maximum silence gap (ms): a pause longer than this ends the utterance; increase it if long sentences keep getting cut off.

**Tuning tips**:

- Misses (speech not detected): switch to `WUKONG_AUDIO_VAD_LOW`, or increase `vad_off_ms`.
- False triggers (silence/noise detected as speech): switch to `WUKONG_AUDIO_VAD_HIGH`, or increase `vad_active_ms`.
- Log evidence: the serial log prints `[vad start]` / `[vad stop]` on VAD transitions (`wukong_audio_aec_vad.c`); cross-check segmentation boundaries against `dump 4` data.

## 5. SPRS Dual-Mic Frontend

The SPRS ESNR frontend (`USING_SPRS_AUDIO_FRONTEND=y`) is an integrated dual-mic algorithm (built-in AEC/NR/VAD, 32ms/512-sample frames). All tunables are in Kconfig (same menu; rerun `make app_config` and rebuild after changes):

- `SPRS_AGC_TARGET_LEVEL` (0–9, default 5, 0=off): AGC target level.
- `SPRS_ENABLE_AEC` (default n): SPRS extra AEC; enable and measure when echo cancellation is insufficient (adds CPU cost).

On successful init the serial log prints `sprs esnr version …` (including the SRAM budget and the effective values of the two parameters above, see `wukong_audio_sprs.c`) — confirm the parameters actually took effect before judging quality. For an underperforming dual-mic board, first rule out hardware with the "mic consistency" item of the [acoustic test](acoustic-test.md) (correlation should be > 0.7), then come back to algorithm parameters.

## FAQ

**Changed a parameter but nothing happened**
For Kconfig items (frontend choice, SPRS parameters) you skipped `make app_config APP_NAME=tuyaos_demo_wukong_ai`; hard-coded source parameters (the two speex calls) only need a fresh `make app`. Verify via the macro values in `include/tuya_app_config.h`.

**Want to tune 3A in UART external-codec mode**
With `CONFIG_USING_UART_AUDIO_INPUT=y` the frontend code in this guide is not compiled; 3A runs inside the external chip — follow the chip vendor's documentation.

**Dumped data is all silence / zero length**
Send `start` first, then produce sound, then `stop`; wrong ordering leaves the cache empty. Also confirm onboard microphone mode (these channels carry no data in UART mode).

**Residual echo and barge-in can never both be satisfied**
`speex_aes_set_param` is fundamentally a trade-off knob between the two, and the algorithm ceiling is heavily hardware-bound — if the loopback signal clips or the mic sits too close to the speaker, fix the structure first (see "clipping distortion" in the [acoustic test](acoustic-test.md)), then tune parameters.

## Support

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
