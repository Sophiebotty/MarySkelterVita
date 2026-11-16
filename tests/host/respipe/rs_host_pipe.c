/*
 * rs_host_pipe.c - serial run of RESOURCE_PIPELINE.md section 6.1 (see
 * rs_host_pipe.h). Test code: it composes the unmodified modules and
 * rs_format in the document's order and records where a pack fails.
 */
#include <stdio.h>
#include <string.h>

#include "cf_pack.h"
#include "cf_zinflate.h"
#include "gp_gdat.h"
#include "rp_io.h"
#include "rp_mem.h"
#include "rs_host_pipe.h"

#define SLOTS 3u

const char *rs_pipe_status_name(int code)
{
    const char *n = rs_status_name(code);
    return n ? n : cf_status_name(code);
}

static int finish(rs_pipe_result *r, int code, int entry, const char *stage, const char *check)
{
    r->code = code;
    r->entry = entry;
    r->stage = code ? stage : "";
    snprintf(r->check, sizeof r->check, "%s", check ? check : "");
    return code;
}

static int rs_fail(rs_pipe_result *r, int code, int entry, const char *stage, const rs_diag *d)
{
    char buf[112];
    snprintf(buf, sizeof buf, "%s: %s", d->layer ? d->layer : "?", d->check ? d->check : "?");
    return finish(r, code, entry, stage, buf);
}

static int cfp1_fail(rs_pipe_result *r, int entry, const char *stage, const uint8_t *raw, size_t len)
{
    char buf[112];
    const char *f = rs_cfp1_field(raw, len, (uint32_t)entry);
    snprintf(buf, sizeof buf, "CFP1 %s", f ? f : "(no field found)");
    return finish(r, CF_ERR_PAYLOAD, entry, stage, buf);
}

static int module_fail(rs_pipe_result *r, int code, int entry, const char *stage, const char *where)
{
    char buf[112];
    snprintf(buf, sizeof buf, "%s: %s", where, rs_pipe_status_name(code));
    return finish(r, code, entry, stage, buf);
}

int rs_pipe_run(const char *path, int apply_policy, uint32_t first_entry, rs_pipe_cb cb, void *ctx,
                rs_pipe_result *res)
{
    gp_gdat a;
    gp_wrap_info w;
    rs_toc toc;
    rs_diag d;
    rs_row row;
    uint8_t *raw0 = NULL, *comp = NULL, *out = NULL, *arena_mem = NULL;
    size_t raw0_len = 0;
    uint32_t i, open_crc, big_r = 0, big_u = 0;
    cf_zarena arena;
    int st;

    memset(res, 0, sizeof *res);
    memset(&d, 0, sizeof d);
    res->entry = -1;
    res->stage = "";

    st = gp_gdat_open(&a, path, GP_STRICT);                                  /* A1: GDAT Proto */
    if (st != RP_OK)
        return module_fail(res, st, -1, "open", "gp_gdat_open");
    st = rs_check_entry_count(a.count, &d);                                  /* A1: ResPipe */
    if (st != RP_OK) {
        gp_gdat_close(&a);
        return rs_fail(res, st, -1, "open", &d);
    }
    st = gp_gdat_load(&a, 0, &w, &raw0, &raw0_len, NULL);                    /* A2 */
    if (st != RP_OK) {
        gp_gdat_close(&a);
        return module_fail(res, st, 0, "open", "gp_gdat_load");
    }
    st = rs_check_wrapper_record(&w, &d);
    if (st == RP_OK && cf_payload_check(raw0, raw0_len, 0, 1, NULL) != RP_OK) {   /* A3 */
        cfp1_fail(res, 0, "open", raw0, raw0_len);
        goto done;
    }
    if (st == RP_OK)
        st = rs_check_envelope(raw0, raw0_len, &d);
    if (st == RP_OK)
        st = rs_check_toc(raw0 + RS_ENVELOPE_BYTES, raw0_len - RS_ENVELOPE_BYTES, a.count, &toc, &d); /* A4 */
    if (st != RP_OK) {
        rs_fail(res, st, 0, "open", &d);
        goto done;
    }
    open_crc = (uint32_t)raw0[20] | ((uint32_t)raw0[21] << 8) | ((uint32_t)raw0[22] << 16) |
               ((uint32_t)raw0[23] << 24);
    for (i = 1; i < a.count; i++) {                                          /* A5 */
        st = rs_check_region(&toc, i, a.idx[i].size, &d);
        if (st != RP_OK) {
            rs_fail(res, st, (int)i, "open", &d);
            goto done;
        }
    }
    for (i = 0; i < a.count; i++)                                            /* A7 slot sizes */
        if (a.idx[i].size > big_r)
            big_r = a.idx[i].size;
    for (i = 0; i < toc.entry_count; i++) {
        rs_toc_row(&toc, i, &row);
        if (row.payload_size + RS_ENVELOPE_BYTES > big_u)
            big_u = row.payload_size + RS_ENVELOPE_BYTES;
    }
    res->n = a.count;
    res->vram = toc.total_vram_bytes;
    res->heap_total = toc.total_heap_bytes;
    res->max_region = big_r;
    res->max_inflated = big_u;
    res->toc_region = a.idx[0].size;
    res->toc_inflated = (uint32_t)raw0_len;
    res->heap_need = 16u * (uint64_t)a.count + a.idx[0].size + raw0_len + SLOTS * ((uint64_t)big_r + big_u) +
                     CF_ZARENA_BYTES + 64u * (uint64_t)a.count + toc.total_heap_bytes;      /* 8.2 */

    comp = (uint8_t *)rp_malloc(big_r);
    out = (uint8_t *)rp_malloc(big_u);
    arena_mem = (uint8_t *)rp_malloc(CF_ZARENA_BYTES);
    if (!comp || !out || !arena_mem) {
        module_fail(res, RP_ERR_NOMEM, -1, "open", "slot allocation");
        goto done;
    }
    arena.base = arena_mem;
    arena.size = CF_ZARENA_BYTES;
    arena.used = 0;

    for (i = first_entry; i < a.count; i++) {
        const uint32_t region = a.idx[i].size;
        rs_resource r;
        if (region > big_r) {                                                /* B1 */
            module_fail(res, RP_ERR_LIMIT, (int)i, "entry", "region over slot");
            goto done;
        }
        if (rp_read_at(a.fp, a.file_size, a.idx[i].offset, comp, region) != RP_OK) {   /* B2 */
            module_fail(res, RP_ERR_SHORT_READ, (int)i, "entry", "read");
            goto done;
        }
        st = gp_wrap_parse(comp, region, GP_STRICT, &w);                     /* B3 */
        if (st != RP_OK) {
            module_fail(res, st, (int)i, "entry", "gp_wrap_parse");
            goto done;
        }
        if (w.usize > big_u) {                                               /* B4 */
            module_fail(res, RP_ERR_LIMIT, (int)i, "entry", "raw size over slot");
            goto done;
        }
        st = cf_zinflate_exact(&arena, comp + GP_WRAP_TOTAL, w.csize, out, w.usize);   /* B5 */
        if (st != RP_OK) {
            module_fail(res, st, (int)i, "entry", "cf_zinflate_exact");
            goto done;
        }
        if (cf_payload_check(out, w.usize, i, 1, NULL) != RP_OK) {           /* B6 */
            cfp1_fail(res, (int)i, "entry", out, w.usize);
            goto done;
        }
        if (i == 0) {                                                        /* C.2 */
            st = rs_check_toc_again(out, w.usize, open_crc, &d);
            if (st != RP_OK) {
                rs_fail(res, st, 0, "entry", &d);
                goto done;
            }
            if (cb)
                cb(ctx, 0, comp, region, out, w.usize, NULL);
            continue;
        }
        st = rs_check_entry(comp, out, w.usize, &toc, i, apply_policy, &r, &d);   /* C.3 */
        if (st != RP_OK) {
            rs_fail(res, st, (int)i, "entry", &d);
            goto done;
        }
        if (cb)
            cb(ctx, i, comp, region, out, w.usize, &r);
    }
    finish(res, RP_OK, -1, "", "");
done:
    rp_free(arena_mem);
    rp_free(out);
    rp_free(comp);
    rp_free(raw0);
    gp_gdat_close(&a);
    return res->code;
}
