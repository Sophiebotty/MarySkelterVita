/*
 * gp_tests.c - runs the PC-defined cases and the on-device measurements.
 *
 * All files come from the fixture allowlist (gp_common.h). All data is
 * synthetic; results say nothing about real Finale archives.
 */
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "gp_csh.h"
#include "gp_gdat.h"
#include "gp_manifest.h"
#include "gp_tests.h"
#include "rp_inflate.h"
#include "rp_io.h"
#include "rp_mem.h"
#include "rp_time.h"

#define MB (1024.0 * 1024.0)
#define OPEN_REPEATS 10

static gp_manifest s_m;           /* ~30 KB: kept out of the stack */

static gp_result *add(gp_suite *s, const char *id, const char *name, gp_mode mode, gp_status expect)
{
    gp_result *r;
    if (s->count >= GP_MAX_RESULTS) r = &s->r[GP_MAX_RESULTS - 1];   /* never expected */
    else r = &s->r[s->count++];
    memset(r, 0, sizeof(*r));
    snprintf(r->id, sizeof(r->id), "%s", id);
    snprintf(r->name, sizeof(r->name), "%s", name);
    snprintf(r->mode, sizeof(r->mode), "%s", mode == GP_LENIENT ? "L" : "S");
    r->expect = expect;
    r->actual = RP_ERR_ARG;
    return r;
}

static uint32_t xorshift(uint32_t *x)
{
    *x ^= *x << 13;
    *x ^= *x >> 17;
    *x ^= *x << 5;
    return *x;
}

/* ------------------------------------------------------------ load all -- */
typedef struct {
    uint32_t       count;
    uint32_t       list_crc;      /* CRC-32 over the payload CRCs (LE, index order) */
    uint32_t       obs;
    gp_load_timing t;
    double         total_ms;
    char           why[80];
} load_result;

static gp_status check_entry(const gp_mfile *mf, uint32_t i, const gp_wrap_info *w, uint32_t crc,
                             char *why, size_t why_len)
{
    const gp_mentry *me;
    if (!mf || mf->n_entries < 0 || i >= (uint32_t)mf->n_entries) {
        snprintf(why, why_len, "no manifest row for entry %u", (unsigned)i);
        return RP_ERR_SIZE_MISMATCH;
    }
    me = &s_m.entries[mf->first_entry + i];
    if ((int64_t)w->usize != me->usize || (int64_t)w->csize != me->csize) {
        snprintf(why, why_len, "entry %u sizes %u/%u vs PC %lld/%lld", (unsigned)i, (unsigned)w->usize,
                 (unsigned)w->csize, (long long)me->usize, (long long)me->csize);
        return RP_ERR_SIZE_MISMATCH;
    }
    if (crc != me->crc) {
        snprintf(why, why_len, "entry %u crc %08x vs PC %08x", (unsigned)i, (unsigned)crc, (unsigned)me->crc);
        return RP_ERR_CRC;
    }
    return RP_OK;
}

/* Loads every entry (index order, or a fixed shuffle), verifying each one
 * against the manifest. stream_only: read the streams without inflating. */
static gp_status load_all(const char *file, gp_mode mode, int shuffled, int stream_only, load_result *lr)
{
    const gp_mfile *mf = gp_manifest_file(&s_m, file);
    char path[96];
    gp_gdat a;
    uint32_t *order = NULL, *crcs = NULL, i, seed = 0x9E3779B9u;
    gp_status st;
    double t0;

    memset(lr, 0, sizeof(*lr));
    gp_manifest_path(&s_m, file, path, sizeof(path));
    t0 = rp_now_ms();
    st = gp_gdat_open(&a, path, mode);
    if (st != RP_OK) {
        snprintf(lr->why, sizeof(lr->why), "open: %s", gp_status_name(st));
        return st;
    }
    lr->obs = a.obs;
    lr->count = a.count;
    if (!mf || mf->n_entries != (int32_t)a.count) {
        snprintf(lr->why, sizeof(lr->why), "entry count differs from PC manifest");
        gp_gdat_close(&a);
        return RP_ERR_SIZE_MISMATCH;
    }
    if (a.count) {
        order = (uint32_t *)rp_malloc(a.count * sizeof(uint32_t));
        crcs = (uint32_t *)rp_malloc(a.count * sizeof(uint32_t));
        if (!order || !crcs) { st = RP_ERR_NOMEM; goto done; }
    }
    for (i = 0; i < a.count; i++) order[i] = i;
    if (shuffled)
        for (i = a.count; i > 1; i--) {
            uint32_t j = xorshift(&seed) % i, tmp = order[i - 1];
            order[i - 1] = order[j];
            order[j] = tmp;
        }
    for (i = 0; i < a.count; i++) {
        uint32_t k = order[i];
        uint8_t *out = NULL;
        size_t len = 0;
        if (stream_only) {
            double r0 = rp_now_ms();
            st = gp_wrap_read_stream(a.fp, a.file_size, a.idx[k].offset, a.idx[k].size, mode, &out, &len);
            lr->t.read_ms += rp_now_ms() - r0;
            if (st == RP_OK) {
                const gp_mentry *me = &s_m.entries[mf->first_entry + k];
                lr->t.read_bytes += len;
                crcs[k] = rp_crc32(out, len);
                if ((int64_t)len != me->csize) {
                    snprintf(lr->why, sizeof(lr->why), "entry %u stream %u vs PC %lld", (unsigned)k,
                             (unsigned)len, (long long)me->csize);
                    st = RP_ERR_SIZE_MISMATCH;
                }
            }
        } else {
            gp_wrap_info w;
            st = gp_gdat_load(&a, k, &w, &out, &len, &lr->t);
            if (st == RP_OK) {
                lr->obs |= w.obs;
                crcs[k] = rp_crc32(out, len);
                st = check_entry(mf, k, &w, crcs[k], lr->why, sizeof(lr->why));
            } else {
                snprintf(lr->why, sizeof(lr->why), "entry %u: %s", (unsigned)k, gp_status_name(st));
            }
        }
        rp_free(out);
        if (st != RP_OK) goto done;
    }
    {
        uint8_t le[4];
        uLong c = crc32(0L, Z_NULL, 0);
        for (i = 0; i < a.count; i++) {
            gp_wr_le32(le, crcs[i]);
            c = crc32(c, le, 4);
        }
        lr->list_crc = (uint32_t)c;
    }
done:
    lr->total_ms = rp_now_ms() - t0;
    rp_free(order);
    rp_free(crcs);
    gp_gdat_close(&a);
    return st;
}

/* --------------------------------------------------------------- cases -- */
static void judge(gp_result *r, const gp_mcase *c, int32_t count)
{
    if (c->expect == GP_EXPECT_ANY) {
        r->pass = r->actual != RP_OK;
        return;
    }
    r->pass = r->actual == c->expect;
    if (r->pass && c->expect == RP_OK) {
        if (r->obs != c->obs) r->pass = 0;
        if (c->count >= 0 && count != c->count) r->pass = 0;
        if (c->has_check && r->check != c->check) r->pass = 0;
    }
}

static void run_case(gp_suite *s, const gp_mcase *c)
{
    gp_result *r = add(s, c->id, c->name, c->mode, c->expect);
    char path[96], pair[96], obs[64];
    int32_t count = -1;
    double t0 = rp_now_ms();

    r->expect_obs = c->obs;
    gp_manifest_path(&s_m, c->file, path, sizeof(path));

    if (strcmp(c->op, "path_open") == 0) {
        FILE *fp = NULL;
        uint64_t sz;
        r->actual = gp_open_checked(c->arg, &fp, &sz);
        if (fp) fclose(fp);
        snprintf(r->detail, sizeof(r->detail), "%s", c->arg);
    } else if (strcmp(c->op, "gdat_open") == 0) {
        gp_gdat a;
        r->actual = gp_gdat_open(&a, path, c->mode);
        if (r->actual == RP_OK) {
            r->obs = a.obs;
            count = (int32_t)a.count;
            r->check = gp_gdat_index_crc(&a);
            gp_gdat_close(&a);
        }
    } else if (strcmp(c->op, "entry_load") == 0) {
        gp_gdat a;
        r->actual = gp_gdat_open(&a, path, c->mode);
        if (r->actual == RP_OK) {
            uint32_t i = (uint32_t)strtoul(c->arg, NULL, 10);
            gp_wrap_info w;
            uint8_t *out = NULL;
            size_t len = 0;
            r->obs = a.obs;
            r->actual = gp_gdat_load(&a, i, &w, &out, &len, NULL);
            if (r->actual == RP_OK) {
                char why[80] = "";
                r->obs |= w.obs;
                r->check = rp_crc32(out, len);
                r->actual = check_entry(gp_manifest_file(&s_m, c->file), i, &w, r->check, why, sizeof(why));
                /* the CRC_COMPARATOR case carries a deliberately wrong expected CRC */
                if (r->actual == RP_OK && c->has_check && r->check != c->check) r->actual = RP_ERR_CRC;
                if (r->actual == RP_ERR_CRC && c->expect == RP_ERR_CRC) why[0] = '\0';
                snprintf(r->detail, sizeof(r->detail), "tag %02x%02x%02x%02x %u->%u %s", w.tag[0], w.tag[1],
                         w.tag[2], w.tag[3], (unsigned)w.csize, (unsigned)w.usize, why);
            }
            rp_free(out);
            gp_gdat_close(&a);
        }
    } else if (strcmp(c->op, "load_seq") == 0 || strcmp(c->op, "load_shuf") == 0) {
        load_result lr;
        /* Correction 6: compare the whole op name. The earlier c->op[5] == 's' test was
         * true for both "load_seq" and "load_shuf", so every LOAD_SEQ case ran shuffled. */
        r->actual = load_all(c->file, c->mode, strcmp(c->op, "load_shuf") == 0, 0, &lr);
        if (r->actual == RP_OK) {
            r->obs = lr.obs;
            count = (int32_t)lr.count;
            r->check = lr.list_crc;
        }
        snprintf(r->detail, sizeof(r->detail), "%u entries %s", (unsigned)lr.count, lr.why);
    } else if (strcmp(c->op, "csh_check") == 0) {
        gp_csh_info ci;
        gp_manifest_path(&s_m, c->arg, pair, sizeof(pair));
        r->actual = gp_csh_check(path, pair, c->mode, &ci);
        if (r->actual == RP_OK) {
            r->obs = ci.obs;
            count = (int32_t)ci.pair_count;
            r->check = ci.records_crc;
        }
        snprintf(r->detail, sizeof(r->detail), "fields %u/%u/%u 2B=%u usize %u pair %u (records opaque)",
                 (unsigned)ci.field[0], (unsigned)ci.field[1], (unsigned)ci.field[2], (unsigned)ci.byte_2b,
                 (unsigned)ci.w.usize, (unsigned)ci.pair_count);
    } else {
        r->actual = RP_ERR_ARG;
        snprintf(r->detail, sizeof(r->detail), "unknown op %s", c->op);
    }
    r->ms = rp_now_ms() - t0;
    judge(r, c, count);
    if (r->detail[0] == '\0' || r->obs) {
        size_t n = strlen(r->detail);
        gp_obs_names(r->obs, obs, sizeof(obs));
        snprintf(r->detail + n, sizeof(r->detail) - n, "%sobs %s count %ld", n ? " " : "", obs, (long)count);
    }
    if (r->actual != RP_OK) r->check = 0;     /* only successful results carry a check value */
}

/* -------------------------------------------------------- performance -- */
static void perf_open(gp_suite *s, const char *id, const char *name, const char *file, uint32_t want)
{
    gp_result *r = add(s, id, name, GP_STRICT, RP_OK);
    char path[96];
    double first = 0, rest = 0;
    int k;
    gp_manifest_path(&s_m, file, path, sizeof(path));
    r->actual = RP_OK;
    for (k = 0; k < OPEN_REPEATS && r->actual == RP_OK; k++) {
        gp_gdat a;
        double t0 = rp_now_ms(), dt;
        r->actual = gp_gdat_open(&a, path, GP_STRICT);
        dt = rp_now_ms() - t0;
        if (r->actual != RP_OK) break;
        if (a.count != want) r->actual = RP_ERR_SIZE_MISMATCH;
        r->check = gp_gdat_index_crc(&a);
        gp_gdat_close(&a);
        if (k == 0) first = dt; else rest += dt;
    }
    r->ms = first;
    r->pass = r->actual == RP_OK;
    snprintf(r->detail, sizeof(r->detail), "%u entries: first %.3f ms, next %d avg %.3f ms", (unsigned)want,
             first, OPEN_REPEATS - 1, rest / (OPEN_REPEATS - 1));
}

/* Folds the allocator's peak since the last rp_mem_reset_peak() into the
 * iteration-wide tracked peak. Called before every reset, so no interval of
 * the iteration is lost. Reads counters only; allocates nothing. */
static void note_suite_peak(gp_suite *s)
{
    rp_mem_stats ms = rp_mem_get();
    if (ms.peak > s->peak_suite_bytes) s->peak_suite_bytes = ms.peak;
}

static void perf_load(gp_suite *s, const char *id, const char *name, const char *file, int shuffled,
                      int stream_only, load_result *keep)
{
    gp_result *r = add(s, id, name, GP_STRICT, RP_OK);
    load_result lr;
    rp_mem_stats ms;
    note_suite_peak(s);
    rp_mem_reset_peak();
    r->actual = load_all(file, GP_STRICT, shuffled, stream_only, &lr);
    ms = rp_mem_get();
    if (ms.peak > s->peak_load_bytes) s->peak_load_bytes = ms.peak;
    note_suite_peak(s);
    r->ms = lr.total_ms;
    r->check = r->actual == RP_OK ? lr.list_crc : 0;
    r->pass = r->actual == RP_OK;
    if (stream_only)
        r->mbps = lr.total_ms > 0 ? (lr.t.read_bytes / MB) / (lr.total_ms / 1000.0) : 0;
    else
        r->mbps = lr.total_ms > 0 ? (lr.t.raw_bytes / MB) / (lr.total_ms / 1000.0) : 0;
    /* load_all() fills lr.why only on failure, so the measurements and the
     * failure reason are reported separately: each fits detail[] in full
     * (a failure line is at most 28 + 79 characters). */
    if (r->actual != RP_OK) {
        snprintf(r->detail, sizeof(r->detail), "%u entries; failed: %s", (unsigned)lr.count,
                 lr.why[0] ? lr.why : "no reason recorded (see status)");
    } else if (stream_only) {
        snprintf(r->detail, sizeof(r->detail), "%u entries %.2f MB stored, read-only, peak %u KB",
                 (unsigned)lr.count, lr.t.read_bytes / MB, (unsigned)(ms.peak / 1024));
    } else {
        snprintf(r->detail, sizeof(r->detail),
                 "%u entries %.2f MB out; read %.0f ms (%.1f MB/s) inflate %.0f ms; peak %u KB",
                 (unsigned)lr.count, lr.t.raw_bytes / MB, lr.t.read_ms,
                 lr.t.read_ms > 0 ? (lr.t.read_bytes / MB) / (lr.t.read_ms / 1000.0) : 0.0, lr.t.inflate_ms,
                 (unsigned)(ms.peak / 1024));
    }
    if (keep) *keep = lr;
}

/* ---------------------------------------------------------------- suite -- */
static void integrity(gp_suite *s, gp_progress_fn progress)
{
    gp_result *r = add(s, "G0-01", "FIXTURE_INTEGRITY", GP_STRICT, RP_OK);
    char why[96];
    double t0 = rp_now_ms();
    uint64_t total = 0;
    int i, bad = 0;

    r->actual = gp_manifest_load(&s_m, why, sizeof(why));
    snprintf(s->fixture_dir, sizeof(s->fixture_dir), "%s", s_m.dir);
    if (r->actual != RP_OK) {
        snprintf(r->detail, sizeof(r->detail), "manifest: %s", why);
        /* The log prints this as "Manifest: <reason>"; copy the reason itself
         * (same 96-byte size as manifest_note) rather than the longer detail. */
        snprintf(s->manifest_note, sizeof(s->manifest_note), "%s", why);
        return;
    }
    if (progress) progress("CHECKING FIXTURES");
    for (i = 0; i < s_m.n_files; i++) {
        char path[96];
        uint32_t crc = 0;
        uint64_t len = 0;
        gp_status st;
        gp_manifest_path(&s_m, s_m.files[i].name, path, sizeof(path));
        st = gp_path_check(path);
        if (st == RP_OK) st = rp_crc_file_chunked(path, 64u * 1024u, &crc, &len);
        total += len;
        if (st != RP_OK || len != s_m.files[i].size || crc != s_m.files[i].crc) {
            if (!bad) snprintf(r->detail, sizeof(r->detail), "%s: %s", s_m.files[i].name, gp_status_name(st));
            bad++;
            r->actual = st != RP_OK ? st : RP_ERR_CRC;
        }
    }
    r->ms = rp_now_ms() - t0;
    r->mbps = r->ms > 0 ? (total / MB) / (r->ms / 1000.0) : 0;
    r->pass = r->actual == RP_OK;
    r->check = (uint32_t)s_m.n_files;
    if (!bad)
        snprintf(r->detail, sizeof(r->detail), "%d files, %d entries, %d cases from %s", s_m.n_files,
                 (int)s_m.n_entries, s_m.n_cases, s_m.dir);
    snprintf(s->manifest_note, sizeof(s->manifest_note), "%d files, %d entry rows, %d cases", s_m.n_files,
             (int)s_m.n_entries, s_m.n_cases);
}

void gp_suite_run(gp_suite *s, gp_progress_fn progress)
{
    load_result seq;
    int seq_idx = -1;
    gp_result *r;
    rp_mem_stats st;
    char msg[64];
    int i;

    memset(s, 0, sizeof(*s));
    rp_mem_snapshot_take(&s->mem_before);
    rp_mem_reset_peak();          /* the iteration-wide peak starts here */

    integrity(s, progress);
    if (s->r[0].actual == RP_OK) {
        for (i = 0; i < s_m.n_cases; i++) {
            if (progress && (i % 8) == 0) {
                snprintf(msg, sizeof(msg), "CASE %d/%d", i + 1, s_m.n_cases);
                progress(msg);
            }
            run_case(s, &s_m.cases[i]);
        }

        if (progress) progress("G6 OPEN / INDEX");
        perf_open(s, "G6-01", "OPEN_INDEX_1024", "perf.gdat", 1024);
        perf_open(s, "G6-02", "OPEN_INDEX_4096", "index4096.gdat", 4096);
        if (progress) progress("G6 LOAD SEQUENTIAL");
        seq_idx = s->count;
        perf_load(s, "G6-03", "LOAD_SEQ_PERF", "perf.gdat", 0, 0, &seq);
        if (progress) progress("G6 LOAD RANDOM");
        perf_load(s, "G6-04", "LOAD_RANDOM_PERF", "perf.gdat", 1, 0, NULL);
        if (progress) progress("G6 READ STREAMS");
        perf_load(s, "G6-05", "READ_STREAMS_SEQ", "perf.gdat", 0, 1, NULL);
        perf_load(s, "G6-06", "READ_STREAMS_RANDOM", "perf.gdat", 1, 1, NULL);

        r = add(s, "G6-07", "INFLATE_ONLY_RATE", GP_STRICT, RP_OK);   /* from G6-03's timing */
        r->actual = s->r[seq_idx].actual;
        r->pass = r->actual == RP_OK;
        r->ms = seq.t.inflate_ms;
        r->mbps = seq.t.inflate_ms > 0 ? (seq.t.raw_bytes / MB) / (seq.t.inflate_ms / 1000.0) : 0;
        r->check = r->pass ? seq.list_crc : 0;
        snprintf(r->detail, sizeof(r->detail), "inflate time within G6-03: %.2f MB in -> %.2f MB out",
                 seq.t.read_bytes / MB, seq.t.raw_bytes / MB);

        if (progress) progress("G6 SMALL ENTRIES");
        perf_load(s, "G6-08", "LOAD_4096_SMALL", "index4096.gdat", 0, 0, NULL);
        r = &s->r[s->count - 1];
        if (r->ms > 0) {
            size_t n = strlen(r->detail);
            snprintf(r->detail + n, sizeof(r->detail) - n, " %.0f entries/s", 4096.0 / (r->ms / 1000.0));
        }
    }

    gp_manifest_free(&s_m);

    /* G6-09: passive measurements only (no allocation probe). Passes when no
     * tracked bytes remain; records the tracked peaks, and the heap and
     * free-memory snapshots taken before and after the iteration. */
    r = add(s, "G6-09", "MEMORY", GP_STRICT, RP_OK);
    note_suite_peak(s);
    st = rp_mem_get();
    s->stats_end = st;
    rp_mem_snapshot_take(&s->mem_after);
    r->actual = st.current == 0 ? RP_OK : RP_ERR_NOMEM;
    r->pass = r->actual == RP_OK;
    r->check = (uint32_t)st.current;
    snprintf(r->detail, sizeof(r->detail),
             "leaked %u B; tracked peak: run %u KB, perf loads %u KB; refused since start %u; no probe",
             (unsigned)st.current, (unsigned)(s->peak_suite_bytes / 1024), (unsigned)(s->peak_load_bytes / 1024),
             (unsigned)st.failed);

    for (i = 0; i < s->count; i++) s->passed += s->r[i].pass;
}

int gp_suite_compare(const gp_suite *runs, int n, char *detail, size_t detail_len)
{
    int k, i, diffs = 0;
    const char *first = NULL;
    for (k = 1; k < n; k++) {
        if (runs[k].count != runs[0].count) {
            snprintf(detail, detail_len, "run %d has %d results vs %d", k + 1, runs[k].count, runs[0].count);
            return 0;
        }
        for (i = 0; i < runs[0].count; i++) {
            const gp_result *a = &runs[0].r[i], *b = &runs[k].r[i];
            if (strcmp(a->id, b->id) != 0 || a->pass != b->pass || a->actual != b->actual || a->obs != b->obs ||
                a->check != b->check) {
                if (!first) first = a->id;
                diffs++;
            }
        }
    }
    if (diffs)
        snprintf(detail, detail_len, "%d differences, first at %s", diffs, first);
    else
        snprintf(detail, detail_len, "%d runs x %d results identical (pass, status, obs, check)", n,
                 runs[0].count);
    return diffs == 0;
}
