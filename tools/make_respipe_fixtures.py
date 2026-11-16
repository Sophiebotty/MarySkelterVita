#!/usr/bin/env python3
"""Generates the synthetic fixtures for Vita ResPipe (Phase 10, stage 10.2, VDNG00006).

Everything here is synthetic and deterministic. No Mary Skelter file is read,
imitated or derived from. The layers are the ones approved in
RESOURCE_PIPELINE.md: GDAT archive -> BILZ wrapper -> zlib stream -> CFP1
envelope -> VRS1 resource (version 1).

Outputs (respipe/fixtures/ by default; git-ignored):
  room.gdat           the reference room with RGBA8 textures (accepted)
  fmt_<format>.gdat   the reference room with its three textures in one of the
                      six formats; mip levels in solid colours (accepted)
  area8.gdat          the room plus filler textures in all six formats,
                      at most 8 MiB of VRS1 payload in all (accepted)
  example.gdat        section 9's worked pack, unchanged: valid VRS1, refused
                      by ResPipe's room policy (case P-01)
  example_room.gdat   section 9's pack with the reference room as entry 3 (accepted)
  bad/<case>.gdat     section 10's other 49 cases, one change each
  rs_fixtures.h       expected values compiled into the app
  expected.json       the same values, for people and for the checker

The files are written to a fresh staging directory, checked there by the
independent reader tools/check_respipe_fixtures.py, and only then moved into
place. Only files this generator owns are replaced; nothing is deleted.
"""
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_OUT = os.path.join(ROOT, "respipe", "fixtures")
CHECKER = os.path.join(HERE, "check_respipe_fixtures.py")

GENERATOR_VERSION = 1
SEED = b"VitaResPipe/synthetic/1"
MIB = 1 << 20
MAX_U = 8 * MIB                     # largest inflated entry: CFP1 envelope + VRS1
MAX_P = MAX_U - 32                  # largest VRS1 payload
WRAP_HDR, WRAP_TOTAL = 0x80, 0x90
ZLEVEL = 9
SLOTS = 3                           # loading-screen ring slots (RESOURCE_PIPELINE.md 7.3)
ARENA = 65536                       # cf_zinflate arena
AREA_LIMIT = 8 * MIB                # area8: total VRS1 payload, table of contents included

KIND_TOC, KIND_TEX, KIND_MESH, KIND_LEVEL = 1, 2, 3, 4
RGBA8, RGB565, DXT1, DXT5, ETC1, PVRTC4 = 1, 2, 3, 4, 5, 6
FORMATS = [RGBA8, RGB565, DXT1, DXT5, ETC1, PVRTC4]
FORMAT_NAMES = {RGBA8: "rgba8", RGB565: "rgb565", DXT1: "dxt1", DXT5: "dxt5", ETC1: "etc1", PVRTC4: "pvrtc4"}
REPEAT, LINEAR, GENMIP = 1, 2, 4

# Diagnostic textures (fmt_*.gdat): level 0 is a white/black checkerboard (PVRTC4: solid
# white, because PVRTC blends each block with its neighbours); levels 1-7 are solid.
# Only 0 and 255 channel values are used, so RGBA8, RGB565, DXT1, DXT5 and PVRTC4 decode
# them exactly; ETC1 adds its +2 modifier to every channel (255 stays 255).
WHITE, BLACK = (255, 255, 255), (0, 0, 0)
TINTS = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0), (0, 255, 255), (255, 0, 255), (0, 0, 0)]
SQUARE = {"wall": 8, "floor": 16, "ceiling": 32}       # checker square size in texels
TEX_SIZE = 128                                          # room textures (measured size, M14)

# The reference room: src/gameplay/room_map.c, room_map.h and level_mesh.c (unmodified).
ROOM_ROWS = [
    "############",
    "#..........#",
    "#..........#",
    "#...##.....#",
    "#...##.....#",
    "#..........#",
    "#.......#..#",
    "#..........#",
    "#..........#",
    "#..#.......#",
    "#..........#",
    "############",
]
ROOM_W = ROOM_D = 12
ROOM_CELL, ROOM_WALL = 2.0, 2.5
ROOM_START = (6, 6)
SHADE_FLOOR, SHADE_CEILING = 235, 170
SHADE_POS_X, SHADE_NEG_X, SHADE_POS_Z, SHADE_NEG_Z = 220, 170, 195, 150
# Resource ids in the room packs. The level keeps id 3, so its payload is byte-identical
# to RESOURCE_PIPELINE.md section 9.7.
ID_WALL, ID_FLOOR, ID_LEVEL, ID_CEILING, ID_MESH = 1, 2, 3, 4, 5

CODE_NAMES = {0: "OK", 10: "RP_ERR_SIZE_MISMATCH", 11: "RP_ERR_BAD_MAGIC", 21: "RP_ERR_ZLIB_DATA",
              106: "GP_ERR_SIZE_RULE", 107: "GP_ERR_CONST", 211: "CF_ERR_PAYLOAD",
              300: "RS_ERR_MAGIC", 301: "RS_ERR_VERSION", 302: "RS_ERR_KIND", 303: "RS_ERR_HEADER_CRC",
              304: "RS_ERR_BODY_SIZE", 305: "RS_ERR_BODY_CRC", 306: "RS_ERR_RESERVED", 307: "RS_ERR_ENUM",
              308: "RS_ERR_RANGE", 309: "RS_ERR_ALIGN", 310: "RS_ERR_DIMENSION", 311: "RS_ERR_MIP_CHAIN",
              312: "RS_ERR_LEVEL_SIZE", 313: "RS_ERR_INDEX", 314: "RS_ERR_FLOAT", 315: "RS_ERR_TOC",
              316: "RS_ERR_DUP_ID", 317: "RS_ERR_REF", 318: "RS_ERR_LIMIT", 319: "RS_ERR_LEVEL_MISMATCH",
              320: "RS_ERR_ENVELOPE"}

# RESOURCE_PIPELINE.md section 10: case, expected code, GDAT entry that fails (-1: the
# archive itself), stage ("open" = section 6.1 A, "entry" = 6.1 B/C).
CASES = [
    ("I-01", 300, 1, "entry"), ("I-02", 301, 1, "entry"), ("I-03", 302, 1, "entry"),
    ("I-04", 303, 1, "entry"), ("I-05", 304, 1, "entry"), ("I-06", 305, 1, "entry"),
    ("I-07", 306, 1, "entry"), ("I-08", 307, 1, "entry"), ("I-09", 307, 1, "entry"),
    ("I-10", 310, 1, "entry"), ("I-11", 311, 1, "entry"), ("I-12", 309, 1, "entry"),
    ("I-13", 312, 1, "entry"), ("I-14", 306, 1, "entry"), ("I-15", 313, 2, "entry"),
    ("I-16", 314, 2, "entry"), ("I-17", 308, 2, "entry"), ("I-18", 307, 2, "entry"),
    ("I-19", 317, 2, "entry"), ("I-20", 307, 3, "entry"), ("I-21", 308, 3, "entry"),
    ("I-22", 315, 0, "open"), ("I-23", 315, 1, "entry"), ("I-24", 316, 0, "open"),
    ("I-25", 315, 0, "open"), ("I-26", 318, 0, "open"),
    ("P-01", 319, 3, "entry"),
    ("C-01", 211, 1, "entry"), ("C-02", 211, 1, "entry"), ("C-03", 211, 1, "entry"),
    ("C-04", 211, 1, "entry"), ("C-05", 211, 1, "entry"), ("C-06", 211, 1, "entry"),
    ("C-07", 211, 1, "entry"), ("C-08", 211, 1, "entry"), ("C-09", 211, 1, "entry"),
    ("C-10", 211, 1, "entry"), ("C-11", 320, 1, "entry"), ("C-12", 315, 1, "entry"),
    ("W-01", 306, 1, "entry"), ("W-02", 107, 1, "entry"), ("W-03", 106, 1, "entry"),
    ("W-04", 21, 1, "entry"), ("W-05", 11, 1, "entry"), ("W-06", 10, 1, "entry"),
    ("W-07", 10, 1, "entry"), ("W-08", 318, 1, "open"), ("W-09", 318, 1, "open"),
    ("W-10", 318, -1, "open"), ("W-11", 318, -1, "open"),
]


def crc(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def a16(n):
    return (n + 15) & ~15


def shake(label, n):
    """Deterministic pseudo-random bytes."""
    return hashlib.shake_256(SEED + b"/" + label.encode()).digest(n)


def xor_bytes(a, b):
    return (int.from_bytes(a, "little") ^ int.from_bytes(b, "little")).to_bytes(len(a), "little")


MASK3 = bytes(b & 3 for b in range(256))


# ---------------------------------------------------------------- VRS1 resources
def vrs1(kind, rid, body):
    head = struct.pack("<4sHHIII8x", b"VRS1", 1, kind, rid, len(body), crc(body))
    return head + struct.pack("<I", crc(head)) + body


def recrc(p, body=True):
    """Recomputes the VRS1 header CRC, and the body CRC unless body is False."""
    p = bytearray(p)
    if body:
        struct.pack_into("<I", p, 16, crc(bytes(p[32:])))
    struct.pack_into("<I", p, 28, crc(bytes(p[:28])))
    return bytes(p)


def put(p, off, fmt, val, fix="both"):
    """One field changed; fix = "both" (VRS1 CRCs updated), "header" or "none"."""
    p = bytearray(p)
    struct.pack_into(fmt, p, off, val)
    p = bytes(p)
    if fix == "both":
        return recrc(p, True)
    if fix == "header":
        return recrc(p, False)
    return p


def level_bytes(fmt, w, h):
    if fmt == RGBA8:
        return 4 * w * h
    if fmt == RGB565:
        return 2 * w * h
    if fmt in (DXT1, ETC1):
        return ((w + 3) // 4) * ((h + 3) // 4) * 8
    if fmt == DXT5:
        return ((w + 3) // 4) * ((h + 3) // 4) * 16
    return max(w, 8) * max(h, 8) // 2


def full_chain(w, h):
    return max(w, h).bit_length()


def level_dims(w, h, n):
    return [(max(1, w >> i), max(1, h >> i)) for i in range(n)]


def texture_body(fmt, w, h, flags, levels):
    n = len(levels)
    body = bytearray(struct.pack("<HHHHII", fmt, w, h, n, flags, 0)) + bytes(8 * n)
    table = []
    for (lw, lh), data in zip(level_dims(w, h, n), levels):
        assert len(data) == level_bytes(fmt, lw, lh)
        off = a16(len(body))
        body += bytes(off - len(body))
        table.append((off, len(data)))
        body += data
    for i, (off, size) in enumerate(table):
        struct.pack_into("<II", body, 16 + 8 * i, off, size)
    return bytes(body)


def texture_body_size(fmt, w, h, n):
    end = 16 + 8 * n
    for lw, lh in level_dims(w, h, n):
        end = a16(end) + level_bytes(fmt, lw, lh)
    return end


def texture_vram(fmt, w, h, n, flags):
    count = full_chain(w, h) if flags & GENMIP else n
    return sum(level_bytes(fmt, lw, lh) for lw, lh in level_dims(w, h, count))


def mesh_body(verts, idx, ranges):
    nr = len(ranges)
    vo = a16(32 + 16 * nr)
    io = a16(vo + 24 * len(verts))
    body = bytearray(struct.pack("<IIHHHHIIII", len(verts), len(idx), 1, 24, nr, 1, 32, vo, io, 0))
    for first, count, tex in ranges:
        body += struct.pack("<IIII", first, count, tex, 0)
    body += bytes(vo - len(body))
    for (x, y, z, u, v), shade in verts:
        body += struct.pack("<5f4B", x, y, z, u, v, shade, shade, shade, 255)
    body += bytes(io - len(body))
    body += struct.pack("<%dH" % len(idx), *idx)
    return bytes(body)


def toc_payload(rows, total_vram=None, total_heap=None):
    tv = sum(r[5] for r in rows) if total_vram is None else total_vram
    th = sum(r[6] for r in rows) if total_heap is None else total_heap
    body = struct.pack("<IIII", len(rows), tv, th, 0)
    body += b"".join(struct.pack("<IHHIIII8x", *r) for r in rows)
    return vrs1(KIND_TOC, 0, body)


class Res:
    """One resource and its table-of-contents row."""

    def __init__(self, rid, kind, fmt, payload, vram, heap, note):
        self.rid, self.kind, self.fmt, self.payload = rid, kind, fmt, payload
        self.vram, self.heap, self.note = vram, heap, note

    def row(self):
        return (self.rid, self.kind, self.fmt, len(self.payload), crc(self.payload), self.vram, self.heap)


def tex_res(rid, fmt, w, h, flags, levels, note):
    body = texture_body(fmt, w, h, flags, levels)
    return Res(rid, KIND_TEX, fmt, vrs1(KIND_TEX, rid, body), texture_vram(fmt, w, h, len(levels), flags), 0, note)


# ---------------------------------------------------------------- texel encoding
def q(v, bits):
    return (v * ((1 << bits) - 1) + 127) // 255


def expand(v, bits):
    v <<= 8 - bits
    return v | (v >> bits)


def to565(c):
    return (q(c[0], 5) << 11) | (q(c[1], 6) << 5) | q(c[2], 5)


def predicted(fmt, c):
    """The colour each format's decoder should produce for this generator's encoding of c."""
    if fmt == RGBA8:
        return tuple(c)
    if fmt in (RGB565, DXT1, DXT5):
        return (expand(q(c[0], 5), 5), expand(q(c[1], 6), 6), expand(q(c[2], 5), 5))
    if fmt == ETC1:                       # differential mode, codeword 0, every pixel index +2
        return tuple(min(255, expand(q(v, 5), 5) + 2) for v in c)
    b4 = q(c[2], 4)                       # PVRTC4 colour A: opaque RGB 5:5:4
    return (expand(q(c[0], 5), 5), expand(q(c[1], 5), 5), expand((b4 << 1) | (b4 >> 3), 5))


def solid_block(fmt, c):
    if fmt == DXT1:
        v = to565(c)
        return struct.pack("<HHI", v, v, 0)
    if fmt == DXT5:
        v = to565(c)
        return bytes([255, 255, 0, 0, 0, 0, 0, 0]) + struct.pack("<HHI", v, v, 0)
    if fmt == ETC1:
        return bytes([q(c[0], 5) << 3, q(c[1], 5) << 3, q(c[2], 5) << 3, 0x02, 0, 0, 0, 0])
    if fmt == PVRTC4:                     # colours A and B equal, both opaque; modulation 0
        a = 0x8000 | (q(c[0], 5) << 10) | (q(c[1], 5) << 5) | (q(c[2], 4) << 1)
        b = 0x8000 | (q(c[0], 5) << 10) | (q(c[1], 5) << 5) | q(c[2], 5)
        return struct.pack("<II", 0, (b << 16) | a)
    raise ValueError(fmt)


def encode_level(fmt, w, h, colour_at):
    """colour_at(x, y) -> (r, g, b). In the block formats every 4x4 block is one colour."""
    if fmt == RGBA8:
        return b"".join(bytes(colour_at(x, y)) + b"\xff" for y in range(h) for x in range(w))
    if fmt == RGB565:
        return b"".join(struct.pack("<H", to565(colour_at(x, y))) for y in range(h) for x in range(w))
    if fmt == PVRTC4:
        c = colour_at(0, 0)
        assert all(colour_at(x, y) == c for y in range(h) for x in range(w)), "PVRTC4 levels must be solid"
        return solid_block(fmt, c) * (max(w, 8) * max(h, 8) // 16)
    bw, bh = (w + 3) // 4, (h + 3) // 4
    return b"".join(solid_block(fmt, colour_at(4 * bx, 4 * by)) for by in range(bh) for bx in range(bw))


def checker(square):
    return lambda x, y: WHITE if ((x // square) + (y // square)) % 2 == 0 else BLACK


def diagnostic_texture(rid, fmt, material, generated):
    w = h = TEX_SIZE
    level0 = (lambda x, y: WHITE) if fmt == PVRTC4 else checker(SQUARE[material])
    if generated:
        return tex_res(rid, fmt, w, h, REPEAT | LINEAR | GENMIP, [encode_level(fmt, w, h, level0)],
                       "%s %s, level 0 only, generated mipmaps" % (material, FORMAT_NAMES[fmt]))
    dims = level_dims(w, h, full_chain(w, h))
    levels = [encode_level(fmt, w, h, level0)]
    for i, (lw, lh) in enumerate(dims[1:], 1):
        levels.append(encode_level(fmt, lw, lh, lambda x, y, c=TINTS[i - 1]: c))
    return tex_res(rid, fmt, w, h, REPEAT | LINEAR, levels,
                   "%s %s, %d stored levels" % (material, FORMAT_NAMES[fmt], len(levels)))


def level_colours(fmt):
    """Expected decoded colours per level of a stored-chain diagnostic texture: lists of RGB."""
    out = [[predicted(fmt, WHITE)] if fmt == PVRTC4 else [predicted(fmt, WHITE), predicted(fmt, BLACK)]]
    out += [[predicted(fmt, c)] for c in TINTS]
    return out


# ---------------------------------------------------------------- room content
def clamp(v):
    return 0 if v < 0 else 255 if v > 255 else v


def wall_rgb(x, y, tint):
    row = y // 16
    xs = (x + (16 if row & 1 else 0)) % TEX_SIZE
    if y % 16 < 2 or xs % 32 < 2:
        return (168, 164, 150)
    t = tint[(row * 4 + xs // 32) % len(tint)] % 24
    return (128 + t, 58 + t // 2, 44 + t // 3)


def floor_rgb(x, y, tint):
    if x % 32 < 2 or y % 32 < 2:
        return (64, 62, 58)
    return (118, 116, 110) if ((x // 32) + (y // 32)) % 2 == 0 else (98, 96, 92)


def ceiling_rgb(x, y, tint):
    if y % 16 == 0:
        return (62, 50, 38)
    g = ((x * 5 + (y // 16) * 37) // 9) % 7
    return (136 + 2 * g, 112 + 2 * g, 84 + g)


ROOM_PATTERNS = {"wall": wall_rgb, "floor": floor_rgb, "ceiling": ceiling_rgb}


def box_down(data, w, h):
    """2x2 box filter with rounding, RGBA8."""
    nw, nh = max(1, w // 2), max(1, h // 2)
    out = bytearray(4 * nw * nh)
    for y in range(nh):
        y0, y1 = min(2 * y, h - 1), min(2 * y + 1, h - 1)
        for x in range(nw):
            x0, x1 = min(2 * x, w - 1), min(2 * x + 1, w - 1)
            p = [4 * (y0 * w + x0), 4 * (y0 * w + x1), 4 * (y1 * w + x0), 4 * (y1 * w + x1)]
            o = 4 * (y * nw + x)
            for c in range(4):
                out[o + c] = (data[p[0] + c] + data[p[1] + c] + data[p[2] + c] + data[p[3] + c] + 2) // 4
    return bytes(out), nw, nh


def room_texture(rid, material):
    w = h = TEX_SIZE
    noise = shake("room/" + material, w * h)
    tint = shake("room/" + material + "/tint", 64)
    fn = ROOM_PATTERNS[material]
    px = bytearray(4 * w * h)
    for y in range(h):
        for x in range(w):
            r, g, b = fn(x, y, tint)
            n = noise[y * w + x] % 7 - 3
            o = 4 * (y * w + x)
            px[o], px[o + 1], px[o + 2], px[o + 3] = clamp(r + n), clamp(g + n), clamp(b + n), 255
    levels, lw, lh = [bytes(px)], w, h
    while lw > 1 or lh > 1:
        nxt, lw, lh = box_down(levels[-1], lw, lh)
        levels.append(nxt)
    return tex_res(rid, RGBA8, w, h, REPEAT | LINEAR, levels, "%s rgba8, box-filtered chain" % material)


def room_solid(cx, cz):
    if cx < 0 or cz < 0 or cx >= ROOM_W or cz >= ROOM_D:
        return True
    return ROOM_ROWS[cz][cx] == "#"


def room_level_body():
    return struct.pack("<HHHHff", ROOM_W, ROOM_D, ROOM_START[0], ROOM_START[1], ROOM_CELL, ROOM_WALL) + \
        bytes(1 if room_solid(x, z) else 0 for z in range(ROOM_D) for x in range(ROOM_W))


def room_mesh_res(rid, tex_wall, tex_floor, tex_ceiling):
    """A Python transcription of level_mesh_build() (src/graphics/level_mesh.c): materials in
    TEX_WALL, TEX_FLOOR, TEX_CEILING order; cells z outer, x inner; quads (a,b,c,d) become
    triangles (a,b,c) and (a,c,d). Every value is exact in binary32."""
    verts, idx, ranges = [], [], []

    def quad(a, b, c, d, shade):
        base = len(verts)
        verts.extend((p, shade) for p in (a, b, c, d))
        idx.extend([base, base + 1, base + 2, base, base + 2, base + 3])

    s, hgt = ROOM_CELL, ROOM_WALL
    vt = hgt / s
    for mat, tex in ((0, tex_wall), (1, tex_floor), (2, tex_ceiling)):
        first = len(idx)
        for cz in range(ROOM_D):
            for cx in range(ROOM_W):
                if room_solid(cx, cz):
                    continue
                x0, z0 = cx * s, cz * s
                x1, z1 = x0 + s, z0 + s
                u0, w0 = float(cx), float(cz)
                u1, w1 = u0 + 1.0, w0 + 1.0
                if mat == 1:
                    quad((x0, 0, z0, u0, w0), (x0, 0, z1, u0, w1), (x1, 0, z1, u1, w1), (x1, 0, z0, u1, w0), SHADE_FLOOR)
                elif mat == 2:
                    quad((x0, hgt, z0, u0, w0), (x1, hgt, z0, u1, w0), (x1, hgt, z1, u1, w1), (x0, hgt, z1, u0, w1),
                         SHADE_CEILING)
                else:
                    if room_solid(cx - 1, cz):
                        quad((x0, 0, z1, w0, vt), (x0, 0, z0, w1, vt), (x0, hgt, z0, w1, 0), (x0, hgt, z1, w0, 0),
                             SHADE_POS_X)
                    if room_solid(cx + 1, cz):
                        quad((x1, 0, z0, w0, vt), (x1, 0, z1, w1, vt), (x1, hgt, z1, w1, 0), (x1, hgt, z0, w0, 0),
                             SHADE_NEG_X)
                    if room_solid(cx, cz - 1):
                        quad((x0, 0, z0, u0, vt), (x1, 0, z0, u1, vt), (x1, hgt, z0, u1, 0), (x0, hgt, z0, u0, 0),
                             SHADE_POS_Z)
                    if room_solid(cx, cz + 1):
                        quad((x1, 0, z1, u0, vt), (x0, 0, z1, u1, vt), (x0, hgt, z1, u1, 0), (x1, hgt, z1, u0, 0),
                             SHADE_NEG_Z)
        ranges.append((first, len(idx) - first, tex))
    body = mesh_body(verts, idx, ranges)
    return Res(rid, KIND_MESH, 0, vrs1(KIND_MESH, rid, body), 24 * len(verts) + 2 * len(idx), 0,
               "reference room mesh: %d vertices, %d indices, ranges wall/floor/ceiling" % (len(verts), len(idx)))


def room_level_res():
    body = room_level_body()
    return Res(ID_LEVEL, KIND_LEVEL, 0, vrs1(KIND_LEVEL, ID_LEVEL, body), 0, len(body), "reference room level")


# ---------------------------------------------------------------- filler textures (area8)
def filler_level(fmt, w, h, label):
    ph = shake(label + "/phase", 4)
    if fmt in (RGBA8, RGB565):
        out = bytearray(4 * w * h)
        rcol = bytes(((x * 256) // w + ph[0]) & 255 for x in range(w))
        bpat = [bytes(((((x >> 3) ^ yb) * 9 + ph[2]) & 255) for x in range(w)) for yb in range((h + 7) // 8)]
        for y in range(h):
            o = 4 * w * y
            out[o:o + 4 * w:4] = rcol
            out[o + 1:o + 4 * w:4] = bytes([((y * 256) // h + ph[1]) & 255]) * w
            out[o + 2:o + 4 * w:4] = bpat[y >> 3]
            out[o + 3:o + 4 * w:4] = b"\xff" * w
        noise = bytearray(shake(label, 4 * w * h).translate(MASK3))
        noise[3::4] = bytes(w * h)
        rgba = xor_bytes(bytes(out), bytes(noise))
        if fmt == RGBA8:
            return rgba
        r, g, b = rgba[0::4], rgba[1::4], rgba[2::4]
        return struct.pack("<%dH" % (w * h), *[(q(r[i], 5) << 11) | (q(g[i], 6) << 5) | q(b[i], 5)
                                               for i in range(w * h)])
    if fmt == PVRTC4:
        bw, bh = max(w, 8) // 4, max(h, 8) // 4
    else:
        bw, bh = (w + 3) // 4, (h + 3) // 4
    rnd = shake(label, 8 * bw * bh)
    blocks = []
    for k in range(bw * bh):
        bx, by = k % bw, k // bw
        r8 = rnd[8 * k:8 * k + 8]
        c = (((bx * 256) // bw + ph[0]) & 255, ((by * 256) // bh + ph[1]) & 255, ph[2])
        hi = to565(c)
        lo = hi ^ 0x0841
        sel = struct.unpack_from("<I", r8, 4)[0]
        if fmt == DXT1:
            blocks.append(struct.pack("<HHI", max(hi, lo), min(hi, lo), sel))
        elif fmt == DXT5:
            blocks.append(bytes([255, r8[0]]) + r8[1:7] + struct.pack("<HHI", hi, lo, sel))
        elif fmt == ETC1:
            blocks.append(bytes([(q(c[0], 5) << 3) | (r8[0] & 7), (q(c[1], 5) << 3) | (r8[1] & 7),
                                 (q(c[2], 5) << 3) | (r8[2] & 7), r8[3]]) + r8[4:8])
        else:
            a = 0x8000 | (q(c[0], 5) << 10) | (q(c[1], 5) << 5) | (q(c[2], 4) << 1)
            b = 0x8000 | ((q(c[0], 5) ^ 3) << 10) | (q(c[1], 5) << 5) | q(c[2], 5)
            blocks.append(struct.pack("<II", sel, (b << 16) | a))
    return b"".join(blocks)


def filler_texture(rid, fmt, size):
    dims = level_dims(size, size, full_chain(size, size))
    levels = [filler_level(fmt, lw, lh, "area8/%d/%d" % (rid, i)) for i, (lw, lh) in enumerate(dims)]
    return tex_res(rid, fmt, size, size, REPEAT | LINEAR, levels,
                   "filler %s %dx%d, %d levels" % (FORMAT_NAMES[fmt], size, size, len(levels)))


# ---------------------------------------------------------------- transport
def envelope(payload, index, magic=b"CFP1", kind=2, width=0, height=0, length=None, body_crc=None, reserved=0):
    return struct.pack("<4sIIIIIII", magic, kind, width, height, len(payload) if length is None else length,
                       crc(payload) if body_crc is None else body_crc, index, reserved)


def region(raw, stream=None, magic=b"BILZ", usize=None, csize=None, const=7, header=None):
    z = zlib.compress(raw, ZLEVEL) if stream is None else stream
    head = bytes(WRAP_HDR) if header is None else header
    return head + magic + struct.pack(">III", len(raw) if usize is None else usize,
                                      len(z) if csize is None else csize, const) + z


def gdat(regions):
    off = 8 + 8 * len(regions)
    out = bytearray(b"GDAT" + struct.pack("<I", len(regions)))
    for r in regions:
        out += struct.pack("<II", off, len(r))
        off += len(r)
    return bytes(out) + b"".join(regions)


def raws_of(payloads):
    return [envelope(p, i) + p for i, p in enumerate(payloads)]


# ---------------------------------------------------------------- section 9 example (exact)
def example_parts():
    colours = [0xF800, 0x07E0, 0x001F, 0xFFFF]                 # red, green, blue, white (RGB565)
    levels = [struct.pack("<HHI", c, c, 0) * (max(1, (d + 3) // 4) ** 2) for d, c in zip((8, 4, 2, 1), colours)]
    t = vrs1(KIND_TEX, 1, texture_body(DXT1, 8, 8, REPEAT | LINEAR, levels))
    quad = [(0.0, 0.0, 0.0, 0.0, 0.0), (0.0, 0.0, 2.0, 0.0, 1.0), (2.0, 0.0, 2.0, 1.0, 1.0), (2.0, 0.0, 0.0, 1.0, 0.0)]
    m = vrs1(KIND_MESH, 2, mesh_body([(v, 235) for v in quad], [0, 1, 2, 0, 2, 3], [(0, 6, 1)]))
    lb = struct.pack("<HHHHff", 3, 3, 1, 1, 2.0, 2.5) + bytes([1, 1, 1, 1, 0, 1, 1, 1, 1])
    lv = vrs1(KIND_LEVEL, 3, lb)
    rows = [(1, KIND_TEX, DXT1, len(t), crc(t), 56, 0),
            (2, KIND_MESH, 0, len(m), crc(m), 24 * 4 + 2 * 6, 0),
            (3, KIND_LEVEL, 0, len(lv), crc(lv), 0, len(lb))]
    return toc_payload(rows), t, m, lv, rows


def stream_of_length(raw, target):
    """A valid zlib stream of raw that is exactly target bytes long (W-09)."""
    flushes = [zlib.Z_SYNC_FLUSH, zlib.Z_FULL_FLUSH]
    if hasattr(zlib, "Z_PARTIAL_FLUSH"):
        flushes.append(zlib.Z_PARTIAL_FLUSH)
    strategies = [zlib.Z_DEFAULT_STRATEGY, zlib.Z_FILTERED, zlib.Z_HUFFMAN_ONLY, zlib.Z_RLE, zlib.Z_FIXED]
    for level in range(10):
        for strategy in strategies:
            for flush in flushes:
                for chunks in range(1, len(raw) + 1):
                    step = -(-len(raw) // chunks)
                    c = zlib.compressobj(level, zlib.DEFLATED, 15, 8, strategy)
                    parts = []
                    for i in range(0, len(raw), step):
                        parts.append(c.compress(raw[i:i + step]))
                        parts.append(c.flush(flush))
                    parts.append(c.flush(zlib.Z_FINISH))
                    s = b"".join(parts)
                    if len(s) == target and zlib.decompress(s) == raw:
                        return s, "level %d, strategy %d, flush %d, %d chunks" % (level, strategy, flush, chunks)
    raise RuntimeError("no zlib stream of %d bytes found" % target)


def damaged_stream(raw):
    """W-04: the zlib stream with one byte inverted, at the position nearest the middle whose result
    is known: inflating it fails with a data error before more than len(raw) bytes come out."""
    z = zlib.compress(raw, ZLEVEL)
    mid = len(z) // 2
    for d in range(len(z)):
        for pos in sorted({mid + d, mid - d}):
            if not 2 <= pos < len(z) - 4:            # inside the deflate data: not the header or Adler-32
                continue
            bad = bytearray(z)
            bad[pos] ^= 0xFF
            bad = bytes(bad)
            try:
                zlib.decompressobj().decompress(bad)
                continue
            except zlib.error:
                pass
            try:
                zlib.decompressobj().decompress(bad, len(raw))
                continue
            except zlib.error:
                return bad, pos
    raise RuntimeError("no damage position with a known result")


def bad_packs():
    """RESOURCE_PIPELINE.md section 10: each case starts from the section 9 pack and makes one change.
    I-cases: the CFP1 envelope and the table-of-contents row always describe the changed payload
    (size and CRC), so the targeted VRS1 check is reached; "CRCs updated" / "no CRC update" in the
    document refer to the VRS1 header and body CRCs."""
    c, t, m, lv, rows = example_parts()
    pays = [c, t, m, lv]
    base_raws = raws_of(pays)
    base_regions = [region(r) for r in base_raws]

    def with_resource(which, newp):
        r2 = list(rows)
        r = list(r2[which - 1])
        r[3], r[4] = len(newp), crc(newp)
        r2[which - 1] = tuple(r)
        p2 = list(pays)
        p2[0], p2[which] = toc_payload(r2), newp
        return gdat([region(x) for x in raws_of(p2)])

    def with_toc(newc, extra_raws=()):
        return gdat([region(x) for x in raws_of([newc, t, m, lv]) + list(extra_raws)])

    def toc_row(row, field, val):
        r2 = [list(r) for r in rows]
        r2[row][field] = val
        return toc_payload([tuple(r) for r in r2])

    def with_raw1(raw):
        return gdat([base_regions[0], region(raw)] + base_regions[2:])

    def with_region1(reg):
        return gdat([base_regions[0], reg] + base_regions[2:])

    u32 = lambda b, o: struct.unpack_from("<I", b, o)[0]
    nan = struct.unpack("<f", struct.pack("<I", 0x7FC00000))[0]
    t_green = recrc(t[:0x50] + struct.pack("<HHI", 0x07E0, 0x07E0, 0) * 4 + t[0x70:])
    l4 = vrs1(KIND_LEVEL, 3, struct.pack("<HHHHff", 4, 4, 1, 1, 2.0, 2.5) +
              bytes([1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1]))
    raw1 = base_raws[1]
    notes = {}
    out = {
        "I-01": with_resource(1, b"VRS2" + t[4:]),
        "I-02": with_resource(1, put(t, 4, "<H", 2, "header")),
        "I-03": with_resource(1, put(t, 6, "<H", 9, "header")),
        "I-04": with_resource(1, put(t, 28, "<I", (u32(t, 28) + 1) & 0xFFFFFFFF, "none")),
        "I-05": with_resource(1, put(t, 12, "<I", 121, "header")),
        "I-06": with_resource(1, put(t, 0x50, "<B", 1, "none")),
        "I-07": with_resource(1, put(t, 20, "<B", 1, "header")),
        "I-08": with_resource(1, put(t, 32 + 0, "<H", 7)),
        "I-09": with_resource(1, put(t, 32 + 8, "<I", 7)),
        "I-10": with_resource(1, put(t, 32 + 2, "<H", 6)),
        "I-11": with_resource(1, put(t, 32 + 6, "<H", 3)),
        "I-12": with_resource(1, put(t, 32 + 16 + 8, "<I", 84)),
        "I-13": with_resource(1, put(t, 32 + 16 + 12, "<I", 16)),
        "I-14": with_resource(1, put(t, 32 + 88, "<B", 0xFF)),
        "I-15": with_resource(2, put(m, 32 + 144 + 10, "<H", 4)),
        "I-16": with_resource(2, put(m, 32 + 48, "<f", nan)),
        "I-17": with_resource(2, put(m, 32 + 32 + 4, "<I", 3)),
        "I-18": with_resource(2, put(m, 32 + 10, "<H", 20)),
        "I-19": with_resource(2, put(m, 32 + 32 + 8, "<I", 7)),
        "I-20": with_resource(3, put(lv, 32 + 16 + 4, "<B", 2)),
        "I-21": with_resource(3, put(lv, 32 + 4, "<H", 0)),
        "I-22": with_toc(c, [envelope(lv, 4) + lv]),
        "I-23": with_toc(toc_row(0, 3, 153)),
        "I-24": with_toc(toc_row(1, 0, 1)),
        "I-25": with_toc(toc_payload(rows, total_vram=165)),
        "I-26": with_toc(toc_row(0, 3, MAX_P + 1)),
        "C-01": with_raw1(envelope(t, 1, magic=b"CFP2") + t),
        "C-02": with_raw1(raw1[:31]),
        "C-03": with_raw1(envelope(t, 1, reserved=1) + t),
        "C-04": with_raw1(envelope(t, 2) + t),
        "C-05": with_raw1(envelope(t, 1, length=len(t) + 1) + t),
        "C-06": with_raw1(envelope(t, 1, length=len(t) - 1) + t),
        "C-07": with_raw1(envelope(t, 1, kind=3) + t),
        "C-08": with_raw1(envelope(t, 1, width=1) + t),
        "C-09": with_raw1(envelope(t, 1, body_crc=(crc(t) + 1) & 0xFFFFFFFF) + t),
        "C-10": with_raw1(envelope(t, 1) + put(t, 0x50, "<B", 1, "none")),
        "C-11": with_raw1(envelope(l4, 1, kind=1, width=4, height=4) + l4),
        "C-12": with_raw1(envelope(t_green, 1) + t_green),
        "W-01": with_region1(region(raw1, header=bytes(0x10) + b"\x01" + bytes(WRAP_HDR - 0x11))),
        "W-02": with_region1(region(raw1, const=6)),
        "W-05": with_region1(region(raw1, magic=b"BILY")),
        "W-06": with_region1(region(raw1, usize=len(raw1) + 1)),
        "W-07": with_region1(region(raw1, usize=len(raw1) - 1)),
        "W-10": gdat([base_regions[0]]),
        "W-11": gdat(base_regions + [region(envelope(lv, i) + lv) for i in range(4, 257)]),
    }
    reg1 = region(raw1)
    z1 = reg1[WRAP_TOTAL:]
    out["W-03"] = with_region1(region(raw1, stream=z1, csize=len(z1) - 1))      # = region - 0x91
    damaged, pos = damaged_stream(raw1)
    out["W-04"] = with_region1(region(raw1, stream=damaged))
    notes["W-04"] = "zlib stream byte %d of %d inverted" % (pos, len(damaged))
    out["W-08"] = with_region1(reg1 + bytes(8 * MIB + 1 - len(reg1)))
    notes["W-08"] = "entry 1's region padded with zero bytes to 8,388,609 bytes"
    u1 = len(raw1)
    limit = u1 + (u1 >> 10) + 64
    s249, how = stream_of_length(raw1, limit + 1)
    out["W-09"] = with_region1(region(raw1, stream=s249))
    notes["W-09"] = "a valid %d-byte zlib stream of the %d-byte entry (%s); limit %d" % (len(s249), u1, how, limit)
    notes["W-11"] = "entries 4-256 are copies of the level with their own CFP1 index"
    notes["I-22"] = "entry 4 is a copy of the level with CFP1 index 4"
    return out, notes


# ---------------------------------------------------------------- packs and summaries
def build_pack(resources):
    """Table of contents first, then the resources in the order given."""
    rows = [r.row() for r in resources]
    payloads = [toc_payload(rows)] + [r.payload for r in resources]
    raws = raws_of(payloads)
    regions = [region(x) for x in raws]
    return gdat(regions), payloads, raws, regions, rows


def file_facts(data):
    return {"file_size": len(data), "file_crc": crc(data), "sha256": hashlib.sha256(data).hexdigest()}


def pack_summary(name, data, payloads, raws, regions, rows, expect_code, expect_entry, resources, notes):
    n = len(regions)
    big_r = max(len(r) for r in regions)
    big_u = max(len(x) for x in raws)
    total_heap = sum(r[6] for r in rows)
    heap_need = 16 * n + len(regions[0]) + len(raws[0]) + SLOTS * (big_r + big_u) + ARENA + 64 * n + total_heap
    s = {"name": name, "entries": n, "payload_bytes": sum(len(p) for p in payloads),
         "inflated_bytes": sum(len(x) for x in raws), "stream_bytes": sum(len(r) - WRAP_TOTAL for r in regions),
         "vram_bytes": sum(r[5] for r in rows), "heap_bytes": total_heap, "toc_region": len(regions[0]),
         "toc_inflated": len(raws[0]), "max_region": big_r, "max_inflated": big_u, "heap_need": heap_need,
         "expect_code": expect_code, "expect_name": CODE_NAMES[expect_code], "expect_entry": expect_entry,
         "notes": notes,
         "resources": [{"entry": i + 1, "id": r.rid, "kind": r.kind, "format": r.fmt, "payload_size": len(r.payload),
                        "payload_crc": crc(r.payload), "vram_bytes": r.vram, "heap_bytes": r.heap, "note": r.note}
                       for i, r in enumerate(resources)]}
    s.update(file_facts(data))
    return s


def room_resources(textures):
    """textures: (wall, floor, ceiling) Res with ids 1, 2, 4. Entries in id order."""
    wall, floor, ceiling = textures
    return [wall, floor, room_level_res(), ceiling, room_mesh_res(ID_MESH, ID_WALL, ID_FLOOR, ID_CEILING)]


def area8_resources():
    res = room_resources((room_texture(ID_WALL, "wall"), room_texture(ID_FLOOR, "floor"),
                          room_texture(ID_CEILING, "ceiling")))
    cycle = [(f, 512) for f in FORMATS] + [(f, 256) for f in FORMATS]
    rid = 6
    while True:
        added = False
        for fmt, size in cycle:
            p = 32 + texture_body_size(fmt, size, size, full_chain(size, size))
            toc = 48 + 32 * (len(res) + 1)
            if toc + sum(len(r.payload) for r in res) + p <= AREA_LIMIT and len(res) < 255:
                res.append(filler_texture(rid, fmt, size))
                assert len(res[-1].payload) == p
                rid += 1
                added = True
        if not added:
            return res


def write_header(out_dir, packs, cases, colours):
    lines = [
        "/* Generated by tools/make_respipe_fixtures.py (version %d). Do not edit." % GENERATOR_VERSION,
        " * Expected values of the synthetic ResPipe fixtures (VRS1 version 1); no game data. */",
        "#ifndef RS_FIXTURES_H",
        "#define RS_FIXTURES_H",
        "",
        "#include <stdint.h>",
        "",
        "#define RS_FIX_GENERATOR_VERSION %d" % GENERATOR_VERSION,
        "#define RS_FIX_SLOTS %d" % SLOTS,
        "",
        "typedef struct {",
        "    const char *id;          /* pack or case name */",
        "    const char *path;        /* below app0:fixtures/ */",
        "    uint32_t file_size;",
        "    uint32_t file_crc;       /* CRC-32 of the whole file */",
        "    uint32_t entries;        /* GDAT entries, table of contents included */",
        "    uint32_t vram_bytes;     /* table of contents total (valid packs; 0 for cases) */",
        "    uint32_t heap_need;      /* RESOURCE_PIPELINE.md 8.2 with RS_FIX_SLOTS slots (valid packs) */",
        "    int32_t expect_code;     /* 0 = accepted */",
        "    int32_t expect_entry;    /* GDAT entry that fails; -1 = none, or the archive itself */",
        "} rs_fix_pack;",
        "",
        "#define RS_FIX_PACK_COUNT %d" % len(packs),
        "static const rs_fix_pack RS_FIX_PACKS[RS_FIX_PACK_COUNT] = {",
    ]
    for p in packs:
        lines.append('    {"%s", "%s", %du, 0x%08xu, %du, %du, %du, %d, %d},' % (
            p["name"][:-5], p["name"], p["file_size"], p["file_crc"], p["entries"], p["vram_bytes"],
            p["heap_need"], p["expect_code"], p["expect_entry"]))
    lines += ["};", "", "#define RS_FIX_CASE_COUNT %d" % len(cases),
              "static const rs_fix_pack RS_FIX_CASES[RS_FIX_CASE_COUNT] = {"]
    for c in cases:
        lines.append('    {"%s", "%s", %du, 0x%08xu, %du, 0u, 0u, %d, %d},' % (
            c["id"], c["path"], c["file_size"], c["file_crc"], c["entries"], c["expect_code"], c["expect_entry"]))
    lines += ["};", "",
              "/* Decoded colour of the diagnostic textures (fmt_*.gdat), 0xRRGGBBAA, per format",
              " * (RGBA8, RGB565, DXT1, DXT5, ETC1, PVRTC4) and stored level 0-7. Level 0 is a",
              " * checkerboard of the two colours (PVRTC4: one colour); 0 = no second colour. */",
              "static const uint32_t RS_FIX_LEVEL_RGBA[6][8][2] = {"]
    for fmt in FORMATS:
        cells = []
        for lv in colours[FORMAT_NAMES[fmt]]:
            vals = ["0x%02x%02x%02xffu" % tuple(cl) for cl in lv] + ["0x00000000u"] * (2 - len(lv))
            cells.append("{%s}" % ", ".join(vals))
        lines.append("    { /* %s */ %s }," % (FORMAT_NAMES[fmt], ", ".join(cells)))
    lines += ["};", "", "#endif /* RS_FIXTURES_H */", ""]
    with open(os.path.join(out_dir, "rs_fixtures.h"), "w", newline="\n") as f:
        f.write("\n".join(lines))


def _generate(out_dir):
    os.makedirs(os.path.join(out_dir, "bad"))
    owned, packs = [], []

    def save(rel, data):
        with open(os.path.join(out_dir, rel), "wb") as f:
            f.write(data)
        owned.append(rel)

    def add_pack(name, resources, expect_code=0, expect_entry=-1, notes=""):
        data, payloads, raws, regions, rows = build_pack(resources)
        save(name, data)
        s = pack_summary(name, data, payloads, raws, regions, rows, expect_code, expect_entry, resources, notes)
        packs.append(s)
        print("%-18s %3d entries, %9d payload bytes, %9d bytes on disk, VRAM %9d, heap need %9d" % (
            name, s["entries"], s["payload_bytes"], s["file_size"], s["vram_bytes"], s["heap_need"]))

    add_pack("room.gdat", room_resources((room_texture(ID_WALL, "wall"), room_texture(ID_FLOOR, "floor"),
                                          room_texture(ID_CEILING, "ceiling"))),
             notes="reference room, RGBA8 128x128 textures with box-filtered chains")
    for fmt in FORMATS:
        gen = fmt in (RGBA8, RGB565)
        add_pack("fmt_%s.gdat" % FORMAT_NAMES[fmt], room_resources((
            diagnostic_texture(ID_WALL, fmt, "wall", False), diagnostic_texture(ID_FLOOR, fmt, "floor", False),
            diagnostic_texture(ID_CEILING, fmt, "ceiling", gen))),
            notes="reference room, %s diagnostic textures%s" % (
                FORMAT_NAMES[fmt], "; ceiling uses generated mipmaps" if gen else ""))
    add_pack("area8.gdat", area8_resources(), notes="reference room plus filler textures (256 and 512 squared)")
    c, t, m, lv, rows = example_parts()
    ex_res = [Res(1, KIND_TEX, DXT1, t, 56, 0, "section 9.2 texture"),
              Res(2, KIND_MESH, 0, m, 108, 0, "section 9.3 mesh"),
              Res(3, KIND_LEVEL, 0, lv, 0, 25, "section 9.5 level (3x3)")]
    add_pack("example.gdat", ex_res, 319, 3, "section 9 pack: valid VRS1, refused by the room policy (P-01)")
    room_lv = room_level_res()
    add_pack("example_room.gdat", ex_res[:2] + [room_lv], notes="section 9 pack with the section 9.7 room as entry 3")

    bad, notes = bad_packs()
    cases = []
    for cid, code, entry, stage in CASES:
        if cid == "P-01":
            rel = "example.gdat"
            data = open(os.path.join(out_dir, rel), "rb").read()
        else:
            rel = "bad/%s.gdat" % cid
            data = bad.pop(cid)
            save(rel, data)
        case = {"id": cid, "path": rel, "expect_code": code, "expect_name": CODE_NAMES[code],
                "expect_entry": entry, "expect_stage": stage, "entries": struct.unpack_from("<I", data, 4)[0],
                "note": notes.get(cid, "")}
        case.update(file_facts(data))
        cases.append(case)
    assert not bad, sorted(bad)

    colours = {FORMAT_NAMES[f]: level_colours(f) for f in FORMATS}
    write_header(out_dir, packs, cases, colours)
    owned.append("rs_fixtures.h")
    doc = {"format": "respipe-fixtures", "version": GENERATOR_VERSION, "spec": "RESOURCE_PIPELINE.md, VRS1 version 1",
           "note": "Synthetic fixtures only. No game data.", "zlib_version": zlib.ZLIB_RUNTIME_VERSION,
           "zlib_level": ZLEVEL, "slots": SLOTS,
           "diagnostic": {"size": TEX_SIZE, "squares": SQUARE, "level_colours": colours,
                          "generated_mip_ceiling": [FORMAT_NAMES[RGBA8], FORMAT_NAMES[RGB565]]},
           "packs": packs, "cases": cases}
    with open(os.path.join(out_dir, "expected.json"), "w", newline="\n") as f:
        json.dump(doc, f, indent=1, sort_keys=True)
        f.write("\n")
    owned.append("expected.json")
    print("%d damaged packs (+ P-01 = example.gdat)" % (len(cases) - 1))
    return owned


def main(output_dir=DEFAULT_OUT):
    output_dir = os.path.abspath(output_dir)
    parent = os.path.dirname(output_dir)
    os.makedirs(parent, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".respipe-fixtures-stage-", dir=parent) as stage_root:
        stage = os.path.join(stage_root, "fixtures")
        owned = _generate(stage)
        sys.stdout.flush()
        subprocess.run([sys.executable, "-I", "-B", CHECKER, "--fixture-dir", stage], check=True)
        os.makedirs(os.path.join(output_dir, "bad"), exist_ok=True)
        for rel in owned:
            os.replace(os.path.join(stage, rel), os.path.join(output_dir, rel))
    print("fixtures written to %s" % output_dir)
    return 0


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output-dir", default=DEFAULT_OUT,
                        help="fixture directory to update (default: respipe/fixtures)")
    sys.exit(main(parser.parse_args().output_dir))
