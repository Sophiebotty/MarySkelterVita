#!/usr/bin/env python3
"""Independent checker for the Vita ContentFit fixtures.

Re-reads every fixture from scratch (it shares no code with the generator),
validates each pack against the strict GDAT Proto layout rules and the CFP1
payload format, recomputes every expected value, and compares the results
with BOTH cf_fixtures.h (what the Vita app is compiled with) and
expected.json. Exit status 0 only when everything agrees.
"""
import argparse
import json
import os
import re
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DIR = os.path.join(os.path.dirname(HERE), "contentfit", "fixtures")
MIB = 1 << 20
WRAP_HDR, WRAP_TOTAL = 0x80, 0x90
MAX_RAW = 16 * MIB
PACK_TARGETS = {"pack08.gdat": 8, "pack16.gdat": 16, "pack32.gdat": 32, "pack64.gdat": 64}
FIELDS = ["name", "target_mib", "file_size", "file_crc", "entries", "raw_total", "tex_count",
          "tex_bytes", "data_bytes", "max_region", "max_usize", "list_crc"]


class Fail(Exception):
    pass


def crc(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def need(cond, msg):
    if not cond:
        raise Fail(msg)


def read_pack(path):
    data = open(path, "rb").read()
    name = os.path.basename(path)
    need(len(data) >= 8, "%s: shorter than 8 bytes" % name)
    need(data[:4] == b"GDAT", "%s: bad magic" % name)
    n = struct.unpack_from("<I", data, 4)[0]
    index_end = 8 + 8 * n
    need(0 < n <= 65536, "%s: entry count %d" % (name, n))
    need(index_end <= len(data), "%s: index past EOF" % name)
    pos = index_end
    out = {"name": name, "file_size": len(data), "file_crc": crc(data), "entries": n, "raw_total": 0,
           "tex_count": 0, "tex_bytes": 0, "data_bytes": 0, "max_region": 0, "max_usize": 0}
    body_crcs, first = [], None
    for i in range(n):
        off, size = struct.unpack_from("<II", data, 8 + 8 * i)
        need(off == pos, "%s entry %d: offset %d, expected gap-free %d" % (name, i, off, pos))
        need(off + size <= len(data), "%s entry %d: past EOF" % (name, i))
        need(size >= WRAP_TOTAL, "%s entry %d: region below 0x90" % (name, i))
        reg = data[off:off + size]
        pos = off + size
        need(reg[WRAP_HDR:WRAP_HDR + 4] == b"BILZ", "%s entry %d: no BILZ" % (name, i))
        usize, csize, const = struct.unpack_from(">III", reg, WRAP_HDR + 4)
        need(const == 7, "%s entry %d: constant %d" % (name, i, const))
        need(csize == size - WRAP_TOTAL, "%s entry %d: size rule" % (name, i))
        need(usize <= MAX_RAW, "%s entry %d: usize above 16 MiB" % (name, i))
        z = zlib.decompressobj()
        raw = z.decompress(reg[WRAP_TOTAL:])
        need(z.eof and not z.unused_data and len(raw) == usize,
             "%s entry %d: inflate not exact (%d of %d, eof %s)" % (name, i, len(raw), usize, z.eof))
        need(len(raw) >= 32, "%s entry %d: payload shorter than CFP1 header" % (name, i))
        magic, kind, w, h, blen, bcrc, idx, res = struct.unpack_from("<4sIIIIIII", raw, 0)
        body = raw[32:]
        need(magic == b"CFP1", "%s entry %d: payload magic" % (name, i))
        need(idx == i and res == 0, "%s entry %d: index/reserved field" % (name, i))
        need(blen == len(body) == usize - 32, "%s entry %d: body length" % (name, i))
        need(crc(body) == bcrc, "%s entry %d: body CRC" % (name, i))
        if kind == 1:
            need(w > 0 and h > 0 and blen == w * h * 4, "%s entry %d: texture size" % (name, i))
            need(w & (w - 1) == 0 and h & (h - 1) == 0, "%s entry %d: texture not power of two" % (name, i))
            need(reg[:4] == b"\x00CFT", "%s entry %d: texture tag" % (name, i))
            out["tex_count"] += 1
            out["tex_bytes"] += blen
        elif kind == 2:
            need(w == 0 and h == 0, "%s entry %d: data entry with dimensions" % (name, i))
            need(reg[:4] == b"\x00CFD", "%s entry %d: data tag" % (name, i))
            out["data_bytes"] += blen
        else:
            raise Fail("%s entry %d: kind %d" % (name, i, kind))
        if i == 0:
            first = (kind, w, h)
        out["raw_total"] += usize
        out["max_region"] = max(out["max_region"], size)
        out["max_usize"] = max(out["max_usize"], usize)
        body_crcs.append(bcrc)
    need(pos == len(data), "%s: %d trailing bytes" % (name, len(data) - pos))
    out["list_crc"] = crc(b"".join(struct.pack("<I", c) for c in body_crcs))
    out["first"] = first
    return out


def parse_header(path):
    text = open(path, encoding="utf-8").read()
    defs = dict(re.findall(r"^#define (CF_FIX_\w+) (\S+)$", text, re.M))
    rows = re.findall(r'^\s*\{"([^"]+)", (.*)\},$', text, re.M)
    packs = {}
    for name, rest in rows:
        vals = [int(v.rstrip("u"), 0) for v in rest.split(", ")]
        need(len(vals) == len(FIELDS) - 1, "cf_fixtures.h: row %s has %d values" % (name, len(vals)))
        packs[name] = dict(zip(FIELDS[1:], vals))
    return defs, packs


def check(fix_dir):
    report = []
    for f in ["io16.bin", "cf_fixtures.h", "expected.json"] + list(PACK_TARGETS):
        need(os.path.isfile(os.path.join(fix_dir, f)), "missing " + f)
    defs, hpacks = parse_header(os.path.join(fix_dir, "cf_fixtures.h"))
    doc = json.load(open(os.path.join(fix_dir, "expected.json"), encoding="utf-8"))

    io = open(os.path.join(fix_dir, "io16.bin"), "rb").read()
    io_vals = {"size": len(io), "crc": crc(io), "half_crc": crc(io[:len(io) // 2])}
    need(io_vals["size"] == 16 * MIB, "io16.bin size %d" % len(io))
    need(len(zlib.compress(io[:MIB], 6)) > MIB * 0.99, "io16.bin is compressible")
    need(int(defs["CF_FIX_IO_SIZE"].rstrip("u")) == io_vals["size"], "header IO size")
    need(int(defs["CF_FIX_IO_CRC"].rstrip("u"), 16) == io_vals["crc"], "header IO CRC")
    need(int(defs["CF_FIX_IO_HALF_CRC"].rstrip("u"), 16) == io_vals["half_crc"], "header IO half CRC")
    need(defs["CF_FIX_IO_NAME"] == '"io16.bin"', "header IO name")
    for k in ("size", "crc", "half_crc"):
        need(doc["io"][k] == io_vals[k], "expected.json io " + k)
    report.append("io16.bin: %d bytes, CRC %08x, first-half CRC %08x; matches header and JSON" % (
        io_vals["size"], io_vals["crc"], io_vals["half_crc"]))

    need(int(defs["CF_FIX_PACK_COUNT"]) == len(PACK_TARGETS) == len(hpacks), "pack count")
    jpacks = {p["name"]: p for p in doc["packs"]}
    for name, mib in PACK_TARGETS.items():
        got = read_pack(os.path.join(fix_dir, name))
        need(got["raw_total"] == mib * MIB, "%s: payload total %d, expected %d" % (name, got["raw_total"], mib * MIB))
        for k in FIELDS[2:]:
            need(hpacks[name][k] == got[k], "%s: header %s %d, recomputed %d" % (name, k, hpacks[name][k], got[k]))
            need(jpacks[name][k] == got[k], "%s: JSON %s differs" % (name, k))
        need(hpacks[name]["target_mib"] == mib, "%s: header target" % name)
        if name == "pack08.gdat":
            need(got["first"] == (1, int(defs["CF_FIX_PORTRAIT_W"]), int(defs["CF_FIX_PORTRAIT_H"])),
                 "portrait entry is not pack08 entry 0")
            need(int(defs["CF_FIX_PORTRAIT_PACK"]) == 0 and int(defs["CF_FIX_PORTRAIT_ENTRY"]) == 0,
                 "portrait location")
        report.append("%s: %d entries, %d payload bytes (%d MiB), %d textures (%d bytes), %d bytes on disk, "
                      "largest region %d, list CRC %08x; matches header and JSON" % (
                          name, got["entries"], got["raw_total"], mib, got["tex_count"], got["tex_bytes"],
                          got["file_size"], got["max_region"], got["list_crc"]))
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--fixture-dir", default=DEFAULT_DIR)
    args = ap.parse_args()
    try:
        for line in check(args.fixture_dir):
            print(line)
    except Fail as e:
        print("FIXTURE CHECK FAILED: %s" % e)
        return 1
    print("contentfit fixtures: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
