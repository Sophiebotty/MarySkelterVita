/*
 * test_rs_loader.c - stage 10.4 PC tests of src/resource/rs_loader.c
 * (RESOURCE_PIPELINE.md sections 6.1, 7 and 8; VRS1 version 1).
 *
 * Run from a work directory whose app0:fixtures/ holds the stage 10.2
 * fixtures and is writable (tests/host/run_respipe_loader_tests.sh). Linked
 * with the unmodified ContentFit reader (cf_pack.c), the PC stand-in
 * rs_plat_pc.c, the 10.3 serial driver (rs_host_pipe.c) for comparison, and
 * -Wl,--wrap=rp_malloc,--wrap=rp_free (failure injection and a check that
 * every allocation and free happens on the main thread).
 *
 *   1. arguments, section 8.2 needs (worked example, every fixture);
 *   2. every valid pack with 1, 2, 3 and 4 slots: result, needs, every
 *      delivered resource byte-identical to gp_gdat_load reading it alone,
 *      nothing allocated while stepping, clean after release;
 *   3. the 50 section 10 cases: the loader's code, entry and stage equal the
 *      fixtures' and the serial driver's;
 *   4. the synthetic packs (rs_synth.c): table of contents larger than
 *      every resource, the one-byte tie, rows declaring 32 bytes;
 *   5. the memory pre-check at its exact boundary, and unknown pools;
 *   6. failure injection: every allocation of the opening path, the reader's
 *      open, semaphores and thread, a short read, a failing consumer; entry 0
 *      changed between its two reads (C.2); the release baseline (7.5);
 *   7. every stop point of section 7.6;
 *   8. pacing: max_entries and the time budget (controlled clock);
 *   9. every file-level header flip: loader result = serial driver result;
 *  10. seeded random load / stop cycles.
 * After every load: tracked rp_malloc bytes back exactly, and no open file,
 * thread or semaphore left. Results go to argv[1] for compare_rs_results.py.
 * argv[2] = "quick" samples the flips (every 16th; for ThreadSanitizer).
 */
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "cf_pack.h"
#include "gp_gdat.h"
#include "rp_mem.h"
#include "rp_time.h"
#include "rs_fixtures.h"
#include "rs_format.h"
#include "rs_host_pipe.h"
#include "rs_loader.h"
#include "rs_plat_pc.h"
#include "rs_synth.h"

#define FIX "app0:fixtures/"
#define FLIP_PATH FIX "_lflip.gdat"

static int g_pass, g_fail, g_quick;
static FILE *g_out;

static void check(int ok, const char *fmt, ...)
{
    va_list ap;
    if (ok) {
        g_pass++;
        return;
    }
    g_fail++;
    printf("FAIL: ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/* ------------------------------------------------------------------ rp_malloc / rp_free wrappers */
void *__real_rp_malloc(size_t n);
void __real_rp_free(void *p);
void *__wrap_rp_malloc(size_t n);
void __wrap_rp_free(void *p);

static pthread_t g_main;
static int g_off_main;                  /* atomic: allocations or frees off the main thread */
static unsigned g_allocs;               /* main thread only */
static unsigned g_fail_alloc_at;        /* the nth allocation from now fails; 0 = off */

void *__wrap_rp_malloc(size_t n)
{
    if (!pthread_equal(pthread_self(), g_main)) {
        __atomic_add_fetch(&g_off_main, 1, __ATOMIC_ACQ_REL);
        return __real_rp_malloc(n);
    }
    g_allocs++;
    if (g_fail_alloc_at > 0 && --g_fail_alloc_at == 0)
        return NULL;
    return __real_rp_malloc(n);
}

void __wrap_rp_free(void *p)
{
    if (!pthread_equal(pthread_self(), g_main))
        __atomic_add_fetch(&g_off_main, 1, __ATOMIC_ACQ_REL);
    __real_rp_free(p);
}

/* ------------------------------------------------------------------ helpers */
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static int clean_since(size_t before, const char *what)
{
    const rs_pc_live lv = rs_pc_live_now();
    const size_t now = rp_mem_get().current;
    const int ok = now == before && lv.open_files == 0 && lv.threads == 0 && lv.semaphores == 0;
    check(ok, "%s: not clean (tracked %zu, before %zu; files %d, threads %d, semaphores %d)", what, now, before,
          lv.open_files, lv.threads, lv.semaphores);
    return ok;
}

typedef struct {
    uint8_t **ref;                      /* reference raws (gp_gdat_load), or NULL */
    size_t   *ref_len;
    uint32_t  n;
    uint32_t  delivered, bad, off_main;
    uint32_t  fail_at;                  /* fail at this entry (0 = never) */
    gp_status fail_code;
    uint32_t  flag_at;                  /* set 'flagged' at this entry */
    int       flagged;
    double    advance_ms;               /* clock advance per call */
    int       keep;                     /* allocate 64 bytes once and keep them (a leak, on purpose) */
    void     *kept;
} consumer;

static gp_status consume(void *ctx, uint32_t e, const rs_resource *r)
{
    consumer *c = (consumer *)ctx;
    if (!pthread_equal(pthread_self(), g_main))
        c->off_main++;
    if (c->advance_ms > 0.0)
        rs_pc_clock_advance(c->advance_ms);
    if (c->keep && !c->kept)
        c->kept = rp_malloc(64);
    c->delivered++;
    if (c->ref && (e >= c->n || (size_t)r->payload_size + 32u != c->ref_len[e] ||
                   memcmp(r->payload, c->ref[e] + 32, r->payload_size) != 0))
        c->bad++;
    if (c->flag_at && e == c->flag_at)
        c->flagged = 1;
    if (c->fail_at && e == c->fail_at)
        return c->fail_code;
    return RP_OK;
}

typedef struct {
    int           code, entry;
    const char   *stage;
    char          check[128];
    rs_load_needs needs;
    gp_status     release;
} lres;

static void describe(const rs_load *l, gp_status st, lres *out)
{
    const rs_load_error *e = rs_load_error_of(l);
    out->code = st;
    out->entry = st ? e->entry : -1;
    out->stage = st ? e->stage : "-";
    if (st == CF_ERR_PAYLOAD)
        snprintf(out->check, sizeof out->check, "CFP1 %s", e->cfp1_field ? e->cfp1_field : "(none)");
    else if (st)
        snprintf(out->check, sizeof out->check, "%s: %s", e->layer ? e->layer : "?", e->check ? e->check : "?");
    else
        out->check[0] = '\0';
    out->needs = l->needs;
}

/* Opens, steps to the end (blocking) and releases; checks it ends clean. */
static void run_loader(const char *path, int slots, int policy, consumer *c, lres *out)
{
    rs_load l;
    rs_load_cfg cfg;
    const size_t before = rp_mem_get().current;
    gp_status st;
    memset(&cfg, 0, sizeof cfg);
    cfg.slots = slots;
    cfg.apply_policy = policy;
    cfg.pools = rs_pc_pools;
    cfg.consume = c ? consume : NULL;
    cfg.consume_ctx = c;
    memset(out, 0, sizeof *out);
    st = rs_load_open(&l, path, &cfg);
    while (st == RP_OK && !rs_load_done(&l))
        st = rs_load_step(&l, 1000000u, 1.0e9, 1, NULL);
    describe(&l, st, out);
    out->release = rs_load_release(&l);
    check(out->release == RP_OK, "%s: release gave %d", path, out->release);
    clean_since(before, path);
}

static const char *serial_stage(const rs_pipe_result *r)
{
    return r->stage && r->stage[0] ? r->stage : "-";
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *b = NULL;
    long n;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        b = (uint8_t *)malloc(n ? (size_t)n : 1u);
        if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
            free(b);
            b = NULL;
        }
        *len = (size_t)n;
    }
    fclose(f);
    return b;
}

/* Every entry read alone with gp_gdat_load (the serial GDAT Proto path). */
static uint32_t load_refs(const char *path, uint8_t ***ref, size_t **len)
{
    gp_gdat a;
    gp_wrap_info w;
    uint32_t i, n;
    if (gp_gdat_open(&a, path, GP_STRICT) != RP_OK)
        return 0;
    n = a.count;
    *ref = (uint8_t **)calloc(n, sizeof **ref);
    *len = (size_t *)calloc(n, sizeof **len);
    for (i = 0; i < n; i++)
        if (gp_gdat_load(&a, i, &w, &(*ref)[i], &(*len)[i], NULL) != RP_OK)
            (*ref)[i] = NULL;
    gp_gdat_close(&a);
    return n;
}

static void free_refs(uint8_t **ref, size_t *len, uint32_t n)
{
    uint32_t i;
    for (i = 0; ref && i < n; i++)
        rp_free(ref[i]);
    free(ref);
    free(len);
}

static void cfg_for(rs_load_cfg *cfg, int slots, int policy, consumer *c)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->slots = slots;
    cfg->apply_policy = policy;
    cfg->pools = rs_pc_pools;
    cfg->consume = c ? consume : NULL;
    cfg->consume_ctx = c;
}

/* ------------------------------------------------------------------ 1. arguments and needs */
static void args_and_needs(void)
{
    rs_load l;
    rs_load_cfg cfg;
    rs_load_needs nd;
    lres r;
    const size_t before = rp_mem_get().current;
    int i;

    cfg_for(&cfg, 3, 1, NULL);
    check(rs_load_open(NULL, FIX "room.gdat", &cfg) == RP_ERR_ARG, "open(NULL loader)");
    check(rs_load_open(&l, NULL, &cfg) == RP_ERR_ARG && l.state == RS_LOAD_EMPTY, "open(NULL path)");
    check(rs_load_open(&l, FIX "room.gdat", NULL) == RP_ERR_ARG, "open(NULL cfg)");
    cfg.slots = 0;
    check(rs_load_open(&l, FIX "room.gdat", &cfg) == RP_ERR_ARG, "open with 0 slots");
    cfg.slots = CF_RING_MAX + 1;
    check(rs_load_open(&l, FIX "room.gdat", &cfg) == RP_ERR_ARG, "open with 5 slots");
    cfg.slots = 3;
    cfg.pools = NULL;
    check(rs_load_open(&l, FIX "room.gdat", &cfg) == RP_ERR_ARG, "open without a snapshot function");
    check(rs_load_step(&l, 1, 1.0, 1, NULL) == RP_ERR_ARG && rs_load_release(&l) == RP_OK &&
          rs_load_toc(&l) == NULL, "an unopened loader");
    rs_load_cancel(&l);
    check(rs_load_needs_for(NULL, NULL, 3, &nd) == RP_ERR_ARG, "needs(NULL)");
    clean_since(before, "argument checks");

    /* section 8.2's worked example: example.gdat, 3 slots: heap = 66,057 + Rt + 3(R + 220) */
    run_loader(FIX "example.gdat", 3, 0, NULL, &r);
    check(r.code == 0 && r.needs.max_inflated == 220 && r.needs.toc_inflated == 176 && r.needs.vram == 164 &&
          r.needs.heap == 66057u + r.needs.toc_region + 3u * ((uint64_t)r.needs.max_region + 220u),
          "8.2 worked example: U %u Ut %u heap %llu", r.needs.max_inflated, r.needs.toc_inflated,
          (unsigned long long)r.needs.heap);
    for (i = 0; i < RS_FIX_PACK_COUNT; i++) {
        char path[160];
        snprintf(path, sizeof path, FIX "%s", RS_FIX_PACKS[i].path);
        run_loader(path, 3, 0, NULL, &r);
        check(r.code == 0 && r.needs.n == RS_FIX_PACKS[i].entries && r.needs.vram == RS_FIX_PACKS[i].vram_bytes &&
              r.needs.heap == RS_FIX_PACKS[i].heap_need && r.needs.kernel == 131072u &&
              r.needs.max_inflated == (r.needs.row_inflated > r.needs.toc_inflated ? r.needs.row_inflated
                                                                                    : r.needs.toc_inflated),
              "%s: needs n %u vram %llu heap %llu U %u", RS_FIX_PACKS[i].path, r.needs.n,
              (unsigned long long)r.needs.vram, (unsigned long long)r.needs.heap, r.needs.max_inflated);
    }
    printf("  arguments refused; section 8.2 needs match the worked example and all %d fixtures\n",
           RS_FIX_PACK_COUNT);
}

/* ------------------------------------------------------------------ 2. valid packs */
static void valid_packs(void)
{
    int i, s, runs = 0, good = 0;
    for (i = 0; i < RS_FIX_PACK_COUNT; i++) {
        const rs_fix_pack *p = &RS_FIX_PACKS[i];
        char path[160];
        uint8_t **ref = NULL;
        size_t *len = NULL;
        uint32_t n;
        snprintf(path, sizeof path, FIX "%s", p->path);
        n = load_refs(path, &ref, &len);
        check(n == p->entries, "%s: reference read", p->path);
        for (s = 1; s <= CF_RING_MAX; s++) {
            consumer c;
            lres r;
            rs_load l;
            rs_load_cfg cfg;
            gp_status st;
            unsigned allocs;
            size_t before;
            memset(&c, 0, sizeof c);
            c.ref = ref;
            c.ref_len = len;
            c.n = n;
            cfg_for(&cfg, s, 1, &c);
            before = rp_mem_get().current;
            st = rs_load_open(&l, path, &cfg);
            allocs = g_allocs;
            while (st == RP_OK && !rs_load_done(&l))
                st = rs_load_step(&l, 1000000u, 1.0e9, 1, NULL);
            check(g_allocs == allocs, "%s, %d slots: the loader allocated while stepping", p->path, s);
            describe(&l, st, &r);
            if (st == RP_OK) {
                const rs_pc_live lv = rs_pc_live_now();
                check(lv.threads == 0 && lv.semaphores == 0 && lv.open_files == 0 && rs_load_toc(&l) != NULL &&
                      rp_mem_get().current == before + l.needs.toc_inflated,
                      "%s, %d slots: after D only the table of contents may remain", p->path, s);
            }
            r.release = rs_load_release(&l);
            check(r.release == RP_OK, "%s: release %d", p->path, r.release);
            clean_since(before, p->path);
            runs++;
            {
                const uint64_t want_heap = p->heap_need - 3u * ((uint64_t)r.needs.max_region + r.needs.max_inflated) +
                                           (uint64_t)s * ((uint64_t)r.needs.max_region + r.needs.max_inflated);
                const int ok = r.code == p->expect_code && r.entry == p->expect_entry && c.bad == 0 &&
                               c.off_main == 0 && c.delivered == (p->expect_code ? 2u : p->entries - 1u) &&
                               r.needs.heap == want_heap && r.needs.n == p->entries;
                check(ok, "%s, %d slots: %d entry %d (%s), delivered %u, differing %u, heap %llu (want %llu)",
                      p->path, s, r.code, r.entry, r.check, c.delivered, c.bad, (unsigned long long)r.needs.heap,
                      (unsigned long long)want_heap);
                good += ok;
            }
            if (s == 3)
                fprintf(g_out, "PACK %s %d %d %s %u %llu %llu\n", p->path, r.code, r.entry, r.stage, r.needs.n,
                        (unsigned long long)r.needs.vram, (unsigned long long)r.needs.heap);
        }
        free_refs(ref, len, n);
    }
    printf("  valid packs: %d of %d runs (10 packs x 1-4 slots) as expected; every delivered resource identical "
           "to gp_gdat_load\n", good, runs);
}

/* ------------------------------------------------------------------ 3. the 50 cases */
static void cases(void)
{
    int i, s, good = 0;
    for (i = 0; i < RS_FIX_CASE_COUNT; i++) {
        const rs_fix_pack *c = &RS_FIX_CASES[i];
        char path[160];
        rs_pipe_result pr;
        snprintf(path, sizeof path, FIX "%s", c->path);
        rs_pipe_run(path, 1, 0, NULL, NULL, &pr);
        for (s = 3; s >= 1; s -= 2) {
            lres r;
            int ok;
            run_loader(path, s, 1, NULL, &r);
            ok = r.code == c->expect_code && r.entry == c->expect_entry && r.code == pr.code &&
                 r.entry == pr.entry && strcmp(r.stage, serial_stage(&pr)) == 0;
            check(ok, "%s (%d slots): loader %d %s entry %d %s (%s); expected %d entry %d; serial %d entry %d %s",
                  c->id, s, r.code, rs_pipe_status_name(r.code), r.entry, r.stage, r.check, c->expect_code,
                  c->expect_entry, pr.code, pr.entry, serial_stage(&pr));
            good += ok;
            if (s == 3) {
                fprintf(g_out, "CASE %s %s %d %d %s %s\n", c->id, c->path, r.code, r.entry, r.stage, r.check);
                printf("  %s %3d %-22s %-5s entry %2d  %s\n", c->id, r.code, rs_pipe_status_name(r.code), r.stage,
                       r.entry, r.check);
            }
        }
    }
    printf("  cases: %d of %d runs (50 cases x 3 and 1 slots) equal the fixtures and the serial driver\n", good,
           2 * RS_FIX_CASE_COUNT);
}

/* ------------------------------------------------------------------ 4. synthetic packs */
static void synth(void)
{
    rs_synth_case sc[RS_SYNTH_CASES];
    int i, mode, s, good = 0, runs = 0;
    if (rs_synth_build_all(sc) != 0) {
        check(0, "building the synthetic packs failed");
        return;
    }
    for (i = 0; i < RS_SYNTH_CASES; i++) {
        for (mode = 1; mode >= 0; mode--) {
            rs_pipe_result pr;
            const int want_code = mode ? sc[i].policy_code : sc[i].format_code;
            const int want_entry = mode ? sc[i].policy_entry : sc[i].format_entry;
            rs_pipe_run(sc[i].path, mode, 0, NULL, NULL, &pr);
            for (s = 1; s <= CF_RING_MAX; s++) {
                lres r;
                int ok;
                if (s != 3 && i != 0)
                    continue;                       /* toc_big with every slot count; the rest with 3 */
                run_loader(sc[i].path, s, mode, NULL, &r);
                ok = r.code == want_code && r.entry == want_entry && r.needs.max_inflated == sc[i].expect_u &&
                     r.needs.row_inflated == sc[i].rows_only_u && r.needs.toc_inflated == sc[i].toc_inflated &&
                     r.code == pr.code && r.entry == pr.entry && strcmp(r.stage, serial_stage(&pr)) == 0 &&
                     r.needs.max_inflated == pr.max_inflated && r.needs.heap == pr.heap_need - 3u *
                     ((uint64_t)pr.max_region + pr.max_inflated) + (uint64_t)s * ((uint64_t)pr.max_region +
                     pr.max_inflated);
                check(ok, "%s (%s, %d slots): loader %d entry %d U %u; expected %d entry %d U %u; serial %d entry %d "
                      "U %u", sc[i].name, mode ? "policy" : "format", s, r.code, r.entry, r.needs.max_inflated,
                      want_code, want_entry, sc[i].expect_u, pr.code, pr.entry, pr.max_inflated);
                good += ok;
                runs++;
                if (s == 3) {
                    fprintf(g_out, "SYNTH %s %s %d %d %s %u\n", sc[i].path, mode ? "policy" : "format", r.code,
                            r.entry, r.stage, r.needs.max_inflated);
                    printf("  %-15s %-6s %3d %-22s entry %2d  U %4u (rows alone %4u, table %4u)\n", sc[i].name,
                           mode ? "policy" : "format", r.code, r.code ? rs_pipe_status_name(r.code) : "OK", r.entry,
                           r.needs.max_inflated, r.needs.row_inflated, r.needs.toc_inflated);
                }
            }
        }
    }
    printf("  synthetic packs: %d of %d loader runs as expected and equal to the serial driver\n", good, runs);
}

/* ------------------------------------------------------------------ 5. memory pre-check */
static gp_status open_with_pools(const char *path, const cf_pools *p, rs_load *l)
{
    rs_load_cfg cfg;
    cfg_for(&cfg, 3, 1, NULL);
    rs_pc_set_pools(p);
    return rs_load_open(l, path, &cfg);
}

static void precheck(void)
{
    cf_pools base, p;
    rs_load l;
    lres r;
    size_t before;
    rs_pc_totals t0;
    uint64_t need_heap, need_vram;
    gp_status st;

    run_loader(FIX "example_room.gdat", 3, 1, NULL, &r);
    need_heap = r.needs.heap;
    need_vram = r.needs.vram;
    rs_pc_default_pools(&base);

    p = base;                                   /* exactly enough: margin + need + ceil(need / 100) */
    p.p[CF_POOL_HEAP].free = CF_MARGIN_HEAP + need_heap + (need_heap + 99u) / 100u;
    before = rp_mem_get().current;
    st = open_with_pools(FIX "example_room.gdat", &p, &l);
    check(st == RP_OK, "heap exactly at the limit: %d", st);
    rs_load_cancel(&l);
    check(rs_load_release(&l) == RP_OK, "release after a cancelled open");
    clean_since(before, "heap at the limit");

    p.p[CF_POOL_HEAP].free -= 1u;               /* one byte short */
    t0 = rs_pc_totals_now();
    st = open_with_pools(FIX "example_room.gdat", &p, &l);
    check(st == CF_ERR_SKIPPED && l.err.entry == -1 && strcmp(l.err.stage, "open") == 0 && l.err.why[0] != '\0',
          "heap one byte short: %d (%s)", st, l.err.why);
    check(rs_pc_totals_now().threads_started == t0.threads_started &&
          rs_pc_totals_now().semas_created == t0.semas_created && rs_pc_totals_now().opens == t0.opens,
          "nothing of the reader may exist after a refused pre-check");
    check(rs_load_release(&l) == RP_OK, "release after a refused pre-check");
    clean_since(before, "heap one byte short");

    p = base;
    p.p[CF_POOL_HEAP].known = 0;
    st = open_with_pools(FIX "example_room.gdat", &p, &l);
    check(st == CF_ERR_UNKNOWN_SIZE, "heap size unknown: %d", st);
    rs_load_release(&l);

    p = base;
    p.p[CF_POOL_VRAM].free = CF_MARGIN_VRAM + need_vram + (need_vram + 99u) / 100u - 1u;
    st = open_with_pools(FIX "example_room.gdat", &p, &l);
    check(st == CF_ERR_SKIPPED, "VRAM one byte short: %d", st);
    rs_load_release(&l);

    p = base;
    p.p[CF_POOL_KERNEL].free = CF_MARGIN_KERNEL + 131072u + 1310u;     /* need + ceil(need / 100) - 1 */
    st = open_with_pools(FIX "example_room.gdat", &p, &l);
    check(st == CF_ERR_SKIPPED, "kernel one byte short: %d", st);
    rs_load_release(&l);

    p = base;
    p.p[CF_POOL_GLRAM].known = 0;               /* vitaGL RAM is not pre-checked (need 0) */
    st = open_with_pools(FIX "example_room.gdat", &p, &l);
    check(st == RP_OK, "unknown vitaGL RAM must not matter: %d", st);
    rs_load_cancel(&l);
    rs_load_release(&l);

    rs_pc_set_pools(&base);
    clean_since(before, "pre-check");
    printf("  pre-check: passes at margin + need + ceil(need/100) exactly, SKIPPED one byte below (heap, VRAM, "
           "kernel), UNKNOWN_SIZE for an unknown heap; nothing of the reader created when refused\n");
}

/* ------------------------------------------------------------------ 6. failure injection */
static int64_t region_offset(const char *path, uint32_t entry, uint32_t *count)
{
    size_t len = 0;
    uint8_t *b = read_file(path, &len);
    int64_t off = -1;
    if (b && len >= 8) {
        *count = le32(b + 4);
        if (entry < *count && 16u + 8u * (size_t)entry <= len)
            off = le32(b + 8 + 8 * entry);
    }
    free(b);
    return off;
}

static void faults(void)
{
    rs_load l;
    rs_load_cfg cfg;
    lres r;
    size_t before;
    unsigned k, n_alloc, a0;
    uint32_t count = 0, e;
    gp_status st;
    int nomem = 0;
    static const char *const PACKS[] = {FIX "room.gdat", FIX "example.gdat"};
    int pi;

    /* every allocation of the opening path, one at a time */
    for (pi = 0; pi < 2; pi++) {
        cfg_for(&cfg, 3, 1, NULL);
        a0 = g_allocs;
        st = rs_load_open(&l, PACKS[pi], &cfg);
        n_alloc = g_allocs - a0;
        rs_load_cancel(&l);
        rs_load_release(&l);
        check(st == RP_OK && n_alloc >= 7, "%s: %u allocations while opening", PACKS[pi], n_alloc);
        for (k = 1; k <= n_alloc; k++) {
            before = rp_mem_get().current;
            g_fail_alloc_at = k;
            st = rs_load_open(&l, PACKS[pi], &cfg);
            g_fail_alloc_at = 0;
            check(st == RP_ERR_NOMEM && l.state == RS_LOAD_FAILED, "%s: allocation %u failing gave %d",
                  PACKS[pi], k, st);
            nomem += st == RP_ERR_NOMEM;
            check(rs_load_release(&l) == RP_OK, "release after allocation %u failed", k);
            clean_since(before, "allocation failure");
        }
    }

    before = rp_mem_get().current;
    rs_pc_fail_next_open();
    run_loader(FIX "room.gdat", 3, 1, NULL, &r);
    check(r.code == RP_ERR_OPEN && r.entry == -1 && strcmp(r.stage, "open") == 0, "reader open fails: %d",
          r.code);
    rs_pc_fail_sema_create(1);
    run_loader(FIX "room.gdat", 3, 1, NULL, &r);
    check(r.code == CF_ERR_THREAD, "first semaphore fails: %d", r.code);
    rs_pc_fail_sema_create(2);
    run_loader(FIX "room.gdat", 3, 1, NULL, &r);
    check(r.code == CF_ERR_THREAD, "second semaphore fails: %d", r.code);
    rs_pc_fail_next_thread();
    run_loader(FIX "room.gdat", 3, 1, NULL, &r);
    check(r.code == CF_ERR_THREAD, "worker thread fails to start: %d", r.code);
    rs_pc_reset();

    for (e = 0; e < 6; e += 1) {                /* a short read at entries 0-5 of room.gdat */
        const int64_t off = region_offset(FIX "room.gdat", e, &count);
        rs_pc_short_read_at(off);
        run_loader(FIX "room.gdat", 3, 1, NULL, &r);
        rs_pc_short_read_at(-1);
        check(r.code == RP_ERR_SHORT_READ && r.entry == (int)e && strcmp(r.stage, "entry") == 0,
              "short read at entry %u: %d entry %d", e, r.code, r.entry);
    }
    for (e = 1; e < 6; e++) {                   /* the consumer (C.4) failing at entries 1-5 */
        consumer c;
        memset(&c, 0, sizeof c);
        c.fail_at = e;
        c.fail_code = CF_ERR_GL;
        run_loader(FIX "room.gdat", 3, 1, &c, &r);
        check(r.code == CF_ERR_GL && r.entry == (int)e && c.delivered == e && strcmp(r.check, "upload: consumer") == 0,
              "consumer failing at entry %u: %d entry %d", e, r.code, r.entry);
    }
    clean_since(before, "failure injection");
    printf("  failures: %d allocation failures in the opening path (room, example), reader open, both semaphores, "
           "thread start, short reads at entries 0-5, consumer failing at 1-5; each released clean\n", nomem);
}

/* ------------------------------------------------------------------ 6b. C.2 and the baseline */
/* C.2: the worker reads entry 0 again; if the file changed in between, its CFP1 body CRC differs
 * from the one checked at A3. The stand-in hands the worker a different, internally valid entry 0
 * of exactly the same compressed size (so every transport check passes). */
static void reread_and_baseline(void)
{
    size_t len = 0, before;
    uint8_t *file = read_file(FIX "example.gdat", &len);
    uint8_t *raw = NULL, *raw2 = NULL, *z = NULL, *region2 = NULL;
    uint32_t off0, size0, usize, csize, pos, x;
    uLongf ulen, zlen = 0;
    int found = 0;
    lres r;
    rs_load l;
    rs_load_cfg cfg;
    cf_pools p;
    consumer c;
    gp_status st, rel;

    if (!file || len < 16) {
        check(0, "example.gdat unreadable");
        free(file);
        return;
    }
    off0 = le32(file + 8);
    size0 = le32(file + 12);
    usize = be32(file + off0 + 0x84);
    csize = be32(file + off0 + 0x88);
    raw = (uint8_t *)malloc(usize);
    raw2 = (uint8_t *)malloc(usize);
    z = (uint8_t *)malloc(compressBound(usize));
    region2 = (uint8_t *)malloc(size0);
    ulen = usize;
    check(uncompress(raw, &ulen, file + off0 + 0x90, csize) == Z_OK && ulen == usize, "entry 0 inflates");
    for (pos = 32; !found && pos < usize; pos++)
        for (x = 1; !found && x < 256; x++) {
            memcpy(raw2, raw, usize);
            raw2[pos] ^= (uint8_t)x;
            wr32(raw2 + 20, (uint32_t)crc32(crc32(0uL, Z_NULL, 0), raw2 + 32, usize - 32u));
            zlen = compressBound(usize);
            found = compress2(z, &zlen, raw2, usize, 9) == Z_OK && zlen == csize;
        }
    check(found, "a different entry 0 of the same compressed size");
    if (found) {
        memcpy(region2, file + off0, 0x90);
        memcpy(region2 + 0x90, z, csize);
        rs_pc_substitute_read(off0, region2, size0);
        run_loader(FIX "example.gdat", 3, 1, NULL, &r);
        rs_pc_substitute_read(-1, NULL, 0);
        check(r.code == RS_ERR_TOC && r.entry == 0 && strcmp(r.stage, "entry") == 0,
              "C.2: entry 0 changed between reads gave %d entry %d %s (%s)", r.code, r.entry, r.stage, r.check);
        printf("  C.2: entry 0 changed between the two reads: %d %s at entry %d (%s)\n", r.code,
               rs_pipe_status_name(r.code), r.entry, r.check);
    }
    free(file);
    free(raw);
    free(raw2);
    free(z);
    free(region2);

    /* A3: entry 0 whose CFP1 envelope is kind 1 (a 4x4 "texture": 64-byte body) passes ContentFit's
     * check and must be refused by the envelope rule (RS_ERR_ENVELOPE), before the table checks. */
    {
        static const char *const ENV0 = FIX "synth/env0_kind1.gdat";
        size_t elen = 0;
        uint8_t *ex = read_file(FIX "example.gdat", &elen), raw0[96], *out = NULL, zb[256];
        uLongf zl = sizeof zb;
        uint32_t n, i, shift, ok_build = 0;
        rs_pipe_result pr;
        if (ex && elen >= 8) {
            n = le32(ex + 4);
            memset(raw0, 0, sizeof raw0);
            memcpy(raw0, "CFP1", 4);
            wr32(raw0 + 4, 1);
            wr32(raw0 + 8, 4);
            wr32(raw0 + 12, 4);
            wr32(raw0 + 16, 64);
            wr32(raw0 + 20, (uint32_t)crc32(crc32(0uL, Z_NULL, 0), raw0 + 32, 64));
            if (compress2(zb, &zl, raw0, sizeof raw0, 9) == Z_OK) {
                const uint32_t old0 = le32(ex + 12), reg0 = 0x90u + (uint32_t)zl;
                out = (uint8_t *)calloc(1, elen - old0 + reg0);
                shift = reg0 - old0;
                memcpy(out, ex, 8u + 8u * n);
                wr32(out + 12, reg0);
                for (i = 1; i < n; i++)
                    wr32(out + 8 + 8 * i, le32(ex + 8 + 8 * i) + shift);
                memcpy(out + 8u + 8u * n + 0x80u, "BILZ", 4);
                out[8u + 8u * n + 0x87u] = 96;
                out[8u + 8u * n + 0x8Au] = (uint8_t)(zl >> 8);
                out[8u + 8u * n + 0x8Bu] = (uint8_t)zl;
                out[8u + 8u * n + 0x8Fu] = 7;
                memcpy(out + 8u + 8u * n + 0x90u, zb, zl);
                memcpy(out + 8u + 8u * n + reg0, ex + le32(ex + 8 + 8), elen - le32(ex + 8 + 8));
                {
                    FILE *f = fopen(ENV0, "wb");
                    ok_build = f && fwrite(out, 1, elen - old0 + reg0, f) == elen - old0 + reg0;
                    if (f)
                        fclose(f);
                }
            }
        }
        check(ok_build, "building %s", ENV0);
        if (ok_build) {
            rs_pipe_run(ENV0, 1, 0, NULL, NULL, &pr);
            run_loader(ENV0, 3, 1, NULL, &r);
            check(r.code == RS_ERR_ENVELOPE && r.entry == 0 && strcmp(r.stage, "open") == 0 && pr.code == r.code &&
                  pr.entry == r.entry, "A3: kind-1 envelope at entry 0: loader %d entry %d %s, serial %d entry %d",
                  r.code, r.entry, r.stage, pr.code, pr.entry);
            printf("  A3: a kind-1 CFP1 envelope at entry 0 (passes ContentFit's check): %d %s at entry %d, %s "
                   "(serial driver: %d)\n", r.code, rs_pipe_status_name(r.code), r.entry, r.stage, pr.code);
        }
        free(ex);
        free(out);
    }

    /* section 7.5 step 5: pools within 64 KiB come back; beyond, or tracked bytes kept, CF_ERR_BASELINE */
    before = rp_mem_get().current;
    for (x = 0; x < 2; x++) {
        cfg_for(&cfg, 3, 1, NULL);
        st = rs_load_open(&l, FIX "room.gdat", &cfg);
        while (st == RP_OK && !rs_load_done(&l))
            st = rs_load_step(&l, 1000000u, 1.0e9, 1, NULL);
        rs_pc_default_pools(&p);
        p.p[CF_POOL_VRAM].free -= CF_BASELINE_TOL + x;          /* exactly the tolerance, then one byte more */
        rs_pc_set_pools(&p);
        rel = rs_load_release(&l);
        check(st == RP_OK && rel == (x ? CF_ERR_BASELINE : RP_OK), "VRAM %s the 64 KiB tolerance: release %d",
              x ? "one byte beyond" : "exactly at", rel);
        rs_pc_default_pools(&p);
        rs_pc_set_pools(&p);
    }
    memset(&c, 0, sizeof c);
    c.keep = 1;
    cfg_for(&cfg, 3, 1, &c);
    st = rs_load_open(&l, FIX "room.gdat", &cfg);
    while (st == RP_OK && !rs_load_done(&l))
        st = rs_load_step(&l, 1000000u, 1.0e9, 1, NULL);
    rel = rs_load_release(&l);
    check(st == RP_OK && c.kept && rel == CF_ERR_BASELINE, "64 tracked bytes kept by the consumer: release %d", rel);
    rp_free(c.kept);
    clean_since(before, "baseline");
    printf("  baseline: VRAM 64 KiB lower passes, one byte more gives CF_ERR_BASELINE; 64 tracked bytes kept "
           "give CF_ERR_BASELINE\n");
}

/* ------------------------------------------------------------------ 7. stop points (section 7.6) */
static int wait_until(int (*cond)(void), int ms)
{
    int i;
    for (i = 0; i < ms; i++) {
        if (cond())
            return 1;
        usleep(1000);
    }
    return cond();
}

static unsigned g_want_reads;
static int reads_reached(void) { return rs_pc_totals_now().preads >= g_want_reads; }

static void stops(void)
{
    rs_load l;
    rs_load_cfg cfg;
    consumer c;
    size_t before = rp_mem_get().current;
    gp_status st;
    unsigned reads;
    double t0, waited;
    uint32_t count = 0, n, tries;

    /* right after opening (A finished; the worker may be anywhere) */
    cfg_for(&cfg, 3, 1, NULL);
    st = rs_load_open(&l, FIX "room.gdat", &cfg);
    rs_load_cancel(&l);
    check(st == RP_OK && l.state == RS_LOAD_STOPPED && rs_load_step(&l, 1, 1.0, 1, NULL) == CF_ERR_ABORTED,
          "stop right after opening");
    rs_load_cancel(&l);                          /* a second stop does nothing */
    check(rs_load_release(&l) == RP_OK && rs_load_release(&l) == RP_OK, "release twice");
    clean_since(before, "stop after opening");

    /* the worker waiting for a free slot: 1 slot, filled, never taken */
    rs_pc_reset();
    cfg_for(&cfg, 1, 1, NULL);
    st = rs_load_open(&l, FIX "room.gdat", &cfg);
    g_want_reads = 1;
    check(st == RP_OK && wait_until(reads_reached, 2000), "worker filled its only slot");
    usleep(20000);
    reads = rs_pc_totals_now().preads;
    rs_load_cancel(&l);
    check(reads == 1 && rs_pc_totals_now().preads == 1, "a worker waiting for a free slot exits without reading "
          "(%u reads)", rs_pc_totals_now().preads);
    rs_load_release(&l);
    clean_since(before, "stop while the worker waits");

    /* slots filled but not taken: 3 slots */
    rs_pc_reset();
    cfg_for(&cfg, 3, 1, NULL);
    st = rs_load_open(&l, FIX "room.gdat", &cfg);
    g_want_reads = 3;
    check(st == RP_OK && wait_until(reads_reached, 2000), "worker filled 3 slots");
    usleep(20000);
    rs_load_cancel(&l);
    check(rs_pc_totals_now().preads == 3, "filled slots are freed unread (%u reads)", rs_pc_totals_now().preads);
    rs_load_release(&l);
    clean_since(before, "stop with filled slots");

    /* the worker in the middle of a read: entry 2 of area8 held for 150 ms */
    rs_pc_reset();
    rs_pc_hold_read_at(region_offset(FIX "area8.gdat", 2, &count), 150);
    cfg_for(&cfg, 3, 1, NULL);
    st = rs_load_open(&l, FIX "area8.gdat", &cfg);
    check(st == RP_OK && wait_until(rs_pc_read_held, 3000), "the held read started");
    t0 = rp_now_ms();
    rs_load_cancel(&l);
    waited = rp_now_ms() - t0;
    check(waited >= 50.0 && rs_pc_totals_now().held_reads == 1, "stop during a read waits for it (%.1f ms)", waited);
    rs_load_release(&l);
    rs_pc_reset();
    clean_since(before, "stop during a read");

    /* the main thread checking a resource: requested from the consumer at entry 2, handled after it */
    memset(&c, 0, sizeof c);
    c.flag_at = 2;
    cfg_for(&cfg, 3, 1, &c);
    st = rs_load_open(&l, FIX "room.gdat", &cfg);
    while (st == RP_OK && !c.flagged && !rs_load_done(&l))
        st = rs_load_step(&l, 1, 1.0e9, 1, NULL);
    rs_load_cancel(&l);
    check(c.flagged && c.delivered == 2 && l.state == RS_LOAD_STOPPED, "stop between entries after entry 2 "
          "(delivered %u)", c.delivered);
    rs_load_release(&l);
    clean_since(before, "stop between entries");

    /* after D: a stop does nothing; release frees the table of contents */
    cfg_for(&cfg, 3, 1, NULL);
    st = rs_load_open(&l, FIX "room.gdat", &cfg);
    while (st == RP_OK && !rs_load_done(&l))
        st = rs_load_step(&l, 1000000u, 1.0e9, 1, NULL);
    rs_load_cancel(&l);
    check(st == RP_OK && l.state == RS_LOAD_DONE && rs_load_toc(&l) != NULL, "stop after D does nothing");
    check(rs_load_release(&l) == RP_OK, "release after D");
    clean_since(before, "stop after D");

    /* best effort, not deterministic: stops while large entries may be inflating */
    for (tries = 0; tries < 20; tries++) {
        cfg_for(&cfg, 1, 1, NULL);
        st = rs_load_open(&l, FIX "area8.gdat", &cfg);
        for (n = 0; st == RP_OK && n < tries % 5; n++)
            st = rs_load_step(&l, 1, 1.0e9, 1, NULL);
        rs_load_cancel(&l);
        rs_load_release(&l);
        clean_since(before, "stop during inflation (best effort)");
    }
    printf("  stops: after opening, worker waiting for a slot, slots filled, mid-read (waited %.0f ms), between "
           "entries, after D, 20 best-effort stops during inflation; all released clean\n", waited);
}

/* ------------------------------------------------------------------ 8. pacing */
static void pacing(void)
{
    rs_load l;
    rs_load_cfg cfg;
    consumer c;
    uint32_t got[4] = {0, 0, 0, 0};
    gp_status st;
    size_t before = rp_mem_get().current;

    memset(&c, 0, sizeof c);
    c.advance_ms = 10.0;
    rs_pc_clock_manual(1);
    rs_pc_clock_set(0.0);
    cfg_for(&cfg, 3, 1, &c);
    st = rs_load_open(&l, FIX "room.gdat", &cfg);              /* entries 0-5; 1-5 reach the consumer */
    if (st == RP_OK)
        st = rs_load_step(&l, 100u, 20.0, 1, &got[0]);         /* entry 0, then 1 (10 ms), 2 (20 ms): stop */
    if (st == RP_OK)
        st = rs_load_step(&l, 1u, 1.0e9, 1, &got[1]);          /* exactly one */
    if (st == RP_OK)
        st = rs_load_step(&l, 100u, 0.0, 1, &got[2]);          /* a zero budget still takes one */
    if (st == RP_OK)
        st = rs_load_step(&l, 100u, 1.0e9, 1, &got[3]);        /* the rest */
    rs_pc_clock_manual(0);
    check(st == RP_OK && got[0] == 3 && got[1] == 1 && got[2] == 1 && got[3] == 1 && rs_load_done(&l),
          "pacing: %u, %u, %u, %u entries", got[0], got[1], got[2], got[3]);
    rs_load_release(&l);
    clean_since(before, "pacing");
    printf("  pacing: 20 ms budget took 3 entries (0 ms, 10 ms, 20 ms), max_entries 1 took 1, a zero budget took 1\n");
}

/* ------------------------------------------------------------------ 9. file-level flips */
typedef struct { unsigned runs, same; } flip_tally;

static void file_flips(const char *rel, flip_tally *ft)
{
    char path[160];
    size_t len = 0;
    uint8_t *data;
    FILE *f;
    uint32_t n, k, o, index_end, step;

    snprintf(path, sizeof path, FIX "%s", rel);
    data = read_file(path, &len);
    check(data != NULL, "%s: cannot read", rel);
    if (!data)
        return;
    f = fopen(FLIP_PATH, "wb");
    check(f && fwrite(data, 1, len, f) == len, "cannot write " FLIP_PATH);
    if (f)
        fclose(f);
    f = fopen(FLIP_PATH, "r+b");
    if (!f) {
        free(data);
        return;
    }
    n = le32(data + 4);
    index_end = 8u + 8u * n;
    step = g_quick ? 16u : 1u;
    for (k = 0; k <= n; k++) {
        const uint32_t lo = k == n ? 0u : le32(data + 8 + 8 * k);
        const uint32_t hi = k == n ? index_end : lo + 0x90u;
        const uint32_t first = (k == n || k == 0) ? 0u : k;
        for (o = lo; o < hi; o += step) {
            rs_pipe_result pr;
            lres r;
            int same;
            fseek(f, (long)o, SEEK_SET);
            fputc(data[o] ^ 0xFF, f);
            fflush(f);
            rs_pipe_run(FLIP_PATH, 1, first, NULL, NULL, &pr);
            run_loader(FLIP_PATH, 3, 1, NULL, &r);
            fseek(f, (long)o, SEEK_SET);
            fputc(data[o], f);
            fflush(f);
            same = r.code == pr.code && r.entry == pr.entry && strcmp(r.stage, serial_stage(&pr)) == 0 && r.code != 0;
            check(same, "%s byte %u: loader %d entry %d %s; serial %d entry %d %s", rel, o, r.code, r.entry, r.stage,
                  pr.code, pr.entry, serial_stage(&pr));
            ft->runs++;
            ft->same += same;
            fprintf(g_out, "FLIPF %s %u 0 %d %d %s\n", rel, o, r.code, r.entry, r.stage);
        }
    }
    fclose(f);
    remove(FLIP_PATH);
    free(data);
}

/* ------------------------------------------------------------------ 10. random cycles */
static void cycles(int count)
{
    static const char *const PACKS[] = {"room.gdat", "fmt_dxt1.gdat", "fmt_pvrtc4.gdat", "example.gdat",
                                        "example_room.gdat", "area8.gdat", "bad/I-08.gdat", "bad/C-05.gdat",
                                        "bad/W-04.gdat", "bad/W-08.gdat", "synth/toc_big.gdat",
                                        "synth/decl32_mesh.gdat"};
    uint32_t seed = 0x12345678u;
    int i, clean = 0;
    for (i = 0; i < count; i++) {
        rs_load l;
        rs_load_cfg cfg;
        char path[160];
        uint32_t steps, n;
        gp_status st;
        const size_t before = rp_mem_get().current;
        seed = seed * 1664525u + 1013904223u;
        snprintf(path, sizeof path, FIX "%s", PACKS[(seed >> 8) % (sizeof PACKS / sizeof PACKS[0])]);
        cfg_for(&cfg, (int)((seed >> 4) % 4u) + 1, 1, NULL);
        steps = (seed >> 16) % 8u;              /* 0-6: stop after that many steps; 7: run to the end */
        st = rs_load_open(&l, path, &cfg);
        for (n = 0; st == RP_OK && !rs_load_done(&l) && (steps == 7u || n < steps); n++)
            st = rs_load_step(&l, 1u + (seed >> 24) % 3u, 1.0e9, (seed >> 12) & 1u, NULL);
        rs_load_cancel(&l);
        rs_load_release(&l);
        clean += clean_since(before, path);
    }
    printf("  random cycles: %d of %d load/stop cycles ended clean\n", clean, count);
}

int main(int argc, char **argv)
{
    flip_tally ft;
    rs_pc_totals tot;
    int i;

    if (argc < 2) {
        fprintf(stderr, "usage: %s RESULTS_FILE [quick]\n", argv[0]);
        return 2;
    }
    g_quick = argc > 2 && strcmp(argv[2], "quick") == 0;
    g_main = pthread_self();
    alarm(1800);                                 /* a hung take fails the run instead of hanging it */
    g_out = fopen(argv[1], "w");
    if (!g_out) {
        perror(argv[1]);
        return 2;
    }
    rs_pc_reset();
    memset(&ft, 0, sizeof ft);

    printf("### 1. arguments and section 8.2 needs\n");
    args_and_needs();
    printf("### 2. valid packs, 1-4 slots\n");
    valid_packs();
    printf("### 3. section 10 cases\n");
    cases();
    printf("### 4. synthetic packs: the section 8.2 slot rule\n");
    synth();
    printf("### 5. memory pre-check\n");
    precheck();
    printf("### 6. failure injection\n");
    faults();
    reread_and_baseline();              /* after synth(): writes into app0:fixtures/synth/ */
    printf("### 7. stop points (section 7.6)\n");
    stops();
    printf("### 8. pacing\n");
    pacing();
    printf("### 9. file-level header flips: loader vs serial driver%s\n", g_quick ? " (every 16th)" : "");
    for (i = 0; i < RS_FIX_PACK_COUNT; i++)
        file_flips(RS_FIX_PACKS[i].path, &ft);
    printf("  flips: %u, loader equal to the serial driver: %u\n", ft.runs, ft.same);
    printf("### 10. random load / stop cycles\n");
    cycles(200);

    tot = rs_pc_totals_now();
    check(__atomic_load_n(&g_off_main, __ATOMIC_ACQUIRE) == 0, "%d allocations or frees off the main thread",
          g_off_main);
    printf("  rp_malloc/rp_free off the main thread: %d; reader threads started %u; semaphore signals dropped at "
           "their maximum (as on the Vita): %u\n", g_off_main, tot.threads_started, tot.sema_overflows);
    fclose(g_out);
    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
