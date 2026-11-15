#!/usr/bin/env python3
"""Generate PC-side fixtures for Vita ResProto (resproto/fixtures/).

Independent implementation: payloads come from a Python PRNG, compression
from Python's standard-library zlib, and the VRP1 container is written from
the format specification in resproto/src/rp_container.h (not from the C
code). The Vita app inflates/parses these to cross-check its own zlib and
VRP1 reader (tests T9x).

All data is synthetic. No game data is read or written.

Usage (from the project root):  python3 -I tools/make_resproto_fixtures.py
"""
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "resproto", "fixtures")
SIZE = 256 * 1024


def prng(seed):
    """Deterministic 32-bit xorshift generator (independent of the C one)."""
    x = seed & 0xFFFFFFFF or 1
    while True:
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        yield x


def gen_random(seed, n):
    g = prng(seed)
    return bytes(next(g) & 0xFF for _ in range(n))


def gen_text(seed, n):
    syl = ["al", "be", "co", "da", "ef", "gi", "ho", "ix", "ju", "ka", "lo", "mi", "nu", "op"]
    g = prng(seed)
    out = bytearray()
    while len(out) < n:
        for _ in range(1 + next(g) % 3):
            out += syl[next(g) % len(syl)].encode()
        out += b"." if next(g) % 12 == 0 else b" "
    return bytes(out[:n])


def crc(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def build_vrp1(items):
    """items: list of (raw_bytes, compress_flag). Returns container bytes."""
    n = len(items)
    header_size, entry_size = 32, 24
    data_off = header_size + n * entry_size
    stored = [zlib.compress(raw, 9) if (z and raw) else raw for raw, z in items]
    index, pos = bytearray(), data_off
    for (raw, z), s in zip(items, stored):
        index += struct.pack("<6I", pos, len(s), len(raw), 1 if (z and raw) else 0, crc(raw), 0)
        pos += len(s)
    total = pos
    head = bytearray(b"VRP1" + struct.pack("<HHIIIII", 1, 0, n, header_size, total, data_off, crc(bytes(index))))
    head += struct.pack("<I", crc(bytes(head)))
    assert len(head) == header_size
    return bytes(head) + bytes(index) + b"".join(stored)


def main():
    os.makedirs(OUT, exist_ok=True)
    lines = ["# kind file raw_size crc32 stored_size   (container: kind file entry_count)"]
    streams = {
        "pc_text.z": gen_text(0xA11CE, SIZE),
        "pc_random.z": gen_random(0xB0B, SIZE),
        "pc_zero.z": bytes(SIZE),
    }
    for name, raw in streams.items():
        z = zlib.compress(raw, 9)
        with open(os.path.join(OUT, name), "wb") as f:
            f.write(z)
        lines.append(f"stream {name} {len(raw)} {crc(raw):08x} {len(z)}")
        print(f"wrote {name}: {len(raw)} -> {len(z)} bytes, crc {crc(raw):08x}")

    items = [(gen_text(1, 3000), True), (gen_random(2, 4096), False), (bytes(10000), True),
             (b"", False), (gen_text(5, 70000), True)]
    blob = build_vrp1(items)
    with open(os.path.join(OUT, "pc_container.vrp"), "wb") as f:
        f.write(blob)
    lines.append(f"container pc_container.vrp {len(items)}")
    print(f"wrote pc_container.vrp: {len(items)} entries, {len(blob)} bytes")

    with open(os.path.join(OUT, "manifest.txt"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote manifest.txt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
