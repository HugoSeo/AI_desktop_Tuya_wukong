import os, struct, tempfile, wave
from pcm_analyze import pcm_to_wav, parse_pcm_name

def test_parse_pcm_name():
    # 带时间戳前缀的会话文件
    assert parse_pcm_name("20260625_143000_mic.pcm") == ("20260625_143000", "mic")
    assert parse_pcm_name("20260625_143000_vad.pcm") == ("20260625_143000", "vad")
    # 兼容无时间戳的裸文件名
    assert parse_pcm_name("aec.pcm") == ("", "aec")
    # 非通道 / 非 .pcm -> None
    assert parse_pcm_name("foo.pcm") is None
    assert parse_pcm_name("notes.txt") is None

def test_pcm_to_wav_roundtrip():
    # 100 个 16-bit 样本的裸 PCM
    samples = [((i * 137) % 65536) - 32768 for i in range(100)]
    raw = struct.pack("<%dh" % len(samples), *samples)
    with tempfile.TemporaryDirectory() as d:
        pcm = os.path.join(d, "mic.pcm")
        wav = os.path.join(d, "mic.wav")
        with open(pcm, "wb") as f:
            f.write(raw)
        pcm_to_wav(pcm, wav, rate=16000, bits=16, ch=1)
        with wave.open(wav, "rb") as w:
            assert w.getframerate() == 16000
            assert w.getsampwidth() == 2
            assert w.getnchannels() == 1
            assert w.readframes(w.getnframes()) == raw
