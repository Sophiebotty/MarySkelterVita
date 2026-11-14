#!/usr/bin/env python3
"""Generate original placeholder LiveArea images for the VPK.

Writes 8-bit palette (indexed) PNGs using only the Python standard library:
  sce_sys/icon0.png                       128 x 128
  sce_sys/livearea/contents/bg.png        840 x 500
  sce_sys/livearea/contents/startup.png   280 x 158

Usage (from the project root):  python3 -I tools/make_placeholder_art.py
"""
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Palette: 0-31 gradient navy->teal, 32 grid line, 33 gold, 34 panel dark.
PALETTE = []
for i in range(32):
    t = i / 31.0
    PALETTE.append((int(255 * (0.05 + 0.02 * t)),
                    int(255 * (0.07 + 0.13 * t)),
                    int(255 * (0.16 + 0.10 * t))))
GRID, GOLD, PANEL = 32, 33, 34
PALETTE += [(31, 51, 77), (217, 179, 77), (20, 23, 33)]


def write_png(path, width, height, pixels):
    """pixels: list of rows, each a list of palette indices."""
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(row) for row in pixels)
    plte = b"".join(bytes(rgb) for rgb in PALETTE)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0))
           + chunk(b"PLTE", plte)
           + chunk(b"IDAT", zlib.compress(raw, 9))
           + chunk(b"IEND", b""))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)
    print("wrote", os.path.relpath(path, ROOT), f"{width}x{height}")


def scene(width, height, grid_step, panel_margin, border):
    rows = []
    for y in range(height):
        grad = min(31, y * 32 // height)
        row = []
        for x in range(width):
            px = grad
            if x % grid_step == 0 or y % grid_step == 0:
                px = GRID
            if panel_margin is not None:
                mx, my = panel_margin
                if mx <= x < width - mx and my <= y < height - my:
                    inner = (mx + border <= x < width - mx - border and
                             my + border <= y < height - my - border)
                    px = PANEL if inner else GOLD
            row.append(px)
        rows.append(row)
    return rows


def icon(size):
    """Simple original motif: gold doorway arch on a gradient with grid."""
    rows = scene(size, size, 16, None, 0)
    cx, top, bottom, half_w, thick = size // 2, size // 5, size - size // 6, size // 4, 6
    for y in range(top, bottom):
        for x in range(cx - half_w - thick, cx + half_w + thick):
            dx, dy = x - cx, y - (top + half_w)
            if y < top + half_w:  # rounded top
                d2 = dx * dx + dy * dy
                outer, inner = (half_w + thick) ** 2, half_w ** 2
                if inner <= d2 < outer:
                    rows[y][x] = GOLD
                elif d2 < inner:
                    rows[y][x] = PANEL
            else:
                if abs(dx) >= half_w:
                    rows[y][x] = GOLD
                else:
                    rows[y][x] = PANEL
    for x in range(cx - half_w - thick - 6, cx + half_w + thick + 6):
        for y in range(bottom, min(size, bottom + 4)):
            rows[y][x] = GOLD
    return rows


def main():
    write_png(os.path.join(ROOT, "sce_sys", "icon0.png"), 128, 128, icon(128))
    write_png(os.path.join(ROOT, "sce_sys", "livearea", "contents", "bg.png"),
              840, 500, scene(840, 500, 40, None, 0))
    write_png(os.path.join(ROOT, "sce_sys", "livearea", "contents", "startup.png"),
              280, 158, scene(280, 158, 20, (30, 30), 3))
    return 0


if __name__ == "__main__":
    sys.exit(main())
