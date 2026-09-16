#!/usr/bin/env python3
"""
TEF (Tuya Emotion Format) 解码器/播放器 —— 纯 Python 参考实现。

不依赖 C 或 libjpeg(JPEG 帧用 Pillow 解码)。严格对应技术规范 §5 的解码流程,
内置两种"刷屏模式":
  - GRAM 局部刷新:只把脏区写入一块保持型画布(模拟面板 GRAM);
  - 自缓存整帧:同样维护整帧画布(桌面预览两者等价,差别在真实硬件的内存归属)。

用法:
  # 解码并导出成 GIF(默认)
  python3 tef_decoder.py in.tef out.gif
  # 导出为 PNG 帧序列
  python3 tef_decoder.py in.tef frames/ --png
  # 实时窗口预览(需 Tkinter)
  python3 tef_decoder.py in.tef --play [--scale 2] [--loop]

作为库:
  from tef_decoder import decode_tef
  frames = decode_tef("in.tef")   # -> list[np.ndarray(H,W,3) uint8]
"""
import sys, struct, zlib, io, os, argparse
import numpy as np
from PIL import Image

MAGIC = b"TEF1"
SEG_PALETTE = 0
SEG_JPEG    = 1
COMP_HEATSHRINK = 0
COMP_DEFLATE    = 1

SEG_FMT  = "<BBHHHII"
SEG_SIZE = 16


# ---------------------------------------------------------------------------
# 熵解码:RAW inflate(与编码端 wbits=-15 对应)
# ---------------------------------------------------------------------------
def inflate_raw(data):
    return zlib.decompress(data, -15)

def hs_decode(data):
    raise NotImplementedError("本 Python 解码器仅实现 deflate 段;"
                              "heatshrink 段请用 C 版,或用 deflate 重新编码。")


# ---------------------------------------------------------------------------
# 位解包:1/4/8bit -> 8bit 索引
# ---------------------------------------------------------------------------
def unpack_indices(buf, bits, npx):
    if bits == 8:
        return np.frombuffer(buf[:npx], np.uint8).astype(np.uint8)
    if bits == 4:
        nb = (npx + 1) // 2
        b = np.frombuffer(buf[:nb], np.uint8)
        hi = (b >> 4) & 0xF
        lo = b & 0xF
        out = np.empty(nb * 2, np.uint8)
        out[0::2] = hi
        out[1::2] = lo
        return out[:npx]
    # bits == 1
    nb = (npx + 7) // 8
    b = np.frombuffer(buf[:nb], np.uint8)
    bitsarr = np.unpackbits(b)            # MSB-first,与编码端一致
    return bitsarr[:npx].astype(np.uint8)


# ---------------------------------------------------------------------------
# 调色板 RGB565 -> RGB888 LUT(开播预转一次)
# ---------------------------------------------------------------------------
def palette_to_rgb(pal565):
    r = (pal565 >> 11) & 0x1F
    g = (pal565 >> 5) & 0x3F
    b = pal565 & 0x1F
    return np.stack([(r << 3) | (r >> 2),
                     (g << 2) | (g >> 4),
                     (b << 3) | (b >> 2)], -1).astype(np.uint8)   # [ncolors,3]


# ---------------------------------------------------------------------------
# 主解码:返回 list[np.ndarray(H,W,3) uint8]
# ---------------------------------------------------------------------------
def decode_tef(path, mode="gram"):
    with open(path, "rb") as f:
        data = f.read()

    # --- 步骤 1:解析头 + 调色板 + 段表(仅 v3, 38B 头) ---
    (magic, ver, W, H, tw, th, N, fps, ncolors, index_bits, flags,
     seg_count) = struct.unpack("<4sHHHBBHHHBBH", data[:22])
    assert magic == MAGIC, "不是 TEF1 文件"
    assert ver == 3, f"仅支持 v3(当前 {ver});旧素材请用 tef_encoder 重新编码"
    # v3: mode,window_bits,flush_policy,rsvd(u8x4) + max_seg_decomp(u32)
    palette_off, seg_table_off = struct.unpack("<II", data[30:38])

    pal565 = np.frombuffer(data[palette_off:palette_off + ncolors * 2], "<u2")
    lut = palette_to_rgb(pal565) if ncolors else None      # 预转缓存

    ntx = (W + tw - 1) // tw
    nty = (H + th - 1) // th
    ntiles = ntx * nty
    bmb = (ntiles + 7) // 8

    segs = []
    for s in range(seg_count):
        off = seg_table_off + s * SEG_SIZE
        method, comp, fstart, fcount, _r, o, clen = struct.unpack(SEG_FMT, data[off:off + SEG_SIZE])
        segs.append((method, comp, fstart, fcount, o, clen))

    # 画布(保持型):GRAM 与自缓存模式在桌面上等价,都维护整帧
    canvas = np.zeros((H, W, 3), np.uint8)
    frames = [None] * N

    # --- 步骤 2:按段解码 ---
    for method, comp, fstart, fcount, o, clen in segs:
        payload = data[o:o + clen]
        if method == SEG_JPEG:
            x, y, w, h = struct.unpack("<HHHH", payload[:8])
            jpg = payload[8:]
            rgb = np.asarray(Image.open(io.BytesIO(jpg)).convert("RGB"), np.uint8)
            canvas[y:y + h, x:x + w] = rgb[:h, :w]           # 写脏矩形
            frames[fstart] = canvas.copy()
        else:
            if comp == COMP_DEFLATE:
                blob = inflate_raw(payload)
            else:
                blob = hs_decode(payload)
            bp = 0
            for k in range(fcount):
                i = fstart + k
                bitmap = blob[bp:bp + bmb]; bp += bmb
                bmarr = np.frombuffer(bitmap, np.uint8)
                # 找脏 tile,统计脏像素数
                dirty_tiles = []
                dpx = 0
                for ti in range(ntiles):
                    if bmarr[ti >> 3] & (1 << (ti & 7)):
                        tx, ty = ti % ntx, ti // ntx
                        x0, y0 = tx * tw, ty * th
                        x1, y1 = min(x0 + tw, W), min(y0 + th, H)
                        dirty_tiles.append((x0, y0, x1, y1))
                        dpx += (x1 - x0) * (y1 - y0)
                # 位解包
                if index_bits == 8:   pkb = dpx
                elif index_bits == 4: pkb = (dpx + 1) // 2
                else:                 pkb = (dpx + 7) // 8
                idxflat = unpack_indices(blob[bp:bp + pkb], index_bits, dpx); bp += pkb
                # 查表上色 + 写脏 tile
                cur = 0
                for (x0, y0, x1, y1) in dirty_tiles:
                    n = (x1 - x0) * (y1 - y0)
                    tile_idx = idxflat[cur:cur + n]; cur += n
                    canvas[y0:y1, x0:x1] = lut[tile_idx].reshape(y1 - y0, x1 - x0, 3)
                frames[i] = canvas.copy()

    # 空帧(理论上不会出现,兜底)
    last = np.zeros((H, W, 3), np.uint8)
    for i in range(N):
        if frames[i] is None:
            frames[i] = last.copy()
        last = frames[i]

    return frames, dict(W=W, H=H, N=N, fps=fps, ncolors=ncolors,
                        index_bits=index_bits, tile=tw, seg_count=seg_count)


# ---------------------------------------------------------------------------
# 导出 / 预览
# ---------------------------------------------------------------------------
def export_gif(frames, meta, out):
    imgs = [Image.fromarray(f) for f in frames]
    dur = max(20, round(1000 / max(1, meta["fps"])))
    imgs[0].save(out, save_all=True, append_images=imgs[1:], duration=dur, loop=0, disposal=1)
    print(f"导出 GIF: {out}  ({meta['N']} 帧, {meta['fps']} fps)")

def export_png(frames, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    for i, f in enumerate(frames):
        Image.fromarray(f).save(os.path.join(out_dir, f"frame_{i:04d}.png"))
    print(f"导出 {len(frames)} 张 PNG 到 {out_dir}/")

def play(frames, meta, scale=1, loop=False):
    try:
        import tkinter as tk
        from PIL import ImageTk
    except Exception as e:
        print("无法启动窗口预览(缺 Tkinter):", e)
        print("改用导出 GIF: python3 tef_decoder.py in.tef out.gif")
        return
    W, H, fps = meta["W"], meta["H"], meta["fps"]
    delay = max(20, round(1000 / max(1, fps)))
    root = tk.Tk(); root.title("TEF 播放预览")
    canvas = tk.Label(root); canvas.pack()
    imgs = [ImageTk.PhotoImage(Image.fromarray(f).resize((W * scale, H * scale), Image.NEAREST)) for f in frames]
    state = {"i": 0}
    def step():
        canvas.configure(image=imgs[state["i"]])
        state["i"] += 1
        if state["i"] >= len(imgs):
            if loop: state["i"] = 0
            else: return
        root.after(delay, step)
    step(); root.mainloop()


def main():
    ap = argparse.ArgumentParser(description="TEF 解码器/播放器(纯 Python)")
    ap.add_argument("input", help="输入 .tef")
    ap.add_argument("output", nargs="?", help="输出 .gif 或 PNG 目录(--png)")
    ap.add_argument("--png", action="store_true", help="导出 PNG 帧序列")
    ap.add_argument("--play", action="store_true", help="窗口实时预览(需 Tkinter)")
    ap.add_argument("--scale", type=int, default=2, help="预览放大倍数")
    ap.add_argument("--loop", action="store_true", help="循环播放")
    ap.add_argument("--mode", choices=["gram", "cache"], default="gram")
    a = ap.parse_args()

    frames, meta = decode_tef(a.input, a.mode)
    print(f"解码: {meta['W']}x{meta['H']} {meta['N']}帧 {meta['fps']}fps "
          f"{meta['ncolors']}色 {meta['index_bits']}bit tile={meta['tile']} 段={meta['seg_count']}")

    if a.play:
        play(frames, meta, a.scale, a.loop)
    elif a.png:
        export_png(frames, a.output or "frames")
    else:
        export_gif(frames, meta, a.output or "out.gif")


if __name__ == "__main__":
    main()
