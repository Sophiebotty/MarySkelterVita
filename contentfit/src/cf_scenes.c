/*
 * cf_scenes.c - C3: 30 fps pacing calibration, scene tiers, 60 fps
 * diagnostic, and texture formats (C3-T).
 */
#include <stdio.h>
#include <string.h>
#include <vitaGL.h>

#include "cf_scene.h"
#include "cf_suite.h"

#define TARGET_30_MS 33.333
#define PACE_TOL_MS  1.0

static double absd(double v) { return v < 0.0 ? -v : v; }

/* ----------------------------------------------------------- C3-P pacing -- */
void cf_group_pacing(cf_ctx *c)
{
    const cf_scene_def *d = cf_scene_find("C3-P");
    cf_result *r, *v;
    int rc2, use2;
    gp_status st;

    cf_progress(c, "C3-P", "30 FPS PACING CHECK");
    cf_scene_create(d, NULL, 0);
    rc2 = cf_plat_set_interval(2);
    c->lock_method = CF_LOCK_INTERVAL2;
    r = cf_add(c, "C3-P", "PACING_INTERVAL2", "INT2");
    st = cf_measure(c, "C3-P INTERVAL2", CF_M_LOCK, 1.0, 3.0, NULL, NULL);
    if (st != RP_OK) {
        r->status = st;
        cf_done(c, r);
        cf_scene_destroy();
        return;
    }
    cf_take_stats(c, r);
    use2 = rc2 == 0 && absd(r->fs.p50 - TARGET_30_MS) <= PACE_TOL_MS;
    r->ok = 1;
    snprintf(r->verdict, sizeof(r->verdict), use2 ? "HONOURED" : "NOT_HONOURED");
    snprintf(r->detail, sizeof(r->detail), "SDL_GL_SetSwapInterval(2) returned %d; median interval %.3f ms", rc2,
             r->fs.p50);
    cf_done(c, r);

    if (!use2) {
        c->lock_method = CF_LOCK_VBLANK2;
        r = cf_add(c, "C3-P", "PACING_VBLANK2", "VBL2");
        st = cf_measure(c, "C3-P VBLANK2", CF_M_LOCK, 1.0, 3.0, NULL, NULL);
        if (st != RP_OK) {
            r->status = st;
            cf_done(c, r);
            cf_scene_destroy();
            return;
        }
        cf_take_stats(c, r);
        r->ok = 1;
        snprintf(r->verdict, sizeof(r->verdict),
                 absd(r->fs.p50 - TARGET_30_MS) <= PACE_TOL_MS ? "LOCKED" : "NOT_LOCKED");
        snprintf(r->detail, sizeof(r->detail),
                 "swap interval 1 plus a wait for every second vertical blank; median interval %.3f ms", r->fs.p50);
        cf_done(c, r);
    }
    v = cf_add(c, "C3-P", "PACING_METHOD", "VERDICT");
    v->ok = 1;
    v->check = (uint32_t)c->lock_method;
    snprintf(v->verdict, sizeof(v->verdict), c->lock_method == CF_LOCK_INTERVAL2 ? "INTERVAL2" : "VBLANK2");
    snprintf(v->detail, sizeof(v->detail), "all LOCK30 runs in this iteration use %s",
             c->lock_method == CF_LOCK_INTERVAL2 ? "swap interval 2" : "swap interval 1 + every second vblank");
    cf_done(c, v);
    cf_scene_destroy();
    cf_plat_set_interval(1);
}

/* --------------------------------------------------------- scene tiers -- */
static void fill_mem(cf_result *r, const cf_pools *base, const cf_pools *made)
{
    r->vram_kib = cf_kib_delta(base, made, CF_POOL_VRAM);
    r->glram_kib = cf_kib_delta(base, made, CF_POOL_GLRAM);
    r->heap_kib = cf_kib_delta(base, made, CF_POOL_HEAP);
}

static void skipped_rows(cf_ctx *c, const cf_scene_def *d, gp_status st, const char *why, int lock, int off)
{
    cf_result *r;
    if (lock) {
        r = cf_add(c, d->id, d->name, "LOCK30");
        r->status = st;
        r->ok = st == CF_ERR_SKIPPED || st == CF_ERR_UNKNOWN_SIZE || st == CF_ERR_UNSUPPORTED;
        snprintf(r->verdict, sizeof(r->verdict), "SKIPPED");
        snprintf(r->detail, sizeof(r->detail), "%s", why);
        cf_done(c, r);
    }
    if (off) {
        r = cf_add(c, d->id, d->name, "OFF");
        r->status = st;
        r->ok = st == CF_ERR_SKIPPED || st == CF_ERR_UNKNOWN_SIZE || st == CF_ERR_UNSUPPORTED;
        snprintf(r->verdict, sizeof(r->verdict), st == CF_ERR_UNSUPPORTED ? "UNSUPPORTED" : "SKIPPED");
        snprintf(r->detail, sizeof(r->detail), "%s", why);
        cf_done(c, r);
    }
}

/* Creates the scene after the memory pre-check. RP_OK when it exists. */
static gp_status prepare(cf_ctx *c, const cf_scene_def *d, cf_pools *base, cf_pools *made, char *why, size_t n)
{
    cf_need need;
    gp_status st;
    (void)c;
    cf_plat_pools(base);
    cf_scene_need(d, &need);
    st = cf_precheck(base, &need, why, n);
    if (st != RP_OK)
        return st;
    st = cf_scene_create(d, why, n);
    cf_plat_pools(made);
    return st;
}

static void finish_scene(cf_ctx *c, const cf_pools *base, int *baseline_ok, char *why, size_t n)
{
    cf_pools now;
    cf_scene_destroy();
    *baseline_ok = cf_settle(c, base, &now, why, n);
}

static void run_tier(cf_ctx *c, const cf_scene_def *d)
{
    cf_pools base, made;
    cf_result *rl, *ro, *rv;
    char why[160], bwhy[128];
    gp_status st;
    int base_ok = 1;
    uint32_t crc;
    cf_fit fit;

    cf_progress(c, d->id, d->name);
    st = prepare(c, d, &base, &made, why, sizeof(why));
    if (st != RP_OK) {
        skipped_rows(c, d, st, why, 1, 1);
        if (st != CF_ERR_SKIPPED && st != CF_ERR_UNKNOWN_SIZE) {
            cf_pools now;
            cf_settle(c, &base, &now, bwhy, sizeof(bwhy));
        }
        rv = cf_add(c, d->id, d->name, "VERDICT");
        rv->status = st;
        rv->ok = st == CF_ERR_SKIPPED || st == CF_ERR_UNKNOWN_SIZE;
        snprintf(rv->verdict, sizeof(rv->verdict), "SKIPPED");
        snprintf(rv->detail, sizeof(rv->detail), "%s", why);
        cf_done(c, rv);
        return;
    }
    crc = cf_scene_crc();

    rl = cf_add(c, d->id, d->name, "LOCK30");
    st = cf_measure(c, d->id, CF_M_LOCK, 2.0, 10.0, NULL, NULL);
    if (st != RP_OK) {
        rl->status = st;
        cf_done(c, rl);
        cf_scene_destroy();
        return;
    }
    cf_take_stats(c, rl);
    rl->ok = 1;
    rl->check = crc;
    fill_mem(rl, &base, &made);
    snprintf(rl->verdict, sizeof(rl->verdict), cf_stable30(&rl->fs) ? "STABLE" : "UNSTABLE");
    snprintf(rl->detail, sizeof(rl->detail), "30 fps locked; %d of %d intervals over 34.0 ms; %d over 51.0 ms",
             rl->fs.over_34, rl->fs.n, rl->fs.over_51);
    cf_done(c, rl);

    ro = cf_add(c, d->id, d->name, "OFF");
    st = cf_measure(c, d->id, CF_M_OFF, 1.0, 5.0, NULL, NULL);
    if (st != RP_OK) {
        ro->status = st;
        cf_done(c, ro);
        cf_scene_destroy();
        return;
    }
    cf_take_stats(c, ro);
    ro->ok = 1;
    ro->check = crc;
    fill_mem(ro, &base, &made);
    snprintf(ro->verdict, sizeof(ro->verdict), cf_60_candidate(&ro->fs) ? "60_CANDIDATE" : "-");
    snprintf(ro->detail, sizeof(ro->detail), "vsync off: rendering cost; P95 %.2f ms (8.3 ms = 60 fps diagnostic)",
             ro->fs.p95);
    cf_done(c, ro);

    finish_scene(c, &base, &base_ok, bwhy, sizeof(bwhy));
    fit = cf_classify(&rl->fs, &ro->fs);
    rv = cf_add(c, d->id, d->name, "VERDICT");
    rv->ok = base_ok;
    rv->status = base_ok ? RP_OK : CF_ERR_BASELINE;
    rv->check = crc;
    fill_mem(rv, &base, &made);
    snprintf(rv->verdict, sizeof(rv->verdict), "%s", cf_fit_name(fit));
    snprintf(rv->detail, sizeof(rv->detail), "cost P95 %.2f ms; locked P95 %.2f ms; %s; content CRC %08x",
             ro->fs.p95, rl->fs.p95, cf_stable30(&rl->fs) ? "stable" : "not stable", (unsigned)crc);
    if (!base_ok) cf_append(rv->detail, sizeof(rv->detail), "; ", bwhy);
    cf_done(c, rv);
}

void cf_group_scenes(cf_ctx *c)
{
    static const char *const IDS[] = {
        "C3-D-S", "C3-D-M", "C3-D-L", "C3-D-XL", "C3-B-S", "C3-B-M", "C3-B-L", "C3-B-XL",
        "C3-V-S", "C3-V-M", "C3-V-L", "C3-V-XL", "C3-U-S", "C3-U-M", "C3-U-L", "C3-U-XL", "C3-X",
    };
    const cf_scene_def *d;
    cf_pools base, made;
    cf_result *r;
    char why[160];
    gp_status st;
    unsigned i;
    int base_ok;

    for (i = 0; i < sizeof(IDS) / sizeof(IDS[0]); i++) {
        if (cf_should_stop(c)) return;
        run_tier(c, cf_scene_find(IDS[i]));
    }
    if (cf_should_stop(c)) return;

    /* 60 fps diagnostic: dungeon M with swap interval 1. Never a verdict. */
    d = cf_scene_find("C3-D-M");
    cf_progress(c, "C3-D60", "60 FPS DIAGNOSTIC");
    st = prepare(c, d, &base, &made, why, sizeof(why));
    r = cf_add(c, "C3-D60", "DUNGEON_M_60FPS_DIAG", "V1");
    if (st != RP_OK) {
        r->status = st;
        r->ok = st == CF_ERR_SKIPPED || st == CF_ERR_UNKNOWN_SIZE;
        snprintf(r->verdict, sizeof(r->verdict), "SKIPPED");
        snprintf(r->detail, sizeof(r->detail), "%s", why);
        cf_done(c, r);
        cf_scene_destroy();
        return;
    }
    r->check = cf_scene_crc();
    st = cf_measure(c, "C3-D60", CF_M_V1, 1.0, 5.0, NULL, NULL);
    if (st == RP_OK) {
        cf_take_stats(c, r);
        r->ok = 1;
        snprintf(r->verdict, sizeof(r->verdict), r->fs.p95 <= 17.4 ? "DIAG_60_OK" : "DIAG_60_NO");
        snprintf(r->detail, sizeof(r->detail), "diagnostic only; swap interval 1; %d of %d intervals over 16.7 ms",
                 r->fs.over_16_7, r->fs.n);
    } else {
        r->status = st;
    }
    finish_scene(c, &base, &base_ok, why, sizeof(why));
    if (st == RP_OK && !base_ok) {
        r->ok = 0;
        r->status = CF_ERR_BASELINE;
        snprintf(r->detail, sizeof(r->detail), "%s", why);
    }
    cf_done(c, r);
}

/* ------------------------------------------------------- C3-T formats -- */
void cf_group_formats(cf_ctx *c)
{
    static const char *const IDS[] = {"C3-T-RGBA8", "C3-T-RGB565", "C3-T-DXT1",
                                      "C3-T-DXT5", "C3-T-ETC1", "C3-T-PVRTC4"};
    unsigned i;
    for (i = 0; i < sizeof(IDS) / sizeof(IDS[0]); i++) {
        const cf_scene_def *d = cf_scene_find(IDS[i]);
        cf_pools base, made;
        cf_result *r;
        char why[160], bwhy[128];
        gp_status st;
        int base_ok;
        uint64_t predicted = cf_scene_texture_bytes(d);

        if (cf_should_stop(c)) return;
        cf_progress(c, d->id, d->name);
        st = prepare(c, d, &base, &made, why, sizeof(why));
        if (st != RP_OK) {
            skipped_rows(c, d, st, why, 0, 1);
            cf_scene_destroy();
            cf_settle(c, &base, &made, bwhy, sizeof(bwhy));
            continue;
        }
        r = cf_add(c, d->id, d->name, "OFF");
        r->check = cf_scene_crc();
        fill_mem(r, &base, &made);
        st = cf_measure(c, d->id, CF_M_OFF, 1.0, 5.0, NULL, NULL);
        if (st != RP_OK) {
            r->status = st;
            cf_done(c, r);
            cf_scene_destroy();
            return;
        }
        cf_take_stats(c, r);
        finish_scene(c, &base, &base_ok, bwhy, sizeof(bwhy));
        r->ok = base_ok;
        r->status = base_ok ? RP_OK : CF_ERR_BASELINE;
        snprintf(r->verdict, sizeof(r->verdict), "SUPPORTED");
        snprintf(r->detail, sizeof(r->detail),
                 "8 x 256^2 %s with mipmaps; textures by format size %.1f KiB; VRAM used by the scene %lld KiB "
                 "(textures + buffers)",
                 cf_tf_name(d->texfmt), (double)predicted / 1024.0, (long long)r->vram_kib);
        if (!base_ok) cf_append(r->detail, sizeof(r->detail), "; ", bwhy);
        cf_done(c, r);
    }
}
