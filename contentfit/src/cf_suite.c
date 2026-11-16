/*
 * cf_suite.c - run orchestration, measurement loop, environment (C0) and
 * repeatability (C5).
 */
#include <stdio.h>
#include <string.h>
#include <vitaGL.h>
#include <zlib.h>

#include "cf_fixdefs.h"
#include "cf_scene.h"
#include "cf_suite.h"
#include "graphics/hud2d.h"
#include "rp_mem.h"
#include "rp_time.h"

static const char *const MODE_NAMES[] = {"OFF", "LOCK30", "V1"};

double cf_mib(uint64_t bytes) { return (double)bytes / (double)CF_MIB; }

int64_t cf_kib_delta(const cf_pools *before, const cf_pools *after, cf_pool_id id)
{
    if (!before->p[id].known || !after->p[id].known)
        return 0;
    return ((int64_t)before->p[id].free - (int64_t)after->p[id].free) / 1024;
}

cf_result *cf_add(cf_ctx *c, const char *id, const char *name, const char *mode)
{
    static cf_result overflow;
    cf_result *r;
    if (c->nres[c->iter] >= CF_MAX_RESULTS) {
        r = &overflow;          /* never expected: CF_MAX_RESULTS covers every row of an iteration */
        cf_report_log(&c->rep, "INTERNAL: result table full; row %s %s not stored for comparison", id, mode);
    } else {
        r = &c->res[c->iter][c->nres[c->iter]++];
    }
    memset(r, 0, sizeof(*r));
    snprintf(r->id, sizeof(r->id), "%s", id);
    snprintf(r->name, sizeof(r->name), "%s", name);
    snprintf(r->mode, sizeof(r->mode), "%s", mode);
    snprintf(r->verdict, sizeof(r->verdict), "-");
    r->status = RP_OK;
    return r;
}

void cf_done(cf_ctx *c, cf_result *r)
{
    cf_report_row(&c->rep, c->iter + 1, r);
    if (r->has_frames)
        cf_report_log(&c->rep, "  %-11s %-7s %-22s %-12s ok=%d %-11s n=%d avg %.2f p50 %.2f p95 %.2f p99 %.2f max %.2f "
                               ">34ms %d | %s",
                      r->id, r->mode, r->name, cf_status_name(r->status), r->ok, r->verdict, r->fs.n, r->fs.avg,
                      r->fs.p50, r->fs.p95, r->fs.p99, r->fs.max, r->fs.over_34, r->detail);
    else
        cf_report_log(&c->rep, "  %-11s %-7s %-22s %-12s ok=%d %-11s %.1f ms %.2f MiB %.2f MiB/s | %s", r->id,
                      r->mode, r->name, cf_status_name(r->status), r->ok, r->verdict, r->ms, r->mbytes, r->mbps,
                      r->detail);
}

void cf_progress(cf_ctx *c, const char *id, const char *what)
{
    cf_report_progress(&c->rep, "iteration %d: starting %s %s (%.1f s into the run)", c->iter + 1, id, what,
                       (rp_now_ms() - c->t_start_ms) / 1000.0);
    if (c->ui) {
        char l1[96];
        snprintf(l1, sizeof(l1), "ITERATION %d/%d  %s", c->iter + 1, c->iterations, id);
        c->ui(c, l1, what);
    }
}

int cf_should_stop(cf_ctx *c)
{
    if (!c->aborted && cf_plat_poll_abort())
        c->aborted = 1;
    return c->aborted;
}

int cf_settle(cf_ctx *c, const cf_pools *base, cf_pools *now, char *why, size_t why_len)
{
    cf_pool_id where = CF_POOL_VRAM;
    int i;
    (void)c;
    if (why && why_len) why[0] = '\0';
    glFinish();
    for (i = 0; i < 30; i++) {
        cf_plat_pools(now);
        if (cf_baseline_ok(base, now, CF_BASELINE_TOL, &where))
            return 1;
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        cf_plat_swap();
        glFinish();
    }
    if (why)
        snprintf(why, why_len, "%s not back to its starting level: %.2f MiB free, %.2f MiB at start",
                 cf_pool_name(where), cf_mib(now->p[where].free), cf_mib(base->p[where].free));
    return 0;
}

static void set_mode(cf_ctx *c, int mode)
{
    if (mode == CF_M_OFF)
        cf_plat_set_interval(0);
    else if (mode == CF_M_V1)
        cf_plat_set_interval(1);
    else
        cf_plat_set_interval(c->lock_method == CF_LOCK_INTERVAL2 ? 2 : 1);
}

gp_status cf_measure(cf_ctx *c, const char *label, int mode, double warm_s, double meas_s, cf_frame_hook hook,
                     void *user)
{
    const int vb2 = mode == CF_M_LOCK && c->lock_method == CF_LOCK_VBLANK2;
    const double warm_ms = warm_s * 1000.0, meas_ms = meas_s * 1000.0;
    double t_prev = -1.0, t_phase;
    int measuring = 0, last_vc;
    uint32_t frame = 0;
    char line[96];

    set_mode(c, mode);
    c->nsamples = 0;
    last_vc = cf_plat_vcount();
    t_phase = rp_now_ms();
    for (;;) {
        double t;
        if (vb2 && frame > 0) {
            int guard = 8;
            while (cf_plat_vcount() - last_vc < 2 && guard-- > 0)
                cf_plat_wait_vblank();
        }
        t = rp_now_ms();
        if (vb2)
            last_vc = cf_plat_vcount();
        if (measuring && t_prev >= 0.0 && c->nsamples < CF_MAX_SAMPLES)
            c->samples[c->nsamples++] = (float)(t - t_prev);
        t_prev = t;
        if (!measuring && t - t_phase >= warm_ms) {
            measuring = 1;
            t_phase = t;
            c->nsamples = 0;
        } else if (measuring && (t - t_phase >= meas_ms || c->nsamples >= CF_MAX_SAMPLES)) {
            break;
        }
        if (cf_should_stop(c))
            return CF_ERR_ABORTED;
        if (hook)
            hook(c, user, frame, measuring);
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        cf_scene_draw(frame);
        snprintf(line, sizeof(line), "%s %s %s %.1f S", label, MODE_NAMES[mode], measuring ? "MEASURE" : "WARMUP",
                 (t - t_phase) / 1000.0);
        hud2d_begin(CF_SCREEN_W, CF_SCREEN_H);
        hud2d_text(10.0f, 6.0f, 2.0f, line, 0.95f, 0.85f, 0.45f);
        hud2d_end();
        cf_plat_swap();
        frame++;
    }
    return RP_OK;
}

void cf_take_stats(cf_ctx *c, cf_result *r)
{
    cf_stats_compute(c->samples, c->nsamples, c->scratch, &r->fs);
    r->has_frames = 1;
    cf_report_frames(&c->rep, c->iter + 1, r->id, r->mode, c->samples, c->nsamples);
}

/* ------------------------------------------------------------------ C0 -- */
static void log_env(cf_ctx *c, const cf_env *e)
{
    static const char *const V[5] = {"VRAM", "RAM", "PHYCONT", "BUDGET(CDLG)", "EXTERNAL"};
    int i;
    cf_report_log(&c->rep, "  clocks: ARM %d MHz, bus %d MHz, GPU %d MHz, GPU crossbar %d MHz", e->arm_mhz,
                  e->bus_mhz, e->gpu_mhz, e->xbar_mhz);
    cf_report_log(&c->rep, "  battery %d%%, charging %d, battery temperature value %d; system time %.0f s; ux0 free %lld bytes",
                  e->battery_pct, e->charging, e->battery_temp, e->uptime_s, (long long)e->ux0_free);
    for (i = 0; i < 5; i++)
        cf_report_log(&c->rep, "  vitaGL pool %-13s total %llu bytes, free %llu bytes", V[i],
                      (unsigned long long)e->vgl_total[i], (unsigned long long)e->vgl_free[i]);
    cf_report_log(&c->rep, "  C library heap block %llu bytes (0 = unknown), in use %llu, obtained %llu",
                  (unsigned long long)e->heap_total, (unsigned long long)e->heap_in_use,
                  (unsigned long long)e->heap_arena);
    cf_report_log(&c->rep, "  kernel free memory: USER_RW %lld, CDRAM %lld, PHYCONT %lld bytes (-1 = unavailable)",
                  (long long)e->kernel_free_user, (long long)e->kernel_free_cdram, (long long)e->kernel_free_phycont);
}

static void log_redirect(cf_ctx *c, const char *when)
{
    int opens = 0, mkdirs = 0, refused = 0;
    cf_plat_redirect_counts(&opens, &mkdirs, &refused);
    cf_report_log(&c->rep, "vitaGL shader cache redirected to %s %s: %d folder creations and %d file opens moved "
                           "from %s; %d refused",
                  CF_VGL_CACHE_DIR, when, mkdirs, opens, CF_VGL_CACHE_DEFAULT, refused);
}

static void group_env(cf_ctx *c)
{
    cf_env e;
    cf_pools p;
    cf_result *r;
    cf_progress(c, "C0-01", "ENVIRONMENT");
    cf_plat_env(&e);
    cf_plat_pools(&p);
    log_env(c, &e);
    r = cf_add(c, "C0-01", "ENVIRONMENT", "-");
    r->vram_kib = (int64_t)(p.p[CF_POOL_VRAM].free / 1024);
    r->glram_kib = (int64_t)(p.p[CF_POOL_GLRAM].free / 1024);
    r->heap_kib = (int64_t)(p.p[CF_POOL_HEAP].free / 1024);
    r->ok = 1;
    snprintf(r->detail, sizeof(r->detail),
             "free: VRAM %.1f of %.1f MiB; GL RAM %.1f of %.1f; heap %.1f of %.1f%s; kernel %.1f; ARM %d MHz",
             cf_mib(p.p[CF_POOL_VRAM].free), cf_mib(p.p[CF_POOL_VRAM].total), cf_mib(p.p[CF_POOL_GLRAM].free),
             cf_mib(p.p[CF_POOL_GLRAM].total), cf_mib(p.p[CF_POOL_HEAP].free), cf_mib(p.p[CF_POOL_HEAP].total),
             p.p[CF_POOL_HEAP].known ? "" : " (UNKNOWN: heap tiers will be skipped)",
             cf_mib(p.p[CF_POOL_KERNEL].free), e.arm_mhz);
    cf_done(c, r);
}

/* ------------------------------------------------------------------ C5 -- */
static int same_row(const cf_result *a, const cf_result *b)
{
    return strcmp(a->id, b->id) == 0 && strcmp(a->mode, b->mode) == 0 && a->status == b->status &&
           a->ok == b->ok && a->check == b->check;
}

static void compare_iterations(cf_ctx *c, int done)
{
    cf_result *r;
    int k, i, diffs = 0, rows_bad = 0;
    int n[CF_MAX_ITER];
    char first[96] = "";

    /* Row counts are taken before the C5 row itself is added to the last
     * iteration, so the comparison covers the test rows only. */
    for (k = 0; k < CF_MAX_ITER; k++) n[k] = c->nres[k];
    c->iter = done > 0 ? done - 1 : 0;
    r = cf_add(c, "C5-01", "REPEATABILITY", "-");
    if (done < 2) {
        snprintf(r->verdict, sizeof(r->verdict), "N/A");
        snprintf(r->detail, sizeof(r->detail), "%d complete iteration(s); nothing to compare", done);
        r->ok = 1;
        c->c5_pass = -1;
    } else {
        for (k = 1; k < done; k++) {
            if (n[k] != n[0]) {
                diffs++;
                if (!first[0]) snprintf(first, sizeof(first), "iteration %d has %d rows, iteration 1 has %d", k + 1,
                                        n[k], n[0]);
                continue;
            }
            for (i = 0; i < n[0]; i++) {
                if (!same_row(&c->res[0][i], &c->res[k][i])) {
                    diffs++;
                    if (!first[0])
                        snprintf(first, sizeof(first), "%s %s differs in iteration %d", c->res[k][i].id,
                                 c->res[k][i].mode, k + 1);
                }
            }
        }
        for (k = 0; k < done; k++)
            for (i = 0; i < n[k]; i++)
                if (!c->res[k][i].ok) rows_bad++;
        c->c5_pass = diffs == 0;
        r->ok = diffs == 0;
        snprintf(r->verdict, sizeof(r->verdict), diffs ? "FAIL" : "PASS");
        snprintf(r->detail, sizeof(r->detail),
                 "%d iterations x %d rows compared on id; mode; status; ok; check: %d difference(s); "
                 "rows not ok across all iterations: %d",
                 done, n[0], diffs, rows_bad);
        cf_append(r->detail, sizeof(r->detail), "; first: ", first);
    }
    snprintf(c->c5_note, sizeof(c->c5_note), "%s", r->detail);
    cf_done(c, r);

    /* timing spread (reported only; never a failure) */
    if (done >= 2) {
        cf_report_log(&c->rep, "Timing spread across iterations (P95 for frame rows, duration otherwise):");
        for (i = 0; i < n[0]; i++) {
            double lo = 1e30, hi = -1e30;
            int ok = 1;
            for (k = 0; k < done && ok; k++) {
                const cf_result *x = &c->res[k][i];
                double v;
                if (i >= n[k] || !same_row(x, &c->res[0][i])) { ok = 0; break; }
                v = x->has_frames ? x->fs.p95 : x->ms;
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
            if (ok && hi > 0.0 && (c->res[0][i].has_frames || c->res[0][i].ms > 0.0))
                cf_report_log(&c->rep, "  %-11s %-7s min %.3f max %.3f spread %.1f%%", c->res[0][i].id,
                              c->res[0][i].mode, lo, hi, lo > 0.0 ? (hi - lo) * 100.0 / lo : 0.0);
        }
    }
}

static void summary(cf_ctx *c, int done)
{
    int i, k;
    cf_report_log(&c->rep, "Summary (one column per complete iteration):");
    for (i = 0; done > 0 && i < c->nres[0]; i++) {
        const cf_result *r0 = &c->res[0][i];
        char cols[96] = "";
        size_t used = 0;
        if (strcmp(r0->verdict, "-") == 0)
            continue;
        for (k = 0; k < done; k++) {
            const char *v = i < c->nres[k] ? c->res[k][i].verdict : "?";
            int w = snprintf(cols + used, sizeof(cols) - used, "%s%s", k ? " / " : "", v);
            if (w < 0 || (size_t)w >= sizeof(cols) - used) break;
            used += (size_t)w;
        }
        cf_report_log(&c->rep, "  %-11s %-7s %-22s %s", r0->id, r0->mode, r0->name, cols);
    }
}

void cf_suite_run(cf_ctx *c, int iterations, cf_ui_fn ui)
{
    int it, done = 0;
    rp_mem_stats m;

    memset(c, 0, sizeof(*c));
    c->iterations = iterations < 1 ? 1 : (iterations > CF_MAX_ITER ? CF_MAX_ITER : iterations);
    c->ui = ui;
    c->t_start_ms = rp_now_ms();
    /* every draw (scenes and hud2d) uses vertex and colour arrays */
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    cf_report_open(&c->rep);
    cf_report_log(&c->rep, "Vita ContentFit run %03d (title %s)", c->rep.run_no, CF_TITLE_ID);
    cf_report_log(&c->rep, "Synthetic content only. No Mary Skelter file was read. Results describe this Vita and "
                           "this project's SDL2 + vitaGL stack, not Mary Skelter Finale.");
    cf_report_log(&c->rep, "zlib %s; fixtures: %d packs; iterations requested: %d", zlibVersion(),
                  cf_fix_pack_count(), c->iterations);
    cf_report_log(&c->rep, "Primary target: stable 30 fps (locked run: <=1%% of frames over 34.0 ms, P95 <= 34.0 ms, "
                           "none over 51.0 ms). 60 fps figures are diagnostic only.");
    cf_report_log(&c->rep, "Memory margins: VRAM 16 MiB, GL RAM 16 MiB, heap 24 MiB, kernel 8 MiB; every step "
                           "is checked before it allocates.");
    cf_plat_env(&c->env_start);
    cf_report_log(&c->rep, "Environment at start:");
    log_env(c, &c->env_start);
    log_redirect(c, "since the app started");

    for (it = 0; it < c->iterations && !c->aborted; it++) {
        c->iter = it;
        c->best_vram_tier = 0;
        c->mem_abort = 0;
        cf_report_log(&c->rep, "Iteration %d", it + 1);
        group_env(c);
        if (!cf_should_stop(c)) cf_group_io(c);
        if (!cf_should_stop(c)) cf_group_loads(c);
        if (!cf_should_stop(c)) cf_group_pacing(c);
        if (!cf_should_stop(c)) cf_group_scenes(c);
        if (!cf_should_stop(c)) cf_group_memory(c);
        if (!cf_should_stop(c)) cf_group_formats(c);
        if (c->aborted)
            cf_report_log(&c->rep, "Iteration %d stopped early: START pressed", it + 1);
        else
            done++;
    }
    compare_iterations(c, done);
    summary(c, done);
    m = rp_mem_get();
    cf_report_log(&c->rep, "Tracked rp_malloc bytes still allocated at the end: %lu; allocations %lu, frees %lu, "
                           "refused %lu",
                  (unsigned long)m.current, (unsigned long)m.allocs, (unsigned long)m.frees, (unsigned long)m.failed);
    log_redirect(c, "at the end of the run (counts since the app started)");
    cf_report_log(&c->rep, "Run time %.1f s. %s", (rp_now_ms() - c->t_start_ms) / 1000.0,
                  c->aborted ? "Stopped early by START." : "Complete.");
    cf_report_progress(&c->rep, "run finished after %.1f s%s", (rp_now_ms() - c->t_start_ms) / 1000.0,
                       c->aborted ? " (stopped early)" : "");
    cf_report_close(&c->rep);
    cf_plat_set_interval(1);
}
