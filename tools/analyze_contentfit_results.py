#!/usr/bin/env python3
"""Independent analysis of Vita ContentFit results (Stage 4).

Recomputes every frame-time statistic from runNNN_frames.csv and checks it
against runNNN.csv, re-derives the 30 fps verdicts (STABLE / FITS_30 /
TIGHT_30 / NOT_30) and the repeatability result (C5), then prints summary
tables. It shares no code with the app; the definitions are written out
again here from the Stage 4 plan:

  percentiles  nearest rank: sorted[ceil(p/100 * n) - 1]
  stable 30    <= 1% of intervals over 34.0 ms, P95 <= 34.0 ms, none over 51.0 ms
  FITS_30      stable and vsync-off P95 <= 16.7 ms
  TIGHT_30     stable and 16.7 < vsync-off P95 <= 25.0 ms
  NOT_30       otherwise
  60 fps       diagnostic only: vsync-off P95 <= 8.3 ms

usage:
  analyze_contentfit_results.py RUN_CSV            (frames file found next to it)
  analyze_contentfit_results.py --stats-selftest FILE
Exit status 0 only when every recomputed value agrees.
"""
import argparse
import csv
import os
import struct
import sys
from collections import defaultdict

LOCK_LIMIT, LOCK_MAX, MISS_PCT = 34.0, 51.0, 1
FIT, TIGHT, SIXTY, OVER60 = 16.7, 25.0, 8.3, 16.7


def f32(text):
    return struct.unpack("<f", struct.pack("<f", float(text)))[0]


def stats(vals):
    n = len(vals)
    if n == 0:
        return None
    s = sorted(vals)

    def rank(p):
        r = (p * n + 99) // 100
        return s[min(max(r, 1), n) - 1]

    acc = 0.0
    for v in vals:              # plain left-to-right double sum, like the app
        acc += v
    return {"n": n, "avg": acc / n, "min": s[0], "max": s[-1], "p50": rank(50), "p95": rank(95), "p99": rank(99),
            "over_16_7": sum(1 for v in vals if v > OVER60), "over_34": sum(1 for v in vals if v > LOCK_LIMIT),
            "over_51": sum(1 for v in vals if v > LOCK_MAX)}


def stable(st):
    return st is not None and 100 * st["over_34"] <= MISS_PCT * st["n"] and st["p95"] <= LOCK_LIMIT and \
        st["max"] <= LOCK_MAX


def fit_class(lock, off):
    if lock is None or off is None:
        return "SKIPPED"
    if not stable(lock) or off["p95"] > TIGHT:
        return "NOT_30"
    return "FITS_30" if off["p95"] <= FIT else "TIGHT_30"


def selftest(path):
    bad = sets = 0
    lines = open(path).read().split("\n")
    i = 0
    while i < len(lines):
        if not lines[i].startswith("SET "):
            i += 1
            continue
        _, _, n = lines[i].split()
        n = int(n)
        vals = [f32(x) for x in lines[i + 1:i + 1 + n]]
        c = lines[i + 1 + n].split()
        i += n + 2
        sets += 1
        st = stats(vals)
        got = {"n": int(c[1]), "avg": float(c[2]), "min": f32(c[3]), "max": f32(c[4]), "p50": f32(c[5]),
               "p95": f32(c[6]), "p99": f32(c[7]), "over_16_7": int(c[8]), "over_34": int(c[9]),
               "over_51": int(c[10])}
        for k, v in got.items():
            ok = abs(v - st[k]) <= 1e-9 * max(1.0, abs(v)) if k == "avg" else v == st[k]
            if not ok:
                bad += 1
                print("stats self-test set %d: %s C %r, Python %r" % (sets, k, v, st[k]))
        if int(c[11]) != int(stable(st)):
            bad += 1
            print("stats self-test set %d: stable C %s, Python %s" % (sets, c[11], stable(st)))
    print("stats self-test: %d sets, %d disagreements" % (sets, bad))
    return 0 if bad == 0 and sets > 0 else 1


def load(run_csv):
    rows = list(csv.DictReader(l for l in open(run_csv, encoding="utf-8") if not l.startswith("#")))
    frames = defaultdict(list)
    fpath = run_csv[:-4] + "_frames.csv"
    if os.path.exists(fpath):
        for r in csv.DictReader(l for l in open(fpath, encoding="utf-8") if not l.startswith("#")):
            frames[(r["iteration"], r["id"], r["mode"])].append(f32(r["ms"]))
    return rows, frames, fpath


def analyze(run_csv):
    rows, frames, fpath = load(run_csv)
    bad = 0
    checked = 0
    by_key = {}
    for r in rows:
        by_key[(r["iteration"], r["id"], r["mode"])] = r
    # 1. statistics of every row with frames
    for r in rows:
        if not r["frames"]:
            continue
        key = (r["iteration"], r["id"], r["mode"])
        st = stats(frames.get(key, []))
        checked += 1
        if st is None:
            bad += 1
            print("MISSING frames for %s" % (key,))
            continue
        for k in ("n", "over_16_7", "over_34", "over_51"):
            col = "frames" if k == "n" else k
            if int(r[col]) != st[k]:
                bad += 1
                print("%s %s: CSV %s, recomputed %s" % (key, k, r[col], st[k]))
        for k in ("avg", "p50", "p95", "p99", "max"):
            col = k + "_ms"
            if abs(float(r[col]) - st[k]) > 1.5e-6:
                bad += 1
                print("%s %s: CSV %s, recomputed %.6f" % (key, k, r[col], st[k]))
        if r["mode"] == "LOCK30" and r["id"].startswith("C3-") and r["verdict"] in ("STABLE", "UNSTABLE"):
            want = "STABLE" if stable(st) else "UNSTABLE"
            if r["verdict"] != want:
                bad += 1
                print("%s verdict %s, recomputed %s" % (key, r["verdict"], want))
        r["_st"] = st
    # 2. fit classes
    fits = defaultdict(dict)
    for r in rows:
        if r["mode"] != "VERDICT" or not r["id"].startswith("C3-") or r["id"] == "C3-P":
            continue
        lock = by_key.get((r["iteration"], r["id"], "LOCK30"))
        off = by_key.get((r["iteration"], r["id"], "OFF"))
        want = fit_class(lock and lock.get("_st"), off and off.get("_st"))
        if r["status"] not in ("OK", "BASELINE"):
            want = "SKIPPED"
        checked += 1
        if r["verdict"] != want:
            bad += 1
            print("%s fit %s, recomputed %s" % ((r["iteration"], r["id"]), r["verdict"], want))
        fits[r["id"]][r["iteration"]] = (r["verdict"], off and off.get("_st"), lock and lock.get("_st"))
    # 3. repeatability
    iters = sorted({r["iteration"] for r in rows if r["id"] != "C5-01"}, key=int)
    per = {it: [r for r in rows if r["iteration"] == it and r["id"] != "C5-01"] for it in iters}
    complete = [it for it in iters if any(r["id"] == "C3-T-PVRTC4" or r["id"] == "C4-SOAK" for r in per[it])]
    c5 = [r for r in rows if r["id"] == "C5-01"]
    if len(complete) >= 2 and c5:
        ref = [(r["id"], r["mode"], r["status"], r["ok"], r["check"]) for r in per[complete[0]]]
        diffs = 0
        for it in complete[1:]:
            cur = [(r["id"], r["mode"], r["status"], r["ok"], r["check"]) for r in per[it]]
            diffs += sum(1 for a, b in zip(ref, cur) if a != b) + abs(len(ref) - len(cur))
        want = "PASS" if diffs == 0 else "FAIL"
        checked += 1
        if c5[0]["verdict"] != want:
            bad += 1
            print("C5 verdict %s, recomputed %s (%d differences)" % (c5[0]["verdict"], want, diffs))
    # 4. summary
    print("Run file: %s (%d rows; frames file %s)" % (run_csv, len(rows), "found" if frames else "MISSING"))
    print("\n30 fps fit by scene (verdict per iteration; median cost P95 / locked P95 in ms)")
    for sid in sorted(fits):
        v = fits[sid]
        costs = sorted(x[1]["p95"] for x in v.values() if x[1])
        locks = sorted(x[2]["p95"] for x in v.values() if x[2])
        med = lambda a: a[len(a) // 2] if a else float("nan")
        print("  %-12s %-34s cost %7.2f  locked %7.2f" % (sid, " / ".join(v[k][0] for k in sorted(v, key=int)),
                                                         med(costs), med(locks)))
    print("\nLoads, reads and memory (per iteration: verdict, ms, MiB/s)")
    for r in rows:
        if r["id"][:2] in ("C1", "C2", "C4") and r["mode"] != "LOCK30":
            print("  it%s %-8s %-7s %-12s ok=%s %-12s %10s ms %8s MiB/s" % (
                r["iteration"], r["id"], r["mode"], r["status"], r["ok"], r["verdict"], r["ms"], r["mib_per_s"]))
    notok = [r for r in rows if r["ok"] != "1"]
    print("\nRows not ok: %d" % len(notok))
    for r in notok:
        print("  it%s %s %s %s: %s" % (r["iteration"], r["id"], r["mode"], r["status"], r["detail"]))
    print("\nRecomputed checks: %d, disagreements: %d" % (checked, bad))
    return 0 if bad == 0 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run_csv", nargs="?")
    ap.add_argument("--stats-selftest")
    a = ap.parse_args()
    if a.stats_selftest:
        return selftest(a.stats_selftest)
    if not a.run_csv:
        ap.error("give a runNNN.csv path")
    return analyze(a.run_csv)


if __name__ == "__main__":
    sys.exit(main())
