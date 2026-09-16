# 音频诊断 PCM 分析

设备端在「设置 → 音频诊断 → SD 卡」选中后，离开页面即开始 dump；回到该页选「关闭」
并离开时，把 5 路缓存落盘到 SD 卡 `/sdcard/tuyaos/audio_dump/`。每次 dump 会话的 5 路
共用同一时间戳前缀，互不覆盖：

    0625_143000_mic.pcm  0625_143000_ref.pcm  0625_143000_aec.pcm
    0625_143000_kws.pcm  0625_143000_vad.pcm

（裸 PCM，16000 Hz / 16-bit / mono；墙钟未同步时前缀退化为自增序号如 `0000_mic.pcm`。）

把这些文件拷到 PC 同一目录后：

    python pcm_analyze.py <目录>           # 目录下所有 *.pcm 转同名 .wav
    python pcm_analyze.py <目录> --plot     # 额外按会话叠画波形 waveforms[_<时间戳>].png

非 16k/16bit/mono 时用 --rate/--bits/--ch 覆盖。--plot 需 numpy + matplotlib。
