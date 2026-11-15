#!/usr/bin/env python3
"""Generate synthetic GDAT-like fixtures for Vita GDAT Proto (gdatproto/fixtures/).

Everything written here is SYNTHETIC. The file layouts follow only the
observations documented in STEAM_ARCHIVE_STRUCTURE.md (sections 4 and 5):

  GDAT archive  "GDAT", u32 LE count, count x (u32 LE offset, u32 LE size),
                entries packed in order with no gaps, last entry ends at EOF.
  Entry wrapper 128-byte header (4-byte tag at offset 0, rest unknown),
                "BILZ" at 0x80, u32 BE uncompressed size, u32 BE compressed
                size, u32 BE 7, zlib stream at 0x90.
  CSH file      "\\0hsc", u32 BE 2, 13, 2, zeros except byte 0x2B = 1, then
                the same BILZ wrapper. Uncompressed size = 12 + 28 x the
                entry count of the paired archive.

The CSH record bytes are PLACEHOLDERS (pseudo-random). The real record format
is unknown and is not imitated. No game file is read, copied or decoded.
Payloads are pseudo-random, pseudo-text, structured and zero-filled data
generated here.

Expected results are written by this script from its own construction
intent (expected.json, plus expected.txt in a line format the Vita parses).
tools/check_gdatproto_fixtures.py re-derives them with a separate reader.

Usage (from the project root):  python3 -I tools/make_gdatproto_fixtures.py
"""
import json
import os
import random
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUT = os.path.join(ROOT, "gdatproto", "fixtures")
CHECKER = os.path.join(ROOT, "tools", "check_gdatproto_fixtures.py")

HDR = 0x90                       # 128-byte header + 16-byte BILZ block
OBS_BITS = ["GAP", "TRAILING", "UNSORTED", "EMPTY", "SIZE_RULE", "CONST", "CSH_FIELD", "CSH_PAIR"]

# Three-letter type codes seen in the sampled archive entries (stored reversed
# after a 00 byte), plus neutral invented codes. Tags are opaque to the reader.
OBSERVED_CODES = ["tex", "mdl", "tid", "CL3", "spk", "bin"]
INVENTED_CODES = ["qqa", "zzb", "xyc"]


def crc(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def tag(code):
    return b"\0" + code[::-1].encode("ascii")


# ---------------------------------------------------------------- payloads --
SYL = [b"al", b"be", b"co", b"da", b"ef", b"gi", b"ho", b"ix", b"ju", b"ka",
       b"lo", b"mi", b"nu", b"op", b"qu", b"re", b"si", b"tu", b"va", b"wo"]


WORDS = [b"".join(SYL[(i * 7 + j * 3) % len(SYL)] for j in range(1 + i % 3)) for i in range(160)]
WORD_WEIGHTS = [1.0 / (1 + i) for i in range(len(WORDS))]      # Zipf-like word frequencies


def pay_text(rng, n):
    out, size = [], 0
    while size < n:
        w = rng.choices(WORDS, WORD_WEIGHTS)[0]
        w += b"." if rng.random() < 0.08 else b" "
        out.append(w)
        size += len(w)
    return b"".join(out)[:n]


def pay_struct(rng, n):
    """Fixed-size records with slowly varying fields (table-like data)."""
    out = bytearray()
    i = 0
    base = rng.randint(0, 1000)
    while len(out) < n:
        out += struct.pack("<IIhhHH8s8s", i, base + i * 3, rng.randint(-40, 40), rng.randint(-40, 40),
                           rng.randint(0, 7), 0xFFFF, b"rec_data", b"\0" * 8)
        i += 1
    return bytes(out[:n])


def pay_noise(rng, n):
    return rng.randbytes(n)


def pay_mixed(rng, n):
    out = bytearray()
    while len(out) < n:
        k = rng.random()
        m = rng.randint(256, 4096)
        out += pay_noise(rng, m) if k < 0.15 else (pay_text(rng, m) if k < 0.6 else pay_struct(rng, m))
    return bytes(out[:n])


def pay_zero(_rng, n):
    return bytes(n)


KINDS = [("struct", pay_struct, 0.42), ("text", pay_text, 0.30), ("mixed", pay_mixed, 0.14),
         ("noise", pay_noise, 0.07), ("zero", pay_zero, 0.07)]


def payload(rng, n):
    r, acc = rng.random(), 0.0
    for name, fn, w in KINDS:
        acc += w
        if r < acc:
            return name, fn(rng, n)
    return KINDS[-1][0], KINDS[-1][1](rng, n)


# ------------------------------------------------------------------ format --
def wrap(tag4, raw, filler=b"", const=7, usize=None, csize=None, stream=None, pad=b""):
    """Builds one wrapped entry. Overrides create deliberate defects."""
    if stream is None:
        stream = zlib.compress(raw, 9)
    hdr = bytearray(0x80)
    hdr[0:4] = tag4
    if filler:
        hdr[4:0x80] = filler[:0x7C]
    blk = b"BILZ" + struct.pack(">III", len(raw) if usize is None else usize,
                                len(stream) if csize is None else csize, const)
    return bytes(hdr) + blk + stream + pad


def gdat(entries, pairs=None, count=None, gap_after_index=0, gaps=None, trailing=b""):
    """Packs entries; returns (bytes, index pairs as written).
    gaps: {i: n} inserts n pad bytes before entry i. pairs: explicit index."""
    n = len(entries)
    pos = 8 + 8 * n + gap_after_index
    idx, body = [], bytearray(b"\xEE" * gap_after_index)
    for i, e in enumerate(entries):
        g = (gaps or {}).get(i, 0)
        body += b"\xEE" * g
        pos += g
        idx.append((pos, len(e)))
        body += e
        pos += len(e)
    if pairs is not None:
        idx = pairs
    head = b"GDAT" + struct.pack("<I", n if count is None else count)
    head += b"".join(struct.pack("<II", o, s) for o, s in idx)
    return head + bytes(body) + trailing, idx


def csh(records, field=(2, 13, 2), flag=1, usize=None, pad=b"", magic=b"\0hsc", stream=None):
    hdr = bytearray(0x80)
    hdr[0:4] = magic
    hdr[4:16] = struct.pack(">III", *field)
    hdr[0x2B] = flag
    if stream is None:
        stream = zlib.compress(records, 9)
    blk = b"BILZ" + struct.pack(">III", len(records) if usize is None else usize, len(stream), 7)
    return bytes(hdr) + blk + stream + pad


def placeholder_records(n, seed):
    """12 + 28*n PLACEHOLDER bytes: pseudo-random, no imitation of real records."""
    return random.Random(seed).randbytes(12 + 28 * n)


# --------------------------------------------------------------- manifest --
class Manifest:
    def __init__(self, out_dir):
        self.out_dir = out_dir
        self.files = {}
        self.cases = []

    def add_file(self, name, data, kind, entries=None, records=None):
        with open(os.path.join(self.out_dir, name), "wb") as f:
            f.write(data)
        rec = {"size": len(data), "crc32": crc(data), "kind": kind}
        if entries is not None:
            rec["entries"] = entries
        if records is not None:
            rec["records_size"] = len(records)
            rec["records_crc32"] = crc(records)
        self.files[name] = rec

    def case(self, cid, name, op, mode, file, arg="", expect="OK", obs=(), count=-1, check=None):
        for o in obs:
            assert o in OBS_BITS, o
        self.cases.append({"id": cid, "name": name, "op": op, "mode": mode, "file": file,
                           "arg": str(arg), "expect": expect, "obs": list(obs), "count": count,
                           "check": check})


def entry_rows(idx, raws, entries, usizes=None, csizes=None):
    rows = []
    for i, (o, s) in enumerate(idx):
        e = entries[i]
        rows.append({"offset": o, "size": s,
                     "tag": e[0:4].hex(),
                     "usize": struct.unpack(">I", e[0x84:0x88])[0] if len(e) >= HDR else -1,
                     "csize": struct.unpack(">I", e[0x88:0x8C])[0] if len(e) >= HDR else -1,
                     "crc32": crc(raws[i])})
    return rows


def index_crc(idx):
    return crc(b"".join(struct.pack("<II", o, s) for o, s in idx))


def list_crc(values):
    return crc(b"".join(struct.pack("<I", v) for v in values))


def make_set(rng, n, size_lo, size_hi, codes, filler_every=0):
    raws, entries, kinds = [], [], []
    for i in range(n):
        size = int(round(size_lo * (size_hi / size_lo) ** rng.random()))
        kind, raw = payload(rng, size)
        filler = rng.randbytes(0x7C) if filler_every and i % filler_every == 1 else b""
        raws.append(raw)
        kinds.append(kind)
        entries.append(wrap(tag(codes[i % len(codes)]), raw, filler=filler))
    return raws, entries, kinds


# -------------------------------------------------------------------- main --
def main(output_dir=DEFAULT_OUT):
    output_dir = os.path.abspath(output_dir)
    parent = os.path.dirname(output_dir)
    os.makedirs(parent, exist_ok=True)
    os.makedirs(output_dir, exist_ok=True)

    # Generate and independently check a fresh sibling directory. Promotion
    # replaces only files this generator owns; every other file is untouched.
    with tempfile.TemporaryDirectory(prefix=".gdatproto-fixtures-stage-", dir=parent) as stage_root:
        stage = os.path.join(stage_root, "fixtures")
        os.makedirs(stage)
        result = _generate(stage)
        subprocess.run([sys.executable, "-I", CHECKER, "--fixture-dir", stage], check=True)
        generated_names = [*result.files.keys(), "expected.json", "expected.txt"]
        for name in generated_names:
            os.replace(os.path.join(stage, name), os.path.join(output_dir, name))
    return 0


def _generate(out_dir):
    m = Manifest(out_dir)
    rng = random.Random(0x47445031)

    # ---------------- G1 valid archives (+ paired CSH) ----------------
    valid = {}
    for name, n, lo, hi, codes, filler in [
        ("v_small", 3, 512, 4096, OBSERVED_CODES[:3], 0),
        ("v_medium", 64, 256, 65536, OBSERVED_CODES + INVENTED_CODES, 3),
        ("v_single", 1, 2048, 2048, ["tex"], 0),
    ]:
        raws, entries, _ = make_set(rng, n, lo, hi, codes, filler)
        data, idx = gdat(entries)
        m.add_file(name + ".gdat", data, "gdat", entry_rows(idx, raws, entries))
        recs = placeholder_records(n, 0x100 + n)
        m.add_file(name + ".csh", csh(recs), "csh", records=recs)
        valid[name] = (raws, entries, idx, data, recs)

    # performance archive: 1,024 entries, mixed compressibility
    perf_raws, perf_entries, perf_kinds = make_set(rng, 1024, 1024, 262144,
                                                   OBSERVED_CODES + INVENTED_CODES, 5)
    data, perf_idx = gdat(perf_entries)
    m.add_file("perf.gdat", data, "gdat", entry_rows(perf_idx, perf_raws, perf_entries))
    recs = placeholder_records(1024, 0x400)
    m.add_file("perf.csh", csh(recs), "csh", records=recs)

    # index archive: 4,096 small entries
    raws4k, ent4k, _ = make_set(rng, 4096, 16, 96, OBSERVED_CODES + INVENTED_CODES)
    data, idx4k = gdat(ent4k)
    m.add_file("index4096.gdat", data, "gdat", entry_rows(idx4k, raws4k, ent4k))

    for cid, f, n, idx in [("G1-01", "v_small", 3, valid["v_small"][2]),
                           ("G1-02", "v_medium", 64, valid["v_medium"][2]),
                           ("G1-03", "v_single", 1, valid["v_single"][2]),
                           ("G1-04", "perf", 1024, perf_idx),
                           ("G1-05", "index4096", 4096, idx4k)]:
        m.case(cid, "OPEN_" + f.upper(), "gdat_open", "strict", f + ".gdat", count=n, check=index_crc(idx))
    m.case("G1-06", "OPEN_SMALL_LENIENT", "gdat_open", "lenient", "v_small.gdat", count=3,
           check=index_crc(valid["v_small"][2]))

    # ---------------- G2 invalid GDAT ----------------
    s_raws, s_ent, s_idx, s_data, _ = valid["v_small"]
    n = len(s_ent)

    def add_bad(cid, cname, fname, data, strict, lenient=None, lobs=(), count=-1, check=None, idx=None,
                raws=None, ents=None):
        m.add_file(fname, data, "gdat",
                   entry_rows(idx, raws, ents) if idx is not None else None)
        m.case(cid + "S", cname, "gdat_open", "strict", fname, expect=strict)
        if lenient is not None:
            m.case(cid + "L", cname + "_LEN", "gdat_open", "lenient", fname, expect=lenient, obs=lobs,
                   count=count if lenient == "OK" else -1, check=check if lenient == "OK" else None)

    add_bad("G2-01", "BAD_MAGIC", "g_bad_magic.gdat", b"GDAX" + s_data[4:], "BAD_MAGIC", "BAD_MAGIC")
    add_bad("G2-02", "SHORT_HEADER", "g_short_header.gdat", s_data[:5], "TRUNCATED")
    add_bad("G2-03", "EMPTY_FILE", "g_empty_file.gdat", b"", "TRUNCATED")
    add_bad("G2-04", "INDEX_PAST_EOF", "g_index_past_eof.gdat",
            b"GDAT" + struct.pack("<I", 60000) + s_data[8:], "TRUNCATED", "TRUNCATED")
    add_bad("G2-05", "DATA_TRUNCATED", "g_data_truncated.gdat", s_data[:-100], "RANGE", "RANGE")
    p = list(s_idx); p[0] = (8, p[0][1])
    add_bad("G2-06", "OFFSET_IN_INDEX", "g_offset_in_index.gdat", gdat(s_ent, pairs=p)[0], "IN_INDEX", "IN_INDEX")
    p = list(s_idx); p[1] = (len(s_data) + 64, p[1][1])
    add_bad("G2-07", "ENTRY_PAST_END", "g_entry_past_end.gdat", gdat(s_ent, pairs=p)[0], "RANGE", "RANGE")
    p = list(s_idx); p[2] = (0xFFFFFF00, 0x200)
    add_bad("G2-08", "OFFSET_WRAP_32", "g_wrap32.gdat", gdat(s_ent, pairs=p)[0], "RANGE", "RANGE")
    p = list(s_idx); p[1] = (p[1][0] - 16, p[1][1])
    add_bad("G2-09", "OVERLAP", "g_overlap.gdat", gdat(s_ent, pairs=p)[0], "OVERLAP", "OVERLAP")
    p = list(s_idx); p[0], p[1] = p[1], p[0]
    ent_sw = [s_ent[1], s_ent[0], s_ent[2]]
    raw_sw = [s_raws[1], s_raws[0], s_raws[2]]
    add_bad("G2-10", "UNSORTED", "g_unsorted.gdat", gdat(s_ent, pairs=p)[0], "UNSORTED", "OK", ["UNSORTED"],
            count=n, check=index_crc(p), idx=p, raws=raw_sw, ents=ent_sw)
    d, gi = gdat(s_ent, gaps={1: 16})
    add_bad("G2-11", "GAP", "g_gap.gdat", d, "GAP", "OK", ["GAP"], count=n, check=index_crc(gi),
            idx=gi, raws=s_raws, ents=s_ent)
    d, gai = gdat(s_ent, gap_after_index=16)
    add_bad("G2-12", "GAP_AFTER_INDEX", "g_gap_after_index.gdat", d, "GAP", "OK", ["GAP"], count=n,
            check=index_crc(gai), idx=gai, raws=s_raws, ents=s_ent)
    add_bad("G2-13", "TRAILING_BYTES", "g_trailing.gdat", s_data + b"\0" * 32, "TRAILING", "OK", ["TRAILING"],
            count=n, check=index_crc(s_idx), idx=s_idx, raws=s_raws, ents=s_ent)
    add_bad("G2-14", "HUGE_COUNT", "g_huge_count.gdat", b"GDAT" + struct.pack("<I", 0xFFFFFFFF) + s_data[8:],
            "COUNT", "COUNT")
    add_bad("G2-15", "ZERO_ENTRIES", "g_zero.gdat", b"GDAT" + struct.pack("<I", 0), "EMPTY", "OK", ["EMPTY"],
            count=0, check=index_crc([]))
    add_bad("G2-16", "ZERO_PLUS_TRAILING", "g_zero_trailing.gdat", b"GDAT" + struct.pack("<I", 0) + b"\0" * 8,
            "EMPTY", "OK", ["EMPTY", "TRAILING"], count=0, check=index_crc([]))

    # ---------------- G3 entry wrapper defects (one archive, one defect per entry) -------------
    w_raws, w_ents, wnames = [], [], []
    base = pay_text(rng, 3000)
    zs = zlib.compress(base, 9)

    def add_w(label, raw, ent):
        w_raws.append(raw); w_ents.append(ent); wnames.append(label)

    add_w("VALID", base, wrap(tag("tex"), base))
    add_w("BAD_BILZ", base, wrap(tag("tex"), base)[:0x80] + b"BILY" + wrap(tag("tex"), base)[0x84:])
    add_w("CSIZE_PAST_ENTRY", base, wrap(tag("tex"), base, csize=len(zs) + 40))
    add_w("CSIZE_RULE", base, wrap(tag("tex"), base, pad=b"\0" * 8))
    add_w("USIZE_OVER_CAP", base, wrap(tag("tex"), base, usize=0x7FFFFFFF))
    add_w("CONST_NOT_7", base, wrap(tag("tex"), base, const=6))
    bad = bytearray(zs); bad[-1] ^= 0xFF
    add_w("ADLER_FLIP", base, wrap(tag("tex"), base, stream=bytes(bad)))
    bad = bytearray(zs); bad[len(bad) // 2] ^= 0x55
    add_w("MIDSTREAM_FLIP", base, wrap(tag("tex"), base, stream=bytes(bad)))
    add_w("STREAM_TRUNCATED", base, wrap(tag("tex"), base, stream=zs[:-32]))
    add_w("USIZE_TOO_SMALL", base, wrap(tag("tex"), base, usize=len(base) - 1))
    add_w("USIZE_TOO_LARGE", base, wrap(tag("tex"), base, usize=len(base) + 1))
    add_w("ENTRY_UNDER_0X90", base, wrap(tag("tex"), base)[:0x40])
    add_w("CRC_COMPARATOR", base, wrap(tag("tex"), base))
    add_w("UNKNOWN_HDR_BYTES", base, wrap(tag("tex"), base, filler=rng.randbytes(0x7C)))
    add_w("STORED_NOT_ZLIB", base, wrap(tag("bin"), base, stream=base))   # unobserved variant
    add_w("CSIZE_RULE_AND_CONST", base, wrap(tag("tex"), base, const=0, pad=b"\0" * 4))
    d, widx = gdat(w_ents)
    m.add_file("w_cases.gdat", d, "gdat", entry_rows(widx, w_raws, w_ents))
    m.case("G3-00", "W_ARCHIVE_OPEN", "gdat_open", "strict", "w_cases.gdat", count=len(w_ents),
           check=index_crc(widx))
    bc = crc(base)
    exp = {  # label: (strict, lenient, lenient obs)
        "VALID": ("OK", "OK", []),
        "BAD_BILZ": ("BAD_MAGIC", "BAD_MAGIC", []),
        "CSIZE_PAST_ENTRY": ("RANGE", "RANGE", []),
        "CSIZE_RULE": ("SIZE_RULE", "OK", ["SIZE_RULE"]),
        "USIZE_OVER_CAP": ("LIMIT", "LIMIT", []),
        "CONST_NOT_7": ("CONST", "OK", ["CONST"]),
        "ADLER_FLIP": ("ZLIB_DATA", "ZLIB_DATA", []),
        "MIDSTREAM_FLIP": ("ANY", "ANY", []),
        "STREAM_TRUNCATED": ("TRUNCATED", "TRUNCATED", []),
        "USIZE_TOO_SMALL": ("SIZE_MISMATCH", "SIZE_MISMATCH", []),
        "USIZE_TOO_LARGE": ("SIZE_MISMATCH", "SIZE_MISMATCH", []),
        "ENTRY_UNDER_0X90": ("TRUNCATED", "TRUNCATED", []),
        "CRC_COMPARATOR": ("CRC", "CRC", []),
        "UNKNOWN_HDR_BYTES": ("OK", "OK", []),
        "STORED_NOT_ZLIB": ("ZLIB_DATA", "ZLIB_DATA", []),
        "CSIZE_RULE_AND_CONST": ("CONST", "OK", ["SIZE_RULE", "CONST"]),
    }
    k = 1
    for i, label in enumerate(wnames):
        st, le, lobs = exp[label]
        chk = (bc ^ 0xFFFFFFFF) if label == "CRC_COMPARATOR" else bc
        m.case("G3-%02dS" % k, "W_" + label, "entry_load", "strict", "w_cases.gdat", i, st,
               check=chk if st in ("OK", "CRC") else None)
        if le != st or lobs:
            m.case("G3-%02dL" % k, "W_" + label + "_LEN", "entry_load", "lenient", "w_cases.gdat", i, le, lobs,
                   check=chk if le in ("OK", "CRC") else None)
        k += 1

    # ---------------- G4 CSH ----------------
    for cid, nm in [("G4-01", "v_small"), ("G4-02", "v_medium"), ("G4-03", "v_single")]:
        recs = valid[nm][4]
        m.case(cid, "CSH_" + nm.upper(), "csh_check", "strict", nm + ".csh", nm + ".gdat",
               count=len(valid[nm][2]), check=crc(recs))
    m.case("G4-04", "CSH_PERF", "csh_check", "strict", "perf.csh", "perf.gdat", count=1024,
           check=m.files["perf.csh"]["records_crc32"])
    recs3 = valid["v_small"][4]

    def add_c(cid, cname, fname, data, strict, lenient=None, lobs=(), pair="v_small.gdat", recs=None):
        m.add_file(fname, data, "csh", records=recs)
        m.case(cid + "S", cname, "csh_check", "strict", fname, pair, strict,
               count=3 if strict == "OK" else -1, check=crc(recs) if strict == "OK" else None)
        if lenient is not None:
            m.case(cid + "L", cname + "_LEN", "csh_check", "lenient", fname, pair, lenient, lobs,
                   count=3 if lenient == "OK" else -1, check=crc(recs) if lenient == "OK" else None)

    add_c("G4-05", "CSH_BAD_MAGIC", "c_bad_magic.csh", csh(recs3, magic=b"\0hsd"), "BAD_MAGIC", "BAD_MAGIC")
    add_c("G4-06", "CSH_FIELD_DIFFERS", "c_field.csh", csh(recs3, field=(2, 14, 2)), "CSH_FIELD", "OK",
          ["CSH_FIELD"], recs=recs3)
    add_c("G4-07", "CSH_BYTE_2B_ZERO", "c_flagbyte.csh", csh(recs3, flag=0), "CSH_FIELD", "OK", ["CSH_FIELD"],
          recs=recs3)
    add_c("G4-08", "CSH_CSIZE_RULE", "c_csize.csh", csh(recs3, pad=b"\0" * 4), "SIZE_RULE", "OK",
          ["SIZE_RULE"], recs=recs3)
    recs4 = placeholder_records(4, 0x777)
    add_c("G4-09", "CSH_PAIR_COUNT", "c_pair.csh", csh(recs4), "CSH_PAIR", "OK", ["CSH_PAIR"], recs=recs4)
    good = bytearray(csh(recs3)); good[-1] ^= 0xFF
    add_c("G4-10", "CSH_CORRUPT_STREAM", "c_corrupt.csh", bytes(good), "ZLIB_DATA", "ZLIB_DATA")
    add_c("G4-11", "CSH_TRUNCATED", "c_truncated.csh", csh(recs3)[:-10], "RANGE", "RANGE")
    add_c("G4-12", "CSH_SHORT_FILE", "c_short.csh", csh(recs3)[:0x60], "TRUNCATED")
    m.case("G4-13S", "CSH_PAIR_MISSING", "csh_check", "strict", "v_small.csh", "missing.gdat", "NOT_FOUND")

    # ---------------- G5 loading ----------------
    mr = valid["v_medium"][0]
    for k, i in enumerate([0, 31, 63]):
        m.case("G5-%02d" % (k + 1), "LOAD_MEDIUM_%d" % i, "entry_load", "strict", "v_medium.gdat", i,
               check=crc(mr[i]))
    m.case("G5-04", "LOAD_SEQ_MEDIUM", "load_seq", "strict", "v_medium.gdat", count=64,
           check=list_crc([crc(r) for r in mr]))
    m.case("G5-05", "LOAD_SHUF_MEDIUM", "load_shuf", "strict", "v_medium.gdat", count=64,
           check=list_crc([crc(r) for r in mr]))
    m.case("G5-06", "LOAD_SEQ_SINGLE", "load_seq", "strict", "v_single.gdat", count=1,
           check=list_crc([crc(valid["v_single"][0][0])]))
    m.case("G5-07", "LOAD_SEQ_GAP_LEN", "load_seq", "lenient", "g_gap.gdat", obs=["GAP"], count=3,
           check=list_crc([crc(r) for r in s_raws]))
    m.case("G5-08", "LOAD_SEQ_UNSORTED_LEN", "load_seq", "lenient", "g_unsorted.gdat", obs=["UNSORTED"], count=3,
           check=list_crc([crc(r) for r in raw_sw]))
    m.case("G5-09", "LOAD_SEQ_TRAILING_LEN", "load_seq", "lenient", "g_trailing.gdat", obs=["TRAILING"], count=3,
           check=list_crc([crc(r) for r in s_raws]))
    m.case("G5-10", "LOAD_SEQ_4096", "load_seq", "strict", "index4096.gdat", count=4096,
           check=list_crc([crc(r) for r in raws4k]))

    # ---------------- path allowlist ----------------
    m.case("G0-02", "PATH_OUTSIDE", "path_open", "strict", "-", "ux0:data/GdatProto/expected.txt", "PATH")
    m.case("G0-03", "PATH_DOTDOT", "path_open", "strict", "-", "app0:fixtures/../eboot.bin", "PATH")
    m.case("G0-04", "PATH_GAME_DIR", "path_open", "strict", "-", "ux0:app/PCSG00000/data.dat", "PATH")

    # ---------------- write manifests ----------------
    ratios = sorted(len(r) / max(1, len(e) - HDR) for r, e in zip(perf_raws, perf_entries))
    stats = {
        "perf_entries": len(perf_raws),
        "perf_uncompressed_bytes": sum(len(r) for r in perf_raws),
        "perf_stored_bytes": sum(len(e) for e in perf_entries),
        "perf_ratio_min": round(ratios[0], 2),
        "perf_ratio_median": round(ratios[len(ratios) // 2], 2),
        "perf_ratio_max": round(ratios[-1], 2),
        "perf_kinds": {k: perf_kinds.count(k) for k, _, _ in KINDS},
        "zlib_version": zlib.ZLIB_VERSION,
        "python": sys.version.split()[0],
    }
    doc = {"format": "gdatproto-expected", "version": 1,
           "note": "Synthetic fixtures only. CSH record bytes are placeholders. No game data.",
           "obs_bits": OBS_BITS, "stats": stats, "files": m.files, "cases": m.cases}
    with open(os.path.join(out_dir, "expected.json"), "w", newline="\n") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")
    write_txt(m, out_dir)
    print("staged %d files, %d cases" % (len(m.files) + 2, len(m.cases)))
    print("perf: %(perf_entries)d entries, %(perf_uncompressed_bytes)d raw bytes, "
          "%(perf_stored_bytes)d stored bytes, ratio min %(perf_ratio_min)s median %(perf_ratio_median)s "
          "max %(perf_ratio_max)s" % stats)
    return m


def write_txt(m, out_dir):
    """Line format for the Vita (see gdatproto/src/gp_manifest.h)."""
    names = list(m.files)
    lines = ["GPX 1"]
    for fi, name in enumerate(names):
        r = m.files[name]
        ents = r.get("entries")
        lines.append("FILE %s %d %08x %d" % (name, r["size"], r["crc32"], len(ents) if ents is not None else -1))
        for i, e in enumerate(ents or []):
            lines.append("ENTRY %d %d %d %d %d %d %08x" % (fi, i, e["offset"], e["size"], e["usize"], e["csize"],
                                                          e["crc32"]))
    for c in m.cases:
        ob = 0
        for o in c["obs"]:
            ob |= 1 << OBS_BITS.index(o)
        lines.append("CASE %s %s %s %s %s %s %s %x %d %s" % (
            c["id"], c["name"], c["op"], c["mode"], c["file"], c["arg"] or "-", c["expect"], ob, c["count"],
            "%08x" % c["check"] if c["check"] is not None else "-"))
    lines.append("END")
    with open(os.path.join(out_dir, "expected.txt"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", default=DEFAULT_OUT,
                        help="fixture directory to update (default: gdatproto/fixtures)")
    args = parser.parse_args()
    sys.exit(main(args.output_dir))
