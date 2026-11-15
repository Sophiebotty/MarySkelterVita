#!/usr/bin/env python3
"""Check gdatproto/fixtures/ against expected.json with a separate reader.

This is a second, independent reading of the synthetic fixtures. It does not
reuse the generator's code: it parses each file from bytes, applies the same
documented rules (strict / lenient), runs every case in expected.json and
compares the outcome with the generator's stated intent. It also checks that
expected.txt (the Vita's copy) carries the same data as expected.json.

It reads only gdatproto/fixtures/. All data there is synthetic.

Usage (from the project root):  python3 -I tools/check_gdatproto_fixtures.py
Exit status 0 if every check passes.
"""
import json
import os
import random
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_FIX = os.path.join(ROOT, "gdatproto", "fixtures")
MAX_ENTRIES = 65536
RAW_CAP = 16 * 1024 * 1024


class Fail(Exception):
    def __init__(self, code):
        super().__init__(code)
        self.code = code


def be(b, o):
    return struct.unpack_from(">I", b, o)[0]


def le(b, o):
    return struct.unpack_from("<I", b, o)[0]


def read_gdat(data, lenient):
    obs = set()
    if len(data) < 8:
        raise Fail("TRUNCATED")
    if data[:4] != b"GDAT":
        raise Fail("BAD_MAGIC")
    n = le(data, 4)
    if n > MAX_ENTRIES:
        raise Fail("COUNT")
    end_index = 8 + 8 * n
    if end_index > len(data):
        raise Fail("TRUNCATED")
    pairs = [(le(data, 8 + 8 * i), le(data, 12 + 8 * i)) for i in range(n)]
    if n == 0:
        if not lenient:
            raise Fail("EMPTY")
        obs.add("EMPTY")
        if len(data) > 8:
            obs.add("TRAILING")
        return pairs, obs
    for o, s in pairs:
        if o + s > len(data):
            raise Fail("RANGE")
        if o < end_index:
            raise Fail("IN_INDEX")
    order = list(range(n))
    if any(pairs[i][0] < pairs[i - 1][0] for i in range(1, n)):
        if not lenient:
            raise Fail("UNSORTED")
        obs.add("UNSORTED")
        order.sort(key=lambda i: (pairs[i][0], i))
    prev_end = end_index
    for i in order:
        o, s = pairs[i]
        if o < prev_end:
            raise Fail("OVERLAP")
        if o > prev_end:
            if not lenient:
                raise Fail("GAP")
            obs.add("GAP")
        prev_end = o + s
    if prev_end < len(data):
        if not lenient:
            raise Fail("TRAILING")
        obs.add("TRAILING")
    return pairs, obs


def parse_wrapper(entry, lenient):
    """Header checks only. Returns (usize, csize, obs)."""
    obs = set()
    if len(entry) < 0x90:
        raise Fail("TRUNCATED")
    if entry[0x80:0x84] != b"BILZ":
        raise Fail("BAD_MAGIC")
    usize, csize, const = be(entry, 0x84), be(entry, 0x88), be(entry, 0x8C)
    avail = len(entry) - 0x90
    if csize > avail:
        raise Fail("RANGE")
    if usize > RAW_CAP:
        raise Fail("LIMIT")
    if const != 7:
        if not lenient:
            raise Fail("CONST")
        obs.add("CONST")
    if csize < avail:
        if not lenient:
            raise Fail("SIZE_RULE")
        obs.add("SIZE_RULE")
    return usize, csize, obs


def inflate_exact(stream, usize):
    if not stream:
        raise Fail("TRUNCATED")
    d = zlib.decompressobj()
    try:
        out = d.decompress(stream, usize + 1)
    except zlib.error:
        raise Fail("ZLIB_DATA")
    if not d.eof:
        raise Fail("SIZE_MISMATCH" if len(out) > usize else "TRUNCATED")
    if len(out) != usize or d.unused_data or d.unconsumed_tail:
        raise Fail("SIZE_MISMATCH")
    return out


def read_wrapper(entry, lenient):
    usize, csize, obs = parse_wrapper(entry, lenient)
    return inflate_exact(entry[0x90:0x90 + csize], usize), obs


def read_csh(data, pair_path, lenient):
    obs = set()
    if len(data) < 0x90:
        raise Fail("TRUNCATED")
    if data[:4] != b"\0hsc":
        raise Fail("BAD_MAGIC")
    zero_ok = all(data[i] == 0 for i in range(0x10, 0x80) if i != 0x2B)
    if (be(data, 4), be(data, 8), be(data, 12)) != (2, 13, 2) or data[0x2B] != 1 or not zero_ok:
        if not lenient:
            raise Fail("CSH_FIELD")
        obs.add("CSH_FIELD")
    usize, csize, wobs = parse_wrapper(data, lenient)
    if not os.path.exists(pair_path):
        raise Fail("NOT_FOUND")
    with open(pair_path, "rb") as f:
        pairs, _ = read_gdat(f.read(), lenient)
    if usize != 12 + 28 * len(pairs):
        if not lenient:
            raise Fail("CSH_PAIR")
        obs.add("CSH_PAIR")
    raw = inflate_exact(data[0x90:0x90 + csize], usize)
    return raw, obs | wobs, len(pairs)


def run_case(c, files, fix):
    lenient = c["mode"] == "lenient"
    path = os.path.join(fix, c["file"]) if c["file"] != "-" else None
    op = c["op"]
    if op == "path_open":
        p = c["arg"]
        ok = (p.startswith("ux0:app/VDNG00004/fixtures/") or p.startswith("app0:fixtures/")) and ".." not in p
        return ("OK" if ok else "PATH"), set(), -1, None
    try:
        data = open(path, "rb").read()
        if op == "gdat_open":
            pairs, obs = read_gdat(data, lenient)
            chk = zlib.crc32(b"".join(struct.pack("<II", o, s) for o, s in pairs)) & 0xFFFFFFFF
            return "OK", obs, len(pairs), chk
        if op == "entry_load":
            pairs, obs = read_gdat(data, lenient)
            o, s = pairs[int(c["arg"])]
            raw, wobs = read_wrapper(data[o:o + s], lenient)
            chk = zlib.crc32(raw) & 0xFFFFFFFF
            if c["check"] is not None and chk != c["check"]:
                return "CRC", obs | wobs, -1, chk
            return "OK", obs | wobs, -1, chk
        if op in ("load_seq", "load_shuf"):
            pairs, obs = read_gdat(data, lenient)
            order = list(range(len(pairs)))
            if op == "load_shuf":
                random.Random(7).shuffle(order)
            crcs = [0] * len(pairs)
            rows = files[c["file"]]["entries"]
            for i in order:
                o, s = pairs[i]
                raw, wobs = read_wrapper(data[o:o + s], lenient)
                obs |= wobs
                crcs[i] = zlib.crc32(raw) & 0xFFFFFFFF
                if crcs[i] != rows[i]["crc32"]:
                    return "CRC", obs, -1, None
            chk = zlib.crc32(b"".join(struct.pack("<I", v) for v in crcs)) & 0xFFFFFFFF
            return "OK", obs, len(pairs), chk
        if op == "csh_check":
            raw, obs, n = read_csh(data, os.path.join(fix, c["arg"]), lenient)
            return "OK", obs, n, zlib.crc32(raw) & 0xFFFFFFFF
    except FileNotFoundError:
        return "NOT_FOUND", set(), -1, None
    except Fail as f:
        return f.code, set(), -1, None
    raise ValueError("unknown op " + op)


def check_txt(doc, fix):
    """expected.txt must carry the same files, entries and cases."""
    bits = doc["obs_bits"]
    lines = open(os.path.join(fix, "expected.txt"), encoding="ascii").read().split("\n")
    if lines[0] != "GPX 1" or "END" not in lines:
        return ["expected.txt header/footer"]
    errs, names = [], list(doc["files"])
    files = [l.split() for l in lines if l.startswith("FILE ")]
    ents = [l.split() for l in lines if l.startswith("ENTRY ")]
    cases = [l.split() for l in lines if l.startswith("CASE ")]
    if [f[1] for f in files] != names:
        errs.append("FILE list differs")
    for f in files:
        r = doc["files"][f[1]]
        if int(f[2]) != r["size"] or int(f[3], 16) != r["crc32"]:
            errs.append("FILE " + f[1])
    n_ent = sum(len(r.get("entries", [])) for r in doc["files"].values())
    if len(ents) != n_ent:
        errs.append("ENTRY count %d vs %d" % (len(ents), n_ent))
    for e in ents:
        r = doc["files"][names[int(e[1])]]["entries"][int(e[2])]
        vals = [int(e[3]), int(e[4]), int(e[5]), int(e[6]), int(e[7], 16)]
        if vals != [r["offset"], r["size"], r["usize"], r["csize"], r["crc32"]]:
            errs.append("ENTRY %s %s" % (e[1], e[2]))
    if len(cases) != len(doc["cases"]):
        errs.append("CASE count")
    for t, c in zip(cases, doc["cases"]):
        ob = sum(1 << bits.index(o) for o in c["obs"])
        want = [c["id"], c["name"], c["op"], c["mode"], c["file"], c["arg"] or "-", c["expect"], "%x" % ob,
                str(c["count"]), "%08x" % c["check"] if c["check"] is not None else "-"]
        if t[1:] != want:
            errs.append("CASE " + c["id"])
    return errs


def main(fix=DEFAULT_FIX):
    doc = json.load(open(os.path.join(fix, "expected.json"), encoding="utf-8"))
    errors = []
    for name, r in doc["files"].items():
        data = open(os.path.join(fix, name), "rb").read()
        if len(data) != r["size"] or (zlib.crc32(data) & 0xFFFFFFFF) != r["crc32"]:
            errors.append("file %s size/crc" % name)
    errors += check_txt(doc, fix)
    passed = 0
    for c in doc["cases"]:
        st, obs, count, chk = run_case(c, doc["files"], fix)
        ok = (st != "OK") if c["expect"] == "ANY" else (st == c["expect"])
        if ok and c["expect"] == "OK":
            ok = obs == set(c["obs"])
            if c["count"] >= 0:
                ok = ok and count == c["count"]
            if c["check"] is not None:
                ok = ok and chk == c["check"]
        if ok:
            passed += 1
        else:
            errors.append("%s %s: got %s obs=%s count=%s, expected %s obs=%s count=%s" % (
                c["id"], c["name"], st, sorted(obs), count, c["expect"], c["obs"], c["count"]))
    print("cases: %d/%d agree with expected.json" % (passed, len(doc["cases"])))
    print("files: %d checked; expected.txt %s" % (len(doc["files"]),
                                                   "matches" if not check_txt(doc, fix) else "DIFFERS"))
    for e in errors:
        print("ERROR:", e)
    return 1 if errors else 0


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture-dir", default=DEFAULT_FIX,
                        help="fixture directory to check (default: gdatproto/fixtures)")
    args = parser.parse_args()
    sys.exit(main(os.path.abspath(args.fixture_dir)))
