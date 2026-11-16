#!/usr/bin/env python3
"""Generates the synthetic fixtures for Vita ContentFit (Stage 4, VDNG00005).

Everything here is synthetic and deterministic. No Mary Skelter file is read
and nothing here is derived from game data. Sizes come from the generic tiers
approved for Stage 4 (8 / 16 / 32 / 64 MiB area packs), not from Finale.

Outputs (contentfit/fixtures/ by default):
  io16.bin        16 MiB of incompressible bytes for the storage-read tests
  pack08.gdat     GDAT-style packs of 8, 16, 32 and 64 MiB of uncompressed
  pack16.gdat     payload, in the layout the hardware-tested GDAT Proto reader
  pack32.gdat     accepts in strict mode (see gdatproto/src/gp_gdat.h and
  pack64.gdat     gp_wrap.h); entry payloads are ContentFit's own "CFP1" format
  cf_fixtures.h   expected values compiled into the Vita app
  expected.json   the same values, for people and for the checker report

CFP1 payload (little-endian), the uncompressed content of every entry:
  0  "CFP1"   4  u32 kind (1 = RGBA8 texture, 2 = data)   8  u32 width
  12 u32 height   16 u32 body length   20 u32 CRC-32 of the body
  24 u32 entry index   28 u32 reserved (0)   32 body

The files are written to a fresh staging directory, checked there by the
independent reader tools/check_contentfit_fixtures.py, and only then moved
into place. Only files this generator owns are replaced.
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
DEFAULT_OUT = os.path.join(ROOT, "contentfit", "fixtures")
CHECKER = os.path.join(HERE, "check_contentfit_fixtures.py")

GENERATOR_VERSION = 1
SEED = b"VitaContentFit/synthetic/1"
MIB = 1 << 20
IO_NAME = "io16.bin"
IO_SIZE = 16 * MIB
PACKS = [("pack08.gdat", 8), ("pack16.gdat", 16), ("pack32.gdat", 32), ("pack64.gdat", 64)]
TEX_SHARE = 0.60                          # share of each pack's payload bytes that are textures
TEX_DIMS = [((256, 256), 40), ((512, 512), 45), ((1024, 1024), 15)]   # (size, weight)
DATA_SIZES = [16 * 1024, 64 * 1024, 256 * 1024, 512 * 1024]
PORTRAIT = (512, 1024)                    # first entry of pack08: the "portrait swap" test
HDR = 32
KIND_TEX, KIND_DATA = 1, 2
TAG = {KIND_TEX: b"\x00CFT", KIND_DATA: b"\x00CFD"}
WRAP_HDR, WRAP_TOTAL = 0x80, 0x90
MAX_RAW = 16 * MIB                        # GP_MAX_RAW in the GDAT Proto reader
OWNED = [IO_NAME] + [p for p, _ in PACKS] + ["cf_fixtures.h", "expected.json"]


def crc(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def stream(label, n):
    """Deterministic pseudo-random bytes."""
    return hashlib.shake_256(SEED + b"/" + label.encode()).digest(n)


class XorShift:
    def __init__(self, seed):
        self.s = (seed & 0xFFFFFFFF) or 0x9E3779B9

    def next(self):
        x = self.s
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.s = x & 0xFFFFFFFF
        return self.s

    def below(self, n):
        return self.next() % n


SPARSE = bytes((b & 3) if b < 16 else 0 for b in range(256))     # about 1 byte in 20 disturbed
ADD = [bytes((b + k) & 0xFF for b in range(256)) for k in range(256)]
TEXT16 = bytes(b"etaoinshrdlucmfw"[b & 15] for b in range(256))
WORDS = (b"area door wall floor stairs key chest enemy party skill item map event flag "
         b"light dark blood tower gate north south east west level guard trap save").split()
PHRASES = []
for _i in range(64):
    _p = b""
    _k = _i
    while len(_p) < 20:
        _p += WORDS[(_k * 7 + len(_p)) % len(WORDS)] + b" "
        _k = _k * 5 + 3
    PHRASES.append(_p[:20])


def xor_bytes(a, b):
    n = len(a)
    return (int.from_bytes(a, "little") ^ int.from_bytes(b, "little")).to_bytes(n, "little")


def tex_body(w, h, label):
    """RGBA8 image: smooth gradients that change every 4 rows, with about
    one byte in twenty disturbed by low-bit noise."""
    rnd = stream(label + "/base", 8)
    ph = list(rnd)
    row = bytearray(w * 4)
    for x in range(w):
        row[4 * x + 0] = (x * 256 // w + ph[0]) & 0xFF
        row[4 * x + 1] = ((x * 512 // w) + ph[1]) & 0xFF
        row[4 * x + 2] = (ph[2] + (x // 8) * 3) & 0xFF
        row[4 * x + 3] = 0xFF if (x // 16 + ph[3]) % 5 else 0xC0
    row = bytes(row)
    rows = []
    for y in range(h):
        blk = y // 4
        sh = ((blk * (1 + ph[4] % 7)) % w) * 4
        r = row[sh:] + row[:sh]
        rows.append(r.translate(ADD[(blk * 1024 // h + ph[5]) & 0xFF]))
    base = b"".join(rows)
    noise = stream(label + "/noise", w * h * 4).translate(SPARSE)
    return xor_bytes(base, noise)


def data_body(n, label):
    """Record-like data: 32-byte records with counters, small categories and
    phrases drawn mostly from a short list."""
    nrec = n // 32
    rnd = stream(label + "/fields", nrec * 4)
    out = bytearray(n)
    for i in range(nrec):
        o = 32 * i
        struct.pack_into("<IHBB", out, o, i, rnd[4 * i] & 7, rnd[4 * i + 1] & 3, 0x20 + (i & 15))
        struct.pack_into("<I", out, o + 8, (i * 37 + (rnd[4 * i + 2] & 15)) & 0xFFFF)
        out[o + 12:o + 32] = PHRASES[(rnd[4 * i + 3] * rnd[4 * i + 3]) >> 10]
    tail = n - 32 * nrec
    if tail:
        out[32 * nrec:] = stream(label + "/tail", tail).translate(TEXT16)
    return bytes(out)


def payload(kind, index, w, h, body):
    return struct.pack("<4sIIIIIII", b"CFP1", kind, w, h, len(body), crc(body), index, 0) + body


def wrap(kind, raw):
    z = zlib.compress(raw, 9)
    hdr = TAG[kind] + b"\0" * (WRAP_HDR - 4)
    return hdr + b"BILZ" + struct.pack(">III", len(raw), len(z), 7) + z


def compose(name, target_mib):
    """Entry list for one pack: (kind, w, h, body_len). Payload bytes sum to
    exactly target_mib MiB."""
    target = target_mib * MIB
    rng = XorShift(crc(SEED + name.encode()))
    tex, data = [], []
    tex_bytes = 0
    if name == "pack08.gdat":
        w, h = PORTRAIT
        tex.append((KIND_TEX, w, h, w * h * 4))
        tex_bytes += HDR + w * h * 4
    tex_goal = int(target * TEX_SHARE)
    while True:
        room = tex_goal - tex_bytes
        cand = [(d, wt) for d, wt in TEX_DIMS if HDR + d[0] * d[1] * 4 <= room]
        if not cand:
            break
        pick = rng.below(sum(wt for _, wt in cand))
        for (w, h), wt in cand:
            if pick < wt:
                tex.append((KIND_TEX, w, h, w * h * 4))
                tex_bytes += HDR + w * h * 4
                break
            pick -= wt
    used = tex_bytes
    while used < target:
        size = DATA_SIZES[rng.below(len(DATA_SIZES))]
        left = target - used - HDR
        if left - size < 64 + HDR:          # last entry takes the exact remainder
            size = left
        data.append((KIND_DATA, 0, 0, size))
        used += HDR + size
    assert used == target, (name, used, target)
    # interleave deterministically; the portrait (if any) stays first
    first = [tex.pop(0)] if name == "pack08.gdat" else []
    rest = tex + data
    for i in range(len(rest) - 1, 0, -1):
        j = rng.below(i + 1)
        rest[i], rest[j] = rest[j], rest[i]
    return first + rest


def build_pack(name, target_mib):
    plan = compose(name, target_mib)
    regions, info = [], []
    for i, (kind, w, h, blen) in enumerate(plan):
        label = "%s/%d" % (name, i)
        body = tex_body(w, h, label) if kind == KIND_TEX else data_body(blen, label)
        assert len(body) == blen
        raw = payload(kind, i, w, h, body)
        assert len(raw) <= MAX_RAW
        reg = wrap(kind, raw)
        regions.append(reg)
        info.append({"kind": kind, "w": w, "h": h, "usize": len(raw), "csize": len(reg) - WRAP_TOTAL,
                     "region": len(reg), "body_crc": crc(body)})
    n = len(regions)
    off = 8 + 8 * n
    idx = []
    for reg in regions:
        idx.append((off, len(reg)))
        off += len(reg)
    head = b"GDAT" + struct.pack("<I", n) + b"".join(struct.pack("<II", o, s) for o, s in idx)
    data = head + b"".join(regions)
    for e, (o, s) in zip(info, idx):
        e["offset"], e["size"] = o, s
    tex_e = [e for e in info if e["kind"] == KIND_TEX]
    summary = {
        "name": name,
        "target_mib": target_mib,
        "file_size": len(data),
        "file_crc": crc(data),
        "entries": n,
        "raw_total": sum(e["usize"] for e in info),
        "tex_count": len(tex_e),
        "tex_bytes": sum(e["w"] * e["h"] * 4 for e in tex_e),
        "data_bytes": sum(e["usize"] - HDR for e in info if e["kind"] == KIND_DATA),
        "max_region": max(e["region"] for e in info),
        "max_usize": max(e["usize"] for e in info),
        "list_crc": crc(b"".join(struct.pack("<I", e["body_crc"]) for e in info)),
        "stored_stream_bytes": sum(e["csize"] for e in info),
    }
    return data, summary, info


def write_header(out_dir, io, packs, portrait):
    lines = [
        "/* Generated by tools/make_contentfit_fixtures.py (version %d). Do not edit." % GENERATOR_VERSION,
        " * Expected values of the synthetic ContentFit fixtures; no game data. */",
        "#ifndef CF_FIXTURES_H",
        "#define CF_FIXTURES_H",
        "",
        '#include "cf_fixdefs.h"',
        "",
        "#define CF_FIX_GENERATOR_VERSION %d" % GENERATOR_VERSION,
        '#define CF_FIX_IO_NAME "%s"' % IO_NAME,
        "#define CF_FIX_IO_SIZE %du" % io["size"],
        "#define CF_FIX_IO_CRC 0x%08xu" % io["crc"],
        "#define CF_FIX_IO_HALF_CRC 0x%08xu" % io["half_crc"],
        "#define CF_FIX_PACK_COUNT %d" % len(packs),
        "#define CF_FIX_PORTRAIT_PACK %d" % portrait["pack"],
        "#define CF_FIX_PORTRAIT_ENTRY %d" % portrait["entry"],
        "#define CF_FIX_PORTRAIT_W %d" % portrait["w"],
        "#define CF_FIX_PORTRAIT_H %d" % portrait["h"],
        "",
        "/* name, target MiB, file size, file CRC, entries, payload bytes, textures,",
        " * texture bytes, data bytes, largest region, largest payload, list CRC */",
        "static const cf_fix_pack CF_FIX_PACKS[CF_FIX_PACK_COUNT] = {",
    ]
    for p in packs:
        lines.append('    {"%s", %d, %du, 0x%08xu, %du, %du, %du, %du, %du, %du, %du, 0x%08xu},' % (
            p["name"], p["target_mib"], p["file_size"], p["file_crc"], p["entries"], p["raw_total"],
            p["tex_count"], p["tex_bytes"], p["data_bytes"], p["max_region"], p["max_usize"], p["list_crc"]))
    lines += ["};", "", "#endif /* CF_FIXTURES_H */", ""]
    with open(os.path.join(out_dir, "cf_fixtures.h"), "w", newline="\n") as f:
        f.write("\n".join(lines))


def _generate(out_dir):
    io = stream("io16", IO_SIZE)
    with open(os.path.join(out_dir, IO_NAME), "wb") as f:
        f.write(io)
    io_info = {"name": IO_NAME, "size": IO_SIZE, "crc": crc(io), "half_crc": crc(io[:IO_SIZE // 2])}
    packs = []
    for name, mib in PACKS:
        data, summary, info = build_pack(name, mib)
        with open(os.path.join(out_dir, name), "wb") as f:
            f.write(data)
        summary["entry_list"] = info
        packs.append(summary)
        print("%s: %d entries, %d payload bytes, %d bytes on disk (ratio %.2f), %d textures (%d bytes)" % (
            name, summary["entries"], summary["raw_total"], summary["file_size"],
            summary["raw_total"] / summary["stored_stream_bytes"], summary["tex_count"], summary["tex_bytes"]))
    first = packs[0]["entry_list"][0]
    portrait = {"pack": 0, "entry": 0, "w": first["w"], "h": first["h"]}
    assert first["kind"] == KIND_TEX and (first["w"], first["h"]) == PORTRAIT
    write_header(out_dir, io_info, packs, portrait)
    doc = {"format": "contentfit-fixtures", "version": GENERATOR_VERSION,
           "note": "Synthetic fixtures only. No game data. Sizes are generic Stage 4 tiers.",
           "zlib_version": zlib.ZLIB_VERSION, "io": io_info, "portrait": portrait, "packs": packs}
    with open(os.path.join(out_dir, "expected.json"), "w", newline="\n") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")


def main(output_dir=DEFAULT_OUT):
    output_dir = os.path.abspath(output_dir)
    parent = os.path.dirname(output_dir)
    os.makedirs(output_dir, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".contentfit-fixtures-stage-", dir=parent) as stage_root:
        stage = os.path.join(stage_root, "fixtures")
        os.makedirs(stage)
        _generate(stage)
        subprocess.run([sys.executable, "-I", CHECKER, "--fixture-dir", stage], check=True)
        for name in OWNED:
            os.replace(os.path.join(stage, name), os.path.join(output_dir, name))
    print("fixtures written to %s" % output_dir)
    return 0


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--output-dir", default=DEFAULT_OUT,
                        help="fixture directory to update (default: contentfit/fixtures)")
    sys.exit(main(parser.parse_args().output_dir))
