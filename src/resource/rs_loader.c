/*
 * rs_loader.c - Vita ResPipe pack loader (RESOURCE_PIPELINE.md section 6.1,
 * section 7 and section 8). See rs_loader.h for the scope and thread rules.
 *
 * Every function runs on the main thread. The worker thread runs only
 * ContentFit's unmodified cf_ring code (reads, gp_wrap_parse,
 * cf_zinflate_exact into its arena, cf_payload_check); everything here,
 * including all VRS1 checks, every allocation and every free, stays on the
 * main thread (section 7.2). Release order (section 7.5): the reader is
 * stopped before the archive is closed, because the worker reads its index.
 */
#include <string.h>

#include "rp_mem.h"
#include "rp_time.h"
#include "rs_loader.h"

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Section 7.5 steps 1, 2 and 4 for what the loader owns. Safe to repeat. */
static void release_parts(rs_load *l)
{
    if (l->ring_started) {
        cf_ring_stop(&l->ring);                         /* 1: worker joined, slots and arena freed */
        l->ring_started = 0;
    }
    if (l->gdat_open) {
        gp_gdat_close(&l->gdat);                        /* 2 */
        l->gdat_open = 0;
    }
    rp_free(l->toc_raw);                                /* 4 */
    l->toc_raw = NULL;
    l->toc_raw_len = 0;
    memset(&l->toc, 0, sizeof l->toc);
}

/* Records the first failure, releases the loader's parts and ends the load. */
static gp_status fail(rs_load *l, gp_status code, int entry, const char *stage, const char *layer, const char *check,
                      const rs_diag *d)
{
    rs_load_error *e = &l->err;
    e->code = code;
    e->entry = entry;
    e->stage = stage;
    e->layer = d && d->layer ? d->layer : layer;
    e->check = d && d->check ? d->check : check;
    e->has_offset = d ? d->has_offset : 0;
    e->offset = d ? d->offset : 0u;
    if (entry >= 1 && l->toc.payload && (uint32_t)entry <= l->toc.entry_count) {
        rs_row row;
        rs_toc_row(&l->toc, (uint32_t)entry - 1u, &row);
        e->resource_id = row.resource_id;
        e->has_resource_id = 1;
    }
    release_parts(l);
    l->state = RS_LOAD_FAILED;
    return code;
}

/* Layer and check for a status the worker reported (6.1 B; codes of 6.5). */
static void worker_names(gp_status st, const char **layer, const char **check)
{
    switch (st) {
    case RP_ERR_SHORT_READ:
        *layer = "read";
        *check = "short read (B2)";
        break;
    case RP_ERR_LIMIT:
        *layer = "slot";
        *check = "region or raw size larger than the slot buffer (B1, B4)";
        break;
    case CF_ERR_PAYLOAD:
        *layer = "CFP1";
        *check = "cf_payload_check (B6)";
        break;
    case RP_ERR_ZLIB_DATA:
    case RP_ERR_SIZE_MISMATCH:
    case RP_ERR_ZLIB:
    case RP_ERR_NOMEM:
        *layer = "zlib";
        *check = "cf_zinflate_exact (B5)";
        break;
    case RP_ERR_TRUNCATED:
        *layer = "wrapper or zlib";
        *check = "region under 0x90 bytes (B3) or stream ends early (B5)";
        break;
    default:
        *layer = "wrapper";
        *check = "gp_wrap_parse, strict (B3)";
        break;
    }
}

gp_status rs_load_needs_for(const gp_gdat *a, const rs_toc *t, uint32_t slots, rs_load_needs *o)
{
    uint32_t i, big_r = 0, rows_u = 0, toc_u;
    rs_row row;

    if (!a || !t || !o || !a->idx || !t->payload || a->count < RS_MIN_ENTRIES || t->entry_count + 1u != a->count ||
        slots < 1u)
        return RP_ERR_ARG;
    memset(o, 0, sizeof *o);
    for (i = 0; i < a->count; i++)                      /* R, entry 0 included */
        if (a->idx[i].size > big_r)
            big_r = a->idx[i].size;
    for (i = 0; i < t->entry_count; i++) {
        rs_toc_row(t, i, &row);
        if (row.payload_size + RS_ENVELOPE_BYTES > rows_u)
            rows_u = row.payload_size + RS_ENVELOPE_BYTES;
    }
    toc_u = 48u + 32u * t->entry_count + RS_ENVELOPE_BYTES;    /* Ut = 80 + 32 x entry_count */
    o->n = a->count;
    o->slots = slots;
    o->max_region = big_r;
    o->row_inflated = rows_u;
    o->max_inflated = rows_u > toc_u ? rows_u : toc_u;          /* 8.2: max(largest payload_size, Pt) + 32 */
    o->toc_region = a->idx[0].size;
    o->toc_inflated = toc_u;
    o->heap = 16u * (uint64_t)a->count + o->toc_region + o->toc_inflated +
              (uint64_t)slots * ((uint64_t)big_r + o->max_inflated) + CF_ZARENA_BYTES + 64u * (uint64_t)a->count +
              t->total_heap_bytes;
    o->vram = t->total_vram_bytes;
    o->kernel = CF_RING_STACK;
    return RP_OK;
}

gp_status rs_load_open(rs_load *l, const char *path, const rs_load_cfg *cfg)
{
    gp_wrap_info w;
    rs_diag d;
    cf_pools now;
    cf_need need;
    gp_status st;
    uint32_t i;

    if (!l)
        return RP_ERR_ARG;
    memset(l, 0, sizeof *l);                            /* state RS_LOAD_EMPTY */
    l->err.entry = -1;
    l->err.stage = "";
    if (!path || !cfg || !cfg->pools || cfg->slots < 1 || cfg->slots > CF_RING_MAX)
        return RP_ERR_ARG;
    memset(&d, 0, sizeof d);
    l->cfg = *cfg;
    l->state = RS_LOAD_RUNNING;
    l->tracked_before = rp_mem_get().current;
    cfg->pools(cfg->pools_ctx, &l->pools_before);

    st = gp_gdat_open(&l->gdat, path, GP_STRICT);                                       /* A1 */
    if (st != RP_OK)
        return fail(l, st, -1, "open", "archive", "gp_gdat_open, strict", NULL);
    l->gdat_open = 1;
    l->count = l->gdat.count;
    st = rs_check_entry_count(l->count, &d);
    if (st != RP_OK)
        return fail(l, st, -1, "open", NULL, NULL, &d);

    st = gp_gdat_load(&l->gdat, 0, &w, &l->toc_raw, &l->toc_raw_len, NULL);           /* A2 */
    if (st != RP_OK)
        return fail(l, st, 0, "open", "wrapper or zlib", "gp_gdat_load of entry 0", NULL);
    st = rs_check_wrapper_record(&w, &d);
    if (st != RP_OK)
        return fail(l, st, 0, "open", NULL, NULL, &d);

    if (cf_payload_check(l->toc_raw, l->toc_raw_len, 0, 1, NULL) != RP_OK) {          /* A3 */
        l->err.cfp1_field = rs_cfp1_field(l->toc_raw, l->toc_raw_len, 0);
        return fail(l, CF_ERR_PAYLOAD, 0, "open", "CFP1", "cf_payload_check", NULL);
    }
    st = rs_check_envelope(l->toc_raw, l->toc_raw_len, &d);
    if (st != RP_OK)
        return fail(l, st, 0, "open", NULL, NULL, &d);
    l->open_crc = le32(l->toc_raw + 20);

    st = rs_check_toc(l->toc_raw + RS_ENVELOPE_BYTES, l->toc_raw_len - RS_ENVELOPE_BYTES, l->count, &l->toc, &d);
    if (st != RP_OK)                                                                    /* A4 */
        return fail(l, st, 0, "open", NULL, NULL, &d);

    for (i = 1; i < l->count; i++) {                                                    /* A5 */
        st = rs_check_region(&l->toc, i, l->gdat.idx[i].size, &d);
        if (st != RP_OK)
            return fail(l, st, (int)i, "open", NULL, NULL, &d);
    }

    st = rs_load_needs_for(&l->gdat, &l->toc, (uint32_t)cfg->slots, &l->needs);        /* A6 */
    if (st != RP_OK)
        return fail(l, st, -1, "open", "memory", "section 8.2 needs", NULL);
    cfg->pools(cfg->pools_ctx, &now);
    memset(&need, 0, sizeof need);
    need.b[CF_POOL_VRAM] = l->needs.vram;
    need.b[CF_POOL_HEAP] = l->needs.heap;
    need.b[CF_POOL_KERNEL] = l->needs.kernel;
    st = cf_precheck(&now, &need, l->err.why, sizeof l->err.why);
    if (st != RP_OK)
        return fail(l, st, -1, "open", "memory", "cf_precheck", NULL);

    st = cf_ring_start(&l->ring, path, &l->gdat, cfg->slots, l->needs.max_region, l->needs.max_inflated, 1, 0);
    if (st != RP_OK)                                                                    /* A7 (stops itself) */
        return fail(l, st, -1, "open", "reader", "cf_ring_start", NULL);
    l->ring_started = 1;
    l->next = 0;
    return RP_OK;
}

/* C.1-C.5 for the slot holding GDAT entry l->next. */
static gp_status take(rs_load *l, cf_slot *s)
{
    const uint32_t i = s->entry;
    rs_resource res;
    rs_diag d;
    gp_status st;

    memset(&d, 0, sizeof d);
    if (i != l->next)                                   /* cf_ring delivers in order; never expected */
        return fail(l, RP_ERR_BAD_INDEX, (int)l->next, "entry", "reader", "slot out of order", NULL);
    if (s->st != RP_OK) {                                                               /* C.1 */
        const char *layer, *check;
        worker_names(s->st, &layer, &check);
        if (s->st == CF_ERR_PAYLOAD)                    /* inflation succeeded: usize bytes are in the slot */
            l->err.cfp1_field = rs_cfp1_field(s->out, s->w.usize, i);
        return fail(l, s->st, (int)i, "entry", layer, check, NULL);
    }
    if (i == 0) {                                                                       /* C.2 */
        st = rs_check_toc_again(s->out, s->w.usize, l->open_crc, &d);
        if (st != RP_OK)
            return fail(l, st, 0, "entry", NULL, NULL, &d);
    } else {
        st = rs_check_entry(s->comp, s->out, s->w.usize, &l->toc, i, l->cfg.apply_policy, &res, &d);   /* C.3 */
        if (st != RP_OK)
            return fail(l, st, (int)i, "entry", NULL, NULL, &d);
        if (l->cfg.consume) {                                                           /* C.4 */
            st = l->cfg.consume(l->cfg.consume_ctx, i, &res);
            if (st != RP_OK)
                return fail(l, st, (int)i, "entry", "upload", "consumer", NULL);
        }
    }
    cf_ring_release(&l->ring, s);                                                       /* C.5 */
    l->next++;
    return RP_OK;
}

gp_status rs_load_step(rs_load *l, uint32_t max_entries, double budget_ms, int wait, uint32_t *processed)
{
    uint32_t done_here = 0;
    double t0;

    if (processed)
        *processed = 0;
    if (!l)
        return RP_ERR_ARG;
    switch (l->state) {
    case RS_LOAD_RUNNING:
        break;
    case RS_LOAD_DONE:
        return RP_OK;
    case RS_LOAD_FAILED:
        return l->err.code;
    case RS_LOAD_STOPPED:
        return CF_ERR_ABORTED;
    default:
        return RP_ERR_ARG;
    }
    t0 = rp_now_ms();
    while (l->next < l->count && done_here < max_entries) {
        cf_slot *s;
        gp_status st;
        if (done_here > 0 && rp_now_ms() - t0 >= budget_ms)
            break;
        s = cf_ring_take(&l->ring, wait);
        if (!s)
            break;                                      /* none ready yet (wait == 0) */
        st = take(l, s);
        if (st != RP_OK) {
            if (processed)
                *processed = done_here;
            return st;
        }
        done_here++;
    }
    if (processed)
        *processed = done_here;
    if (l->next == l->count) {                                                          /* D */
        cf_ring_stop(&l->ring);
        l->ring_started = 0;
        gp_gdat_close(&l->gdat);
        l->gdat_open = 0;
        l->state = RS_LOAD_DONE;
    }
    return RP_OK;
}

int rs_load_done(const rs_load *l)
{
    return l && l->state == RS_LOAD_DONE;
}

void rs_load_cancel(rs_load *l)
{
    if (!l || l->state != RS_LOAD_RUNNING)
        return;
    release_parts(l);
    l->state = RS_LOAD_STOPPED;
}

gp_status rs_load_release(rs_load *l)
{
    cf_pools now;
    cf_pool_id where = CF_POOL_COUNT;

    if (!l)
        return RP_ERR_ARG;
    if (l->state == RS_LOAD_EMPTY || l->state == RS_LOAD_RELEASED)
        return RP_OK;
    rs_load_cancel(l);                                  /* a running load is stopped first */
    release_parts(l);                                   /* after D: the table of contents */
    l->state = RS_LOAD_RELEASED;
    l->cfg.pools(l->cfg.pools_ctx, &now);               /* 7.5 step 5 (GL objects: stage 10.5) */
    if (rp_mem_get().current != l->tracked_before ||
        !cf_baseline_ok(&l->pools_before, &now, CF_BASELINE_TOL, &where))
        return CF_ERR_BASELINE;
    return RP_OK;
}

const rs_load_error *rs_load_error_of(const rs_load *l)
{
    return l ? &l->err : NULL;
}

const rs_toc *rs_load_toc(const rs_load *l)
{
    if (!l || (l->state != RS_LOAD_RUNNING && l->state != RS_LOAD_DONE) || !l->toc.payload)
        return NULL;
    return &l->toc;
}
