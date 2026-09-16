<!-- Auto-translated from ../../troubleshooting/audio-stream-capture.md. Do not edit manually. -->

# Capturing the 5 Uplink Audio Streams (SD Card)

Record the 5 audio streams of the voice uplink (MIC / REF / AEC / KWS / VAD) **as full sessions to an SD card**, copy them to a PC and convert to WAV for analysis — to answer questions like "why isn't AEC cancelling cleanly", "why doesn't the wake word trigger", and "what does the cloud actually hear".

Division of labor vs serial capture (tyuTool `dump 0~4`, see [Acoustic Structure Test](acoustic-test.md)): the serial path suits **short, per-channel** captures alongside test tones; this path is toggled from the device UI and captures **all 5 streams in sync, over long periods, in real usage scenarios** (talking over music). Both use the same data taps (the same `AUDIO_DUMP_*` channels — see the channel table in [Audio Algorithm Tuning](audio-tuning.md) section 2).

## Prerequisites

Two config options (**both preset on for T5AI_BOARD**; other boards must enable them and rerun `make app_config`):

| Option | Location | Default | Role |
|---|---|---|---|
| `ENABLE_AUDIO_ANALYSIS` | `build/APPconfig` | n (T5AI_BOARD preset: y) | master switch of the audio analysis feature: when off, `audio_dump_write()` is a no-op and the whole capture path is dead |
| `UI_FEATURE_AUDIO_DIAG` | `src/ui/Kconfig` | y | the "Audio diagnostics" page entry in the UI (requires the wukong page set, `UI_WUKONG_PAGES=y`) |

Also required:

- An SD card inserted and mountable (files land on the card).
- Onboard microphone mode (`USING_BOARD_AUDIO_INPUT`; all data taps live in the onboard audio pipeline).
- The SD/UART capture channels additionally require `ENABLE_EXT_RAM` (session buffers live in PSRAM, see the memory note below).

## Procedure

1. **Start capturing**: on the device, open "App Center → Diagnostics → Audio diagnostics", select "SD card", then **leave the page** — the selection takes effect on leave (the page is apply-on-leave, so intermediate taps don't start/stop sessions repeatedly).
2. **Reproduce the problem**: use the device normally — talk over TTS/music playback (AEC/barge-in), say the wake word (KWS), hold normal conversations (upload quality). All 5 streams are written continuously during the session.
3. **Stop capturing**: return to the "Audio diagnostics" page, select "Off", and leave the page again — remaining buffers are flushed and the session files are finalized. The log prints `audio_dump sd: deinit (overrun=N)` at teardown; a non-zero `N` means the card couldn't keep up and frames were dropped (see FAQ).
4. **Collect the data**: files are under `/sdcard/tuyaos/audio_dump/` on the card; each session's 5 files share one timestamp prefix and never overwrite each other:

   ```text
   0625_143000_mic.pcm  0625_143000_ref.pcm  0625_143000_aec.pcm
   0625_143000_kws.pcm  0625_143000_vad.pcm
   ```

   When wall-clock time is not synced (not paired yet, etc.) the prefix degrades to an incrementing sequence (`0000_mic.pcm`). Format: **raw PCM, 16000 Hz / 16-bit / mono**.
5. **Convert/analyze on the PC**: copy the `.pcm` files into one directory and batch-convert with the repo script:

   ```bash
   python scripts/audio_diag/pcm_analyze.py <dir>          # convert every *.pcm to a same-named .wav
   python scripts/audio_diag/pcm_analyze.py <dir> --plot   # additionally overlay all 5 waveforms per session as PNG (needs numpy + matplotlib)
   ```

   You can also import the raw PCM directly into Audition / ocenaudio (enter the format above). Script details: [`scripts/audio_diag/README.md`](../../../scripts/audio_diag/README.md).

## Reading the 5 Streams

| File | Content | Typical use |
|---|---|---|
| `*_mic.pcm` | raw microphone signal | clipping, gain, noise floor |
| `*_ref.pcm` | speaker loopback reference | loopback clipping, loopback path health |
| `*_aec.pcm` | post-echo-cancellation output | compare against mic/ref to judge AEC |
| `*_kws.pcm` | signal fed to the wake word engine | wake word misses |
| `*_vad.pcm` | slices actually uploaded to the cloud after VAD | "what the cloud hears", segmentation boundaries |

**Typical diagnosis**:

- **AEC not cancelling cleanly**: align and compare the three streams (start with the `--plot` overlay) — residue in `aec` strongly correlated with `ref` → first check `ref`/`mic` for clipping (a structural problem, handle per the [acoustic test](acoustic-test.md)); with clipping ruled out, tune residual echo suppression ([Audio Algorithm Tuning](audio-tuning.md) section 3).
- **Wake word not triggering**: is the voice in the `kws` stream clean and complete? If `aec` looks fine but `kws` is clearly abnormal, the problem sits between the frontend and KWS.
- **Sounds fine locally but cloud recognition is poor**: check the `vad` stream — clipped leading/trailing syllables point to VAD segmentation parameters ([Audio Algorithm Tuning](audio-tuning.md) section 4); if `vad` itself is clean, look cloud-side.
- In talk-over-playback scenarios, the relative amplitude of `mic` vs `ref` directly reflects the echo path strength — when the echo dwarfs the voice, no AEC parameter can save it; lower the volume / fix the structure first.

## FAQ

**No "Audio diagnostics" entry on the Diagnostics page**
The entry only shows when **at least one real capture channel exists** (`ui_svc_audio_diag_available()`: the entry hides when "Off" is the only available channel). Check in order:

1. `ENABLE_AUDIO_ANALYSIS` off — no channel is compiled, entry hidden;
2. `ENABLE_AUDIO_ANALYSIS` on but `ENABLE_EXT_RAM` off and the LAN channel (`ENABLE_APP_AI_MONITOR`) off too — SD/UART are both unavailable, entry equally hidden;
3. `UI_FEATURE_AUDIO_DIAG` off, or the board runs a board-custom UI (not the wukong page set) — the page itself is not compiled.

**Selected "SD card" but no files appear**
Check in order: card inserted and mountable; did you **leave the page** after selecting (no effect until you leave); did you finish by selecting "Off" and leaving again (files aren't finalized otherwise); looking in the right directory (`/sdcard/tuyaos/audio_dump/`).

**Files shorter than the capture / waveform jumps**
Whole blocks are dropped when the card can't keep up; a non-zero `overrun=N` in the teardown log confirms it. Use a faster SD card or shorten the session.

**Memory**: an SD capture session holds about **1.6MB of PSRAM** (5 streams × double buffer × 160KB, `src/miscs/audio_analysis/audio_dump.c`), released when the session closes. On memory-tight configs, avoid running it alongside other large-memory features (e.g. image generation).

**What are the "UART" and "LAN" options on the page**
Two other output channels of the same capture framework: UART pairs with tyuTool serial capture (the command path in the [acoustic test](acoustic-test.md)); LAN is realtime network streaming (additionally requires `ENABLE_APP_AI_MONITOR`). For algorithm debugging, prefer the SD card path — its data is the most complete.

## Support

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
