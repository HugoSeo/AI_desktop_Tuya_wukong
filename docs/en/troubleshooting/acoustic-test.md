<!-- Auto-translated from ../../troubleshooting/acoustic-test.md. Do not edit manually. -->

# Hardware Acoustic Structure Testing

## Overview

If users experience poor dialogue quality or a subpar experience with dialogue-based toys, this document can be used as a reference for hardware and acoustic troubleshooting. This section only applies to onboard-audio scenarios; if an external voice chip is used, refer to that voice chip's acoustic-structure tuning method instead.

The platform debugs audio data in `.pcm` format, `16-bit` width, `16k` sample rate.

Recommended software for viewing audio data: `Adobe Audition` or `ocenaudio`.

## Audio Processing Flow

AI dialogue is based on traditional streaming-media technology and shares many similarities with VoIP and live video streaming. Audio processing on the device consists of an uplink (push) part and a downlink (pull) part: the uplink covers microphone capture, 3A processing, and speech encoding; the downlink covers speech decoding, audio processing, and speaker output.

![ao/ai](https://images.tuyacn.com/fe-static/docs/img/27d839c9-00c6-4c96-8882-b4b26f798d58.png)

Explanation of the terms in the figure:

**AGC**: auto gain control

**AEC**: acoustic echo cancellation

**NS**: noise suppression

Besides what is shown in the figure, audio processing may also include other modules such as DRC and VAD. Among all the audio modules, the AEC module has the greatest impact on voice interaction — the effectiveness of echo cancellation depends not only on the algorithm but is also closely related to hardware performance and structural design. Because of the impact of the hardware structure and design, audio processing needs to remove echo as much as possible while still preserving the near-end dialogue, in order to keep the AI voice dialogue smooth.

## Acoustic Structure Testing

### Test Tools

Use Tuya's official serial debug tool **[tyuTool](https://github.com/tuya/tyutool/tree/master)** for testing.
- The tool runs cross-platform (offering both GUI and CLI modes).
- While the app is running, you can send control commands over serial to play specified test audio and capture (dump) data from each audio channel for analysis.
- See the tool's GitHub repository README for detailed installation and usage instructions.

### Test Commands

After connecting to the device using `tyuTool`'s AI Debug serial mode (or directly via a serial terminal), you can use the following commands to control recording and playback to assist with acoustic structure testing:

| Command | Description |
| --- | --- |
| `start` | Start recording and audio processing |
| `stop` | Stop recording and audio processing |
| `reset` | Reset the recording state |
| `bg 0` | Play white noise |
| `bg 1` | Play a 1KHz, 0dB single tone |
| `bg 2` | Play a continuous sweep (50Hz - 7.5kHz) |
| `bg 3` | Play a discrete-frequency sweep (see the frequency table below) |
| `bg 4` | Play the minimum single tone (or use as a silence test) |
| `volume <0-100>` | Set the playback volume (e.g. `volume 70` sets the volume to 70%) |
| `micgain <0-100>` | Set the microphone gain (e.g. `micgain 70`) |
| `dump 0` | Capture raw microphone (MIC) input channel data |
| `dump 1` | Capture reference (REF) loopback channel data |
| `dump 2` | Capture audio data after AEC algorithm processing |
| `dump 3` | Capture the audio data fed into the KWS module |
| `dump 4` | Capture the audio data sent to the cloud AI Agent |

### Test Procedure

The acoustic structure design can be checked by playing test audio; follow the steps below to play the corresponding audio and either listen to it or capture the data for analysis:

1. **Connect the device**: connect to the device's serial port using `tyuTool` or another serial tool.
2. **Initialize the environment**: send the `reset` command to clear any previously left-over recording state. You can also configure suitable `volume` and `micgain` values as needed.
3. **Start capture**: send the `start` command to begin recording audio data.
4. **Play test audio**: send the corresponding `bg <mode>` command. Available test audio includes:
   - **1k single tone** (`bg 1`): 2s duration, 0dB.
   - **White noise** (`bg 0`).
   - **Continuous sweep** (`bg 2`): continuous sweep from 50Hz to 7500Hz.
   - **Discrete-frequency sweep** (`bg 3`): frequency and duration info in the table below.
   - **Silence/single tone** (`bg 4`).
5. **Stop capture**: after the audio finishes playing, send the `stop` command to stop capture; the captured data is cached on the device.
6. **Capture and analyze data**: send `dump 0` to capture microphone data, then send `dump 1` to capture speaker loopback data. Import the captured `.pcm` data into `Adobe Audition` or `ocenaudio` for waveform and spectrum analysis, to check for harmonic distortion, DC bias, clipping, and similar issues.

### Automated Testing

To simplify the process, `tyuTool` provides an automated audio-test command:
1. **Run automated testing**: enter the corresponding command in the serial UI or CLI (see the tool's help for the `ser_auto` mode).
2. **Test flow**: the tool automatically runs the following steps in sequence:
   - Play and capture **white noise** data.
   - Play and capture **1K-0dB single tone** data.
   - Play and capture **silence** data.
3. **Automatic report generation**: once all data has been captured, the tool automatically runs the audio-analysis algorithm and produces a test report covering DC bias, clipping distortion, total harmonic distortion, latency stability, and more.

#### Discrete-Frequency Sweep Test Frequency Table

| Frequency (Hz) | Duration (s) | Amplitude (normalized) |
| --- | --- | --- |
| 1000 | 0.5 | 0.8 |
| 7500 | 0.3 | 0.8 |
| 5800 | 0.3 | 0.8 |
| 4500 | 0.3 | 0.8 |
| 3500 | 0.3 | 0.8 |
| 2750 | 0.3 | 0.8 |
| 2150 | 0.3 | 0.8 |
| 1700 | 0.3 | 0.8 |
| 1300 | 0.3 | 0.8 |
| 785 | 0.3 | 0.8 |
| 600 | 0.3 | 0.8 |
| 475 | 0.3 | 0.8 |
| 370 | 0.3 | 0.8 |
| 285 | 0.3 | 0.8 |
| 225 | 0.3 | 0.8 |
| 175 | 0.3 | 0.8 |
| 135 | 0.3 | 0.8 |
| 100 | 0.3 | 0.8 |
| 80 | 0.3 | 0.8 |
| 65 | 0.3 | 0.8 |
| 50 | 0.3 | 0.8 |

You can play one or more of the above signals to assess speaker and microphone performance — whether there is harmonic distortion, DC bias, clipping, and so on.

## Audio Issues

### 1. DC Bias

DC bias refers to a constant DC voltage component superimposed on an audio signal, causing the signal as a whole to deviate from the zero level (reference level). This can be caused by issues in the hardware, circuit design, or the signal-transmission path, and it degrades audio quality.

The figures below show example waveforms with and without DC bias.

![dc offset](https://images.tuyacn.com/fe-static/docs/img/f88b9f57-129b-4c22-b49f-47121b5434c7.png)

![dc offset](https://images.tuyacn.com/fe-static/docs/img/6c51d422-a1c0-4025-bc14-0830a300d8b1.png)

A slight DC bias has little effect on audio processing; a larger DC bias affects the audio's dynamic range and can cause clipping distortion. If it is above 0.01 or below -0.01, a DC-offset-removal algorithm (e.g. a high-pass filter) is recommended. In the test program, either the single tone or white noise can be used to compute the DC bias.

![dc offset](https://images.tuyacn.com/fe-static/docs/img/38aef766-bc8e-4316-ac96-35acb24ce475.png)

### 2. Clipping Distortion

Clipping distortion occurs when an audio signal exceeds the maximum value representable by the digital audio signal. Because some platforms use a hardware loopback circuit, the loopback signal and the microphone signal need to be judged separately.

*Case 1*: for example, when playing a 1k single tone, the ref signal is normal but the microphone-captured signal clips, as shown below.

![clip](https://images.tuyacn.com/fe-static/docs/img/86de1fdc-1216-4e32-b3d4-57aecfdbfed5.png)

The likely cause is the microphone being too close to the speaker; adjust the relative position of the microphone and speaker. If the relative position cannot be adjusted, you can instead lower `micgain` in the script.

*Case 2*: the ref signal clips but the microphone signal is normal, as shown below.

![clip2](https://images.tuyacn.com/fe-static/docs/img/6f3104f1-23b2-428a-853a-96d6c6ea02a4.png)

The likely cause is the speaker volume being too high; lower the speaker's playback loudness, which can be adjusted by decreasing `volume`. The figure below shows a passing clipping-detection result in the test report; if it fails, `samples` records the total number of samples that failed.

![clip2](https://images.tuyacn.com/fe-static/docs/img/2873b3ec-b202-44e1-8025-6f243b13deb6.png)

### 3. Total Harmonic Distortion

Total Harmonic Distortion (THD) is an important metric for measuring signal distortion. It describes the ratio between the harmonic content and the fundamental content in the output signal, reflecting how faithfully the system reproduces the original signal. In practice, due to the nonlinear characteristics of components (e.g. the nonlinear region of transistors, nonlinear vibration of the speaker diaphragm), the output signal generates extra harmonics — components at integer multiples of the input signal's fundamental frequency (2nd harmonic, 3rd harmonic, etc.). The script provides a THD calculation based on 1k. Exceeding the speaker's rated power usually causes significant harmonic distortion; if the THD check fails, it is recommended to lower the speaker volume. In addition, low speaker or microphone quality can also cause harmonic distortion.

![clip2](https://images.tuyacn.com/fe-static/docs/img/34b7e0a2-5f4b-4268-a614-7c8f6765a0ca.png)

THD is generally recommended to be within 5% (i.e. 0.05); above this value, the total-harmonic-distortion check fails.


### 4. Microphone Consistency

The test program supports acoustic verification for dual microphones. For dual-mic processing algorithms such as beamforming, the microphones need good consistency with each other. If microphone consistency is poor, replacing the microphone is recommended.

![clip2](https://images.tuyacn.com/fe-static/docs/img/08784858-2cca-4fa9-be50-7705eb27ff89.png)

The higher this correlation coefficient, the better — ideally 1.0. A value above 0.7 is recommended.

### 5. Latency Stability

Dropped frames, data loss, or processing anomalies can cause the microphone and loopback channel data to become misaligned, resulting in unstable latency. The test program can compute the latency to evaluate latency stability.

![clip2](https://images.tuyacn.com/fe-static/docs/img/c027afe4-ac76-4cd2-9e51-6ad207c33134.png)

If this check fails (`false`), it means the latency fluctuation is too large and has a noticeable impact on AEC; capture data in the context of the specific business scenario to locate and troubleshoot the issue.

## Support

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
