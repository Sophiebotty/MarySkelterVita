/*
 * cf_load.c - C2: area loads from the synthetic GDAT-style packs.
 *
 * SERIAL uses the hardware-tested GDAT Proto reader unchanged (gp_gdat_load:
 * stdio read, then inflate). PIPE reads regions on a worker thread
 * (cf_ring) while the main thread inflates, verifies and uploads. Texture
 * entries are uploaded to VRAM; data entries stay in the heap until the whole
 * pack is loaded, as an area's resident data would.
 */
#include <stdio.h>
#include <string.h>
#include <vitaGL.h>
#include <zlib.h>

#include "cf_fixdefs.h"
#include "cf_pack.h"
#include "cf_suite.h"
#include "rp_inflate.h"
#include "rp_mem.h"
#include "rp_time.h"

typedef struct {
    double open_ms, read_ms, inflate_ms, upload_ms, wait_ms, total_ms;
    uint32_t entries, textures;
    uint64_t raw;
    uint32_t list_crc;
} load_stats;

static GLuint upload(const cf_payload *p, double *ms)
{
    GLuint t = 0;
    double t0;
    int guard = 64;
    while (glGetError() != GL_NO_ERROR && --guard > 0) { }   /* only this upload's errors count */
    t0 = rp_now_ms();
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)p->w, (GLsizei)p->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, p->body);
    glBindTexture(GL_TEXTURE_2D, 0);
    *ms += rp_now_ms() - t0;
    return t;
}

static void path_of(const cf_fix_pack *fp, char *out, size_t n) { snprintf(out, n, "%s%s", CF_FIX_PREFIX, fp->name); }

/* Accepts one inflated payload: verify, then upload (texture) or keep (data). */
static gp_status accept(uint8_t *out, uint32_t usize, uint32_t i, GLuint *tex, uint32_t tex_cap, uint8_t **keep,
                        load_stats *ls, uLong *list)
{
    cf_payload p;
    uint8_t le[4];
    gp_status st = cf_payload_check(out, usize, i, 1, &p);
    if (st == RP_OK && p.kind == CF_KIND_TEX && ls->textures >= tex_cap)
        st = RP_ERR_SIZE_MISMATCH;               /* more textures than the fixture header lists */
    if (st != RP_OK) {
        rp_free(out);
        return st;
    }
    gp_wr_le32(le, p.body_crc);
    *list = crc32(*list, le, 4);
    ls->raw += usize;
    ls->entries++;
    if (p.kind == CF_KIND_TEX) {
        tex[ls->textures++] = upload(&p, &ls->upload_ms);
        rp_free(out);
        if (glGetError() != GL_NO_ERROR)
            return CF_ERR_GL;
    } else {
        keep[i] = out;
    }
    return RP_OK;
}

static void load_pack(cf_ctx *c, int pi, int pipe)
{
    const cf_fix_pack *fp = cf_fix_pack_get(pi);
    cf_pools base, peak, now;
    cf_need need;
    cf_result *r;
    load_stats ls;
    gp_gdat a;
    cf_ring ring;
    GLuint *tex = NULL;
    uint8_t **keep = NULL;
    uLong list = crc32(0L, Z_NULL, 0);
    char id[16], name[40], path[64], why[160], bwhy[128];
    gp_status st;
    double t0;
    uint32_t i;
    int base_ok;

    path_of(fp, path, sizeof(path));
    snprintf(id, sizeof(id), "C2-%02d", fp->target_mib);
    snprintf(name, sizeof(name), "LOAD_%dMIB_PACK", fp->target_mib);
    if (cf_should_stop(c)) return;
    cf_progress(c, id, pipe ? "AREA LOAD PIPELINED" : "AREA LOAD SERIAL");
    r = cf_add(c, id, name, pipe ? "PIPE" : "SERIAL");
    memset(&ls, 0, sizeof(ls));
    memset(&a, 0, sizeof(a));
    memset(&ring, 0, sizeof(ring));
    ring.thread = -1;

    memset(&need, 0, sizeof(need));
    need.b[CF_POOL_VRAM] = fp->tex_bytes;
    need.b[CF_POOL_HEAP] = (uint64_t)fp->data_bytes + fp->entries * (uint64_t)(CF_PAYLOAD_HDR + sizeof(void *)) +
                           fp->max_usize + fp->max_region + fp->entries * 16u + fp->tex_count * sizeof(GLuint) +
                           (pipe ? cf_ring_heap_bytes(3, fp->max_region, 0, 0) : 0u);
    need.b[CF_POOL_KERNEL] = pipe ? CF_RING_STACK : 0u;
    cf_plat_pools(&base);
    peak = base;
    st = cf_precheck(&base, &need, why, sizeof(why));
    if (st != RP_OK) {
        r->status = st;
        r->ok = 1;
        snprintf(r->verdict, sizeof(r->verdict), "SKIPPED");
        snprintf(r->detail, sizeof(r->detail), "%s", why);
        cf_done(c, r);
        return;
    }
    tex = (GLuint *)rp_malloc((fp->tex_count ? fp->tex_count : 1u) * sizeof(GLuint));
    keep = (uint8_t **)rp_malloc(fp->entries * sizeof(uint8_t *));
    if (!tex || !keep) {
        st = RP_ERR_NOMEM;
        goto cleanup;
    }
    memset(keep, 0, fp->entries * sizeof(uint8_t *));

    t0 = rp_now_ms();
    st = gp_gdat_open(&a, path, GP_STRICT);
    ls.open_ms = rp_now_ms() - t0;
    if (st == RP_OK && a.count != fp->entries) st = RP_ERR_SIZE_MISMATCH;
    if (st == RP_OK && pipe) st = cf_ring_start(&ring, path, &a, 3, fp->max_region, 0, 0, 0);
    for (i = 0; st == RP_OK && i < a.count; i++) {
        uint8_t *out = NULL;
        uint32_t usize = 0;
        if (!pipe) {
            gp_wrap_info w;
            size_t len = 0;
            gp_load_timing t;
            memset(&t, 0, sizeof(t));
            st = gp_gdat_load(&a, i, &w, &out, &len, &t);
            ls.read_ms += t.read_ms;
            ls.inflate_ms += t.inflate_ms;
            usize = (uint32_t)len;
        } else {
            gp_wrap_info w;
            double tw = rp_now_ms(), ti;
            cf_slot *s = cf_ring_take(&ring, 1);
            memset(&w, 0, sizeof(w));
            ls.wait_ms += rp_now_ms() - tw;
            st = s ? s->st : CF_ERR_THREAD;
            if (st == RP_OK) {
                ls.read_ms += s->read_ms;
                st = gp_wrap_parse(s->comp, s->region, GP_STRICT, &w);
            }
            if (st == RP_OK) {
                out = (uint8_t *)rp_malloc(w.usize ? w.usize : 1u);
                if (!out) st = RP_ERR_NOMEM;
            }
            if (st == RP_OK) {
                ti = rp_now_ms();
                st = rp_inflate_exact(s->comp + GP_WRAP_TOTAL, w.csize, out, w.usize);
                ls.inflate_ms += rp_now_ms() - ti;
            }
            if (s) cf_ring_release(&ring, s);
            if (st != RP_OK) {
                rp_free(out);
                out = NULL;
            }
            usize = w.usize;
        }
        if (st == RP_OK)
            st = accept(out, usize, i, tex, fp->tex_count, keep, &ls, &list);
    }
    if (st == RP_OK) glFinish();
    ls.total_ms = rp_now_ms() - t0;
    ls.list_crc = (uint32_t)list;
    cf_plat_pools(&peak);
    if (st == RP_OK && (ls.list_crc != fp->list_crc || ls.raw != fp->raw_total || ls.textures != fp->tex_count))
        st = RP_ERR_CRC;

cleanup:
    cf_ring_stop(&ring);
    if (a.fp || a.idx) gp_gdat_close(&a);
    if (tex && ls.textures) glDeleteTextures((GLsizei)ls.textures, tex);
    if (keep)
        for (i = 0; i < fp->entries; i++) rp_free(keep[i]);
    rp_free(keep);
    rp_free(tex);
    base_ok = cf_settle(c, &base, &now, bwhy, sizeof(bwhy));

    r->status = st != RP_OK ? st : (base_ok ? RP_OK : CF_ERR_BASELINE);
    r->ok = r->status == RP_OK;
    r->ms = ls.total_ms;
    r->mbytes = cf_mib(ls.raw);
    r->mbps = ls.total_ms > 0.0 ? r->mbytes / (ls.total_ms / 1000.0) : 0.0;
    r->vram_kib = cf_kib_delta(&base, &peak, CF_POOL_VRAM);
    r->glram_kib = cf_kib_delta(&base, &peak, CF_POOL_GLRAM);
    r->heap_kib = cf_kib_delta(&base, &peak, CF_POOL_HEAP);
    r->check = r->ok ? ls.list_crc : 0;
    if (fp->target_mib == 8 || fp->target_mib == 32) {
        double target = fp->target_mib == 8 ? 2000.0 : 10000.0;
        snprintf(r->verdict, sizeof(r->verdict), !r->ok ? "ERROR" : (ls.total_ms <= target ? "MEETS" : "MISSES"));
    } else {
        snprintf(r->verdict, sizeof(r->verdict), r->ok ? "INFO" : "ERROR");
    }
    snprintf(r->detail, sizeof(r->detail),
             "%u entries (%u textures); open %.0f ms, read %.0f ms, inflate %.0f ms, upload %.0f ms, wait %.0f ms;"
             " %.2f MiB on disk",
             (unsigned)ls.entries, (unsigned)ls.textures, ls.open_ms, ls.read_ms, ls.inflate_ms, ls.upload_ms,
             ls.wait_ms, cf_mib(fp->file_size));
    if (fp->target_mib == 8) cf_append(r->detail, sizeof(r->detail), "; ", "target 2 s (battle set)");
    if (fp->target_mib == 32) cf_append(r->detail, sizeof(r->detail), "; ", "target 10 s (area)");
    if (st != RP_OK) cf_append(r->detail, sizeof(r->detail), "; failed: ", cf_status_name(st));
    if (!base_ok) cf_append(r->detail, sizeof(r->detail), "; ", bwhy);
    cf_done(c, r);
}

/* C2-PORT: one 512x1024 portrait entry from an archive that is already open. */
static void portrait_swap(cf_ctx *c)
{
    const cf_fix_pack *fp = cf_fix_pack_get(cf_fix_portrait_pack());
    const uint32_t entry = cf_fix_portrait_entry();
    cf_pools base, now;
    cf_need need;
    cf_result *r;
    gp_gdat a;
    gp_wrap_info w;
    cf_payload p;
    uint8_t *out = NULL;
    size_t len = 0;
    GLuint t = 0;
    double t0, up = 0.0;
    char path[64], why[160], bwhy[128];
    gp_status st;
    int base_ok;

    if (cf_should_stop(c)) return;
    memset(&p, 0, sizeof(p));
    cf_progress(c, "C2-PORT", "PORTRAIT SWAP");
    r = cf_add(c, "C2-PORT", "PORTRAIT_SWAP_2MIB", "-");
    memset(&need, 0, sizeof(need));
    need.b[CF_POOL_VRAM] = 2u * CF_MIB;
    need.b[CF_POOL_HEAP] = (uint64_t)fp->max_usize + fp->max_region + fp->entries * 16u;
    cf_plat_pools(&base);
    st = cf_precheck(&base, &need, why, sizeof(why));
    if (st != RP_OK) {
        r->status = st;
        r->ok = 1;
        snprintf(r->verdict, sizeof(r->verdict), "SKIPPED");
        snprintf(r->detail, sizeof(r->detail), "%s", why);
        cf_done(c, r);
        return;
    }
    path_of(fp, path, sizeof(path));
    st = gp_gdat_open(&a, path, GP_STRICT);
    if (st == RP_OK) {
        t0 = rp_now_ms();
        st = gp_gdat_load(&a, entry, &w, &out, &len, NULL);
        if (st == RP_OK) st = cf_payload_check(out, len, entry, 1, &p);
        if (st == RP_OK && p.kind != CF_KIND_TEX) st = CF_ERR_PAYLOAD;
        if (st == RP_OK) {
            t = upload(&p, &up);
            glFinish();
            if (glGetError() != GL_NO_ERROR) st = CF_ERR_GL;
        }
        r->ms = rp_now_ms() - t0;
        gp_gdat_close(&a);
    }
    rp_free(out);
    if (t) glDeleteTextures(1, &t);
    base_ok = cf_settle(c, &base, &now, bwhy, sizeof(bwhy));
    r->status = st != RP_OK ? st : (base_ok ? RP_OK : CF_ERR_BASELINE);
    r->ok = r->status == RP_OK;
    r->mbytes = cf_mib(len);
    r->mbps = r->ms > 0.0 ? r->mbytes / (r->ms / 1000.0) : 0.0;
    r->check = r->ok ? p.body_crc : 0;
    snprintf(r->verdict, sizeof(r->verdict), !r->ok ? "ERROR" : (r->ms <= 500.0 ? "MEETS" : "MISSES"));
    snprintf(r->detail, sizeof(r->detail), "%s entry %u: read + inflate + verify + upload (upload %.1f ms); "
             "target 0.5 s", fp->name, (unsigned)entry, up);
    if (st != RP_OK) cf_append(r->detail, sizeof(r->detail), "; failed: ", cf_status_name(st));
    if (!base_ok) cf_append(r->detail, sizeof(r->detail), "; ", bwhy);
    cf_done(c, r);
}

void cf_group_loads(cf_ctx *c)
{
    int i;
    for (i = 0; i < cf_fix_pack_count(); i++) {
        load_pack(c, i, 0);
        load_pack(c, i, 1);
    }
    portrait_swap(c);
}
