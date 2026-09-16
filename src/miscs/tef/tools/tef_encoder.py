#!/usr/bin/env python3
"""
TEF (Tuya Emotion Format) 编码器 —— Python 参考实现。

严格对应技术规范 §4 编码流程的七步:
  1. GIF 解码与合成      (Pillow)
  2. 全局调色板量化 + 位深自适应 (1/4/8bit)
  3. 自适应 tile 尺寸扫描 (8/16/32/64 选最优)
  4. 逐帧 tile 差分 -> 脏块位图
  5. 逐帧编码方式判定    (调色板 vs JPEG,比较压缩后大小)
  6. 成段 + 压缩         (多帧共享一条流,位图并入流)
  7. 写文件

输出的 .tef 与 C 版解码器 (tef_codec) 字节级兼容。

依赖: Pillow, numpy  (JPEG 帧需要 Pillow 的 JPEG 支持,默认自带)
用法:
  python3 tef_encoder.py input.gif output.tef [--seg 16] [--jpeg-q 85]
                         [--tiles 8,16,32,64] [--rgb888] [--stabilize auto]

编码器接口(库):
  encode_gif(gif_path, out_path, seg=16, jpeg_q=85, tiles=(8,16,32,64)) -> dict(stats)
"""
import sys, struct, zlib, io, argparse
import numpy as np
from PIL import Image

# ----------------------------------------------------------------------------
# 常量(与 tef_format.h 对应)
# ----------------------------------------------------------------------------
MAGIC        = b"TEF1"
VERSION      = 3      # v3: 头部新增 mode/window_bits/flush_policy/max_seg_decomp
SEG_PALETTE  = 0
SEG_JPEG     = 1
COMP_HEATSHRINK = 0     # 本 Python 版仅用 deflate;heatshrink 由 C 版可选
COMP_DEFLATE    = 1

def deflate_raw(data, window=14):
    """RAW deflate(无 zlib 头/尾),与 C 端 miniz tinfl 兼容。window(9~15)决定
    回溯距离上限 = 解码端环形字典大小(1<<window):14 => 16KB 字典,比 15 仅
    大 ~1.4% flash,却省一半解码 RAM。"""
    co = zlib.compressobj(9, zlib.DEFLATED, -window)
    return co.compress(data) + co.flush()

# v3 头 38 字节: 前 22B 与 v1 相同(到 seg_count),随后 mode,window_bits,
# flush_policy,rsvd(各 u8) + max_seg_decomp(u32) + palette_off/seg_table_off(u32)
HEADER_COMMON_FMT = "<4sHHHBBHHHBBH"   # 22 字节: magic..seg_count
MODE_ROM = 0          # deflate(本编码器固定;RAM 优先模式见 TEF v3 P2)
FLUSH_IMMEDIATE = 0
SEG_FMT    = "<BBHHHII"          # 16 字节: method,comp,fstart,fcount,_r,offset,comp_len


# ============================================================================
# 步骤 1 — GIF 解码与合成:逐帧按 disposal 合成为不透明 RGB 帧
# ============================================================================
def decode_gif(gif_path, max_frames=None):
    im = Image.open(gif_path)
    W, H = im.size
    N = im.n_frames if max_frames is None else min(im.n_frames, max_frames)
    canvas = Image.new("RGBA", (W, H), (0, 0, 0, 255))
    frames = []
    durations = []
    for i in range(N):
        im.seek(i)
        canvas.alpha_composite(im.convert("RGBA"))
        frames.append(np.asarray(canvas.convert("RGB"), np.uint8).copy())
        durations.append(im.info.get("duration", 40))
    fps = max(1, round(1000 / (sum(durations) / len(durations)))) if durations else 25
    return np.stack(frames), W, H, N, fps


# ============================================================================
# 步骤 1.5 — 时域整形(--stabilize,编码端前处理,格式/解码器零改动)
#   AI 生成与抖动量化会把噪声烤进像素:帧间变化像素多为 Δ≤24 的微小闪烁,
#   把块差分脏区吹大数倍。逐像素比较本帧与(已稳定的)上一帧,
#   max(ΔR,ΔG,ΔB) ≤ 阈值 的像素钉回上一帧值——真动作 Δ 大不受影响。
#   对整形后帧无损;对原始输出为感知无损。默认关闭,显式开启。
#   注意:时域钉回对缓慢渐变内容(淡入淡出类)会产生台阶感,不建议开启。
# ============================================================================
STAB_LEVELS = (8, 16, 24, 32)   # 候选阈值
STAB_MICRO  = 24                # 微差上界:Δ≤24 视为人眼不可见的量化闪烁
STAB_RATIO  = 0.40              # auto:微差占变化像素比例低于此值 => 干净素材,跳过
STAB_COVER  = 0.90              # auto:选覆盖 ≥90% 变化像素的最小阈值

def analyze_stability(frames):
    """统计原始相邻帧变化像素(Δ>0)的 Δ 分布,Δ = max(|ΔR|,|ΔG|,|ΔB|)。
    返回 cover 字典: cover[T] = P(Δ≤T | Δ>0);无变化像素时返回 None。"""
    d = frames.astype(np.int16)
    delta = np.abs(d[1:] - d[:-1]).max(axis=-1)      # [N-1,H,W]
    changed = delta[delta > 0]
    if changed.size == 0:
        return None
    return {t: float((changed <= t).mean()) for t in STAB_LEVELS}

def stabilize_frames(frames, threshold):
    """逐帧顺序整形(必须对比已稳定的上一帧,不可跨帧向量化)。
    返回 (整形后帧, 被钉回像素占变化像素比例)。"""
    out = frames.copy()
    pinned = changed = 0
    for i in range(1, len(out)):
        delta = np.abs(out[i].astype(np.int16)
                       - out[i - 1].astype(np.int16)).max(axis=-1)
        mask = delta <= threshold
        out[i][mask] = out[i - 1][mask]
        pinned  += int(((delta > 0) & mask).sum())
        changed += int((delta > 0).sum())
    return out, (pinned / changed if changed else 0.0)

def resolve_stabilize(frames, stabilize):
    """把 --stabilize 参数解析为 (阈值或 None, 说明文字)。
    auto: 微差占比 < STAB_RATIO 跳过;否则取覆盖 ≥STAB_COVER 变化像素的
    最小阈值,全部不达标时取最大档(剩余大 Δ 属真动作,钉回微差仍然安全)。"""
    if stabilize is None:
        return None, "off"
    if stabilize != "auto":
        return int(stabilize), f"Δ≤{int(stabilize)}(手动)"
    cover = analyze_stability(frames)
    if cover is None:
        return None, "auto → 跳过(静态素材,无变化像素)"
    if cover[STAB_MICRO] < STAB_RATIO:
        return None, (f"auto → 跳过(干净素材,微差 Δ≤{STAB_MICRO} "
                      f"仅占变化像素 {cover[STAB_MICRO]:.0%})")
    th = next((t for t in STAB_LEVELS if cover[t] >= STAB_COVER), STAB_LEVELS[-1])
    return th, f"auto → Δ≤{th}(微差占比 {cover[STAB_MICRO]:.0%},覆盖 {cover[th]:.0%})"


# ============================================================================
# 步骤 2 — 全局调色板量化 + 位深自适应
#   返回: idx[N,H,W] (uint8 索引), pal565[ncolors] (uint16), ncolors, bits
# ============================================================================
def to565(a):  # a[...,3] uint8 -> uint16 RGB565
    return (((a[..., 0] >> 3).astype(np.uint16) << 11)
            | ((a[..., 1] >> 2).astype(np.uint16) << 5)
            | (a[..., 2] >> 3))

def bits_for(ncolors):
    return 1 if ncolors <= 2 else (4 if ncolors <= 16 else 8)

def quantize_global(frames, W, H, N):
    f565 = to565(frames)                      # 先降到 RGB565 域
    cols = np.unique(f565)
    if len(cols) <= 256:
        # 精确全局调色板 —— 无损
        lut = {int(c): i for i, c in enumerate(cols)}
        idx = np.vectorize(lut.get)(f565).astype(np.uint8)
        pal = cols.astype(np.uint16)
    else:
        # 中位切分量化到 256 色 —— 有损(通常仅写实素材触发)
        q = Image.fromarray(frames.reshape(N * H, W, 3)).quantize(colors=256, dither=Image.NONE)
        idx = np.asarray(q, np.uint8).reshape(N, H, W)
        pr = np.array(q.getpalette()[:768], np.uint8).reshape(-1, 3)
        pal = to565(pr[None, :, :])[0].astype(np.uint16)
    ncolors = len(pal)          # 以实际调色板大小为准(量化后=256)
    return idx, pal, ncolors, bits_for(ncolors)


# ============================================================================
# 位打包 / 差分 辅助
# ============================================================================
def pack_indices(a, bits):
    """8bit 索引数组 -> 按位深打包的字节(与解码端一致,帧内字节对齐)。"""
    a = a.reshape(-1).astype(np.uint8)
    if bits == 8:
        return a.tobytes()
    if bits == 4:
        if len(a) % 2:
            a = np.append(a, np.uint8(0))
        return (((a[0::2] & 0xF) << 4) | (a[1::2] & 0xF)).astype(np.uint8).tobytes()
    # bits == 1
    pad = (-len(a)) % 8
    if pad:
        a = np.append(a, np.zeros(pad, np.uint8))
    b = a.reshape(-1, 8)
    return (b * (1 << np.arange(7, -1, -1))).sum(1).astype(np.uint8).tobytes()

def tile_grid(W, H, TW):
    ntx = (W + TW - 1) // TW
    nty = (H + TW - 1) // TW
    return ntx, nty, ntx * nty

def frame_dirty_tiles(idx, i, W, H, TW):
    """步骤 4: 返回本帧脏 tile 的 (位图 bytes, 拼接索引 uint8[], 包围盒)。"""
    ntx, nty, ntiles = tile_grid(W, H, TW)
    bm = bytearray((ntiles + 7) // 8)
    parts = []
    bx0, by0, bx1, by1 = W, H, 0, 0
    prev = None if i == 0 else idx[i - 1]
    cur = idx[i]
    for ty in range(nty):
        for tx in range(ntx):
            x0, y0 = tx * TW, ty * TW
            x1, y1 = min(x0 + TW, W), min(y0 + TW, H)
            blk = cur[y0:y1, x0:x1]
            dirty = (prev is None) or not np.array_equal(blk, prev[y0:y1, x0:x1])
            if dirty:
                ti = ty * ntx + tx
                bm[ti >> 3] |= 1 << (ti & 7)
                parts.append(blk.reshape(-1))
                bx0, by0 = min(bx0, x0), min(by0, y0)
                bx1, by1 = max(bx1, x1), max(by1, y1)
    indices = np.concatenate(parts) if parts else np.zeros(0, np.uint8)
    bbox = (bx0, by0, bx1, by1) if parts else (0, 0, 2, 2)
    return bytes(bm), indices, bbox


# ============================================================================
# 步骤 5 — 逐帧编码方式判定(调色板 vs JPEG,比较压缩后大小)
# ============================================================================
def jpeg_encode_565(pal, idx_frame, rect, quality):
    """把脏矩形(索引->RGB565->RGB888)编码为 baseline JPEG,返回 (bytes, (x,y,w,h))。"""
    x0, y0, x1, y1 = rect
    # 8 对齐
    rx0, ry0 = x0 & ~7, y0 & ~7
    rx1, ry1 = min(idx_frame.shape[1], (x1 + 7) & ~7), min(idx_frame.shape[0], (y1 + 7) & ~7)
    sub_idx = idx_frame[ry0:ry1, rx0:rx1]
    v = pal[sub_idx]                            # RGB565
    r = ((v >> 11) & 0x1F); g = ((v >> 5) & 0x3F); b = (v & 0x1F)
    rgb = np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.uint8)
    buf = io.BytesIO()
    Image.fromarray(rgb).save(buf, "JPEG", quality=quality)
    return buf.getvalue(), (rx0, ry0, rx1 - rx0, ry1 - ry0)

def classify_frame(pal, idx, i, W, H, TW, bits, quality):
    """返回 ('pal', 载荷估计) 或 ('jpeg', jpeg_bytes, rect)。比较压缩后大小。"""
    bm, indices, bbox = frame_dirty_tiles(idx, i, W, H, TW)
    if len(indices) == 0:
        return ("pal", 0, bm, indices)          # 空帧仍走调色板(极小)
    # 候选 A:调色板(压缩后)
    packed = pack_indices(indices, bits)
    palA = len(deflate_raw(packed)) + len(bm)
    # quality<=0 → 禁用 JPEG 候选(设备端解码器不带 JPEG,嵌入式素材用这个)
    if quality is None or quality <= 0:
        return ("pal", palA, bm, indices)
    # 候选 B:JPEG
    jbytes, jrect = jpeg_encode_565(pal, idx[i], bbox, quality)
    jB = 8 + len(jbytes)
    if jB < palA:
        return ("jpeg", jbytes, jrect)
    return ("pal", palA, bm, indices)


# ============================================================================
# 步骤 6+7 — 成段、压缩、写文件(给定 tile 尺寸,产出完整 .tef 字节)
# ============================================================================
def pack_header(W, H, TW, N, fps, ncolors, bits, flags, seg_count,
                max_seg_decomp, palette_off, seg_table_off, window):
    h = struct.pack(HEADER_COMMON_FMT, MAGIC, VERSION, W, H, TW, TW, N, fps,
                    ncolors, bits, flags, seg_count)
    h += struct.pack("<BBBB", MODE_ROM, window, FLUSH_IMMEDIATE, 0)
    h += struct.pack("<I", max_seg_decomp)   # 最大段明文字节数(解码端按真值分配)
    h += struct.pack("<II", palette_off, seg_table_off)
    return h

def encode_with_tile(idx, pal, W, H, N, fps, ncolors, bits, flags, TW, seg_len, quality, window):
    """按指定 tile 尺寸执行步骤 4-7,返回完整 .tef 字节。"""
    # 先对每帧分类(步骤 5)
    kinds = [classify_frame(pal, idx, i, W, H, TW, bits, quality) for i in range(N)]

    palette_off = 38                    # v3 头 38 字节
    body = bytearray()
    segs = []
    max_seg_decomp = 0                  # 全文件最大段明文(解码端按真值分配)
    body_base = palette_off + ncolors * 2

    i = 0
    while i < N:
        if kinds[i][0] == "jpeg":
            # JPEG 段(单帧)
            _, jbytes, (x, y, w, h) = kinds[i]
            offset = body_base + len(body)
            payload = struct.pack("<HHHH", x, y, w, h) + jbytes
            body += payload
            segs.append((SEG_JPEG, 0, i, 1, offset, len(payload)))
            i += 1
        else:
            # 调色板段:合并连续调色板帧,至多 seg_len 帧
            j = i
            blob = bytearray()
            while j < N and kinds[j][0] == "pal" and (j - i) < seg_len:
                _, _, bm, indices = kinds[j]
                blob += bm                                  # 位图并入流
                blob += pack_indices(indices, bits)         # 打包索引
                j += 1
            comp = deflate_raw(bytes(blob), window)         # RAW deflate(与 C 端兼容)
            max_seg_decomp = max(max_seg_decomp, len(blob))
            offset = body_base + len(body)
            body += comp
            segs.append((SEG_PALETTE, COMP_DEFLATE, i, j - i, offset, len(comp)))
            i = j

    seg_table_off = body_base + len(body)
    hdr = pack_header(W, H, TW, N, fps, ncolors, bits, flags, len(segs),
                      max_seg_decomp, palette_off, seg_table_off, window)
    out = bytearray()
    out += hdr
    out += pal.astype("<u2").tobytes()
    out += body
    for s in segs:
        out += struct.pack(SEG_FMT, s[0], s[1], s[2], s[3], 0, s[4], s[5])
    return bytes(out), segs


# ============================================================================
# 步骤 3 — 自适应 tile 扫描:对候选尺寸各编码一遍,选最小
# ============================================================================
def _encode_frames(frames, W, H, N, fps, flags, tiles, seg, jpeg_q, window):
    """步骤 2-6:给定(可能已整形的)RGB 帧,量化 + tile 扫描选最优,返回
    (size, TW, data, segs, ncolors, bits)。"""
    idx, pal, ncolors, bits = quantize_global(frames, W, H, N)       # 步骤 2
    best = None
    for TW in tiles:
        if TW > W and TW > H:
            continue
        data, segs = encode_with_tile(idx, pal, W, H, N, fps, ncolors, bits, flags, TW, seg, jpeg_q, window)
        if best is None or len(data) < best[0]:
            best = (len(data), TW, data, segs)                        # 步骤 3 选最优
    return best + (ncolors, bits)


def encode_gif(gif_path, out_path, seg=16, jpeg_q=85, tiles=(8, 16, 32, 64),
               rgb888=False, max_frames=None, verbose=True, window=14,
               stabilize=None):
    frames, W, H, N, fps = decode_gif(gif_path, max_frames)          # 步骤 1
    flags = 1 if rgb888 else 0
    stab_th, stab_info = resolve_stabilize(frames, stabilize)        # 步骤 1.5

    if stab_th is None:
        best = _encode_frames(frames, W, H, N, fps, flags, tiles, seg, jpeg_q, window)
    else:
        sframes, pinned = stabilize_frames(frames, stab_th)
        stab_info += f",钉回 {pinned:.0%} 变化像素"
        best = _encode_frames(sframes, W, H, N, fps, flags, tiles, seg, jpeg_q, window)
        if stabilize == "auto":
            # auto 结果保险:Δ 统计识别不了渐变类素材(整形反而涨体积),
            # 编码后对比,没变小就回退未整形版;手动指定阈值不回退。
            plain = _encode_frames(frames, W, H, N, fps, flags, tiles, seg, jpeg_q, window)
            if best[0] >= plain[0]:
                stab_info += f" → 回退(整形 {best[0]:,}B ≥ 原始 {plain[0]:,}B)"
                best = plain

    size, TW, data, segs, ncolors, bits = best
    with open(out_path, "wb") as f:
        f.write(data)

    pal_frames = sum(s[3] for s in segs if s[0] == SEG_PALETTE)
    jpeg_frames = sum(1 for s in segs if s[0] == SEG_JPEG)
    stats = dict(size=size, W=W, H=H, N=N, fps=fps, ncolors=ncolors,
                 index_bits=bits, tile=TW, seg_count=len(segs),
                 pal_frames=pal_frames, jpeg_frames=jpeg_frames,
                 stabilize=stab_info)
    if verbose:
        print(f"{out_path}: {W}x{H} {N}帧 {ncolors}色 {bits}bit  ->  {size:,} 字节")
        print(f"  tile={TW}  段数={len(segs)}  调色板帧={pal_frames}  JPEG帧={jpeg_frames}")
        if stabilize is not None:
            print(f"  stabilize: {stab_info}")
    return stats


def main():
    ap = argparse.ArgumentParser(description="TEF 编码器 (GIF -> .tef)")
    ap.add_argument("input");  ap.add_argument("output")
    ap.add_argument("--seg", type=int, default=16)
    ap.add_argument("--jpeg-q", type=int, default=85, help="<=0 禁用 JPEG 帧(纯调色板)")
    ap.add_argument("--tiles", default="8,16,32,64")
    ap.add_argument("--rgb888", action="store_true")
    ap.add_argument("--max-frames", type=int, default=None)
    ap.add_argument("--window", type=int, default=14, choices=range(9, 16),
                    help="deflate 窗口位数(9~15);解码端环形字典 = 1<<window 字节")
    ap.add_argument("--stabilize", default=None,
                    choices=["auto"] + [str(t) for t in STAB_LEVELS],
                    help="时域整形阈值,抑制 AI/抖动素材的帧间噪声;"
                         "auto 按 Δ 分布自动选档或跳过。默认关闭")
    a = ap.parse_args()
    tiles = tuple(int(x) for x in a.tiles.split(","))
    encode_gif(a.input, a.output, a.seg, a.jpeg_q, tiles, a.rgb888, a.max_frames,
               window=a.window, stabilize=a.stabilize)


if __name__ == "__main__":
    main()
