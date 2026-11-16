/*
 * test_cf_dryrun.c - runs the whole ContentFit suite on the PC with the GL and
 * platform stand-ins, writing real result files under ./ux0:data/ContentFit/.
 * Host timings are not measurements; this checks the run's logic: every step
 * runs or is skipped for a stated reason, nothing leaks, files are complete.
 *
 * usage: test_cf_dryrun MODE
 *   full               3 iterations, swap interval 2 honoured
 *   quick-vblank       1 iteration, interval 2 refused -> every-second-vblank pacing
 *   quick-heap-unknown 1 iteration, heap size unknown -> heap steps skipped
 *   quick-tight-heap   1 iteration, 10 MiB of heap already in use -> 96 MiB tier skipped
 *   abort              START "pressed" part way through the first iteration
 */
#include <stdio.h>
#include <string.h>
#include <vitaGL.h>

#include "cf_plat_host.h"
#include "cf_suite.h"
#include "rp_mem.h"

static cf_ctx s_ctx;

static const cf_result *find(const cf_ctx *c, int it, const char *id, const char *mode)
{
    int i;
    for (i = 0; i < c->nres[it]; i++)
        if (strcmp(c->res[it][i].id, id) == 0 && strcmp(c->res[it][i].mode, mode) == 0) return &c->res[it][i];
    return NULL;
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "full";
    int iterations = strcmp(mode, "full") == 0 ? 3 : 1;
    int it, i, bad = 0, fails = 0;
    stub_counts sc;
    cf_ctx *c = &s_ctx;

    if (strcmp(mode, "quick-vblank") == 0) host_set_interval2_fails(1);
    if (strcmp(mode, "quick-heap-unknown") == 0) host_set_heap(128ull << 20, 2ull << 20, 0);
    if (strcmp(mode, "quick-tight-heap") == 0) host_set_heap(128ull << 20, 10ull << 20, 1);
    if (strcmp(mode, "abort") == 0) host_abort_after_polls(4000);

    cf_suite_run(c, iterations, NULL);

    printf("mode %s: run %03d, report %s, aborted %d\n", mode, c->rep.run_no, c->rep.ok ? "written" : "NOT written",
           c->aborted);
    for (it = 0; it < iterations; it++) {
        int n_ok = 0, n_skip = 0;
        for (i = 0; i < c->nres[it]; i++) {
            const cf_result *r = &c->res[it][i];
            if (r->ok) n_ok++;
            else {
                bad++;
                printf("  NOT OK it%d %-11s %-7s %-12s %s\n", it + 1, r->id, r->mode, cf_status_name(r->status),
                       r->detail);
            }
            if (r->status == CF_ERR_SKIPPED || r->status == CF_ERR_UNKNOWN_SIZE) {
                n_skip++;
                printf("  skipped it%d %-11s %-7s %s\n", it + 1, r->id, r->mode, r->detail);
            }
        }
        printf("  iteration %d: %d rows, %d ok, %d skipped by the pre-check\n", it + 1, c->nres[it], n_ok, n_skip);
    }
    printf("  C5: %s\n", c->c5_note);
    stub_get_counts(&sc);
    printf("  after the run: tracked bytes %lu, live textures %ld, texture bytes %llu, buffer bytes %llu\n",
           (unsigned long)rp_mem_get().current, sc.tex_live, (unsigned long long)sc.tex_bytes,
           (unsigned long long)sc.buf_bytes);

#define EXPECT(cond, ...) do { if (!(cond)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
    EXPECT(c->rep.run_no > 0 && c->rep.ok, "results were not saved");
    EXPECT(rp_mem_get().current == 0, "tracked bytes left: %lu", (unsigned long)rp_mem_get().current);
    EXPECT(sc.tex_live == 0 && sc.buf_bytes == 0, "GL objects left: %ld textures, %llu buffer bytes", sc.tex_live,
           (unsigned long long)sc.buf_bytes);
    EXPECT(sc.gl_errors_raised == 0, "the stand-in GL saw %ld invalid calls during the run", sc.gl_errors_raised);
    if (strcmp(mode, "abort") == 0) {
        EXPECT(c->aborted, "abort was not seen");
        EXPECT(c->c5_pass == -1, "C5 should be N/A after an abort");
    } else {
        const cf_result *pm = find(c, 0, "C3-P", "VERDICT");
        EXPECT(!c->aborted, "run aborted");
        EXPECT(bad == 0, "%d rows not ok", bad);
        EXPECT(c->c5_pass == (iterations > 1 ? 1 : -1), "C5 result %d", c->c5_pass);
        EXPECT(pm && strcmp(pm->verdict, strcmp(mode, "quick-vblank") == 0 ? "VBLANK2" : "INTERVAL2") == 0,
               "pacing method %s", pm ? pm->verdict : "missing");
    }
    if (strcmp(mode, "quick-heap-unknown") == 0) {
        const cf_result *h = find(c, 0, "C4-H16", "-");
        EXPECT(h && h->status == CF_ERR_UNKNOWN_SIZE, "heap tier with unknown heap: %s",
               h ? cf_status_name(h->status) : "missing");
    }
    if (strcmp(mode, "quick-tight-heap") == 0) {
        const cf_result *h96 = find(c, 0, "C4-H96", "-"), *h64 = find(c, 0, "C4-H64", "-");
        EXPECT(h96 && h96->status == CF_ERR_SKIPPED, "96 MiB tier should be skipped: %s",
               h96 ? cf_status_name(h96->status) : "missing");
        EXPECT(h64 && h64->status == RP_OK, "64 MiB tier should run");
    }
    if (strcmp(mode, "full") == 0 || strcmp(mode, "quick-vblank") == 0) {
        const cf_result *v64 = find(c, 0, "C4-V64", "-"), *h96 = find(c, 0, "C4-H96", "-");
        EXPECT(v64 && v64->status == RP_OK, "64 MiB VRAM tier with 83 MiB free");
        EXPECT(h96 && h96->status == RP_OK, "96 MiB heap tier with 126 MiB free");
    }
    printf("dry run %s: %s\n", mode, fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
