#!/usr/bin/env python3
"""Compares the stage 10.3 C results (the file test_rs_format writes) with
expected.json, RESOURCE_PIPELINE.md and the independent Python checker
(tools/check_respipe_fixtures.py, imported read-only).

  CODE   the C names of codes 300-320 against the document's table 6.4
  PACK   each valid pack: code, entry, entries, VRAM, heap need (expected.json)
  CASE   each of the 50 cases: code, entry and stage against expected.json, the
         document's table and the checker; CF_ERR_PAYLOAD also by CFP1 field
  FLIPF  each file-level flip: the checker evaluates the same flipped file;
         code, entry and stage must agree
  FLIPE  each entry-level flip: the same mutation, built here independently,
         through the checker's own CFP1, VRS1 and table-of-contents functions
         in section 6.1's order; the code must agree
The number of FLIPF and FLIPE lines is also recomputed from the packs, so a
flip the C test skipped is reported. Exit status 0 only when all agree.
"""
import argparse
import importlib.util
import json
import os
import re
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))

spec = importlib.util.spec_from_file_location("check_respipe", os.path.join(ROOT, "tools", "check_respipe_fixtures.py"))
ck = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ck)


def crc(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def le16(b, o):
    return b[o] | (b[o + 1] << 8)


def le32(b, o):
    return int.from_bytes(b[o:o + 4], "little")


def body_header_bytes(raw):
    kind, b = le16(raw, 38), raw[64:]
    if kind == 1:
        return 16 + 32 * le32(b, 0)
    if kind == 2:
        return 16 + 8 * le16(b, 6)
    if kind == 3:
        return 32 + 16 * le16(b, 12)
    return 16


def mutate(raw, o, mode):
    """The C test's flip: invert byte o; for "fix", recompute the VRS1 body CRC (body bytes),
    the VRS1 header CRC (unless o is in it), then the CFP1 body CRC."""
    m = bytearray(raw)
    m[o] ^= 0xFF
    if mode == "fix" and o >= 32:
        if o >= 64:
            m[48:52] = crc(bytes(m[64:])).to_bytes(4, "little")
        if not 60 <= o < 64:
            m[60:64] = crc(bytes(m[32:60])).to_bytes(4, "little")
        m[20:24] = crc(bytes(m[32:])).to_bytes(4, "little")
    return bytes(m)


def toc_rows(toc_payload, n):
    return ck.toc_check(toc_payload, n, "open", 0)


def entry_code(m, e, n, toc_payload, room_body, mode):
    """B6 then C.2/C.3 (or A3-A4 for entry 0) on one inflated entry, with the checker's functions."""
    if ck.cfp1_field(m, e) is not None:
        return 211
    try:
        if e == 0:
            if le32(m, 4) != 2:
                return 320
            ck.toc_check(m[32:], n, "open", 0)
            return 0
        tp = toc_payload
        if mode == "fix":
            t = bytearray(toc_payload)
            at = 32 + 16 + 32 * (e - 1) + 12
            t[at:at + 4] = m[20:24]
            t[16:20] = crc(bytes(t[32:])).to_bytes(4, "little")
            t[28:32] = crc(bytes(t[:28])).to_bytes(4, "little")
            tp = bytes(t)
        rows = toc_rows(tp, n)
        if le32(m, 4) != 2:                                             # C.3.2
            return 320
        row = rows[e - 1]
        if len(m) != row["size"] + 32 or le32(m, 20) != row["crc"]:     # C.3.3
            return 315
        kind, rid, body = ck.common_header(m[32:], "entry", e)          # C.3.4
        if rid != row["id"] or kind != row["kind"]:                     # C.3.5
            return 315
        facts = {2: ck.texture_check, 3: ck.mesh_check, 4: ck.level_check}[kind](body, "entry", e)   # C.3.6
        if facts["format"] != row["format"] or facts["vram"] != row["vram"] or facts["heap"] != row["heap"]:
            return 315                                                  # C.3.7
        tex_ids = {r["id"] for r in rows if r["kind"] == 2}
        if any(t not in tex_ids for t in facts["refs"]):                # C.3.8
            return 317
        if kind == 4 and body != room_body:                             # C.3.9
            return 319
        return 0
    except ck.Fail as f:
        return f.code


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--results", required=True)
    ap.add_argument("--fixture-dir", default=os.path.join(ROOT, "respipe", "fixtures"))
    args = ap.parse_args()

    problems = []
    by = {}
    for text in open(args.results, encoding="utf-8"):
        text = text.rstrip("\n")
        if not text:
            continue
        kind = text.split(" ", 1)[0]
        # CASE lines end with free text (the failing check); every other line is plain fields
        by.setdefault(kind, []).append(text.split(" ", 6) if kind == "CASE" else text.split())
    man = json.load(open(os.path.join(args.fixture_dir, "expected.json"), encoding="utf-8"))
    doc = open(ck.DOC, encoding="utf-8").read()
    _hexes, table = ck.read_doc()
    rows_src, rw, rd, cell, wall, _shade = ck.read_room_sources()
    room_body = ck.reference_room_body(rows_src, rw, rd, cell, wall)
    files = {}

    def data_of(rel):
        if rel not in files:
            files[rel] = open(os.path.join(args.fixture_dir, rel), "rb").read()
        return files[rel]

    # CODE
    doc_codes = {int(c): n for c, n in re.findall(r"^\| (3\d\d) \| `(RS_ERR_\w+)` \|", doc, re.M)}
    c_codes = {int(l[1]): l[2] for l in by.get("CODE", [])}
    if c_codes != doc_codes or len(doc_codes) != 21:
        problems.append("code names differ from table 6.4: C %s, document %s" % (c_codes, doc_codes))

    # PACK
    packs = {p["name"]: p for p in man["packs"]}
    seen = set()
    for l in by.get("PACK", []):
        rel, code, entry, stage = l[1], l[2], l[3], l[4]
        n, vram, heap = int(l[5]), int(l[6]), int(l[7])
        p = packs.get(rel)
        seen.add(rel)
        if p is None or (int(code), int(entry)) != (p["expect_code"], p["expect_entry"]) or \
                (n, vram, heap) != (p["entries"], p["vram_bytes"], p["heap_need"]):
            problems.append("PACK %s: C %s %s %s %s %s %s" % (rel, code, entry, stage, n, vram, heap))
    if seen != set(packs):
        problems.append("PACK lines missing: %s" % sorted(set(packs) - seen))

    # CASE
    cases = {c["id"]: c for c in man["cases"]}
    seen = set()
    for l in by.get("CASE", []):
        cid, rel, code, entry, stage = l[1], l[2], int(l[3]), int(l[4]), l[5]
        check_text = l[6] if len(l) > 6 else ""
        c = cases.get(cid)
        seen.add(cid)
        r = ck.evaluate(data_of(rel), room_body)
        doc_text, doc_code = table.get(cid, ("", None))
        ok = c is not None and rel == c["path"] and (code, entry, stage) == (c["expect_code"], c["expect_entry"],
                                                                             c["expect_stage"])
        ok = ok and code == doc_code and (code, entry, stage) == (r["code"], r["entry"], r["stage"])
        if ok and code == 211:
            ok = check_text.startswith("CFP1 ") and check_text[5:].lower() in doc_text.lower() and \
                r["detail"][5:] == check_text[5:]
        if not ok:
            problems.append("CASE %s: C %d entry %d %s (%s); manifest %s; document %s; checker %d entry %d %s (%s)" % (
                cid, code, entry, stage, check_text, c and (c["expect_code"], c["expect_entry"], c["expect_stage"]),
                doc_code, r["code"], r["entry"], r["stage"], r["detail"]))
    if seen != set(cases) or len(cases) != 50:
        problems.append("CASE lines missing: %s" % sorted(set(cases) - seen))

    # FLIPF
    want_f = {}
    for rel in packs:
        d = data_of(rel)
        n = le32(d, 4)
        want_f[rel] = 8 + 8 * n + 0x90 * n
    # gp_gdat_open (unmodified) returns only a status, not the index entry it rejected, so the C
    # side reports those failures for the archive (-1). For them only code and stage are compared.
    gdat_open_codes = {9, 11, 16, 17, 18, 101, 102, 103, 104, 105}
    got_f, agree_f, archive_level = {}, 0, 0
    for l in by.get("FLIPF", []):
        rel, o, code, entry, stage = l[1], int(l[2]), int(l[4]), int(l[5]), l[6]
        got_f[rel] = got_f.get(rel, 0) + 1
        d = bytearray(data_of(rel))
        d[o] ^= 0xFF
        r = ck.evaluate(bytes(d), room_body)
        index_failure = entry == -1 and stage == "open" and code in gdat_open_codes
        same = (code, stage) == (r["code"], r["stage"] or "-") and (index_failure or entry == r["entry"])
        if same and code != 0:
            agree_f += 1
            archive_level += index_failure and r["entry"] != -1
        else:
            problems.append("FLIPF %s byte %d: C %d entry %d %s; checker %d entry %d %s (%s)" % (
                rel, o, code, entry, stage, r["code"], r["entry"], r["stage"], r["detail"]))
    if got_f != want_f:
        problems.append("FLIPF counts %s, expected %s" % (got_f, want_f))

    # FLIPE
    want_e, got_e, agree_e = {}, {}, 0
    entries = {}
    for rel in packs:
        r = ck.run_pack_without_policy(data_of(rel))
        if r["code"] != 0:
            problems.append("%s: checker cannot read it (%s)" % (rel, r["name"]))
            continue
        raws = [e["raw"] for e in r["entries"]]
        entries[rel] = raws
        want_e[rel] = sum(32 + 2 * (64 + body_header_bytes(raw) - 32) for raw in raws)
    for l in by.get("FLIPE", []):
        rel, e, o, mode, code = l[1], int(l[2]), int(l[3]), l[4], int(l[5])
        got_e[rel] = got_e.get(rel, 0) + 1
        raws = entries.get(rel)
        if raws is None:
            continue
        m = mutate(raws[e], o, mode)
        want = entry_code(m, e, len(raws), raws[0][32:], room_body, mode)
        if want == code and (mode == "fix" or code != 0):
            agree_e += 1
        else:
            problems.append("FLIPE %s entry %d byte %d %s: C %d, checker %d" % (rel, e, o, mode, code, want))
    if got_e != want_e:
        problems.append("FLIPE counts %s, expected %s" % (got_e, want_e))

    bounds = [l for l in by.get("BOUND", [])]
    for l in bounds:
        if l[1] != l[2]:
            problems.append("BOUND %s: C %s, expected %s" % (" ".join(l[3:]), l[1], l[2]))
    if len(bounds) < 50:
        problems.append("only %d boundary results" % len(bounds))
    print("  codes 300-320: %d names, %s" % (len(c_codes), "match table 6.4" if c_codes == doc_codes else "DIFFER"))
    print("  boundaries: %d results, %d as expected" % (len(bounds), sum(l[1] == l[2] for l in bounds)))
    print("  packs: %d lines; cases: %d lines" % (len(by.get("PACK", [])), len(by.get("CASE", []))))
    print("  file-level flips: %d, agreeing with the checker: %d (of which %d GDAT index failures compared by "
          "code and stage only: gp_gdat_open does not report the entry)" % (sum(got_f.values()), agree_f,
                                                                           archive_level))
    print("  entry-level flips: %d, agreeing with the checker: %d" % (sum(got_e.values()), agree_e))
    for p in problems[:40]:
        print("DISAGREEMENT: " + p)
    if len(problems) > 40:
        print("... %d more" % (len(problems) - 40))
    print("%d disagreements" % len(problems))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
