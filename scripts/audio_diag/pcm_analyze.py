#!/usr/bin/env python3
"""把音频诊断 dump 的裸 PCM 文件转成 WAV，并可选叠画波形。

设备端 dump 约定：16000 Hz / 16-bit / mono（可用参数覆盖）。
文件名：`<时间戳>_<通道>.pcm`，例如 `0625_143000_mic.pcm`（时间戳为 MMDD_HHMMSS）；通道取值
mic/ref/aec/kws/vad。一次 dump 会话的 5 路共用同一时间戳前缀（便于分组、不互相覆盖）。
未带时间戳的裸 `mic.pcm` 等也兼容识别。
"""
import argparse
import os
import wave

CHANNELS = ["mic", "ref", "aec", "kws", "vad"]


def pcm_to_wav(pcm_path, wav_path, rate=16000, bits=16, ch=1):
    with open(pcm_path, "rb") as f:
        data = f.read()
    with wave.open(wav_path, "wb") as w:
        w.setnchannels(ch)
        w.setsampwidth(bits // 8)
        w.setframerate(rate)
        w.writeframes(data)
    return wav_path


def parse_pcm_name(fname):
    """解析 dump 文件名 -> (时间戳前缀, 通道)。

    '0625_143000_mic.pcm' -> ('0625_143000', 'mic')
    'mic.pcm'                  -> ('', 'mic')
    非通道 .pcm 或非 .pcm      -> None
    """
    if not fname.endswith(".pcm"):
        return None
    stem = fname[:-4]
    for ch in CHANNELS:
        if stem == ch:
            return ("", ch)
        if stem.endswith("_" + ch):
            return (stem[:-(len(ch) + 1)], ch)
    return None


def _plot(dir_path, rate, bits):
    try:
        import numpy as np
        import matplotlib.pyplot as plt
    except ImportError:
        print("[plot] 需要 numpy + matplotlib，跳过绘图")
        return
    dtype = {8: np.int8, 16: np.int16, 32: np.int32}.get(bits)
    if dtype is None:
        print("[plot] 不支持的位深 %d（仅 8/16/32），跳过绘图" % bits)
        return

    # 按会话(时间戳前缀)分组
    sessions = {}
    for f in sorted(os.listdir(dir_path)):
        parsed = parse_pcm_name(f)
        if parsed is None:
            continue
        stamp, ch = parsed
        sessions.setdefault(stamp, {})[ch] = os.path.join(dir_path, f)
    if not sessions:
        return

    for stamp, chans in sorted(sessions.items()):
        present = [(ch, chans[ch]) for ch in CHANNELS if ch in chans]
        if not present:
            continue
        fig, axes = plt.subplots(len(present), 1, sharex=True,
                                 figsize=(12, 2 * len(present)))
        if len(present) == 1:
            axes = [axes]
        for ax, (name, path) in zip(axes, present):
            sig = np.fromfile(path, dtype=dtype)
            t = np.arange(len(sig)) / float(rate)
            ax.plot(t, sig, linewidth=0.5)
            ax.set_ylabel(name)
        axes[-1].set_xlabel("time (s)")
        suffix = ("_" + stamp) if stamp else ""
        out = os.path.join(dir_path, "waveforms%s.png" % suffix)
        fig.tight_layout()
        fig.savefig(out, dpi=120)
        plt.close(fig)
        print("[plot] 已保存 %s" % out)


def main():
    ap = argparse.ArgumentParser(description="音频诊断 PCM 分析工具")
    ap.add_argument("dir", help="包含 *.pcm 的目录（从 SD 卡 /sdcard/tuyaos/audio_dump 拷出）")
    ap.add_argument("--rate", type=int, default=16000)
    ap.add_argument("--bits", type=int, default=16)
    ap.add_argument("--ch", type=int, default=1)
    ap.add_argument("--plot", action="store_true", help="按会话叠画各路波形到 waveforms[_<时间戳>].png")
    args = ap.parse_args()

    converted = 0
    for f in sorted(os.listdir(args.dir)):
        if not f.endswith(".pcm"):
            continue
        if parse_pcm_name(f) is None:
            print("跳过无法识别的文件 %s" % f)
            continue
        pcm = os.path.join(args.dir, f)
        wav_name = f[:-4] + ".wav"
        pcm_to_wav(pcm, os.path.join(args.dir, wav_name), args.rate, args.bits, args.ch)
        print("转换 %s -> %s" % (f, wav_name))
        converted += 1
    if converted == 0:
        print("目录下没有可识别的 *.pcm（期望 [<时间戳>_]<mic|ref|aec|kws|vad>.pcm）")
    if args.plot:
        _plot(args.dir, args.rate, args.bits)


if __name__ == "__main__":
    main()
