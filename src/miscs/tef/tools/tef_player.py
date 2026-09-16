#!/usr/bin/env python3
"""
TEF 播放器 —— 桌面窗口播放 .tef 动画,带播放控制。

复用 tef_decoder.decode_tef 的解码逻辑(纯 Python,不依赖 C/libjpeg)。

用法:
  python3 tef_player.py in.tef                 # 默认播放(循环)
  python3 tef_player.py in.tef --scale 3       # 放大 3 倍
  python3 tef_player.py in.tef --fps 30        # 覆盖帧率
  python3 tef_player.py in.tef --no-loop       # 播完即停
  python3 tef_player.py in.tef --info          # 只打印信息,不开窗(可无 GUI 环境)

窗口内快捷键:
  空格      暂停 / 继续
  ← / →     上一帧 / 下一帧(暂停时逐帧)
  ↑ / ↓     加速 / 减速
  R         从头播放
  L         切换循环
  S         保存当前帧为 PNG
  Esc / Q   退出

依赖: Pillow, numpy;窗口播放需要 Tkinter(多数 Python 自带)。
"""
import sys, os, time, argparse
import numpy as np

try:
    from tef_decoder import decode_tef
except ImportError:
    sys.exit("需要同目录下的 tef_decoder.py(纯 Python 解码器)。")


def print_info(meta, frames):
    W, H, N, fps = meta["W"], meta["H"], meta["N"], meta["fps"]
    dur = N / fps if fps else 0
    print(f"文件信息:")
    print(f"  画布      {W} x {H}")
    print(f"  帧数      {N}")
    print(f"  帧率      {fps} fps   (总时长约 {dur:.1f}s)")
    print(f"  颜色      {meta['ncolors']} 色 / {meta['index_bits']}bit 索引")
    print(f"  tile      {meta['tile']}")
    print(f"  段数      {meta['seg_count']}")
    print(f"  解码帧    {len(frames)} 帧,首帧尺寸 {frames[0].shape}")


class Player:
    def __init__(self, frames, meta, scale, loop, fps_override, src_path):
        import tkinter as tk
        from PIL import Image, ImageTk
        self.tk, self.Image, self.ImageTk = tk, Image, ImageTk
        self.frames = frames
        self.meta = meta
        self.scale = scale
        self.loop = loop
        self.fps = fps_override or meta["fps"] or 25
        self.src_path = src_path
        self.W, self.H = meta["W"], meta["H"]
        self.i = 0
        self.paused = False
        self.speed = 1.0

        self.root = tk.Tk()
        self.root.title(os.path.basename(src_path))
        self.root.configure(bg="#1e1e1e")
        self.label = tk.Label(self.root, bg="#1e1e1e")
        self.label.pack()
        self.status = tk.Label(self.root, fg="#cccccc", bg="#1e1e1e",
                               font=("Menlo", 11), anchor="w")
        self.status.pack(fill="x")

        # 预转所有帧为 PhotoImage(桌面播放,内存换流畅)
        self.photos = [self._to_photo(f) for f in frames]

        # 键盘绑定
        r = self.root
        r.bind("<space>", lambda e: self.toggle_pause())
        r.bind("<Left>",  lambda e: self.step(-1))
        r.bind("<Right>", lambda e: self.step(1))
        r.bind("<Up>",    lambda e: self.change_speed(1.25))
        r.bind("<Down>",  lambda e: self.change_speed(0.8))
        r.bind("r", lambda e: self.restart())
        r.bind("l", lambda e: self.toggle_loop())
        r.bind("s", lambda e: self.save_frame())
        r.bind("q", lambda e: r.destroy())
        r.bind("<Escape>", lambda e: r.destroy())

        self._show(0)
        self._schedule()

    def _to_photo(self, f):
        img = self.Image.fromarray(f)
        if self.scale != 1:
            img = img.resize((self.W * self.scale, self.H * self.scale), self.Image.NEAREST)
        return self.ImageTk.PhotoImage(img)

    def _delay_ms(self):
        return max(10, int(1000 / (self.fps * self.speed)))

    def _show(self, i):
        self.label.configure(image=self.photos[i])
        st = "⏸ 暂停" if self.paused else "▶ 播放"
        lp = "循环" if self.loop else "单次"
        self.status.configure(
            text=f"  {st}  帧 {i+1}/{len(self.frames)}  {self.fps:.0f}fps ×{self.speed:.2f}  {lp}"
                 f"   [空格]暂停 [←→]逐帧 [↑↓]调速 [R]重播 [L]循环 [S]存帧 [Q]退出")

    def _schedule(self):
        if not self.paused:
            self.i += 1
            if self.i >= len(self.frames):
                if self.loop:
                    self.i = 0
                else:
                    self.i = len(self.frames) - 1
                    self.paused = True
            self._show(self.i)
        self.root.after(self._delay_ms(), self._schedule)

    # --- 控制 ---
    def toggle_pause(self): self.paused = not self.paused; self._show(self.i)
    def toggle_loop(self):  self.loop = not self.loop; self._show(self.i)
    def change_speed(self, k): self.speed = min(8.0, max(0.125, self.speed * k)); self._show(self.i)
    def restart(self): self.i = 0; self.paused = False; self._show(0)
    def step(self, d):
        self.paused = True
        self.i = (self.i + d) % len(self.frames)
        self._show(self.i)
    def save_frame(self):
        out = f"{os.path.splitext(os.path.basename(self.src_path))[0]}_frame{self.i:04d}.png"
        self.Image.fromarray(self.frames[self.i]).save(out)
        print("已保存", out)

    def run(self):
        self.root.mainloop()


def main():
    ap = argparse.ArgumentParser(description="TEF 播放器")
    ap.add_argument("input", help="输入 .tef")
    ap.add_argument("--scale", type=int, default=2, help="放大倍数(默认 2)")
    ap.add_argument("--fps", type=float, default=None, help="覆盖帧率")
    ap.add_argument("--no-loop", action="store_true", help="播完即停")
    ap.add_argument("--info", action="store_true", help="仅打印信息,不开窗")
    a = ap.parse_args()

    frames, meta = decode_tef(a.input)
    print_info(meta, frames)
    if a.info:
        return

    try:
        import tkinter  # noqa
    except Exception as e:
        print("\n无法开窗播放(缺 Tkinter):", e)
        print("可改用: python3 tef_decoder.py", a.input, "out.gif   然后用看图工具播放")
        return

    Player(frames, meta, a.scale, not a.no_loop, a.fps, a.input).run()


if __name__ == "__main__":
    main()
