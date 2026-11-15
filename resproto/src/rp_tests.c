/*
 * rp_tests.c - repeatable test suite. Every test states its expected
 * result (RP_OK or a specific error) and records the actual one.
 */
#include <stdio.h>
#include <string.h>

#include "rp_container.h"
#include "rp_inflate.h"
#include "rp_io.h"
#include "rp_tests.h"
#include "rp_time.h"

#define MB (1024.0 * 1024.0)

static rp_result *add(rp_suite *s, const char *id, const char *name, int expect)
{
    rp_result *r = &s->r[s->count < RP_MAX_RESULTS ? s->count++ : RP_MAX_RESULTS - 1];
    memset(r, 0, sizeof(*r));
    snprintf(r->id, sizeof(r->id), "%s", id);
    snprintf(r->name, sizeof(r->name), "%s", name);
    r->expect = expect;
    return r;
}

static void finish(rp_suite *s, rp_result *r, rp_err actual)
{
    r->actual = actual;
    r->pass = (r->expect == RP_EXPECT_ANY_ERROR) ? (actual != RP_OK) : ((int)actual == r->expect);
    if (r->pass) s->passed++;
}

static double mbps(double bytes, double ms) { return ms > 0.0 ? (bytes / MB) / (ms / 1000.0) : 0.0; }

/* ---- T1-T4: plain file I/O ---- */

static void t_io(rp_suite *s, const rp_fixture_set *fx, rp_progress_fn progress)
{
    char path[96];
    rp_result *r;
    uint64_t sz;
    int i;

    r = add(s, "T1A", "OPEN EXISTING", RP_OK);
    finish(s, r, rp_file_size(rp_work_path("io_4k.bin", path, sizeof(path)), &sz));
    r->check = (uint32_t)sz;

    r = add(s, "T1B", "OPEN MISSING", RP_ERR_NOT_FOUND);
    finish(s, r, rp_file_size(rp_work_path("does_not_exist.bin", path, sizeof(path)), &sz));

    for (i = 0; i < RP_IO_FILES; i++) {
        static const char *const ids[RP_IO_FILES] = { "T2A", "T2B", "T2C", "T2D" };
        uint8_t *buf; size_t len; double t0, t1, t2; rp_err e;
        char nm[32];
        snprintf(nm, sizeof(nm), "READ ALL %s", fx->io[i].name);
        if (progress) progress(nm);
        r = add(s, ids[i], nm, RP_OK);
        rp_work_path(fx->io[i].name, path, sizeof(path));
        t0 = rp_now_ms();
        e = rp_read_all(path, RP_MAX_READ_ALL, &buf, &len);
        t1 = rp_now_ms();
        if (e == RP_OK) {
            uint32_t crc = rp_crc32(buf, len);
            rp_free(buf);
            if (len != fx->io[i].size) e = RP_ERR_SIZE_MISMATCH;
            else if (crc != fx->io[i].crc) e = RP_ERR_CRC;
            r->check = crc;
        }
        t2 = 0.0;                                 /* duration of the second read, if it ran */
        if (e == RP_OK) {                         /* second read: may hit OS cache */
            double ta = rp_now_ms();
            if (rp_read_all(path, RP_MAX_READ_ALL, &buf, &len) == RP_OK) rp_free(buf);
            t2 = rp_now_ms() - ta;
        }
        r->ms = t1 - t0;
        r->mbps = mbps((double)fx->io[i].size, r->ms);
        if (t2 > 0.0)
            snprintf(r->detail, sizeof(r->detail), "read1 %.1f MB/s  read2 %.1f MB/s",
                     r->mbps, mbps((double)fx->io[i].size, t2));
        else
            snprintf(r->detail, sizeof(r->detail), "read1 %.1f MB/s  read2 not run", r->mbps);
        finish(s, r, e);
    }

    if (progress) progress("CHUNKED 16M");
    {
        uint32_t crc = 0; uint64_t len = 0; double t0 = rp_now_ms(); rp_err e;
        r = add(s, "T3", "CHUNKED 64K x 16M", RP_OK);
        e = rp_crc_file_chunked(rp_work_path(fx->io[3].name, path, sizeof(path)), 64u * 1024u, &crc, &len);
        r->ms = rp_now_ms() - t0;
        r->mbps = mbps((double)len, r->ms);
        if (e == RP_OK && crc != fx->io[3].crc) e = RP_ERR_CRC;
        r->check = crc;
        snprintf(r->detail, sizeof(r->detail), "%.1f MB/s", r->mbps);
        finish(s, r, e);
    }

    if (progress) progress("RANDOM READS");
    {
        FILE *fp = NULL; uint64_t size = 0; rp_err e; int k, bad = 0;
        uint8_t got[4096], want[4096];
        uint32_t st = 0xC0FFEEu;
        double t0;
        r = add(s, "T4", "1000 RANDOM 4K READS", RP_OK);
        e = rp_open(rp_work_path(fx->io[3].name, path, sizeof(path)), &fp, &size);
        if (e == RP_OK && size <= sizeof(got)) e = RP_ERR_TRUNCATED;   /* need room for 4 KB reads */
        t0 = rp_now_ms();
        for (k = 0; e == RP_OK && k < 1000; k++) {
            uint64_t off;
            st ^= st << 13; st ^= st >> 17; st ^= st << 5;
            off = (uint64_t)(st % (uint32_t)(size - sizeof(got)));
            e = rp_read_at(fp, size, off, got, sizeof(got));
            if (e == RP_OK) {
                rp_gen_random_at(fx->io[3].seed, off, want, sizeof(want));
                if (memcmp(got, want, sizeof(got)) != 0) bad++;
            }
        }
        r->ms = rp_now_ms() - t0;
        if (fp) fclose(fp);                       /* always closed, even after a failed read */
        if (e == RP_OK && bad) e = RP_ERR_CRC;
        r->check = (uint32_t)bad;
        snprintf(r->detail, sizeof(r->detail), "%.1f us/read  mismatches %d", r->ms, bad);
        finish(s, r, e);
    }
}

/* ---- T5-T8: container validation ---- */

static rp_err open_close(const char *name, uint32_t *count_out)
{
    char path[96];
    rp_container c;
    rp_err e = rp_container_open(&c, rp_work_path(name, path, sizeof(path)));
    if (e == RP_OK) {
        if (count_out) *count_out = c.count;
        rp_container_close(&c);
    }
    return e;
}

static void t_container(rp_suite *s, rp_progress_fn progress)
{
    static const struct { const char *id, *name, *file; int expect; } CASES[] = {
        { "T5B", "BAD MAGIC",          "bad_magic.vrp",   RP_ERR_BAD_MAGIC },
        { "T5C", "BAD VERSION",        "bad_version.vrp", RP_ERR_BAD_VERSION },
        { "T5D", "BAD HEADER CRC",     "bad_hcrc.vrp",    RP_ERR_BAD_HEADER_CRC },
        { "T5E", "BAD INDEX CRC",      "bad_icrc.vrp",    RP_ERR_BAD_INDEX_CRC },
        { "T6A", "TRUNCATED FILE",     "truncated.vrp",   RP_ERR_TRUNCATED },
        { "T6B", "FILE LONGER THAN DECL", "padded.vrp",   RP_ERR_SIZE_MISMATCH },
        { "T7A", "ENTRY PAST END",     "range.vrp",       RP_ERR_RANGE },
        { "T7B", "OVERLAPPING ENTRIES", "overlap.vrp",    RP_ERR_OVERLAP },
        { "T7C", "OFFSET+SIZE OVERFLOW", "overflow.vrp",  RP_ERR_RANGE },
        { "T7D", "HUGE ENTRY COUNT",   "hugecount.vrp",   RP_ERR_COUNT },
        { "T8A", "EMPTY FILE",         "empty.bin",       RP_ERR_TRUNCATED },
        { "T8B", "ZERO ENTRIES",       "zero.vrp",        RP_OK },
    };
    char path[96];
    rp_result *r;
    size_t i;

    if (progress) progress("CONTAINER VALID");
    {   /* T5A: open the valid container and load every entry */
        rp_container c; rp_err e; uint32_t i2, total = 0, crcsum = 0; double t0 = rp_now_ms();
        r = add(s, "T5A", "VALID CONTAINER LOAD", RP_OK);
        e = rp_container_open(&c, rp_work_path("valid.vrp", path, sizeof(path)));
        if (e == RP_OK) {
            if (c.count != RP_VALID_ENTRIES) e = RP_ERR_COUNT;
            for (i2 = 0; e == RP_OK && i2 < c.count; i2++) {
                uint8_t *d; uint32_t n;
                e = rp_container_load(&c, i2, &d, &n);
                if (e == RP_OK) { total += n; crcsum ^= c.entries[i2].crc32; if (d) rp_free(d); }
            }
            rp_container_close(&c);
        }
        r->ms = rp_now_ms() - t0;
        r->mbps = mbps((double)total, r->ms);
        r->check = crcsum ^ total;
        snprintf(r->detail, sizeof(r->detail), "%u entries  %u raw bytes  %.1f MB/s",
                 (unsigned)RP_VALID_ENTRIES, (unsigned)total, r->mbps);
        finish(s, r, e);
    }

    if (progress) progress("CONTAINER INVALID");
    for (i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        rp_mem_stats before = rp_mem_get();
        uint32_t cnt = 0xFFFFFFFFu;
        rp_err e;
        size_t grew;
        rp_mem_reset_peak();
        r = add(s, CASES[i].id, CASES[i].name, CASES[i].expect);
        e = open_close(CASES[i].file, &cnt);
        grew = rp_mem_get().peak - before.current;
        r->check = (uint32_t)e;
        snprintf(r->detail, sizeof(r->detail), "%s  peak alloc %u B", CASES[i].file, (unsigned)grew);
        /* rejection must not allocate more than the index of the valid file */
        if (e == r->expect && grew > 64u * 1024u) e = RP_ERR_LIMIT;
        if (CASES[i].expect == RP_OK && e == RP_OK && cnt != 0) e = RP_ERR_COUNT;
        finish(s, r, e);
    }
}

/* ---- T9: cross-implementation fixtures (made on a PC with Python zlib) ---- */

static void t_pc_fixtures(rp_suite *s, rp_progress_fn progress)
{
    static const char *const dirs[2] = { RP_PC_FIXTURE_DIR_UX0, RP_PC_FIXTURE_DIR_APP0 };
    char path[128], line[160];
    FILE *mf = NULL;
    int d, n = 0;
    rp_result *r;

    if (progress) progress("PC FIXTURES");
    for (d = 0; d < 2 && !mf; d++) {
        snprintf(path, sizeof(path), "%s/manifest.txt", dirs[d]);
        mf = fopen(path, "rb");
        if (mf) snprintf(s->pc_fixture_dir, sizeof(s->pc_fixture_dir), "%s", dirs[d]);
    }
    if (!mf) {
        r = add(s, "T9", "PC FIXTURE MANIFEST", RP_OK);
        snprintf(r->detail, sizeof(r->detail), "manifest not found in ux0:app or app0:");
        finish(s, r, RP_ERR_NOT_FOUND);
        return;
    }
    while (fgets(line, sizeof(line), mf) && n < 6) {
        char kind[16], file[48], id[8];
        unsigned raw = 0, crc = 0, stored = 0;
        if (line[0] == '#') continue;
        if (sscanf(line, "%15s %47s %u %x %u", kind, file, &raw, &crc, &stored) < 3) continue;
        snprintf(id, sizeof(id), "T9%c", 'A' + n++);
        snprintf(path, sizeof(path), "%s/%s", s->pc_fixture_dir, file);
        if (strcmp(kind, "stream") == 0) {
            uint8_t *z = NULL, *out = NULL; size_t zl = 0; rp_err e; double t0;
            r = add(s, id, file, RP_OK);
            e = rp_read_all(path, RP_MAX_READ_ALL, &z, &zl);
            if (e == RP_OK && zl != stored) e = RP_ERR_SIZE_MISMATCH;
            if (e == RP_OK && raw > RP_MAX_ENTRY_RAW) e = RP_ERR_LIMIT;
            if (e == RP_OK && !(out = (uint8_t *)rp_malloc(raw ? raw : 1))) e = RP_ERR_NOMEM;
            t0 = rp_now_ms();
            if (e == RP_OK) e = rp_inflate_exact(z, zl, out, raw);
            r->ms = rp_now_ms() - t0;
            r->mbps = mbps((double)raw, r->ms);
            if (e == RP_OK && rp_crc32(out, raw) != crc) e = RP_ERR_CRC;
            r->check = crc;
            snprintf(r->detail, sizeof(r->detail), "python zlib -> vita inflate  %u -> %u B", stored, raw);
            if (z) rp_free(z);
            if (out) rp_free(out);
            finish(s, r, e);
        } else if (strcmp(kind, "container") == 0) {
            rp_container c; rp_err e; uint32_t i2;
            r = add(s, id, file, RP_OK);
            e = rp_container_open(&c, path);
            if (e == RP_OK) {
                if (c.count != raw) e = RP_ERR_COUNT;
                for (i2 = 0; e == RP_OK && i2 < c.count; i2++) {
                    uint8_t *dd; uint32_t nn;
                    e = rp_container_load(&c, i2, &dd, &nn);
                    if (e == RP_OK && dd) rp_free(dd);
                }
                rp_container_close(&c);
            }
            r->check = raw;
            snprintf(r->detail, sizeof(r->detail), "python VRP1 writer, %u entries", raw);
            finish(s, r, e);
        }
    }
    fclose(mf);
}

/* ---- T10-T12: zlib round trip, corruption handling, throughput ---- */

static void t_zlib(rp_suite *s, rp_progress_fn progress)
{
    static const char *const kind_names[RP_PAY_KINDS] = { "TEXT", "RANDOM", "ZERO" };
    const size_t N = 1u << 20;
    uint8_t *raw = (uint8_t *)rp_malloc(N), *out = (uint8_t *)rp_malloc(N + 1);
    uint8_t *z[RP_PAY_KINDS] = { 0 };
    size_t zl[RP_PAY_KINDS] = { 0 };
    char nm[32], id[8];
    rp_result *r;
    int k;

    if (!raw || !out) {
        r = add(s, "T10", "ZLIB BUFFERS", RP_OK);
        finish(s, r, RP_ERR_NOMEM);
        if (raw) rp_free(raw);
        if (out) rp_free(out);
        return;
    }
    if (progress) progress("ZLIB ROUND TRIP");
    for (k = 0; k < RP_PAY_KINDS; k++) {
        rp_err e; double t0;
        snprintf(id, sizeof(id), "T10%c", 'A' + k);
        snprintf(nm, sizeof(nm), "ROUND TRIP 1M %s", kind_names[k]);
        r = add(s, id, nm, RP_OK);
        rp_gen_payload((rp_payload_kind)k, 0x3000u + (uint32_t)k, raw, N);
        t0 = rp_now_ms();
        e = rp_deflate_alloc(raw, N, 6, &z[k], &zl[k]);
        r->ms = rp_now_ms() - t0;                       /* deflate time */
        if (e == RP_OK) e = rp_inflate_exact(z[k], zl[k], out, N);
        if (e == RP_OK && memcmp(raw, out, N) != 0) e = RP_ERR_CRC;
        r->check = (uint32_t)zl[k];
        snprintf(r->detail, sizeof(r->detail), "ratio %.2f  deflate %.1f ms", zl[k] ? (double)N / (double)zl[k] : 0.0, r->ms);
        finish(s, r, e);
    }

    if (progress) progress("ZLIB CORRUPTION");
    if (z[RP_PAY_TEXT]) {
        uint8_t *bad = (uint8_t *)rp_malloc(zl[RP_PAY_TEXT]);
        const size_t L = zl[RP_PAY_TEXT];
        if (bad) {
            memcpy(bad, z[RP_PAY_TEXT], L); bad[L - 1] ^= 0xFF;     /* Adler-32 trailer */
            r = add(s, "T11A", "ADLER32 TRAILER FLIP", RP_ERR_ZLIB_DATA);
            finish(s, r, rp_inflate_exact(bad, L, out, N));

            memcpy(bad, z[RP_PAY_TEXT], L); bad[L / 2] ^= 0x5A;     /* inside the stream */
            r = add(s, "T11B", "MID-STREAM BYTE FLIP", RP_EXPECT_ANY_ERROR);
            finish(s, r, rp_inflate_exact(bad, L, out, N));
            snprintf(r->detail, sizeof(r->detail), "rejected as %s", rp_err_name(r->actual));

            r = add(s, "T11C", "TRUNCATED STREAM", RP_ERR_TRUNCATED);
            finish(s, r, rp_inflate_exact(z[RP_PAY_TEXT], L / 2, out, N));

            r = add(s, "T11D", "DECLARED SIZE TOO SMALL", RP_ERR_SIZE_MISMATCH);
            finish(s, r, rp_inflate_exact(z[RP_PAY_TEXT], L, out, N - 1));

            r = add(s, "T11E", "DECLARED SIZE TOO LARGE", RP_ERR_SIZE_MISMATCH);
            finish(s, r, rp_inflate_exact(z[RP_PAY_TEXT], L, out, N + 1));
            rp_free(bad);
        }
    }
    {   /* T11F: decompression bomb is refused at open, before any allocation */
        rp_mem_stats before = rp_mem_get();
        rp_err e;
        size_t grew;
        rp_mem_reset_peak();
        r = add(s, "T11F", "DECLARED 2GB ENTRY", RP_ERR_LIMIT);
        e = open_close("bomb.vrp", NULL);
        grew = rp_mem_get().peak - before.current;
        snprintf(r->detail, sizeof(r->detail), "bomb.vrp  peak alloc %u B", (unsigned)grew);
        finish(s, r, e);
    }

    if (progress) progress("INFLATE SPEED");
    for (k = 0; k < RP_PAY_KINDS; k++) {
        rp_err e = RP_OK; double t0; int rep;
        snprintf(id, sizeof(id), "T12%c", 'A' + k);
        snprintf(nm, sizeof(nm), "INFLATE SPEED %s", kind_names[k]);
        r = add(s, id, nm, RP_OK);
        if (!z[k]) { finish(s, r, RP_ERR_NOMEM); continue; }
        t0 = rp_now_ms();
        for (rep = 0; rep < 4 && e == RP_OK; rep++)
            e = rp_inflate_exact(z[k], zl[k], out, N);
        r->ms = (rp_now_ms() - t0) / 4.0;
        r->mbps = mbps((double)N, r->ms);
        r->check = (uint32_t)zl[k];
        snprintf(r->detail, sizeof(r->detail), "%.1f MB/s output  %.2f ms per MB", r->mbps, r->ms);
        finish(s, r, e);
    }
    for (k = 0; k < RP_PAY_KINDS; k++) if (z[k]) rp_free(z[k]);
    rp_free(raw);
    rp_free(out);
}

void rp_suite_run(rp_suite *s, const rp_fixture_set *fx, rp_progress_fn progress)
{
    rp_result *r;
    memset(s, 0, sizeof(*s));
    rp_mem_snapshot_take(&s->mem_before);

    if (fx->status != RP_OK) {
        r = add(s, "T0", "FIXTURE GENERATION", RP_OK);
        snprintf(r->detail, sizeof(r->detail), "failed at %s", fx->failed_file);
        finish(s, r, fx->status);
        return;
    }
    t_io(s, fx, progress);
    t_container(s, progress);
    t_pc_fixtures(s, progress);
    t_zlib(s, progress);

    /* T13: all tracked memory released; peak; free memory; largest malloc */
    if (progress) progress("MEMORY");
    s->stats_end = rp_mem_get();
    r = add(s, "T13", "NO LEAKED BYTES", RP_OK);
    /* snapshot first: the probe's large allocations would otherwise grow the
     * reported heap figures */
    rp_mem_snapshot_take(&s->mem_after);
    s->probe_max_bytes = rp_mem_probe_max(256u * 1024u * 1024u);
    snprintf(r->detail, sizeof(r->detail), "leaked %u B  peak %u KB  allocs %u  max malloc %u MB",
             (unsigned)s->stats_end.current, (unsigned)(s->stats_end.peak / 1024u),
             (unsigned)s->stats_end.allocs, (unsigned)(s->probe_max_bytes >> 20));
    r->check = (uint32_t)s->stats_end.current;
    finish(s, r, s->stats_end.current == 0 ? RP_OK : RP_ERR_NOMEM);
}

int rp_suite_compare(const rp_suite *runs, int n, char *detail, size_t detail_len,
                     double *ms_min, double *ms_max)
{
    int i, k, same = 1, diffs = 0;
    double total_min = 0, total_max = 0;
    for (k = 0; k < n; k++) {
        double t = 0;
        for (i = 0; i < runs[k].count; i++) t += runs[k].r[i].ms;
        if (k == 0 || t < total_min) total_min = t;
        if (k == 0 || t > total_max) total_max = t;
        if (k == 0) continue;
        if (runs[k].count != runs[0].count) { same = 0; diffs++; continue; }
        for (i = 0; i < runs[0].count; i++) {
            const rp_result *a = &runs[0].r[i], *b = &runs[k].r[i];
            /* T13 'check' is leaked bytes; T2-T4 checks are CRCs; all must match */
            if (a->pass != b->pass || a->actual != b->actual || a->check != b->check) { same = 0; diffs++; }
        }
    }
    if (ms_min) *ms_min = total_min;
    if (ms_max) *ms_max = total_max;
    snprintf(detail, detail_len, "%d runs  differences %d  timed total %.0f-%.0f ms",
             n, diffs, total_min, total_max);
    return same;
}
