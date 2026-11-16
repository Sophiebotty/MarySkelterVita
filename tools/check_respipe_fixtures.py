#!/usr/bin/env python3
"""Independent checker for the Vita ResPipe fixtures (Phase 10, stage 10.2, VDNG00006).

Shares no code with tools/make_respipe_fixtures.py. Written from
RESOURCE_PIPELINE.md (VRS1 version 1). It

 1. runs every pack through its own model of every check in section 6.1, in
    order: strict GDAT open, entry count, table of contents (wrapper, exact
    inflate, CFP1, VRS1), region limits, then each entry (wrapper, inflate,
    CFP1, envelope rule, table-of-contents match, VRS1 checks, references,
    application policy). Format is compared with the table of contents
    together with vram_bytes and heap_bytes (step C.3.7), the order the
    owner chose on 2026-11-16 for case I-08;
 2. checks that every case in section 10 gives the code the document states,
    at the GDAT entry and stage in the manifest (and, for CF_ERR_PAYLOAD, the
    CFP1 field the document names);
 3. checks that example.gdat holds exactly the document's hex examples, and
    that every accepted pack's level is the document's reference room;
 4. decodes every level of every diagnostic texture (full RGBA8, RGB565,
    DXT1, DXT5 and ETC1 decoders; PVRTC4 only as identical opaque blocks with
    modulation 0, which is exact for those) and compares the colours with
    rs_fixtures.h and with the planned colours;
 5. checks that each room texture's mip chain is the 2x2 box filter of the
    level above it;
 6. derives the reference-room mesh from src/gameplay/room_map.c, room_map.h
    and src/graphics/level_mesh.c (read, never changed) and compares it with
    every room pack's mesh, and checks every triangle faces into the room;
 7. recomputes every value in rs_fixtures.h and expected.json, and each
    pack's memory need (section 8.2) against the measured free memory (M1).

Exit status 0 only when everything agrees.
--evaluate FILE prints one pack's result instead (exit 0 accepted, 2 rejected).
"""
import argparse
import hashlib
import json
import math
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_DIR = os.path.join(ROOT, "respipe", "fixtures")
DOC = os.path.join(ROOT, "RESOURCE_PIPELINE.md")
ROOM_C = os.path.join(ROOT, "src", "gameplay", "room_map.c")
ROOM_H = os.path.join(ROOM_C[:-2] + ".h")
MESH_C = os.path.join(ROOT, "src", "graphics", "level_mesh.c")

MIB = 1 << 20
LIMIT8 = 8 * MIB
MAX_PAYLOAD = LIMIT8 - 32
GP_MAX_ENTRIES = 65536
GP_MAX_RAW = 16 * MIB
ZERO124_CRC = 0x15D70E0C
SLOTS = 3
ARENA = 65536

CODES = {
    "OK": 0, "RP_ERR_TRUNCATED": 9, "RP_ERR_SIZE_MISMATCH": 10, "RP_ERR_BAD_MAGIC": 11, "RP_ERR_COUNT": 16,
    "RP_ERR_RANGE": 17, "RP_ERR_OVERLAP": 18, "RP_ERR_LIMIT": 19, "RP_ERR_ZLIB_DATA": 21,
    "GP_ERR_IN_INDEX": 101, "GP_ERR_UNSORTED": 102, "GP_ERR_GAP": 103, "GP_ERR_TRAILING": 104,
    "GP_ERR_EMPTY": 105, "GP_ERR_SIZE_RULE": 106, "GP_ERR_CONST": 107, "CF_ERR_PAYLOAD": 211,
    "RS_ERR_MAGIC": 300, "RS_ERR_VERSION": 301, "RS_ERR_KIND": 302, "RS_ERR_HEADER_CRC": 303,
    "RS_ERR_BODY_SIZE": 304, "RS_ERR_BODY_CRC": 305, "RS_ERR_RESERVED": 306, "RS_ERR_ENUM": 307,
    "RS_ERR_RANGE": 308, "RS_ERR_ALIGN": 309, "RS_ERR_DIMENSION": 310, "RS_ERR_MIP_CHAIN": 311,
    "RS_ERR_LEVEL_SIZE": 312, "RS_ERR_INDEX": 313, "RS_ERR_FLOAT": 314, "RS_ERR_TOC": 315,
    "RS_ERR_DUP_ID": 316, "RS_ERR_REF": 317, "RS_ERR_LIMIT": 318, "RS_ERR_LEVEL_MISMATCH": 319,
    "RS_ERR_ENVELOPE": 320}
NAMES = {v: k for k, v in CODES.items()}

# Measured free memory at start (RESOURCE_PIPELINE.md M1) and the margins of section 8.1.
FREE = {"vram": int(83.9 * MIB), "heap": 134217728 - 57680, "kernel": 12 * MIB}
MARGIN = {"vram": 16 * MIB, "heap": 24 * MIB, "kernel": 8 * MIB}

# The planned diagnostic colours (stage 10.2 plan): level 0 white/black checkerboard (PVRTC4:
# white), levels 1-7 red, green, blue, yellow, cyan, magenta, black.
PLANNED = [[(255, 255, 255), (0, 0, 0)], [(255, 0, 0)], [(0, 255, 0)], [(0, 0, 255)], [(255, 255, 0)],
           [(0, 255, 255)], [(255, 0, 255)], [(0, 0, 0)]]
FORMAT_NAMES = ["rgba8", "rgb565", "dxt1", "dxt5", "etc1", "pvrtc4"]
VALID_PACKS = ["room.gdat"] + ["fmt_%s.gdat" % f for f in FORMAT_NAMES] + ["area8.gdat", "example.gdat",
                                                                          "example_room.gdat"]


class Fail(Exception):
    def __init__(self, name, stage, entry, detail=""):
        super().__init__(name)
        self.name, self.code, self.stage, self.entry, self.detail = name, CODES[name], stage, entry, detail


class Bad(Exception):
    """A disagreement found by the checker itself."""


def need(cond, name, stage, entry, detail=""):
    if not cond:
        raise Fail(name, stage, entry, detail)


def crc(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def le16(b, o):
    return b[o] | (b[o + 1] << 8)


def le32(b, o):
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)


def be32(b, o):
    return (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]


def f32(b, o):
    return struct.unpack_from("<f", b, o)[0]


def up16(n):
    return -(-n // 16) * 16


def is_pow2(n):
    return n >= 1 and (n & (n - 1)) == 0


def all_zero(b, lo, hi):
    return not any(b[lo:hi])


def level_size(fmt, w, h):
    blocks = -(-w // 4) * -(-h // 4)
    return {1: 4 * w * h, 2: 2 * w * h, 3: 8 * blocks, 4: 16 * blocks, 5: 8 * blocks,
            6: max(w, 8) * max(h, 8) // 2}[fmt]


# ------------------------------------------------------------------ transport (sections 4.3-4.5)
def gdat_open(data):
    """gp_gdat_open in strict mode (RESOURCE_PIPELINE.md 4.3 and 6.5)."""
    st = "open"
    need(len(data) >= 8, "RP_ERR_TRUNCATED", st, -1, "file under 8 bytes")
    need(data[:4] == b"GDAT", "RP_ERR_BAD_MAGIC", st, -1, "GDAT magic")
    n = le32(data, 4)
    need(n <= GP_MAX_ENTRIES, "RP_ERR_COUNT", st, -1, "entry count")
    end_of_index = 8 + 8 * n
    need(end_of_index <= len(data), "RP_ERR_TRUNCATED", st, -1, "index past end of file")
    need(n != 0, "GP_ERR_EMPTY", st, -1, "no entries")
    ents = [(le32(data, 8 + 8 * i), le32(data, 12 + 8 * i)) for i in range(n)]
    for i, (off, size) in enumerate(ents):
        need(off + size <= len(data), "RP_ERR_RANGE", st, i, "entry outside file")
        need(off >= end_of_index, "GP_ERR_IN_INDEX", st, i, "entry inside index")
    need(all(ents[i][0] >= ents[i - 1][0] for i in range(1, n)), "GP_ERR_UNSORTED", st, -1, "unsorted")
    pos = end_of_index
    for i, (off, size) in enumerate(ents):
        need(off >= pos, "RP_ERR_OVERLAP", st, i, "overlap")
        need(off == pos, "GP_ERR_GAP", st, i, "gap")
        pos = off + size
    need(pos == len(data), "GP_ERR_TRAILING", st, -1, "bytes after last entry")
    return [data[off:off + size] for off, size in ents]


def wrapper(reg, stage, entry):
    """gp_wrap_parse in strict mode: returns (raw size, compressed size)."""
    need(len(reg) >= 0x90, "RP_ERR_TRUNCATED", stage, entry, "region under 0x90")
    need(reg[0x80:0x84] == b"BILZ", "RP_ERR_BAD_MAGIC", stage, entry, "BILZ magic")
    usize, csize, const = be32(reg, 0x84), be32(reg, 0x88), be32(reg, 0x8C)
    room = len(reg) - 0x90
    need(csize <= room, "RP_ERR_RANGE", stage, entry, "compressed size over region")
    need(usize <= GP_MAX_RAW, "RP_ERR_LIMIT", stage, entry, "raw size over 16 MiB")
    need(const == 7, "GP_ERR_CONST", stage, entry, "constant")
    need(csize == room, "GP_ERR_SIZE_RULE", stage, entry, "compressed size under region")
    return usize, csize


def inflate_exact(stream, usize, stage, entry):
    """Complete stream, exactly usize bytes, every input byte used, Adler-32 (section 4.1 table)."""
    need(len(stream) > 0, "RP_ERR_TRUNCATED", stage, entry, "empty stream")
    d = zlib.decompressobj()
    try:
        out = d.decompress(stream, usize + 1)
    except zlib.error as e:
        raise Fail("RP_ERR_ZLIB_DATA", stage, entry, "zlib: %s" % e)
    need(len(out) <= usize, "RP_ERR_SIZE_MISMATCH", stage, entry, "output longer than raw size")
    need(d.eof, "RP_ERR_TRUNCATED", stage, entry, "stream ends early")
    need(not d.unused_data, "RP_ERR_SIZE_MISMATCH", stage, entry, "input left over")
    need(len(out) == usize, "RP_ERR_SIZE_MISMATCH", stage, entry, "output shorter than raw size")
    return out


def cfp1_field(raw, index):
    """The first CFP1 field that fails, in section 4.5's order, or None."""
    if len(raw) < 32:
        return "length"
    if raw[:4] != b"CFP1":
        return "magic"
    kind, w, h, blen, _bcrc, idx, res = struct.unpack_from("<7I", raw, 4)
    if res:
        return "reserved"
    if idx != index:
        return "index"
    if blen != len(raw) - 32:
        return "body length"
    if kind == 2:
        if w or h:
            return "kind"
    elif kind == 1:
        if not (is_pow2(w) and is_pow2(h) and w <= 4096 and h <= 4096 and 4 * w * h == blen):
            return "kind"
    else:
        return "kind"
    if crc(raw[32:]) != le32(raw, 20):
        return "body CRC"
    return None


# ------------------------------------------------------------------ VRS1 (sections 5 and 6.2-6.3)
def common_header(p, st, e):
    need(len(p) >= 32, "RS_ERR_BODY_SIZE", st, e, "payload under 32 bytes")
    need(p[:4] == b"VRS1", "RS_ERR_MAGIC", st, e, "magic")
    need(le16(p, 4) == 1, "RS_ERR_VERSION", st, e, "version")
    need(crc(p[:28]) == le32(p, 28), "RS_ERR_HEADER_CRC", st, e, "header CRC")
    kind = le16(p, 6)
    need(1 <= kind <= 4, "RS_ERR_KIND", st, e, "kind")
    need(all_zero(p, 20, 28), "RS_ERR_RESERVED", st, e, "header bytes 20-27")
    need(le32(p, 12) == len(p) - 32, "RS_ERR_BODY_SIZE", st, e, "body_size")
    need(crc(p[32:]) == le32(p, 16), "RS_ERR_BODY_CRC", st, e, "body CRC")
    return kind, le32(p, 8), p[32:]


def toc_check(p, n_archive, st, e):
    kind, rid, b = common_header(p, st, e)
    need(kind == 1 and rid == 0, "RS_ERR_TOC", st, e, "entry 0 is not a table of contents")
    need(len(b) >= 16, "RS_ERR_RANGE", st, e, "table of contents header")
    count = le32(b, 0)
    need(len(b) == 16 + 32 * count, "RS_ERR_RANGE", st, e, "table of contents size")
    need(all_zero(b, 12, 16), "RS_ERR_RESERVED", st, e, "table of contents reserved")
    need(1 <= count <= 255 and count == n_archive - 1, "RS_ERR_TOC", st, e, "entry count")
    rows, seen = [], set()
    for i in range(count):
        o = 16 + 32 * i
        row = {"id": le32(b, o), "kind": le16(b, o + 4), "format": le16(b, o + 6), "size": le32(b, o + 8),
               "crc": le32(b, o + 12), "vram": le32(b, o + 16), "heap": le32(b, o + 20)}
        need(all_zero(b, o + 24, o + 32), "RS_ERR_RESERVED", st, e, "row %d reserved" % i)
        need(row["id"] != 0 and 2 <= row["kind"] <= 4, "RS_ERR_TOC", st, e, "row %d id or kind" % i)
        good_fmt = 1 <= row["format"] <= 6 if row["kind"] == 2 else row["format"] == 0
        need(good_fmt, "RS_ERR_ENUM", st, e, "row %d format" % i)
        need(32 <= row["size"] <= MAX_PAYLOAD, "RS_ERR_LIMIT", st, e, "row %d payload_size" % i)
        need(row["id"] not in seen, "RS_ERR_DUP_ID", st, e, "row %d id" % i)
        seen.add(row["id"])
        rows.append(row)
    need(le32(b, 4) == sum(r["vram"] for r in rows) and le32(b, 8) == sum(r["heap"] for r in rows),
         "RS_ERR_TOC", st, e, "totals")
    return rows


def texture_check(b, st, e):
    need(len(b) >= 16, "RS_ERR_RANGE", st, e, "texture header")
    fmt, w, h, n = le16(b, 0), le16(b, 2), le16(b, 4), le16(b, 6)
    flags = le32(b, 8)
    need(1 <= fmt <= 6, "RS_ERR_ENUM", st, e, "format")
    need(flags < 8 and (not flags & 4 or fmt in (1, 2)), "RS_ERR_ENUM", st, e, "flags")
    need(all_zero(b, 12, 16), "RS_ERR_RESERVED", st, e, "texture reserved")
    need(is_pow2(w) and is_pow2(h) and w <= 2048 and h <= 2048 and (fmt != 6 or w == h),
         "RS_ERR_DIMENSION", st, e, "size")
    chain = int(math.log2(max(w, h))) + 1
    need(n in (1, chain) and (not flags & 4 or n == 1), "RS_ERR_MIP_CHAIN", st, e, "mip_count")
    dims = [(max(1, w >> i), max(1, h >> i)) for i in range(n)]
    need(fmt != 2 or all(lh == 1 for lw, lh in dims if lw == 1), "RS_ERR_DIMENSION", st, e, "RGB565 1-wide level")
    need(16 + 8 * n <= len(b), "RS_ERR_RANGE", st, e, "level table")
    end = 16 + 8 * n
    levels = []
    for i, (lw, lh) in enumerate(dims):
        off, size = le32(b, 16 + 8 * i), le32(b, 20 + 8 * i)
        need(off % 16 == 0, "RS_ERR_ALIGN", st, e, "level %d offset" % i)
        need(off == up16(end), "RS_ERR_RANGE", st, e, "level %d position" % i)
        need(size == level_size(fmt, lw, lh), "RS_ERR_LEVEL_SIZE", st, e, "level %d size" % i)
        need(off + size <= len(b), "RS_ERR_RANGE", st, e, "level %d end" % i)
        need(all_zero(b, end, off), "RS_ERR_RESERVED", st, e, "padding before level %d" % i)
        levels.append((lw, lh, b[off:off + size]))
        end = off + size
    need(end == len(b), "RS_ERR_RANGE", st, e, "body after last level")
    count = chain if flags & 4 else n
    vram = sum(level_size(fmt, max(1, w >> i), max(1, h >> i)) for i in range(count))
    return {"format": fmt, "vram": vram, "heap": 0, "refs": [], "w": w, "h": h, "flags": flags, "levels": levels}


def mesh_check(b, st, e):
    need(len(b) >= 32, "RS_ERR_RANGE", st, e, "mesh header")
    vc, ic = le32(b, 0), le32(b, 4)
    vfmt, stride, nr, prim = le16(b, 8), le16(b, 10), le16(b, 12), le16(b, 14)
    ro, vo, io = le32(b, 16), le32(b, 20), le32(b, 24)
    need(vfmt == 1 and stride == 24 and prim == 1, "RS_ERR_ENUM", st, e, "vertex format, stride or primitive")
    need(le32(b, 28) == 0, "RS_ERR_RESERVED", st, e, "mesh reserved")
    need(3 <= vc <= 65536 and 3 <= ic <= 393216 and ic % 3 == 0 and 1 <= nr <= 16, "RS_ERR_LIMIT", st, e, "counts")
    need(ro % 16 == 0 and vo % 16 == 0 and io % 16 == 0, "RS_ERR_ALIGN", st, e, "block offsets")
    need(ro == 32 and vo == up16(32 + 16 * nr) and io == up16(vo + 24 * vc) and len(b) == io + 2 * ic,
         "RS_ERR_RANGE", st, e, "block positions or body end")
    need(all_zero(b, 32 + 16 * nr, vo) and all_zero(b, vo + 24 * vc, io), "RS_ERR_RESERVED", st, e, "padding")
    ranges, nxt = [], 0
    for r in range(nr):
        first, count, tex, res = struct.unpack_from("<4I", b, 32 + 16 * r)
        need(res == 0, "RS_ERR_RESERVED", st, e, "range %d reserved" % r)
        need(first == nxt and count > 0 and count % 3 == 0, "RS_ERR_RANGE", st, e, "range %d" % r)
        nxt = first + count
        ranges.append((first, count, tex))
    need(nxt == ic, "RS_ERR_RANGE", st, e, "ranges do not cover the indices")
    verts = []
    for v in range(vc):
        vals = struct.unpack_from("<5f", b, vo + 24 * v)
        need(all(math.isfinite(x) for x in vals), "RS_ERR_FLOAT", st, e, "vertex %d" % v)
        verts.append((vals, tuple(b[vo + 24 * v + 20:vo + 24 * v + 24])))
    idx = list(struct.unpack_from("<%dH" % ic, b, io))
    need(all(i < vc for i in idx), "RS_ERR_INDEX", st, e, "index")
    return {"format": 0, "vram": 24 * vc + 2 * ic, "heap": 0, "refs": [r[2] for r in ranges if r[2]],
            "verts": verts, "indices": idx, "ranges": ranges}


def level_check(b, st, e):
    need(len(b) >= 16, "RS_ERR_RANGE", st, e, "level header")
    w, d, sx, sz = le16(b, 0), le16(b, 2), le16(b, 4), le16(b, 6)
    need(1 <= w <= 64 and 1 <= d <= 64, "RS_ERR_LIMIT", st, e, "size")
    need(len(b) == 16 + w * d, "RS_ERR_RANGE", st, e, "body end")
    cs, wh = f32(b, 8), f32(b, 12)
    need(math.isfinite(cs) and math.isfinite(wh) and cs > 0 and wh > 0, "RS_ERR_FLOAT", st, e, "cell or wall size")
    need(all(c in (0, 1) for c in b[16:]), "RS_ERR_ENUM", st, e, "cell value")
    need(sx < w and sz < d and b[16 + sz * w + sx] == 0, "RS_ERR_RANGE", st, e, "start cell")
    return {"format": 0, "vram": 0, "heap": len(b), "refs": []}


# ------------------------------------------------------------------ the reference room, from the C sources
def read_room_sources():
    src = open(ROOM_C, encoding="utf-8").read()
    block = re.search(r"ROOM_ROWS\[ROOM_D\]\s*=\s*\{(.*?)\};", src, re.S).group(1)
    rows = re.findall(r'"([#.]+)"', block)
    hdr = open(ROOM_H, encoding="utf-8").read()
    w = int(re.search(r"#define ROOM_W\s+(\d+)", hdr).group(1))
    d = int(re.search(r"#define ROOM_D\s+(\d+)", hdr).group(1))
    cell = float(re.search(r"#define ROOM_CELL_SIZE\s+([0-9.]+)f", hdr).group(1))
    wall = float(re.search(r"#define ROOM_WALL_HEIGHT\s+([0-9.]+)f", hdr).group(1))
    mesh_src = open(MESH_C, encoding="utf-8").read()
    shade = {k: int(v) for k, v in re.findall(r"#define SHADE_(\w+)\s+(\d+)", mesh_src)}
    if len(rows) != d or any(len(r) != w for r in rows):
        raise Bad("room_map.c grid is not %dx%d" % (w, d))
    return rows, w, d, cell, wall, shade


def reference_room_body(rows, w, d, cell, wall):
    # start cell (6, 6): the main app's start (RESOURCE_PIPELINE.md 5.8)
    return struct.pack("<HHHHff", w, d, 6, 6, cell, wall) + bytes(1 if c == "#" else 0 for r in rows for c in r)


def derive_room_mesh(rows, w, d, s, hgt, shade):
    """level_mesh_build() re-derived: per material (wall, floor, ceiling), cells z then x; every quad
    (a, b, c, d) gives triangles (a, b, c) and (a, c, d); UVs one repeat per cell."""
    def solid(x, z):
        return not (0 <= x < w and 0 <= z < d) or rows[z][x] == "#"

    vt = hgt / s
    verts, idx, ranges, owners = [], [], [], []
    # Wall faces: neighbour offset, shade name, corners as (x key, y, z key, u key, v) where keys
    # pick from the cell's x0/x1, z0/z1, u0/u1 (= cx, cx+1) and w0/w1 (= cz, cz+1).
    walls = [((-1, 0), "POS_X", [("x0", 0, "z1", "w0", vt), ("x0", 0, "z0", "w1", vt),
                                  ("x0", hgt, "z0", "w1", 0), ("x0", hgt, "z1", "w0", 0)]),
             ((1, 0), "NEG_X", [("x1", 0, "z0", "w0", vt), ("x1", 0, "z1", "w1", vt),
                                 ("x1", hgt, "z1", "w1", 0), ("x1", hgt, "z0", "w0", 0)]),
             ((0, -1), "POS_Z", [("x0", 0, "z0", "u0", vt), ("x1", 0, "z0", "u1", vt),
                                  ("x1", hgt, "z0", "u1", 0), ("x0", hgt, "z0", "u0", 0)]),
             ((0, 1), "NEG_Z", [("x1", 0, "z1", "u0", vt), ("x0", 0, "z1", "u1", vt),
                                 ("x0", hgt, "z1", "u1", 0), ("x1", hgt, "z1", "u0", 0)])]
    floor = ("FLOOR", [("x0", 0, "z0", "u0", "w0"), ("x0", 0, "z1", "u0", "w1"),
                       ("x1", 0, "z1", "u1", "w1"), ("x1", 0, "z0", "u1", "w0")])
    ceiling = ("CEILING", [("x0", hgt, "z0", "u0", "w0"), ("x1", hgt, "z0", "u1", "w0"),
                           ("x1", hgt, "z1", "u1", "w1"), ("x0", hgt, "z1", "u0", "w1")])
    for material in ("wall", "floor", "ceiling"):
        start = len(idx)
        for cz in range(d):
            for cx in range(w):
                if solid(cx, cz):
                    continue
                k = {"x0": cx * s, "x1": cx * s + s, "z0": cz * s, "z1": cz * s + s,
                     "u0": float(cx), "u1": float(cx + 1), "w0": float(cz), "w1": float(cz + 1)}
                if material == "wall":
                    faces = [(sh, cs) for (dx, dz), sh, cs in walls if solid(cx + dx, cz + dz)]
                else:
                    faces = [floor if material == "floor" else ceiling]
                for sh, corners in faces:
                    base = len(verts)
                    for xk, y, zk, uk, v in corners:
                        verts.append(((k[xk], float(y), k[zk], k[uk], k[v] if isinstance(v, str) else float(v)),
                                      (shade[sh],) * 3 + (255,)))
                    idx += [base, base + 1, base + 2, base, base + 2, base + 3]
                    owners += [(cx, cz)] * 2
        ranges.append((start, len(idx) - start))
    return verts, idx, ranges, owners


def faces_inward(verts, idx, owners, s, hgt, rows):
    """Each triangle's normal (b-a) x (c-a) points into the open cell it belongs to."""
    for t in range(len(idx) // 3):
        a, b, c = (verts[idx[3 * t + k]][0][:3] for k in range(3))
        u = [b[i] - a[i] for i in range(3)]
        v = [c[i] - a[i] for i in range(3)]
        n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        cx, cz = owners[t]
        centre = ((cx + 0.5) * s, hgt / 2, (cz + 0.5) * s)
        if sum(n[i] * (centre[i] - a[i]) for i in range(3)) <= 0 or rows[cz][cx] != ".":
            return False
    return True


# ------------------------------------------------------------------ the pipeline of section 6.1
def evaluate(data, room_body):
    """Returns a dict: code 0 and the pack's facts, or the first failure."""
    try:
        return run_pack(data, room_body)
    except Fail as f:
        return {"code": f.code, "name": f.name, "stage": f.stage, "entry": f.entry, "detail": f.detail}


def run_pack(data, room_body):
    regions = gdat_open(data)                                                    # A1 (GDAT Proto)
    n = len(regions)
    need(2 <= n <= 256, "RS_ERR_LIMIT", "open", -1, "entry count %d" % n)       # A1 (ResPipe)
    r0 = regions[0]                                                              # A2
    usize0, csize0 = wrapper(r0, "open", 0)
    raw0 = inflate_exact(r0[0x90:0x90 + csize0], usize0, "open", 0)
    need(le32(r0, 0) == 0 and crc(r0[4:0x80]) == ZERO124_CRC, "RS_ERR_RESERVED", "open", 0, "wrapper header")
    field = cfp1_field(raw0, 0)                                                   # A3
    need(field is None, "CF_ERR_PAYLOAD", "open", 0, "CFP1 " + str(field))
    need(le32(raw0, 4) == 2, "RS_ERR_ENVELOPE", "open", 0, "CFP1 kind")
    toc_crc = le32(raw0, 20)
    rows = toc_check(raw0[32:], n, "open", 0)                                     # A4
    for i in range(1, n):                                                        # A5
        u = rows[i - 1]["size"] + 32
        need(len(regions[i]) <= LIMIT8 and len(regions[i]) - 0x90 <= u + (u >> 10) + 64,
             "RS_ERR_LIMIT", "open", i, "region or compressed size")
    big_r = max(len(r) for r in regions)                                         # A6, A7: slot sizes
    big_u = max([len(raw0)] + [r["size"] + 32 for r in rows])
    tex_ids = {r["id"] for r in rows if r["kind"] == 2}
    entries = []
    for i, reg in enumerate(regions):                                            # B and C, in order
        need(len(reg) <= big_r, "RP_ERR_LIMIT", "entry", i, "region over slot")
        usize, csize = wrapper(reg, "entry", i)
        need(usize <= big_u, "RP_ERR_LIMIT", "entry", i, "raw size over slot")
        raw = inflate_exact(reg[0x90:0x90 + csize], usize, "entry", i)
        field = cfp1_field(raw, i)
        need(field is None, "CF_ERR_PAYLOAD", "entry", i, "CFP1 " + str(field))
        if i == 0:
            need(le32(raw, 20) == toc_crc, "RS_ERR_TOC", "entry", 0, "CRC differs from the open")
            entries.append({"region": len(reg), "raw": raw})
            continue
        row = rows[i - 1]
        need(all_zero(reg, 0, 0x80), "RS_ERR_RESERVED", "entry", i, "wrapper header")             # C.3.1
        need(le32(raw, 4) == 2, "RS_ERR_ENVELOPE", "entry", i, "CFP1 kind")                         # C.3.2
        need(usize == row["size"] + 32 and le32(raw, 20) == row["crc"], "RS_ERR_TOC", "entry", i,
             "envelope size or CRC vs table of contents")                                          # C.3.3
        payload = raw[32:]
        kind, rid, body = common_header(payload, "entry", i)                                        # C.3.4
        need(rid == row["id"] and kind == row["kind"], "RS_ERR_TOC", "entry", i, "id or kind")     # C.3.5
        facts = {2: texture_check, 3: mesh_check, 4: level_check}[kind](body, "entry", i)           # C.3.6
        need(facts["format"] == row["format"] and facts["vram"] == row["vram"] and facts["heap"] == row["heap"],
             "RS_ERR_TOC", "entry", i, "format, vram or heap")                                      # C.3.7
        need(all(t in tex_ids for t in facts["refs"]), "RS_ERR_REF", "entry", i, "texture id")    # C.3.8
        if kind == 4 and room_body is not None:
            need(body == room_body, "RS_ERR_LEVEL_MISMATCH", "entry", i, "not the reference room")  # C.3.9
        facts.update({"region": len(reg), "raw": raw, "payload": payload, "kind": kind, "id": rid, "row": row})
        entries.append(facts)
    total_heap = sum(r["heap"] for r in rows)
    heap_need = 16 * n + len(r0) + len(raw0) + SLOTS * (big_r + big_u) + ARENA + 64 * n + total_heap
    return {"code": 0, "name": "OK", "stage": "", "entry": -1, "detail": "", "n": n, "rows": rows,
            "entries": entries, "max_region": big_r, "max_inflated": big_u, "heap_need": heap_need,
            "vram": sum(r["vram"] for r in rows), "heap": total_heap, "toc_region": len(r0),
            "toc_inflated": len(raw0)}


# ------------------------------------------------------------------ texture decoding
def ex(v, bits):
    v <<= 8 - bits
    return v | (v >> bits)


def rgb565(v):
    return (ex(v >> 11, 5), ex((v >> 5) & 63, 6), ex(v & 31, 5))


ETC_MODS = [(2, 8), (5, 17), (9, 29), (13, 42), (18, 60), (24, 80), (33, 106), (47, 183)]


def etc1_texels(blk):
    hi, lo = int.from_bytes(blk[:4], "big"), int.from_bytes(blk[4:], "big")
    flip, diff = hi & 1, (hi >> 1) & 1
    tables = ((hi >> 5) & 7, (hi >> 2) & 7)
    if diff:
        def sign3(v):
            return v - 8 if v & 4 else v
        b1 = [(hi >> sh) & 31 for sh in (27, 19, 11)]
        b2 = [b1[k] + sign3((hi >> sh) & 7) for k, sh in enumerate((24, 16, 8))]
        if not all(0 <= v <= 31 for v in b2):
            raise Bad("ETC1 differential colour out of range")
        bases = ([ex(v, 5) for v in b1], [ex(v, 5) for v in b2])
    else:
        bases = ([((hi >> sh) & 15) * 17 for sh in (28, 20, 12)], [((hi >> sh) & 15) * 17 for sh in (24, 16, 8)])
    out = {}
    for x in range(4):
        for y in range(4):
            sub = (y >= 2) if flip else (x >= 2)
            k = 4 * x + y
            sel = (((lo >> (16 + k)) & 1) << 1) | ((lo >> k) & 1)
            a, b = ETC_MODS[tables[sub]]
            m = (a, b, -a, -b)[sel]
            out[(x, y)] = tuple(min(255, max(0, c + m)) for c in bases[sub]) + (255,)
    return out


def dxt_texels(blk, four_colour_always):
    c0, c1, bits = le16(blk, 0), le16(blk, 2), le32(blk, 4)
    p0, p1 = rgb565(c0), rgb565(c1)
    if c0 > c1 or four_colour_always:
        pal = [p0 + (255,), p1 + (255,), tuple((2 * a + b) // 3 for a, b in zip(p0, p1)) + (255,),
               tuple((a + 2 * b) // 3 for a, b in zip(p0, p1)) + (255,)]
    else:
        pal = [p0 + (255,), p1 + (255,), tuple((a + b) // 2 for a, b in zip(p0, p1)) + (255,), (0, 0, 0, 0)]
    return {(x, y): pal[(bits >> (2 * (4 * y + x))) & 3] for y in range(4) for x in range(4)}


def dxt5_alpha(blk):
    a0, a1 = blk[0], blk[1]
    bits = int.from_bytes(blk[2:8], "little")
    if a0 > a1:
        pal = [a0, a1] + [((7 - k) * a0 + k * a1) // 7 for k in range(1, 7)]
    else:
        pal = [a0, a1] + [((5 - k) * a0 + k * a1) // 5 for k in range(1, 5)] + [0, 255]
    return {(x, y): pal[(bits >> (3 * (4 * y + x))) & 7] for y in range(4) for x in range(4)}


def pvrtc_uniform(data):
    """PVRTC4 restricted to identical blocks: both colours opaque, standard modulation, modulation
    data 0, colours A and B equal after expansion. Returns the colour, which is then exact."""
    blk = data[:8]
    if any(data[k:k + 8] != blk for k in range(0, len(data), 8)):
        raise Bad("PVRTC4 level is not made of identical blocks")
    mod, col = le32(blk, 0), le32(blk, 4)
    if mod != 0 or col & 1 or not col & 0x8000 or not col & 0x80000000:
        raise Bad("PVRTC4 block is not opaque, standard mode, modulation 0")
    b4 = (col >> 1) & 15
    a = (ex((col >> 10) & 31, 5), ex((col >> 5) & 31, 5), ex((b4 << 1) | (b4 >> 3), 5))
    b = (ex((col >> 26) & 31, 5), ex((col >> 21) & 31, 5), ex((col >> 16) & 31, 5))
    if a != b:
        raise Bad("PVRTC4 colours A and B differ")
    return a + (255,)


def decode(fmt, w, h, data):
    """Returns {(x, y): (r, g, b, a)} for every texel of the level."""
    if fmt == 1:
        return {(x, y): tuple(data[4 * (y * w + x):4 * (y * w + x) + 4]) for y in range(h) for x in range(w)}
    if fmt == 2:
        return {(x, y): rgb565(le16(data, 2 * (y * w + x))) + (255,) for y in range(h) for x in range(w)}
    if fmt == 6:
        c = pvrtc_uniform(data)
        return {(x, y): c for y in range(h) for x in range(w)}
    bw, size = -(-w // 4), (16 if fmt == 4 else 8)
    out = {}
    for k in range(len(data) // size):
        blk = data[size * k:size * k + size]
        bx, by = k % bw, k // bw
        if fmt == 3:
            tex = dxt_texels(blk, False)
        elif fmt == 4:
            alpha = dxt5_alpha(blk)
            tex = {p: c[:3] + (alpha[p],) for p, c in dxt_texels(blk[8:], True).items()}
        else:
            tex = etc1_texels(blk)
        for (x, y), c in tex.items():
            if 4 * bx + x < w and 4 * by + y < h:
                out[(4 * bx + x, 4 * by + y)] = c
    return out


def box_filter(texels, w, h):
    nw, nh = max(1, w // 2), max(1, h // 2)
    out = {}
    for y in range(nh):
        for x in range(nw):
            px = [texels[(min(2 * x + i, w - 1), min(2 * y + j, h - 1))] for j in (0, 1) for i in (0, 1)]
            out[(x, y)] = tuple((sum(p[c] for p in px) + 2) // 4 for c in range(4))
    return out


# ------------------------------------------------------------------ the document
def read_doc():
    doc = open(DOC, encoding="utf-8").read()
    hexes = {}
    for name, body in re.findall(r"<!-- hex:(\w+) -->\s*```text\n(.*?)```", doc, re.S):
        out = bytearray()
        for line in body.splitlines():
            parts = line.split()
            if parts:
                if int(parts[0], 16) != len(out):
                    raise Bad("hex block %s: bad offset %s" % (name, parts[0]))
                out += bytes(int(x, 16) for x in parts[1:])
        hexes[name] = bytes(out)
    table = {m[0]: (m[1], int(m[2])) for m in re.findall(r"^\| ([ICPW]-\d\d) \|(.*)\((\d+)\) \|$", doc, re.M)}
    return hexes, table


def parse_header(path):
    text = open(path, encoding="utf-8").read()
    defs = {k: int(v) for k, v in re.findall(r"#define (RS_FIX_\w+) (\d+)", text)}
    rx = r'\{"([^"]+)", "([^"]+)", (\d+)u, 0x([0-9a-f]{8})u, (\d+)u, (\d+)u, (\d+)u, (-?\d+), (-?\d+)\},'

    def table(name):
        block = re.search(r"%s\[[A-Z_]+\] = \{\n(.*?)\n\};" % name, text, re.S).group(1)
        return [(m[0], m[1], int(m[2]), int(m[3], 16), int(m[4]), int(m[5]), int(m[6]), int(m[7]), int(m[8]))
                for m in re.findall(rx, block)]

    colours = re.search(r"RS_FIX_LEVEL_RGBA\[6\]\[8\]\[2\] = \{\n(.*?)\n\};", text, re.S).group(1)
    vals = [int(v, 16) for v in re.findall(r"0x([0-9a-f]{8})u", colours)]
    return defs, table("RS_FIX_PACKS"), table("RS_FIX_CASES"), vals


# ------------------------------------------------------------------ the full check
def check(fixture_dir):
    problems = []
    stats = {"textures decoded": 0, "levels decoded": 0, "texels decoded": 0, "box-filter levels": 0,
             "meshes compared": 0, "vertices compared": 0, "hex dumps compared": 0}

    def expect(cond, msg):
        if not cond:
            problems.append(msg)

    if crc(b"123456789") != 0xCBF43926 or crc(bytes(124)) != ZERO124_CRC:
        raise Bad("CRC-32 self-test failed")
    hexes, table = read_doc()
    rows_src, rw, rd, cell, wall, shade = read_room_sources()
    room_body = reference_room_body(rows_src, rw, rd, cell, wall)
    man = json.load(open(os.path.join(fixture_dir, "expected.json"), encoding="utf-8"))
    defs, h_packs, h_cases, h_colours = parse_header(os.path.join(fixture_dir, "rs_fixtures.h"))

    # files present: exactly the manifest's
    listed = {p["name"] for p in man["packs"]} | {c["path"] for c in man["cases"]} | {"rs_fixtures.h",
                                                                                         "expected.json"}
    present = set()
    for dirpath, _dirs, files in os.walk(fixture_dir):
        for f in files:
            present.add(os.path.relpath(os.path.join(dirpath, f), fixture_dir).replace(os.sep, "/"))
    expect(present == listed, "files differ from the manifest: extra %s, missing %s"
           % (sorted(present - listed), sorted(listed - present)))
    expect(sorted(p["name"] for p in man["packs"]) == sorted(VALID_PACKS), "pack list differs from the plan")

    # header defines and counts
    expect(defs.get("RS_FIX_GENERATOR_VERSION") == man["version"], "generator version differs")
    expect(defs.get("RS_FIX_SLOTS") == SLOTS == man["slots"], "slot count differs")
    expect(defs.get("RS_FIX_PACK_COUNT") == len(h_packs) == len(man["packs"]), "pack count differs")
    expect(defs.get("RS_FIX_CASE_COUNT") == len(h_cases) == len(man["cases"]) == len(table) == 50,
           "case count differs")

    results = {}
    files = {}

    def load(rel):
        if rel not in files:
            files[rel] = open(os.path.join(fixture_dir, rel), "rb").read()
        return files[rel]

    def facts(data):
        return {"file_size": len(data), "file_crc": crc(data), "sha256": hashlib.sha256(data).hexdigest()}

    # ---------------- valid packs
    hdr_packs = {p[1]: p for p in h_packs}
    for p in man["packs"]:
        name = p["name"]
        data = load(name)
        r = evaluate(data, room_body)
        results[name] = r
        want = 319 if name == "example.gdat" else 0
        expect(r["code"] == want == p["expect_code"], "%s: result %d %s (%s), expected %d" % (
            name, r["code"], r["name"], r.get("detail"), want))
        for k, v in facts(data).items():
            expect(p[k] == v, "%s: %s differs" % (name, k))
        hp = hdr_packs.get(name)
        expect(hp is not None and hp[2:4] == (len(data), crc(data)), "%s: rs_fixtures.h size or CRC differs" % name)
        if name == "example.gdat":
            expect(r["entry"] == 3 == p["expect_entry"] and r["stage"] == "entry", "example.gdat: wrong failing entry")
            r = run_pack_without_policy(data)
        if r["code"] != 0:
            continue
        expect(hp[4:9] == (r["n"], r["vram"], r["heap_need"], p["expect_code"], p["expect_entry"]),
               "%s: rs_fixtures.h values differ" % name)
        mine = {"entries": r["n"], "vram_bytes": r["vram"], "heap_bytes": r["heap"], "max_region": r["max_region"],
                "max_inflated": r["max_inflated"], "heap_need": r["heap_need"], "toc_region": r["toc_region"],
                "toc_inflated": r["toc_inflated"],
                "payload_bytes": sum(len(e["raw"]) - 32 for e in r["entries"]),
                "inflated_bytes": sum(len(e["raw"]) for e in r["entries"]),
                "stream_bytes": sum(e["region"] - 0x90 for e in r["entries"])}
        for k, v in mine.items():
            expect(p[k] == v, "%s: %s is %s, checker %s" % (name, k, p[k], v))
        res = [{"entry": i, "id": e["id"], "kind": e["kind"], "format": e["format"],
                "payload_size": len(e["payload"]), "payload_crc": crc(e["payload"]), "vram_bytes": e["vram"],
                "heap_bytes": e["heap"]} for i, e in enumerate(r["entries"]) if i]
        expect([{k: x[k] for k in res[0]} for x in p["resources"]] == res, "%s: resource list differs" % name)
        for pool, amount in (("vram", r["vram"]), ("heap", r["heap_need"]), ("kernel", 131072)):
            expect(FREE[pool] >= MARGIN[pool] + amount + -(-amount // 100),
                   "%s: %s need %d does not fit the measured free memory" % (name, pool, amount))
        expect(all(e["region"] <= LIMIT8 and len(e["raw"]) <= LIMIT8 for e in r["entries"]),
               "%s: an entry over 8 MiB" % name)
        payload_total = mine["payload_bytes"]
        if name == "area8.gdat":
            expect(payload_total <= 8 * MIB, "area8.gdat: payload over 8 MiB")
            expect({(e["format"], e["w"]) for e in r["entries"][1:] if e["kind"] == 2 and e["id"] >= 6}
                   == {(f, s) for f in range(1, 7) for s in (256, 512)}, "area8.gdat: filler set incomplete")
            expect(all(e["w"] <= 512 for e in r["entries"][1:] if e["kind"] == 2), "area8.gdat: texture over 512")

    # ---------------- the document's examples
    ex = run_pack_without_policy(load("example.gdat"))
    if ex["code"] == 0:
        e = ex["entries"]
        mine = {"toc": e[0]["raw"][32:], "texture": e[1]["payload"], "mesh": e[2]["payload"],
                "level": e[3]["payload"], "envelope0": e[0]["raw"][:32], "envelope1": e[1]["raw"][:32],
                "entry3": e[3]["raw"]}
        for k, v in mine.items():
            stats["hex dumps compared"] += 1
            expect(hexes.get(k) == v, "example.gdat: %s differs from the document's hex dump" % k)
    else:
        problems.append("example.gdat fails before the policy: %s" % ex["name"])
    expect(hexes.get("room", b"")[32:] == room_body, "document's room differs from room_map.c")
    for name in VALID_PACKS:
        r = results.get(name, {})
        if name != "example.gdat" and r.get("code") == 0:
            lv = [x for x in r["entries"][1:] if x["kind"] == 4]
            stats["hex dumps compared"] += 1
            expect(len(lv) == 1 and lv[0]["payload"] == hexes.get("room"), "%s: level is not section 9.7's" % name)

    # ---------------- the room mesh and textures
    d_verts, d_idx, d_ranges, owners = derive_room_mesh(rows_src, rw, rd, cell, wall, shade)
    expect(faces_inward(d_verts, d_idx, owners, cell, wall, rows_src), "derived mesh: a face points out of the room")
    planned = [[tuple(c) for c in lv] for lv in PLANNED]
    for name in VALID_PACKS[:8]:
        r = results.get(name, {})
        if r.get("code") != 0:
            continue
        by_id = {x["id"]: x for x in r["entries"][1:]}
        m = by_id.get(5)
        expect(m is not None and m["kind"] == 3, "%s: no mesh with id 5" % name)
        if m:
            stats["meshes compared"] += 1
            stats["vertices compared"] += len(m["verts"])
            expect([(v, c) for v, c in m["verts"]] == d_verts and m["indices"] == d_idx and
                   [(a, b) for a, b, _ in m["ranges"]] == d_ranges and [t for *_, t in m["ranges"]] == [1, 2, 4],
                   "%s: room mesh differs from level_mesh.c's rules" % name)
        for rid, material in ((1, "wall"), (2, "floor"), (4, "ceiling")):
            t = by_id.get(rid)
            if t is None or t["kind"] != 2:
                problems.append("%s: no %s texture" % (name, material))
                continue
            if name in ("room.gdat", "area8.gdat"):
                expect(t["format"] == 1 and t["w"] == t["h"] == 128 and len(t["levels"]) == 8,
                       "%s: %s is not RGBA8 128 with 8 levels" % (name, material))
                prev = None
                for lw, lh, data in t["levels"]:
                    tex = decode(1, lw, lh, data)
                    expect(all(c[3] == 255 for c in tex.values()), "%s: %s alpha" % (name, material))
                    if prev:
                        stats["box-filter levels"] += 1
                        expect(box_filter(*prev) == tex, "%s: %s level %dx%d is not the box filter" % (
                            name, material, lw, lh))
                    prev = (tex, lw, lh)
                continue
            fmt = FORMAT_NAMES.index(name[4:-5]) + 1
            table_c = h_colours[(fmt - 1) * 16:(fmt - 1) * 16 + 16]
            gen = material == "ceiling" and fmt in (1, 2)
            expect(t["format"] == fmt and t["w"] == t["h"] == man["diagnostic"]["size"] and
                   len(t["levels"]) == (1 if gen else 8) and t["flags"] == (7 if gen else 3),
                   "%s: %s texture shape or flags" % (name, material))
            sq = man["diagnostic"]["squares"][material]
            for lvn, (lw, lh, data) in enumerate(t["levels"]):
                tex = decode(fmt, lw, lh, data)
                want = [((v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255, v & 255)
                        for v in table_c[2 * lvn:2 * lvn + 2] if v]
                json_c = man["diagnostic"]["level_colours"][FORMAT_NAMES[fmt - 1]][lvn]
                expect([list(w_[:3]) for w_ in want] == json_c and all(w_[3] == 255 for w_ in want),
                       "%s: rs_fixtures.h and expected.json colours differ (level %d)" % (name, lvn))
                plan = planned[lvn][:1] if (fmt == 6 and lvn == 0) else planned[lvn]
                if fmt == 5:
                    ok_plan = all(len(want) == len(plan) and all(0 <= w_[k] - p[k] <= 2 for k in range(3))
                                  for w_, p in zip(want, plan))
                else:
                    ok_plan = [w_[:3] for w_ in want] == plan
                expect(ok_plan and len(want) == len(plan), "%s: level %d colours %s are not the planned %s" % (
                    name, lvn, want, plan))
                if lvn == 0 and len(want) == 2:
                    good = all(c == want[((x // sq) + (y // sq)) % 2] for (x, y), c in tex.items())
                else:
                    good = all(c == want[0] for c in tex.values())
                expect(good and len(tex) == lw * lh, "%s: %s level %d decodes wrongly" % (name, material, lvn))
                stats["levels decoded"] += 1
                stats["texels decoded"] += len(tex)
            stats["textures decoded"] += 1

    # ---------------- the cases
    hdr_cases = {c[0]: c for c in h_cases}
    for c in man["cases"]:
        cid = c["id"]
        data = load(c["path"])
        r = evaluate(data, room_body)
        results[cid] = r
        doc_text, doc_code = table.get(cid, ("", None))
        ok = r["code"] == doc_code == c["expect_code"] and r["entry"] == c["expect_entry"] and \
            r["stage"] == c["expect_stage"]
        if ok and r["name"] == "CF_ERR_PAYLOAD":
            ok = r["detail"][5:].lower() in doc_text.lower()
        expect(ok, "%s: checker %d %s (%s, entry %d, %s); document %s; manifest %d entry %d %s" % (
            cid, r["code"], r["name"], r["stage"], r["entry"], r["detail"], doc_code, c["expect_code"],
            c["expect_entry"], c["expect_stage"]))
        for k, v in facts(data).items():
            expect(c[k] == v, "%s: %s differs" % (cid, k))
        hc = hdr_cases.get(cid)
        expect(hc is not None and hc[1] == c["path"] and hc[2:5] == (len(data), crc(data), le32(data, 4)) and
               hc[7:9] == (c["expect_code"], c["expect_entry"]), "%s: rs_fixtures.h row differs" % cid)
    w09 = load("bad/W-09.gdat")
    reg = gdat_open(w09)[1]
    expect(be32(reg, 0x88) == len(reg) - 0x90 == 249 and zlib.decompress(reg[0x90:]) ==
           run_pack_without_policy(load("example.gdat"))["entries"][1]["raw"], "W-09: stream is not a valid 249 bytes")
    w08 = gdat_open(load("bad/W-08.gdat"))
    expect(len(w08[1]) == LIMIT8 + 1, "W-08: region is not 8,388,609 bytes")
    expect(le32(load("bad/W-11.gdat"), 4) == 257 and le32(load("bad/W-10.gdat"), 4) == 1, "W-10/W-11 entry counts")
    results["_stats"] = stats
    return problems, results


def run_pack_without_policy(data):
    """The same pipeline without the application policy (section 5.8): format checks only."""
    return evaluate(data, None)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fixture-dir", default=DEFAULT_DIR)
    ap.add_argument("--evaluate", metavar="FILE", help="print one pack's result (exit 0 accepted, 2 rejected)")
    args = ap.parse_args()
    if args.evaluate:
        rows, w, d, cell, wall, _ = read_room_sources()
        r = evaluate(open(args.evaluate, "rb").read(), reference_room_body(rows, w, d, cell, wall))
        print("RESULT %d %s stage=%s entry=%d %s" % (r["code"], r["name"], r["stage"] or "-", r["entry"], r["detail"]))
        return 0 if r["code"] == 0 else 2
    try:
        problems, results = check(args.fixture_dir)
    except (Bad, OSError, ValueError, KeyError, AttributeError) as e:
        print("CHECK FAILED: %s: %s" % (type(e).__name__, e))
        return 1
    for name in VALID_PACKS:
        r = results.get(name, {})
        print("  %-18s %s" % (name, "accepted" if r.get("code") == 0 else "%d %s (entry %d: %s)" % (
            r.get("code", -1), r.get("name"), r.get("entry", -1), r.get("detail"))))
    cases = sorted((k for k in results if k[1:2] == "-"), key=lambda c: ("IPCW".index(c[0]), c))
    for cid in cases:
        r = results[cid]
        print("  %s %3d %-22s stage %-5s entry %2d  %s" % (cid, r["code"], r["name"], r["stage"], r["entry"],
                                                             r["detail"]))
    print("  cases: %d checked, each against the document, the manifest and rs_fixtures.h" % len(cases))
    print("  " + ", ".join("%s %d" % kv for kv in results["_stats"].items()))
    for p in problems:
        print("DISAGREEMENT: " + p)
    print("%d disagreements" % len(problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
