#!/usr/bin/env python3
"""
Convert PNG files to LVGL v8 C sources (TRUE_COLOR_ALPHA, multi LV_COLOR_DEPTH).
Output matches lv_draw_sw_img.c expectations for pixel layout.

Example (cwd = this script's directory, i.e. assets/):
    python3 png_to_lvgl_v8_icon_c.py picture ../icon
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from PIL import Image


def c_ident_from_stem(stem: str) -> str:
    """Prefix with icon_ to avoid clashes with libc (e.g. clock) and C keywords."""
    s = re.sub(r"[^0-9a-zA-Z_]", "_", stem)
    if s and s[0].isdigit():
        s = "_" + s
    base = s or "img"
    if base.startswith("icon_"):
        return base
    return f"icon_{base}"


def rgb565_full(r: int, g: int, b: int) -> int:
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F
    return (r5 << 11) | (g6 << 5) | b5


def color8_full(r: int, g: int, b: int) -> int:
    red = (r >> 5) & 0x7
    green = (g >> 5) & 0x7
    blue = (b >> 6) & 0x3
    return (red << 5) | (green << 2) | blue


def format_rows(data: bytes, per_line: int = 12) -> str:
    lines: list[str] = []
    for i in range(0, len(data), per_line):
        chunk = data[i : i + per_line]
        lines.append("  " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    return "\n".join(lines)


def build_pixels_rgba(img: Image.Image) -> list[tuple[int, int, int, int]]:
    rgba = img.convert("RGBA")
    w, h = rgba.size
    px = list(rgba.getdata())
    return [(p[0], p[1], p[2], p[3]) for p in px]


def pack_depth_8_alpha(pixels: list[tuple[int, int, int, int]]) -> bytes:
    out = bytearray()
    for r, g, b, a in pixels:
        out.append(color8_full(r, g, b))
        out.append(a)
    return bytes(out)


def pack_depth_16_alpha(pixels: list[tuple[int, int, int, int]], swap16: bool) -> bytes:
    out = bytearray()
    for r, g, b, a in pixels:
        v = rgb565_full(r, g, b)
        lo = v & 0xFF
        hi = (v >> 8) & 0xFF
        if swap16:
            out.extend((hi, lo, a))
        else:
            out.extend((lo, hi, a))
    return bytes(out)


def pack_depth_32_alpha(pixels: list[tuple[int, int, int, int]]) -> bytes:
    out = bytearray()
    for r, g, b, a in pixels:
        out.extend((b & 0xFF, g & 0xFF, r & 0xFF, a & 0xFF))
    return bytes(out)


def emit_c_file(
    out_path: Path,
    symbol: str,
    w: int,
    h: int,
    b8: bytes,
    b16: bytes,
    b16s: bytes,
    b32: bytes,
) -> None:
    map_name = f"{symbol}_map"
    body = f'''/**
 * @file {out_path.name}
 * @brief LVGL v8 image (TRUE_COLOR_ALPHA), generated from PNG
 * @note Regenerate: cd view/assets/res && python3 png_to_lvgl_v8_icon_c.py picture ../icon
 */
#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST uint8_t {map_name}[] = {{
#if LV_COLOR_DEPTH == 1 || LV_COLOR_DEPTH == 8
  /*Pixel format: LV_IMG_CF_TRUE_COLOR_ALPHA, 8-bit color + alpha*/
{format_rows(b8)}
#endif
#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP == 0
  /*Pixel format: RGB565 LE + alpha*/
{format_rows(b16)}
#endif
#if LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP != 0
  /*Pixel format: RGB565 byte-swapped + alpha*/
{format_rows(b16s)}
#endif
#if LV_COLOR_DEPTH == 32
  /*Pixel format: B,G,R,A*/
{format_rows(b32)}
#endif
}};

const lv_img_dsc_t {symbol} = {{
  .header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = {w},
  .header.h = {h},
  .data_size = sizeof({map_name}),
  .data = {map_name},
}};
'''
    out_path.write_text(body, encoding="utf-8")


def convert_png(png_path: Path, out_dir: Path) -> None:
    stem = png_path.stem
    symbol = c_ident_from_stem(stem)
    out_c = out_dir / f"{stem}.c"
    img = Image.open(png_path)
    if img.mode not in ("RGB", "RGBA", "P", "L"):
        img = img.convert("RGBA")
    w, h = img.size
    pixels = build_pixels_rgba(img)
    b8 = pack_depth_8_alpha(pixels)
    b16 = pack_depth_16_alpha(pixels, swap16=False)
    b16s = pack_depth_16_alpha(pixels, swap16=True)
    b32 = pack_depth_32_alpha(pixels)
    emit_c_file(out_c, symbol, w, h, b8, b16, b16s, b32)


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Convert PNGs to LVGL v8 C image sources.",
        epilog="Example: python3 png_to_lvgl_v8_icon_c.py picture ../icon",
    )
    ap.add_argument("picture_dir", type=Path, help="Directory containing PNG files")
    ap.add_argument("out_dir", type=Path, help="Output directory for .c files")
    args = ap.parse_args()
    pic = args.picture_dir.resolve()
    out = args.out_dir.resolve()
    if not pic.is_dir():
        print(f"Not a directory: {pic}", file=sys.stderr)
        return 1
    out.mkdir(parents=True, exist_ok=True)
    pngs = sorted(pic.glob("*.png"))
    if not pngs:
        print(f"No PNG in {pic}", file=sys.stderr)
        return 1
    for p in pngs:
        convert_png(p, out)
        print(p.name, "->", out / f"{p.stem}.c")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
