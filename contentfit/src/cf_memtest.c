/*
 * cf_memtest.c - C4: memory fit.
 *
 *  C4-V16..V64  textures resident in VRAM (1 MiB RGBA8 textures)
 *  C4-H16..H96  CPU working set in the C library heap (2 MiB chunks, written
 *               and read back)
 *  C4-CYC       20 area load/unload cycles (two different size mixes), then
 *               the largest VRAM tier that fitted is allocated again
 *  C4-STRM      dungeon M at locked 30 fps while a worker streams and inflates
 *               the 16 MiB pack and the main thread uploads its textures
 *  C4-SOAK      battle M at locked 30 fps for 30 s with 48 MiB of VRAM and
 *               64 MiB of heap held
 *
 * Every step passes cf_precheck before allocating anything. While allocating,
 * the pool is read again after every chunk: falling below a margin or memory
 * landing in another vitaGL pool stops the step (MARGIN / SPILL). If memory
 * does not return to its starting level afterwards, the remaining memory
 * steps are recorded as not run instead of running on a dirty baseline.
 */
#include <stdio.h>
#include <string.h>
#include <vitaGL.h>

#include "cf_fixdefs.h"
#include "cf_pack.h"
#include "cf_scene.h"
#include "cf_suite.h"
#include "rp_mem.h"
#include "rp_time.h"

#define TEX_DIM   512                 /* 512 x 512 RGBA8 = 1 MiB */
#define CHUNK     (2u * CF_MIB)
#define MAX_TIER  96

static int abort_row(cf_ctx *c, const char *id, const char *name, const char *mode)
{
    cf_result *r;
    if (!c->mem_abort) return 0;
    r = cf_add(c, id, name, mode);
    r->status = CF_ERR_BASELINE;
    r->ok = 0;
    snprintf(r->verdict, sizeof(r->verdict), "NOT_RUN");
    snprintf(r->detail, sizeof(r->detail), "not run: an earlier memory step did not return to its starting level");
    cf_done(c, r);
    return 1;
}

static void skip_row(cf_ctx *c, cf_result *r, gp_status st, const char *why)
{
    r->status = st;
    r->ok = st == CF_ERR_SKIPPED || st == CF_ERR_UNKNOWN_SIZE;
    snprintf(r->verdict, sizeof(r->verdict), "SKIPPED");
    snprintf(r->detail, sizeof(r->detail), "%s", why);
    cf_done(c, r);
}

/* Allocates 'mib' 1 MiB textures. Checks VRAM margin and spill after each. */
static gp_status alloc_vram(int mib, const uint8_t *pattern, GLuint *ids, int *made, double *ms, char *why,
                            size_t n)
{
    cf_pools prev, now;
    cf_pool_id where = CF_POOL_GLRAM;
    gp_status st = RP_OK;
    double t0 = rp_now_ms();
    int k;
    *made = 0;
    cf_plat_pools(&prev);
    for (k = 0; k < mib; k++) {
        GLenum e;
        while (glGetError() != GL_NO_ERROR) { }
        glGenTextures(1, &ids[k]);
        (*made)++;
        glBindTexture(GL_TEXTURE_2D, ids[k]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_DIM, TEX_DIM, 0, GL_RGBA, GL_UNSIGNED_BYTE, pattern);
        e = glGetError();
        if (e != GL_NO_ERROR) {
            snprintf(why, n, "texture %d: GL error 0x%X", k + 1, (unsigned)e);
            st = CF_ERR_GL;
            break;
        }
        cf_plat_pools(&now);
        st = cf_guard(&now, CF_POOL_VRAM, why, n);
        if (st != RP_OK) break;
        if (cf_spilled(&prev, &now, CF_POOL_VRAM, CF_BASELINE_TOL, &where)) {
            snprintf(why, n, "texture %d took memory from %s (%.2f MiB less free)", k + 1, cf_pool_name(where),
                     cf_mib(prev.p[where].free - now.p[where].free));
            st = CF_ERR_SPILL;
            break;
        }
        prev = now;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glFinish();
    *ms = rp_now_ms() - t0;
    return st;
}

static void vram_tier(cf_ctx *c, int mib, const uint8_t *pattern)
{
    GLuint ids[64];
    cf_pools base, full, now;
    cf_need need;
    cf_result *r;
    char id[16], name[40], why[160], bwhy[128];
    gp_status st;
    double ms = 0.0;
    int made = 0, base_ok;

    snprintf(id, sizeof(id), "C4-V%d", mib);
    snprintf(name, sizeof(name), "VRAM_RESIDENT_%dMIB", mib);
    if (cf_should_stop(c) || abort_row(c, id, name, "-")) return;
    cf_progress(c, id, name);
    r = cf_add(c, id, name, "-");
    memset(&need, 0, sizeof(need));
    need.b[CF_POOL_VRAM] = (uint64_t)mib * CF_MIB;
    cf_plat_pools(&base);
    st = cf_precheck(&base, &need, why, sizeof(why));
    if (st != RP_OK) {
        skip_row(c, r, st, why);
        return;
    }
    why[0] = '\0';
    st = alloc_vram(mib, pattern, ids, &made, &ms, why, sizeof(why));
    cf_plat_pools(&full);
    if (made) glDeleteTextures(made, ids);
    base_ok = cf_settle(c, &base, &now, bwhy, sizeof(bwhy));
    if (!base_ok) c->mem_abort = 1;
    r->status = st != RP_OK ? st : (base_ok ? RP_OK : CF_ERR_BASELINE);
    r->ok = r->status == RP_OK;
    r->ms = ms;
    r->mbytes = (double)made;
    r->mbps = ms > 0.0 ? (double)made / (ms / 1000.0) : 0.0;
    r->vram_kib = cf_kib_delta(&base, &full, CF_POOL_VRAM);
    r->glram_kib = cf_kib_delta(&base, &full, CF_POOL_GLRAM);
    r->heap_kib = cf_kib_delta(&base, &full, CF_POOL_HEAP);
    r->check = r->ok ? (uint32_t)mib : 0;
    if (r->ok && mib > c->best_vram_tier) c->best_vram_tier = mib;
    snprintf(r->verdict, sizeof(r->verdict), r->ok ? "FITS" : (st == RP_OK ? "BASELINE" : "STOPPED"));
    snprintf(r->detail, sizeof(r->detail),
             "%d of %d x 1 MiB RGBA8 textures; VRAM used %.1f MiB, %.1f MiB left; upload %.1f MiB/s", made, mib,
             (double)r->vram_kib / 1024.0, cf_mib(full.p[CF_POOL_VRAM].free), r->mbps);
    cf_append(r->detail, sizeof(r->detail), "; ", why);
    if (!base_ok) cf_append(r->detail, sizeof(r->detail), "; ", bwhy);
    cf_done(c, r);
}

static void heap_tier(cf_ctx *c, int mib)
{
    uint32_t *chunk[MAX_TIER / 2];
    const int n = mib / 2;
    cf_pools base, now;
    cf_need need;
    cf_result *r;
    char id[16], name[40], why[160], bwhy[128];
    gp_status st = RP_OK;
    double t0;
    int k, made = 0, base_ok;
    uint32_t bad = 0, w;

    snprintf(id, sizeof(id), "C4-H%d", mib);
    snprintf(name, sizeof(name), "HEAP_WORKING_SET_%dMIB", mib);
    if (cf_should_stop(c) || abort_row(c, id, name, "-")) return;
    cf_progress(c, id, name);
    r = cf_add(c, id, name, "-");
    memset(&need, 0, sizeof(need));
    need.b[CF_POOL_HEAP] = (uint64_t)mib * CF_MIB;
    cf_plat_pools(&base);
    st = cf_precheck(&base, &need, why, sizeof(why));
    if (st != RP_OK) {
        skip_row(c, r, st, why);
        return;
    }
    why[0] = '\0';
    memset(chunk, 0, sizeof(chunk));
    t0 = rp_now_ms();
    for (k = 0; k < n; k++) {
        chunk[k] = (uint32_t *)rp_malloc(CHUNK);
        if (!chunk[k]) {
            snprintf(why, sizeof(why), "chunk %d of %d refused by the allocator", k + 1, n);
            st = RP_ERR_NOMEM;
            break;
        }
        made++;
        for (w = 0; w < CHUNK / 4u; w++)
            chunk[k][w] = ((uint32_t)k << 20) ^ w ^ 0xA5A5A5A5u;
        cf_plat_pools(&now);
        st = cf_guard(&now, CF_POOL_HEAP, why, sizeof(why));
        if (st == RP_OK && now.p[CF_POOL_KERNEL].known) st = cf_guard(&now, CF_POOL_KERNEL, why, sizeof(why));
        if (st != RP_OK) break;
    }
    for (k = 0; k < made; k++)                     /* read everything back */
        for (w = 0; w < CHUNK / 4u; w++)
            if (chunk[k][w] != (((uint32_t)k << 20) ^ w ^ 0xA5A5A5A5u)) bad++;
    r->ms = rp_now_ms() - t0;
    cf_plat_pools(&now);
    r->heap_kib = cf_kib_delta(&base, &now, CF_POOL_HEAP);
    for (k = 0; k < made; k++) rp_free(chunk[k]);
    base_ok = cf_settle(c, &base, &now, bwhy, sizeof(bwhy));
    if (!base_ok) c->mem_abort = 1;
    if (st == RP_OK && bad) st = RP_ERR_CRC;
    r->status = st != RP_OK ? st : (base_ok ? RP_OK : CF_ERR_BASELINE);
    r->ok = r->status == RP_OK;
    r->mbytes = 2.0 * made;
    r->mbps = r->ms > 0.0 ? r->mbytes / (r->ms / 1000.0) : 0.0;
    r->check = r->ok ? (uint32_t)mib : 0;
    snprintf(r->verdict, sizeof(r->verdict), r->ok ? "FITS" : (st == RP_OK ? "BASELINE" : "STOPPED"));
    snprintf(r->detail, sizeof(r->detail),
             "%d of %d x 2 MiB chunks written and read back (%u bad words); heap %.1f MiB free at start", made, n,
             (unsigned)bad, cf_mib(base.p[CF_POOL_HEAP].free));
    cf_append(r->detail, sizeof(r->detail), "; ", why);
    if (!base_ok) cf_append(r->detail, sizeof(r->detail), "; ", bwhy);
    cf_done(c, r);
}

/* --------------------------------------------------------------- cycles -- */
typedef struct { int dim, count; } tex_mix;
typedef struct { uint32_t size, count; } blob_mix;
static const tex_mix AREA_TEX[2][3] = {{{1024, 2}, {512, 4}, {256, 8}}, {{512, 10}, {256, 12}, {128, 16}}};
static const blob_mix AREA_BLOB[2][3] = {{{CF_MIB, 4}, {256u * CF_KIB, 8}, {64u * CF_KIB, 16}},
                                         {{512u * CF_KIB, 8}, {128u * CF_KIB, 16}, {16u * CF_KIB, 64}}};
#define AREA_MAX_TEX 40
#define AREA_MAX_BLOB 88

static gp_status one_area(int which, const uint8_t *pattern, char *why, size_t n)
{
    GLuint t[AREA_MAX_TEX];
    void *b[AREA_MAX_BLOB];
    int nt = 0, nb = 0, i, j;
    gp_status st = RP_OK;
    while (glGetError() != GL_NO_ERROR) { }
    for (i = 0; i < 3 && st == RP_OK; i++) {
        for (j = 0; j < AREA_TEX[which][i].count; j++) {
            int d = AREA_TEX[which][i].dim;
            glGenTextures(1, &t[nt]);
            glBindTexture(GL_TEXTURE_2D, t[nt++]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, d, d, 0, GL_RGBA, GL_UNSIGNED_BYTE, pattern);
        }
        for (j = 0; j < (int)AREA_BLOB[which][i].count && st == RP_OK; j++) {
            b[nb] = rp_malloc(AREA_BLOB[which][i].size);
            if (!b[nb]) {
                snprintf(why, n, "area %c: heap block refused", 'A' + which);
                st = RP_ERR_NOMEM;
                break;
            }
            memset(b[nb++], 0x40 + which, AREA_BLOB[which][i].size);
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    if (st == RP_OK && glGetError() != GL_NO_ERROR) {
        snprintf(why, n, "area %c: texture upload error", 'A' + which);
        st = CF_ERR_GL;
    }
    glFinish();
    glDeleteTextures(nt, t);
    for (i = 0; i < nb; i++) rp_free(b[i]);
    return st;
}

static void cycles(cf_ctx *c)
{
    const char *id = "C4-CYC", *name = "AREA_CYCLES_20";
    cf_pools base, now;
    cf_need need;
    cf_result *r;
    uint8_t *pattern;
    char why[160], bwhy[128];
    gp_status st;
    int k, base_ok, refit = 0;
    int64_t drift[CF_POOL_COUNT];

    if (cf_should_stop(c) || abort_row(c, id, name, "-")) return;
    cf_progress(c, id, name);
    r = cf_add(c, id, name, "-");
    memset(&need, 0, sizeof(need));
    need.b[CF_POOL_VRAM] = 14u * CF_MIB;
    need.b[CF_POOL_HEAP] = 7u * CF_MIB + 4u * CF_MIB;
    cf_plat_pools(&base);
    st = cf_precheck(&base, &need, why, sizeof(why));
    if (st != RP_OK) {
        skip_row(c, r, st, why);
        return;
    }
    why[0] = '\0';
    pattern = (uint8_t *)rp_malloc(4u * CF_MIB);
    if (!pattern) {
        r->status = RP_ERR_NOMEM;
        cf_done(c, r);
        return;
    }
    cf_tex_rgba(pattern, 1024, 1024, 77u, CF_STY_TILES);
    cf_plat_pools(&base);                       /* baseline includes the pattern buffer */
    memset(drift, 0, sizeof(drift));
    r->ms = rp_now_ms();
    for (k = 0; k < 20 && st == RP_OK; k++) {
        int p;
        st = one_area(k & 1, pattern, why, sizeof(why));
        glFinish();
        cf_plat_pools(&now);
        for (p = 0; p < CF_POOL_COUNT; p++) {
            int64_t dk = cf_kib_delta(&base, &now, (cf_pool_id)p);
            if (dk > drift[p]) drift[p] = dk;
        }
        if (cf_should_stop(c)) { st = CF_ERR_ABORTED; break; }
    }
    r->ms = rp_now_ms() - r->ms;
    rp_free(pattern);
    pattern = NULL;
    base_ok = cf_settle(c, &base, &now, bwhy, sizeof(bwhy));
    if (!base_ok) c->mem_abort = 1;
    /* fragmentation check: the largest VRAM tier that fitted earlier must fit again */
    if (st == RP_OK && base_ok && c->best_vram_tier > 0) {
        GLuint ids[64];
        int made = 0;
        double ms;
        cf_pools b2;
        uint8_t *pat = (uint8_t *)rp_malloc(CF_MIB);
        memset(&need, 0, sizeof(need));
        need.b[CF_POOL_VRAM] = (uint64_t)c->best_vram_tier * CF_MIB;
        cf_plat_pools(&b2);
        if (pat && cf_precheck(&b2, &need, bwhy, sizeof(bwhy)) == RP_OK) {
            memset(pat, 0x5A, CF_MIB);
            refit = alloc_vram(c->best_vram_tier, pat, ids, &made, &ms, bwhy, sizeof(bwhy)) == RP_OK ? 1 : -1;
            if (made) glDeleteTextures(made, ids);
        } else {
            refit = -1;
        }
        rp_free(pat);
        if (!cf_settle(c, &b2, &now, bwhy, sizeof(bwhy))) {
            c->mem_abort = 1;
            base_ok = 0;
        }
    }
    r->status = st != RP_OK ? st : (!base_ok ? CF_ERR_BASELINE : (refit < 0 ? CF_ERR_MARGIN : RP_OK));
    r->ok = r->status == RP_OK;
    r->vram_kib = drift[CF_POOL_VRAM];
    r->glram_kib = drift[CF_POOL_GLRAM];
    r->heap_kib = drift[CF_POOL_HEAP];
    r->check = r->ok ? 20u : 0u;
    snprintf(r->verdict, sizeof(r->verdict), r->ok ? "NO_DRIFT" : (refit < 0 ? "REFIT_FAILED" : "DRIFT"));
    snprintf(r->detail, sizeof(r->detail),
             "20 cycles of 14 MiB textures + 7 MiB heap; largest drift VRAM %lld KiB, GL RAM %lld KiB, heap %lld KiB;"
             " refit of %d MiB VRAM tier: %s",
             (long long)drift[CF_POOL_VRAM], (long long)drift[CF_POOL_GLRAM], (long long)drift[CF_POOL_HEAP],
             c->best_vram_tier, refit > 0 ? "ok" : (refit < 0 ? "FAILED" : "not run"));
    cf_append(r->detail, sizeof(r->detail), "; ", why);
    if (refit < 0 || !base_ok) cf_append(r->detail, sizeof(r->detail), "; ", bwhy);
    cf_done(c, r);
}

/* ------------------------------------------------------------ streaming -- */
typedef struct {
    cf_ring  ring;
    GLuint   tex[4];
    int      rot;
    uint32_t entries, uploads;
    uint64_t comp_bytes;
    int      errors;
    gp_status first_err;
    double   upload_ms, worst_upload_ms;
} stream_state;

static void stream_hook(cf_ctx *c, void *user, uint32_t frame, int measuring)
{
    stream_state *s = (stream_state *)user;
    cf_slot *slot = cf_ring_take(&s->ring, 0);
    (void)c;
    (void)frame;
    if (!slot) return;
    if (slot->st != RP_OK) {
        if (!s->errors) s->first_err = slot->st;
        s->errors++;
    } else {
        if (slot->pay.kind == CF_KIND_TEX) {
            double t0 = rp_now_ms(), dt;
            glBindTexture(GL_TEXTURE_2D, s->tex[s->rot]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)slot->pay.w, (GLsizei)slot->pay.h, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, slot->pay.body);
            glBindTexture(GL_TEXTURE_2D, 0);
            s->rot = (s->rot + 1) & 3;
            dt = rp_now_ms() - t0;
            if (measuring) {
                s->upload_ms += dt;
                s->uploads++;
                if (dt > s->worst_upload_ms) s->worst_upload_ms = dt;
            }
        }
        if (measuring) {
            s->entries++;
            s->comp_bytes += slot->region;
        }
    }
    cf_ring_release(&s->ring, slot);
}

static void streaming(cf_ctx *c)
{
    const char *id = "C4-STRM", *name = "STREAMING_DUNGEON_M";
    const cf_scene_def *d = cf_scene_find("C3-D-M");
    const cf_fix_pack *fp = cf_fix_pack_get(1);           /* 16 MiB pack */
    static stream_state s;
    cf_pools base, now;
    cf_need need;
    cf_result *r;
    gp_gdat a;
    char path[64], why[160], bwhy[128];
    gp_status st;
    int base_ok;

    if (cf_should_stop(c) || abort_row(c, id, name, "LOCK30")) return;
    cf_progress(c, id, name);
    r = cf_add(c, id, name, "LOCK30");
    memset(&s, 0, sizeof(s));
    s.ring.thread = -1;
    memset(&a, 0, sizeof(a));
    cf_scene_need(d, &need);
    need.b[CF_POOL_VRAM] += 4u * (uint64_t)(fp->max_usize - CF_PAYLOAD_HDR);
    need.b[CF_POOL_HEAP] += cf_ring_heap_bytes(2, fp->max_region, fp->max_usize, 1) + fp->entries * 16u;
    need.b[CF_POOL_KERNEL] = CF_RING_STACK;
    cf_plat_pools(&base);
    st = cf_precheck(&base, &need, why, sizeof(why));
    if (st != RP_OK) {
        skip_row(c, r, st, why);
        return;
    }
    st = cf_scene_create(d, why, sizeof(why));
    snprintf(path, sizeof(path), "%s%s", CF_FIX_PREFIX, fp->name);
    if (st == RP_OK) st = gp_gdat_open(&a, path, GP_STRICT);
    if (st == RP_OK) st = cf_ring_start(&s.ring, path, &a, 2, fp->max_region, fp->max_usize, 1, 1);
    if (st == RP_OK) {
        glGenTextures(4, s.tex);
        st = cf_measure(c, id, CF_M_LOCK, 2.0, 10.0, stream_hook, &s);
        if (st == RP_OK) cf_take_stats(c, r);
    }
    cf_ring_stop(&s.ring);
    if (a.fp || a.idx) gp_gdat_close(&a);
    if (s.tex[0]) glDeleteTextures(4, s.tex);
    cf_scene_destroy();
    base_ok = cf_settle(c, &base, &now, bwhy, sizeof(bwhy));
    if (!base_ok) c->mem_abort = 1;
    if (st == RP_OK && s.errors) st = s.first_err;
    r->status = st != RP_OK ? st : (base_ok ? RP_OK : CF_ERR_BASELINE);
    r->ok = r->status == RP_OK;
    r->mbytes = cf_mib(s.comp_bytes);
    r->mbps = r->mbytes / 10.0;
    if (r->ok)
        snprintf(r->verdict, sizeof(r->verdict), cf_stable30(&r->fs) ? "NO_HITCHES" : "HITCHES");
    else
        snprintf(r->verdict, sizeof(r->verdict), "ERROR");
    snprintf(r->detail, sizeof(r->detail),
             "%u entries streamed while measuring (%.2f MiB/s compressed); %u textures uploaded, average %.2f ms, "
             "worst %.2f ms; %d errors; %d of %d intervals over 34.0 ms",
             (unsigned)s.entries, r->mbps, (unsigned)s.uploads, s.uploads ? s.upload_ms / s.uploads : 0.0,
             s.worst_upload_ms, s.errors, r->fs.over_34, r->fs.n);
    if (st != RP_OK) cf_append(r->detail, sizeof(r->detail), "; ", why);
    if (!base_ok) cf_append(r->detail, sizeof(r->detail), "; ", bwhy);
    cf_done(c, r);
}

/* ----------------------------------------------------------------- soak -- */
static void soak(cf_ctx *c)
{
    const char *id = "C4-SOAK", *name = "SOAK_BATTLE_M_48V_64H";
    const cf_scene_def *d = cf_scene_find("C3-B-M");
    GLuint ids[48];
    uint32_t *chunk[32];
    cf_pools base, now;
    cf_need need;
    cf_result *r;
    uint8_t *pattern = NULL;
    char why[160], bwhy[128];
    gp_status st;
    double ms;
    int made = 0, nchunks = 0, k, base_ok;

    if (cf_should_stop(c) || abort_row(c, id, name, "LOCK30")) return;
    cf_progress(c, id, name);
    r = cf_add(c, id, name, "LOCK30");
    cf_scene_need(d, &need);
    need.b[CF_POOL_VRAM] += 48u * CF_MIB;
    need.b[CF_POOL_HEAP] += 64u * CF_MIB + CF_MIB;
    cf_plat_pools(&base);
    st = cf_precheck(&base, &need, why, sizeof(why));
    if (st != RP_OK) {
        skip_row(c, r, st, why);
        return;
    }
    why[0] = '\0';
    memset(chunk, 0, sizeof(chunk));
    pattern = (uint8_t *)rp_malloc(CF_MIB);
    st = pattern ? RP_OK : RP_ERR_NOMEM;
    if (st == RP_OK) {
        memset(pattern, 0x33, CF_MIB);
        st = alloc_vram(48, pattern, ids, &made, &ms, why, sizeof(why));
    }
    for (k = 0; st == RP_OK && k < 32; k++) {
        chunk[k] = (uint32_t *)rp_malloc(CHUNK);
        if (!chunk[k]) {
            st = RP_ERR_NOMEM;
            break;
        }
        nchunks++;
        memset(chunk[k], k, CHUNK);
        cf_plat_pools(&now);
        st = cf_guard(&now, CF_POOL_HEAP, why, sizeof(why));
    }
    if (st == RP_OK) st = cf_scene_create(d, why, sizeof(why));
    cf_plat_pools(&now);
    r->vram_kib = cf_kib_delta(&base, &now, CF_POOL_VRAM);
    r->heap_kib = cf_kib_delta(&base, &now, CF_POOL_HEAP);
    r->glram_kib = cf_kib_delta(&base, &now, CF_POOL_GLRAM);
    if (st == RP_OK) {
        r->check = cf_scene_crc();
        st = cf_measure(c, id, CF_M_LOCK, 2.0, 30.0, NULL, NULL);
        if (st == RP_OK) cf_take_stats(c, r);
    }
    cf_scene_destroy();
    if (made) glDeleteTextures(made, ids);
    for (k = 0; k < nchunks; k++) rp_free(chunk[k]);
    rp_free(pattern);
    base_ok = cf_settle(c, &base, &now, bwhy, sizeof(bwhy));
    if (!base_ok) c->mem_abort = 1;
    r->status = st != RP_OK ? st : (base_ok ? RP_OK : CF_ERR_BASELINE);
    r->ok = r->status == RP_OK;
    if (!r->ok) r->check = 0;
    snprintf(r->verdict, sizeof(r->verdict), !r->ok ? "ERROR" : (cf_stable30(&r->fs) ? "STABLE" : "UNSTABLE"));
    snprintf(r->detail, sizeof(r->detail),
             "battle M at locked 30 fps for 30 s holding %d MiB of textures and %d MiB of heap; VRAM used %.1f MiB",
             made, 2 * nchunks, (double)r->vram_kib / 1024.0);
    cf_append(r->detail, sizeof(r->detail), "; ", why);
    if (!base_ok) cf_append(r->detail, sizeof(r->detail), "; ", bwhy);
    cf_done(c, r);
}

void cf_group_memory(cf_ctx *c)
{
    static const int VT[] = {16, 32, 48, 64};
    static const int HT[] = {16, 32, 64, 96};
    uint8_t *pattern;
    unsigned i;

    pattern = (uint8_t *)rp_malloc(CF_MIB);
    if (pattern) {
        cf_tex_rgba(pattern, TEX_DIM, TEX_DIM, 55u, CF_STY_TILES);
        for (i = 0; i < 4; i++) vram_tier(c, VT[i], pattern);
        rp_free(pattern);
    } else {
        cf_result *r = cf_add(c, "C4-V16", "VRAM_RESIDENT", "-");
        r->status = RP_ERR_NOMEM;
        snprintf(r->detail, sizeof(r->detail), "1 MiB pattern buffer could not be allocated");
        cf_done(c, r);
    }
    for (i = 0; i < 4; i++) heap_tier(c, HT[i]);
    cycles(c);
    streaming(c);
    soak(c);
}
