<!-- Auto-translated from ../../troubleshooting/audio-playback-stutter.md. Do not edit manually. -->

# Music/TTS Playback Stutter Troubleshooting

This guide troubleshoots **playback-side** issues: periodic dropouts, choppy audio, or slow start when playing music (URL streams) or TTS (cloud-delivered speech streams). For uplink issues (recognition/echo/VAD) see [Audio Algorithm Tuning](audio-tuning.md).

## Understand the Pipeline First: Stutter = Output Ring Underrun

Playback data flow (implemented in the `src/miscs/audio_player/` submodule; see its README for module details):

```
Cloud ──► datasink (mem = TTS streaming / url = HTTP music / file)
           │  single-threaded pull by the player thread: URL download
           │  is NOT a separate thread — http_read_content runs inside
           │  the player thread (10ms yield)
           ▼
      decode (MP3/OPUS…) → resample → mix
           ▼
      tal_audio_output ring buffer ──► speaker
```

**Key fact**: when the output ring buffer runs empty, the lower layer feeds zeros (silence) — that is the audible "stutter", and it **produces no log at all**. So don't hunt for error messages; use AP-STAT statistics to determine which stage of the chain is starving.

## Step 1: Enable AP-STAT Diagnostics

AP-STAT has a two-level gate:

1. **Compile-in (build time)**: `make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai` → `Audio Configuration` → `Audio Player` → check `Enable [AP-STAT] playback diagnostics` (`CONFIG_AI_PLAYER_DEBUG_STATS`, off by default), then `make app_config` + `make app`, rebuild and flash.
2. **Turn on (runtime)**: on the device, open "App Center → Diagnostics" and enable the "Playback Stats" switch (the row only shows when the stats code is compiled in). The switch is memory-only and defaults to off — re-enable after a reboot; code can also call `tuya_ai_player_debug_stats_set(TRUE)`.

For debug firmware it's recommended to ship with `CONFIG_AI_PLAYER_DEBUG_STATS=y` — with the switch off nothing is printed, and when field data is needed you just flip the switch instead of rebuilding and reflashing.

Once enabled, one line is printed every 2 seconds during playback (FG = foreground TTS/prompt tones, BG = background music):

```
[AP-STAT FG] loop=.. sink(r=.. B=.. zero=.. max=..ms) dec(n=.. B=.. max=..ms) rs_max=..ms wr(B=.. audio=..ms blkmax=..ms) win=..ms
```

| Field | Meaning |
|---|---|
| `sink(r/B/zero/max)` | source reads: count / bytes / **zero-reads** (no data available) / max single-read time |
| `dec(n/B/max)` | decode: count / output bytes / max single-decode time |
| `rs_max` | max single resample time |
| `wr(B/audio/blkmax)` | write-out: bytes / **converted audio duration (ms)** / max single-write block time |
| `win` | actual window span (nominal 2000ms) |

## Step 2: Three Reading Rules

**① `audio` clearly below `win`, with large `sink` `zero`/`max` → upstream starvation (network-starved).**
Each 2-second window must write out ≈2000ms of audio to keep up; `audio=1850` means a 150ms deficit every 2 seconds — guaranteed periodic stutter. Check the URL datasink logs (`datasink_url.c`, NOTICE level, always on):

- `url sink connected: cost N ms` — connection time (first TLS handshake can take seconds; this shows up as **slow start**, not stutter).
- `url sink reconnect #n at offset ...` — **reconnection**; one https reconnect costs hundreds of ms to seconds and is always audible. Repeated reconnects = network/CDN-side problems (throttling, throughput collapse); the device can only mitigate with larger buffers — the real fix is on the cloud side.
- `url sink first data N bytes, M ms after start` — first-byte latency.

**② Large `dec`/`rs_max` times with `wr blkmax≈0` → CPU-starved (decoder preempted by a heavy thread).**
`blkmax≈0` means the output ring is chronically non-full (writes never wait): decode output cannot keep up with consumption. Look for high-CPU features started at the same moment — measured case: enabling camera preview (DVP 17fps) pushed per-frame decode from 22 to 60ms and supply rate from 99.8% to 92%, causing sustained underrun. The player thread priority has been raised to `THREAD_PRIO_0` (`svc_ai_player.c`; the thread self-blocks when the ring is full so it cannot starve others) — if your scenario still reproduces, apply the same reasoning to find what is hogging the core.

**③ Large `wr blkmax` → output ring full, writes blocking — this is the healthy state.**
A full downstream means supply is sufficient; look elsewhere for the stutter (e.g. upper-layer stop/start logic, track switching).

## TTS Choppiness and the Pre-buffer Mechanism

TTS uses the memory datasink (mem sink). With streaming delivery under network jitter, play-as-it-arrives produces high-frequency "micro-stutters". The player has a built-in **pre-buffer** (`datasink_mem.c`): at start and after every stream stall it accumulates data until the watermark is reached (or EOF, or a timeout fallback) before releasing it to the decoder, merging many micro-stutters into a single perceptible pause.

Kconfig (`Audio Player` menu → `Streaming pre-buffer`):

| Option | Default | Notes |
|---|---|---|
| `AI_PLAYER_MEM_PREBUF_BYTES` | 2048 | watermark (bytes), 0 = off. **Must be well below** `AI_PLAYER_RINGBUF_SIZE` (16384); set near capacity it can never fill and only the timeout releases it |
| `AI_PLAYER_MEM_PREBUF_TIMEOUT_MS` | 500 | timeout fallback: releases anyway when upstream stalls or bitrate is too low |

On completion it prints `mem sink prebuf done: N bytes in M ms` (empty timeout releases are not printed, to avoid log spam). Tuning approach:

- **Watermark ≈ bitrate × jitter window you want to absorb.** First enable AP-STAT to measure the actual TTS bitrate (`sink B` ÷ `win`), then convert; blindly raising the watermark only linearly increases start/resume latency.
- Still frequent micro-stutters → raise the watermark; noticeably slower start/barge-in response → lower the watermark or shorten the timeout.
- The LITE player (`AI_PLAYER_LITE=y`, e.g. T3 boards) has no datasink layer; neither pre-buffer nor AP-STAT applies.

## Slow Start (Distinct from Mid-playback Stutter)

Slow start has its own accounting, all visible in the url sink logs:

1. Large `connected: cost` (seconds) — first TLS connection overhead, a network environment issue.
2. After connecting, `dec n>0` but `B=0` for a while — typical of mp3 files with a large ID3 cover image (hundreds of KB) at the head; the decoder must read past it before producing sound. The real fix is server-side: strip the cover or use a Range request to skip it.

## FAQ

**Enabled AP-STAT but no output**
The diagnostic is not compiled in LITE mode (`depends on !AI_PLAYER_LITE`); also confirm you ran `make app_config` after changing Kconfig.

**Stutter only appears when a specific page/feature is active**
Classic rule-② scenario: compare per-stage AP-STAT times before and after the stutter onset and find the high-load feature (camera, algorithm, UI animation) started at that moment — don't blame the network first.

**Enlarged the output ring but still periodic stutter**
With insufficient supply rate (e.g. sustained 92%), any buffer size only lengthens the interval between stutters — first fix the supply rate to ≈100% per rules ① and ②.

**TTS drops half a syllable at the start of each sentence**
Not a pre-buffer issue — pre-buffering only delays sound, it never drops data. Check uplink VAD segmentation instead ([Audio Algorithm Tuning](audio-tuning.md), section 4).

## Support

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
