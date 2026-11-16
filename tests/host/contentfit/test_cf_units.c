/*
 * test_cf_units.c - ContentFit unit tests on the PC (no SDL, no GPU).
 *
 *  1. statistics: fixed cases + random sets written to a file that
 *     tools/analyze_contentfit_results.py --stats-selftest recomputes
 *  2. memory pre-check: exact boundaries, unknown pools, guard, spill, baseline
 *  3. paths: write/read allowlists and no-overwrite creation
 *  4. cf_zinflate_exact == rp_inflate_exact on every fixture entry and on
 *     damaged streams; payload checks reject damaged payloads
 *  5. ring reader: worker delivers every entry of every pack, in order, and
 *     the worker-inflate mode produces the same payload CRCs
 *  6. generated content is deterministic (same CRCs twice)
 *
 * Run from a directory that contains "app0:fixtures" (link to
 * contentfit/fixtures) and "ux0:data". argv[1]: stats self-test output file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "cf_budget.h"
#include "cf_fixdefs.h"
#include "cf_geom.h"
#include "cf_pack.h"
#include "cf_paths.h"
#include "cf_plat.h"
#include "cf_stats.h"
#include "cf_zinflate.h"
#include "gp_gdat.h"
#include "rp_inflate.h"
#include "rp_io.h"
#include "rp_mem.h"

static int g_fail, g_checks;

#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ------------------------------------------------------------- stats -- */
static uint32_t rng = 0x12345678u;
static uint32_t xs(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void test_stats(const char *out_path)
{
    static float v[20000], scratch[20000];
    cf_frame_stats s, off;
    FILE *f;
    int set, i;

    /* fixed: 1..100 -> nearest rank P50 = 50, P95 = 95, P99 = 99 */
    for (i = 0; i < 100; i++) v[i] = (float)(i + 1);
    cf_stats_compute(v, 100, scratch, &s);
    CHECK(s.n == 100 && s.p50 == 50.0 && s.p95 == 95.0 && s.p99 == 99.0 && s.min == 1.0 && s.max == 100.0,
          "nearest rank 1..100: p50 %g p95 %g p99 %g", s.p50, s.p95, s.p99);
    CHECK(s.avg == 50.5, "avg %g", s.avg);
    CHECK(s.over_16_7 == 84 && s.over_34 == 66 && s.over_51 == 49, "threshold counts %d %d %d", s.over_16_7,
          s.over_34, s.over_51);
    cf_stats_compute(v, 1, scratch, &s);
    CHECK(s.n == 1 && s.p50 == 1.0 && s.p99 == 1.0, "single sample");
    cf_stats_compute(v, 0, scratch, &s);
    CHECK(s.n == 0, "empty");

    /* 30 fps rules: 300 frames at 33.3 ms with k slow frames */
    for (i = 0; i < 300; i++) v[i] = 33.3f;
    cf_stats_compute(v, 300, scratch, &s);
    CHECK(cf_stable30(&s), "steady 33.3 ms is stable");
    v[10] = v[20] = v[30] = 50.0f;                     /* 3 of 300 = 1% over 34 ms: still stable */
    cf_stats_compute(v, 300, scratch, &s);
    CHECK(cf_stable30(&s) && s.over_34 == 3, "1%% misses allowed (over_34 %d)", s.over_34);
    v[40] = 50.0f;                                     /* 4 of 300: not stable */
    cf_stats_compute(v, 300, scratch, &s);
    CHECK(!cf_stable30(&s), "4 of 300 misses is not stable");
    for (i = 0; i < 300; i++) v[i] = 33.3f;
    v[5] = 51.5f;                                      /* one double miss: not stable */
    cf_stats_compute(v, 300, scratch, &s);
    CHECK(!cf_stable30(&s) && s.over_51 == 1, "a frame over 51 ms is not stable");
    for (i = 0; i < 300; i++) v[i] = 33.3f;
    cf_stats_compute(v, 300, scratch, &s);
    /* samples are floats: 16.7f is 16.7000008 > 16.7, so test just inside and outside */
    for (i = 0; i < 100; i++) v[i] = 16.69f;
    cf_stats_compute(v, 100, scratch, &off);
    CHECK(cf_classify(&s, &off) == CF_FIT_30, "16.69 ms cost fits 30");
    for (i = 0; i < 100; i++) v[i] = 16.7f;
    cf_stats_compute(v, 100, scratch, &off);
    CHECK(cf_classify(&s, &off) == CF_TIGHT_30, "16.7f (just above 16.7) is tight");
    for (i = 0; i < 100; i++) v[i] = 16.8f;
    cf_stats_compute(v, 100, scratch, &off);
    CHECK(cf_classify(&s, &off) == CF_TIGHT_30, "16.8 ms cost is tight");
    for (i = 0; i < 100; i++) v[i] = 25.0f;
    cf_stats_compute(v, 100, scratch, &off);
    CHECK(cf_classify(&s, &off) == CF_TIGHT_30, "25.0 ms cost is tight");
    for (i = 0; i < 100; i++) v[i] = 25.1f;
    cf_stats_compute(v, 100, scratch, &off);
    CHECK(cf_classify(&s, &off) == CF_NOT_30, "25.1 ms cost does not fit");
    for (i = 0; i < 100; i++) v[i] = 8.29f;
    cf_stats_compute(v, 100, scratch, &off);
    CHECK(cf_60_candidate(&off), "8.29 ms is a 60 fps candidate");
    for (i = 0; i < 100; i++) v[i] = 8.31f;
    cf_stats_compute(v, 100, scratch, &off);
    CHECK(!cf_60_candidate(&off), "8.31 ms is not");
    off.n = 0;
    CHECK(cf_classify(&s, &off) == CF_FIT_SKIPPED, "missing run -> skipped");

    /* random sets for the Python reference */
    f = fopen(out_path, "w");
    CHECK(f != NULL, "cannot write %s", out_path);
    if (!f) return;
    for (set = 0; set < 40; set++) {
        int n = 1 + (int)(xs() % 3000);
        for (i = 0; i < n; i++) {
            uint32_t r = xs();
            float base = (set % 3 == 0) ? 33.3f : ((set % 3 == 1) ? 4.0f : 16.7f);
            v[i] = base + (float)(r % 4000) / 1000.0f - ((r & 0x10000) ? 2.0f : 0.0f) + ((r % 97) == 0 ? 20.0f : 0.0f);
        }
        cf_stats_compute(v, n, scratch, &s);
        fprintf(f, "SET %d %d\n", set, n);
        for (i = 0; i < n; i++) fprintf(f, "%.9g\n", (double)v[i]);
        fprintf(f, "STATS %d %.9f %.9g %.9g %.9g %.9g %.9g %d %d %d %d\n", s.n, s.avg, s.min, s.max, s.p50,
                s.p95, s.p99, s.over_16_7, s.over_34, s.over_51, cf_stable30(&s));
    }
    fclose(f);
}

/* ------------------------------------------------------------ budget -- */
static void pool(cf_pools *p, cf_pool_id id, uint64_t free_bytes)
{
    p->p[id].known = 1;
    p->p[id].total = free_bytes + CF_MIB;
    p->p[id].free = free_bytes;
}

static void test_budget(void)
{
    cf_pools p;
    cf_need n;
    char why[160];
    cf_pool_id where = CF_POOL_COUNT;
    uint64_t need = 64ull * CF_MIB, req = CF_MARGIN_VRAM + need + (need + 99) / 100;

    memset(&p, 0, sizeof(p));
    memset(&n, 0, sizeof(n));
    CHECK(cf_with_allowance(100) == 101 && cf_with_allowance(101) == 103 && cf_with_allowance(0) == 0,
          "allowance rounding");
    n.b[CF_POOL_VRAM] = need;
    pool(&p, CF_POOL_VRAM, req);
    CHECK(cf_precheck(&p, &n, why, sizeof(why)) == RP_OK, "exactly at the limit runs: %s", why);
    pool(&p, CF_POOL_VRAM, req - 1);
    CHECK(cf_precheck(&p, &n, why, sizeof(why)) == CF_ERR_SKIPPED, "one byte short is skipped");
    CHECK(strstr(why, "VRAM") != NULL, "reason names the pool: %s", why);
    pool(&p, CF_POOL_VRAM, CF_MARGIN_VRAM - 1);
    CHECK(cf_precheck(&p, &n, why, sizeof(why)) == CF_ERR_SKIPPED, "below the margin alone is skipped");
    p.p[CF_POOL_VRAM].known = 0;
    CHECK(cf_precheck(&p, &n, why, sizeof(why)) == CF_ERR_UNKNOWN_SIZE, "unknown pool is never used");

    /* the 96 MiB heap tier: 96 + 0.96 + 24 = 120.96 MiB */
    memset(&n, 0, sizeof(n));
    n.b[CF_POOL_HEAP] = 96ull * CF_MIB;
    pool(&p, CF_POOL_HEAP, CF_MARGIN_HEAP + cf_with_allowance(96ull * CF_MIB));
    CHECK(cf_precheck(&p, &n, why, sizeof(why)) == RP_OK, "96 MiB tier at 120.96 MiB free runs");
    pool(&p, CF_POOL_HEAP, CF_MARGIN_HEAP + cf_with_allowance(96ull * CF_MIB) - 1);
    CHECK(cf_precheck(&p, &n, why, sizeof(why)) == CF_ERR_SKIPPED, "96 MiB tier one byte short is skipped");
    pool(&p, CF_POOL_HEAP, 128ull * CF_MIB - 7ull * CF_MIB);
    CHECK(cf_precheck(&p, &n, why, sizeof(why)) == RP_OK, "128 MiB heap with 7 MiB in use fits the 96 MiB tier");
    pool(&p, CF_POOL_HEAP, 128ull * CF_MIB - 8ull * CF_MIB);
    CHECK(cf_precheck(&p, &n, why, sizeof(why)) == CF_ERR_SKIPPED, "128 MiB heap with 8 MiB in use skips it");
    p.p[CF_POOL_HEAP].known = 0;
    CHECK(cf_precheck(&p, &n, why, sizeof(why)) == CF_ERR_UNKNOWN_SIZE, "unknown heap: every heap tier skipped");

    /* several pools: the first refusing pool is reported */
    memset(&p, 0, sizeof(p));
    memset(&n, 0, sizeof(n));
    n.b[CF_POOL_VRAM] = CF_MIB;
    n.b[CF_POOL_KERNEL] = 128u * 1024u;
    pool(&p, CF_POOL_VRAM, 100ull * CF_MIB);
    pool(&p, CF_POOL_KERNEL, CF_MARGIN_KERNEL + 100u * 1024u);
    CHECK(cf_precheck(&p, &n, why, sizeof(why)) == CF_ERR_SKIPPED && strstr(why, "KERNEL"),
          "kernel margin protects thread stacks: %s", why);
    CHECK(cf_precheck(&p, NULL, why, sizeof(why)) == RP_ERR_ARG, "NULL need");

    /* guard */
    pool(&p, CF_POOL_HEAP, CF_MARGIN_HEAP);
    CHECK(cf_guard(&p, CF_POOL_HEAP, why, sizeof(why)) == RP_OK, "guard at the margin passes");
    pool(&p, CF_POOL_HEAP, CF_MARGIN_HEAP - 1);
    CHECK(cf_guard(&p, CF_POOL_HEAP, why, sizeof(why)) == CF_ERR_MARGIN, "guard below the margin stops");

    /* spill and baseline */
    {
        cf_pools a, b;
        memset(&a, 0, sizeof(a));
        pool(&a, CF_POOL_VRAM, 80ull * CF_MIB);
        pool(&a, CF_POOL_GLRAM, 60ull * CF_MIB);
        pool(&a, CF_POOL_HEAP, 100ull * CF_MIB);
        b = a;
        b.p[CF_POOL_VRAM].free -= CF_MIB;
        CHECK(!cf_spilled(&a, &b, CF_POOL_VRAM, CF_BASELINE_TOL, &where), "VRAM use is not a spill");
        b.p[CF_POOL_GLRAM].free -= CF_BASELINE_TOL;
        CHECK(!cf_spilled(&a, &b, CF_POOL_VRAM, CF_BASELINE_TOL, &where), "64 KiB in GL RAM tolerated");
        b.p[CF_POOL_GLRAM].free -= 1;
        CHECK(cf_spilled(&a, &b, CF_POOL_VRAM, CF_BASELINE_TOL, &where) && where == CF_POOL_GLRAM,
              "one byte past the tolerance is a spill into GLRAM");
        b = a;
        b.p[CF_POOL_HEAP].free -= CF_BASELINE_TOL;
        CHECK(cf_baseline_ok(&a, &b, CF_BASELINE_TOL, &where), "64 KiB below baseline is ok");
        b.p[CF_POOL_HEAP].free -= 1;
        CHECK(!cf_baseline_ok(&a, &b, CF_BASELINE_TOL, &where) && where == CF_POOL_HEAP, "dirty heap baseline");
    }
}

/* ------------------------------------------------------------- paths -- */
static void test_paths(void)
{
    static const char *const WRITE_GOOD[] = {"ux0:data/ContentFit/run001.csv", "ux0:data/ContentFit/work/io16.bin"};
    static const char *const WRITE_BAD[] = {"ux0:data/ContentFit/", "ux0:data/ContentFit", "ux0:data/GdatProto/run003.csv",
                                            "ux0:data/ResProto/x", "ux0:data/ContentFit/../GdatProto/a",
                                            "app0:fixtures/io16.bin", "ux0:app/VDNG00005/x", "", "ux0:data/ContentFitX/a"};
    static const char *const READ_GOOD[] = {"app0:fixtures/io16.bin", "ux0:data/ContentFit/work/io16.bin"};
    static const char *const READ_BAD[] = {"app0:fixtures/", "app0:fixtures/../eboot.bin", "ux0:data/ContentFit/run001.csv",
                                           "ux0:app/PCSG00000/data.dat", "ux0:data/GdatProto/x", "app0:eboot.bin",
                                           "ux0:data/ContentFit/work/"};
    unsigned i;
    FILE *f = NULL;
    cf_run_files rf;
    for (i = 0; i < sizeof(WRITE_GOOD) / sizeof(WRITE_GOOD[0]); i++)
        CHECK(cf_path_write_ok(WRITE_GOOD[i]), "write ok %s", WRITE_GOOD[i]);
    for (i = 0; i < sizeof(WRITE_BAD) / sizeof(WRITE_BAD[0]); i++)
        CHECK(!cf_path_write_ok(WRITE_BAD[i]), "write refused %s", WRITE_BAD[i]);
    for (i = 0; i < sizeof(READ_GOOD) / sizeof(READ_GOOD[0]); i++)
        CHECK(cf_path_read_ok(READ_GOOD[i]), "read ok %s", READ_GOOD[i]);
    for (i = 0; i < sizeof(READ_BAD) / sizeof(READ_BAD[0]); i++)
        CHECK(!cf_path_read_ok(READ_BAD[i]), "read refused %s", READ_BAD[i]);
    CHECK(!cf_path_write_ok(NULL) && !cf_path_read_ok(NULL), "NULL paths refused");

    /* vitaGL shader-cache redirect (prefix rewrite only) */
    {
        static const char *const SAME[] = {"ux0:data/shader_cachex", "ux0:data/shader_cach", "ux0:data/ContentFit/run001.csv",
                                           "app0:fixtures/io16.bin", "ux0:/data/shader_cache/v1", "ux0:data/Shader_cache/v1",
                                           "ux0:data/ContentFit/shader_cache/v1/f/a.gxp", ""};
        char out[128];
        unsigned k;
        CHECK(cf_redirect_vgl_cache("ux0:data/shader_cache", out, sizeof(out)) == 1 &&
              strcmp(out, "ux0:data/ContentFit/shader_cache") == 0, "cache root redirected: %s", out);
        CHECK(cf_redirect_vgl_cache("ux0:data/shader_cache/v3/f/0000ABCD-0123456789ABCDEF.gxp", out, sizeof(out)) == 1 &&
              strcmp(out, "ux0:data/ContentFit/shader_cache/v3/f/0000ABCD-0123456789ABCDEF.gxp") == 0,
              "cache file redirected: %s", out);
        CHECK(cf_path_write_ok(out), "redirected path is inside ContentFit's write allowlist");
        for (k = 0; k < sizeof(SAME) / sizeof(SAME[0]); k++) {
            strcpy(out, "untouched");
            CHECK(cf_redirect_vgl_cache(SAME[k], out, sizeof(out)) == 0 && strcmp(out, "untouched") == 0,
                  "not redirected: '%s'", SAME[k]);
        }
        CHECK(cf_redirect_vgl_cache(NULL, out, sizeof(out)) == 0, "NULL path not redirected");
        /* exactly fitting and one byte short */
        CHECK(cf_redirect_vgl_cache("ux0:data/shader_cache/v1", out, sizeof("ux0:data/ContentFit/shader_cache/v1")) == 1,
              "exact fit redirected");
        CHECK(cf_redirect_vgl_cache("ux0:data/shader_cache/v1", out, sizeof("ux0:data/ContentFit/shader_cache/v1") - 1) == -1,
              "one byte short is refused");
        CHECK(cf_redirect_vgl_cache("ux0:data/shader_cache/v1", NULL, 64) == -1, "no output buffer is refused");
    }
    CHECK(cf_raw_open("ux0:data/GdatProto/run001.csv") < 0, "raw open outside the allowlist refused");

    rp_mkdir("ux0:data/ContentFit");
    CHECK(cf_create_new("ux0:data/GdatProto/new.csv", &f) == CF_ERR_PATH && !f, "create outside allowlist refused");
    CHECK(cf_create_new("ux0:data/ContentFit/unit_new.txt", &f) == RP_OK && f, "create new file");
    if (f) { fputs("first\n", f); fclose(f); f = NULL; }
    CHECK(cf_create_new("ux0:data/ContentFit/unit_new.txt", &f) == CF_ERR_EXISTS && !f, "existing file not reopened");
    {
        char buf[16] = "";
        FILE *r = fopen("ux0:data/ContentFit/unit_new.txt", "rb");
        if (r) { if (!fgets(buf, sizeof(buf), r)) buf[0] = '\0'; fclose(r); }
        CHECK(strcmp(buf, "first\n") == 0, "existing file content unchanged");
    }
    /* run numbering: any one of the four files makes a number unavailable */
    cf_run_files_for(1, &rf);
    CHECK(strcmp(rf.csv, "ux0:data/ContentFit/run001.csv") == 0 &&
          strcmp(rf.progress, "ux0:data/ContentFit/run001_progress.txt") == 0, "run file names");
    CHECK(cf_run_pick(cf_exists_stat, NULL) == 1, "first free run is 1");
    f = fopen("ux0:data/ContentFit/run001_frames.csv", "wb");
    if (f) fclose(f);
    CHECK(cf_run_pick(cf_exists_stat, NULL) == 2, "run 1 taken by its frames file alone");
    f = fopen("ux0:data/ContentFit/run002_progress.txt", "wb");
    if (f) fclose(f);
    CHECK(cf_run_pick(cf_exists_stat, NULL) == 3, "run 2 taken by its progress file alone");
    remove("ux0:data/ContentFit/run001_frames.csv");     /* test scaffolding only (host temp dir) */
    remove("ux0:data/ContentFit/run002_progress.txt");
    remove("ux0:data/ContentFit/unit_new.txt");
}

/* -------------------------------------------------- inflate / payload -- */
static void test_inflate_and_payload(void)
{
    static uint8_t arena_mem[CF_ZARENA_BYTES];
    cf_zarena ar = {arena_mem, sizeof(arena_mem), 0};
    int pi;
    long compared = 0;
    for (pi = 0; pi < cf_fix_pack_count(); pi++) {
        const cf_fix_pack *fp = cf_fix_pack_get(pi);
        char path[64];
        gp_gdat a;
        uint32_t i;
        uLong list = crc32(0L, Z_NULL, 0);
        snprintf(path, sizeof(path), "%s%s", CF_FIX_PREFIX, fp->name);
        CHECK(gp_gdat_open(&a, path, GP_STRICT) == RP_OK, "open %s strictly", path);
        if (!a.fp) continue;
        CHECK(a.count == fp->entries, "%s entry count", fp->name);
        for (i = 0; i < a.count; i++) {
            uint8_t *reg = (uint8_t *)malloc(a.idx[i].size);
            gp_wrap_info w;
            uint8_t *x, *y;
            cf_payload p;
            uint8_t le[4];
            if (!reg) { CHECK(0, "malloc"); break; }
            CHECK(rp_read_at(a.fp, a.file_size, a.idx[i].offset, reg, a.idx[i].size) == RP_OK, "read region");
            CHECK(gp_wrap_parse(reg, a.idx[i].size, GP_STRICT, &w) == RP_OK, "%s entry %u wrapper", fp->name, (unsigned)i);
            x = (uint8_t *)malloc(w.usize);
            y = (uint8_t *)malloc(w.usize);
            if (x && y) {
                gp_status s1 = rp_inflate_exact(reg + GP_WRAP_TOTAL, w.csize, x, w.usize);
                gp_status s2 = cf_zinflate_exact(&ar, reg + GP_WRAP_TOTAL, w.csize, y, w.usize);
                CHECK(s1 == RP_OK && s2 == RP_OK && memcmp(x, y, w.usize) == 0, "%s entry %u inflate equal",
                      fp->name, (unsigned)i);
                CHECK(cf_payload_check(y, w.usize, i, 1, &p) == RP_OK, "%s entry %u payload", fp->name, (unsigned)i);
                gp_wr_le32(le, p.body_crc);
                list = crc32(list, le, 4);
                compared++;
                if (i == 0) {
                    /* damaged inputs: both inflaters must agree on the error */
                    uint8_t *bad = (uint8_t *)malloc(w.csize);
                    if (bad) {
                        memcpy(bad, reg + GP_WRAP_TOTAL, w.csize);
                        bad[w.csize - 1] ^= 0xFF;
                        s1 = rp_inflate_exact(bad, w.csize, x, w.usize);
                        s2 = cf_zinflate_exact(&ar, bad, w.csize, y, w.usize);
                        CHECK(s1 == s2 && s1 == RP_ERR_ZLIB_DATA, "adler flip: %d vs %d", s1, s2);
                        s1 = rp_inflate_exact(bad, w.csize - 40, x, w.usize);
                        s2 = cf_zinflate_exact(&ar, bad, w.csize - 40, y, w.usize);
                        CHECK(s1 == s2 && s1 == RP_ERR_TRUNCATED, "truncated: %d vs %d", s1, s2);
                        s1 = rp_inflate_exact(reg + GP_WRAP_TOTAL, w.csize, x, w.usize - 1);
                        s2 = cf_zinflate_exact(&ar, reg + GP_WRAP_TOTAL, w.csize, y, w.usize - 1);
                        CHECK(s1 == s2 && s1 == RP_ERR_SIZE_MISMATCH, "short output: %d vs %d", s1, s2);
                        free(bad);
                    }
                    {   /* tiny arena -> NOMEM, never a write outside it */
                        static uint8_t tiny[512];
                        cf_zarena t = {tiny, sizeof(tiny), 0};
                        CHECK(cf_zinflate_exact(&t, reg + GP_WRAP_TOTAL, w.csize, y, w.usize) == RP_ERR_NOMEM,
                              "tiny arena reports NOMEM");
                    }
                    /* payload damage */
                    memcpy(y, x, w.usize);
                    y[0] = 'X';
                    CHECK(cf_payload_check(y, w.usize, i, 1, NULL) == CF_ERR_PAYLOAD, "bad magic");
                    memcpy(y, x, w.usize);
                    CHECK(cf_payload_check(y, w.usize, i + 1, 1, NULL) == CF_ERR_PAYLOAD, "wrong index");
                    y[w.usize - 1] ^= 1;
                    CHECK(cf_payload_check(y, w.usize, i, 1, NULL) == CF_ERR_PAYLOAD, "body CRC");
                    CHECK(cf_payload_check(y, w.usize, i, 0, NULL) == RP_OK, "CRC not checked when asked");
                    CHECK(cf_payload_check(y, w.usize - 1, i, 0, NULL) == CF_ERR_PAYLOAD, "length");
                    CHECK(cf_payload_check(y, 31, i, 0, NULL) == CF_ERR_PAYLOAD, "shorter than header");
                }
            }
            free(x);
            free(y);
            free(reg);
        }
        CHECK((uint32_t)list == fp->list_crc, "%s list CRC %08x vs %08x", fp->name, (unsigned)list,
              (unsigned)fp->list_crc);
        gp_gdat_close(&a);
    }
    printf("  inflate compared on %ld entries\n", compared);
}

/* --------------------------------------------------------------- ring -- */
static void test_ring(void)
{
    int pi, mode;
    for (pi = 0; pi < cf_fix_pack_count(); pi++) {
        const cf_fix_pack *fp = cf_fix_pack_get(pi);
        for (mode = 0; mode < 2; mode++) {
            char path[64];
            gp_gdat a;
            cf_ring r;
            uint32_t i;
            uLong list = crc32(0L, Z_NULL, 0);
            snprintf(path, sizeof(path), "%s%s", CF_FIX_PREFIX, fp->name);
            if (gp_gdat_open(&a, path, GP_STRICT) != RP_OK) { CHECK(0, "open %s", path); continue; }
            CHECK(cf_ring_start(&r, path, &a, mode ? 2 : 3, fp->max_region, mode ? fp->max_usize : 0, mode, 0) == RP_OK,
                  "ring start %s mode %d", fp->name, mode);
            for (i = 0; i < a.count; i++) {
                cf_slot *s = cf_ring_take(&r, 1);
                uint8_t le[4];
                if (!s) { CHECK(0, "no slot"); break; }
                CHECK(s->entry == i && s->st == RP_OK, "%s slot %u entry %u status %d", fp->name, (unsigned)i,
                      (unsigned)s->entry, s->st);
                if (mode) {
                    gp_wr_le32(le, s->pay.body_crc);
                } else {
                    gp_wrap_info w;
                    uint8_t *out;
                    cf_payload p;
                    gp_wrap_parse(s->comp, s->region, GP_STRICT, &w);
                    out = (uint8_t *)malloc(w.usize);
                    CHECK(out && rp_inflate_exact(s->comp + GP_WRAP_TOTAL, w.csize, out, w.usize) == RP_OK &&
                          cf_payload_check(out, w.usize, i, 1, &p) == RP_OK, "pipe entry %u", (unsigned)i);
                    gp_wr_le32(le, out ? p.body_crc : 0u);
                    free(out);
                }
                list = crc32(list, le, 4);
                cf_ring_release(&r, s);
            }
            cf_ring_stop(&r);
            cf_ring_stop(&r);                         /* twice is safe */
            CHECK((uint32_t)list == fp->list_crc, "%s ring mode %d list CRC", fp->name, mode);
            gp_gdat_close(&a);
        }
    }
    /* looping stream stopped early, while the worker may be blocked */
    {
        const cf_fix_pack *fp = cf_fix_pack_get(1);
        char path[64];
        gp_gdat a;
        cf_ring r;
        int k;
        snprintf(path, sizeof(path), "%s%s", CF_FIX_PREFIX, fp->name);
        if (gp_gdat_open(&a, path, GP_STRICT) == RP_OK) {
            CHECK(cf_ring_start(&r, path, &a, 2, fp->max_region, fp->max_usize, 1, 1) == RP_OK, "loop ring");
            for (k = 0; k < (int)fp->entries + 7; k++) {
                cf_slot *s = cf_ring_take(&r, 1);
                CHECK(s && s->st == RP_OK && s->entry == (uint32_t)k % fp->entries, "loop slot %d", k);
                if (s) cf_ring_release(&r, s);
            }
            cf_ring_stop(&r);
            gp_gdat_close(&a);
        }
    }
    CHECK(rp_mem_get().current == 0, "ring leaves no tracked bytes (%lu)", (unsigned long)rp_mem_get().current);
}

/* ------------------------------------------------------- determinism -- */
static void test_geom(void)
{
    static cf_vtx v[(101) * (61)];
    static float w[101 * 61];
    static uint8_t px[1024 * 1024 * 4];
    uint32_t c1, c2;
    int a;
    /* sine table sanity: quadrant values and symmetry */
    CHECK(cf_isin(0) == 0 && cf_isin(1024) == 32767 && cf_isin(2048) == 0 && cf_isin(3072) == -32767,
          "sine quadrants %d %d %d %d", cf_isin(0), cf_isin(1024), cf_isin(2048), cf_isin(3072));
    for (a = 0; a < 4096; a += 7) CHECK(cf_isin(a) == -cf_isin(4096 - a) || a == 0, "odd symmetry at %d", a);
    cf_geom_ellipsoid(v, w, 100, 60, 0.55f, 1.2f, 0.55f, 200);
    c1 = rp_crc32((const uint8_t *)v, sizeof(v));
    cf_geom_ellipsoid(v, w, 100, 60, 0.55f, 1.2f, 0.55f, 200);
    c2 = rp_crc32((const uint8_t *)v, sizeof(v));
    CHECK(c1 == c2, "ellipsoid deterministic");
    cf_tex_rgba(px, 1024, 1024, 3000u, CF_STY_BACKDROP);
    c1 = rp_crc32(px, sizeof(px));
    cf_tex_rgba(px, 1024, 1024, 3000u, CF_STY_BACKDROP);
    CHECK(c1 == rp_crc32(px, sizeof(px)), "texture deterministic");
    CHECK(cf_tf_chain_bytes(CF_TF_RGBA8, 256, 256, 1) == 349524u, "RGBA8 256 chain %u",
          (unsigned)cf_tf_chain_bytes(CF_TF_RGBA8, 256, 256, 1));
    CHECK(cf_tf_level_bytes(CF_TF_DXT1, 1, 1) == 8u && cf_tf_level_bytes(CF_TF_DXT5, 2, 2) == 16u &&
          cf_tf_level_bytes(CF_TF_PVRTC4, 4, 4) == 32u && cf_tf_level_bytes(CF_TF_ETC1, 256, 256) == 32768u,
          "compressed level sizes");
}

int main(int argc, char **argv)
{
    printf("== statistics\n");
    test_stats(argc > 1 ? argv[1] : "stats_selftest.txt");
    printf("== memory pre-check\n");
    test_budget();
    printf("== paths\n");
    test_paths();
    printf("== inflate and payloads\n");
    test_inflate_and_payload();
    printf("== ring reader (worker thread)\n");
    test_ring();
    printf("== deterministic content\n");
    test_geom();
    printf("unit checks: %d, failures: %d\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
