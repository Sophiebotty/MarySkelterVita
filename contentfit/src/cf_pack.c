/*
 * cf_pack.c - CFP1 payload checks and the background ring reader.
 */
#include <string.h>
#include <zlib.h>

#include "cf_pack.h"
#include "cf_plat.h"
#include "rp_mem.h"
#include "rp_time.h"

gp_status cf_payload_check(const uint8_t *p, size_t len, uint32_t index, int verify_crc, cf_payload *out)
{
    cf_payload v;
    if (!p || len < CF_PAYLOAD_HDR || memcmp(p, "CFP1", 4) != 0)
        return CF_ERR_PAYLOAD;
    v.kind = gp_le32(p + 4);
    v.w = gp_le32(p + 8);
    v.h = gp_le32(p + 12);
    v.body_len = gp_le32(p + 16);
    v.body_crc = gp_le32(p + 20);
    v.index = gp_le32(p + 24);
    v.body = p + CF_PAYLOAD_HDR;
    if (gp_le32(p + 28) != 0 || v.index != index || (uint64_t)v.body_len != (uint64_t)len - CF_PAYLOAD_HDR)
        return CF_ERR_PAYLOAD;
    if (v.kind == CF_KIND_TEX) {
        if (v.w == 0 || v.h == 0 || (v.w & (v.w - 1)) || (v.h & (v.h - 1)) || v.w > 4096 || v.h > 4096 ||
            (uint64_t)v.w * v.h * 4u != v.body_len)
            return CF_ERR_PAYLOAD;
    } else if (v.kind == CF_KIND_DATA) {
        if (v.w != 0 || v.h != 0)
            return CF_ERR_PAYLOAD;
    } else {
        return CF_ERR_PAYLOAD;
    }
    if (verify_crc && (uint32_t)crc32(crc32(0L, Z_NULL, 0), v.body, (uInt)v.body_len) != v.body_crc)
        return CF_ERR_PAYLOAD;
    if (out) *out = v;
    return RP_OK;
}

uint64_t cf_ring_heap_bytes(int nslots, uint32_t comp_cap, uint32_t out_cap, int inflate_in_worker)
{
    uint64_t per = comp_cap + (inflate_in_worker ? (uint64_t)out_cap : 0u);
    return (uint64_t)nslots * per + (inflate_in_worker ? CF_ZARENA_BYTES : 0u);
}

static int stopping(cf_ring *r) { return __atomic_load_n(&r->stop, __ATOMIC_ACQUIRE); }

static int worker(void *arg)
{
    cf_ring *r = (cf_ring *)arg;
    uint32_t k = 0;
    for (;;) {
        cf_slot *s;
        const gp_entry *e;
        double t0, t1;
        int64_t got;

        if (!r->loop && k >= r->count)
            break;
        cf_sema_wait(r->sem_free);
        if (stopping(r))
            break;
        s = &r->slot[k % (uint32_t)r->nslots];
        s->entry = r->loop ? k % r->count : k;
        e = &r->a->idx[s->entry];
        s->region = e->size;
        s->read_ms = s->inflate_ms = 0.0;
        s->st = RP_OK;
        if (e->size > r->comp_cap) {
            s->st = RP_ERR_LIMIT;
        } else {
            t0 = rp_now_ms();
            got = cf_raw_pread(r->fd, s->comp, e->size, e->offset);
            t1 = rp_now_ms();
            s->read_ms = t1 - t0;
            if (got != (int64_t)e->size)
                s->st = RP_ERR_SHORT_READ;
        }
        if (s->st == RP_OK && r->inflate_in_worker) {
            s->st = gp_wrap_parse(s->comp, s->region, GP_STRICT, &s->w);
            if (s->st == RP_OK && s->w.usize > r->out_cap)
                s->st = RP_ERR_LIMIT;
            if (s->st == RP_OK) {
                t0 = rp_now_ms();
                s->st = cf_zinflate_exact(&r->arena, s->comp + GP_WRAP_TOTAL, s->w.csize, s->out, s->w.usize);
                if (s->st == RP_OK)
                    s->st = cf_payload_check(s->out, s->w.usize, s->entry, 1, &s->pay);
                s->inflate_ms = rp_now_ms() - t0;
            }
        }
        cf_sema_signal(r->sem_ready);
        k++;
    }
    return 0;
}

gp_status cf_ring_start(cf_ring *r, const char *path, const gp_gdat *a, int nslots, uint32_t comp_cap,
                        uint32_t out_cap, int inflate_in_worker, int loop)
{
    int i;
    memset(r, 0, sizeof(*r));
    r->fd = r->thread = r->sem_free = r->sem_ready = -1;
    if (!a || a->count == 0 || nslots < 1 || nslots > CF_RING_MAX)
        return RP_ERR_ARG;
    r->a = a;
    r->nslots = nslots;
    r->inflate_in_worker = inflate_in_worker;
    r->loop = loop;
    r->count = a->count;
    r->comp_cap = comp_cap;
    r->out_cap = out_cap;
    for (i = 0; i < nslots; i++) {
        r->slot[i].comp = (uint8_t *)rp_malloc(comp_cap);
        if (inflate_in_worker) r->slot[i].out = (uint8_t *)rp_malloc(out_cap);
        if (!r->slot[i].comp || (inflate_in_worker && !r->slot[i].out)) {
            cf_ring_stop(r);
            return RP_ERR_NOMEM;
        }
    }
    if (inflate_in_worker) {
        r->arena_mem = (uint8_t *)rp_malloc(CF_ZARENA_BYTES);
        if (!r->arena_mem) {
            cf_ring_stop(r);
            return RP_ERR_NOMEM;
        }
        r->arena.base = r->arena_mem;
        r->arena.size = CF_ZARENA_BYTES;
    }
    r->fd = cf_raw_open(path);
    if (r->fd < 0) {
        cf_ring_stop(r);
        return RP_ERR_OPEN;
    }
    r->sem_free = cf_sema_create(nslots, nslots);
    r->sem_ready = cf_sema_create(0, nslots);
    if (r->sem_free < 0 || r->sem_ready < 0) {
        cf_ring_stop(r);
        return CF_ERR_THREAD;
    }
    r->thread = cf_thread_start(worker, r, CF_RING_STACK);
    if (r->thread < 0) {
        cf_ring_stop(r);
        return CF_ERR_THREAD;
    }
    return RP_OK;
}

cf_slot *cf_ring_take(cf_ring *r, int wait)
{
    cf_slot *s;
    if (r->thread < 0)
        return NULL;
    if (wait)
        cf_sema_wait(r->sem_ready);
    else if (!cf_sema_try(r->sem_ready))
        return NULL;
    s = &r->slot[r->taken % (uint32_t)r->nslots];
    r->taken++;
    return s;
}

void cf_ring_release(cf_ring *r, cf_slot *s)
{
    (void)s;
    cf_sema_signal(r->sem_free);
}

void cf_ring_stop(cf_ring *r)
{
    int i;
    if (r->thread >= 0) {
        __atomic_store_n(&r->stop, 1, __ATOMIC_RELEASE);
        for (i = 0; i < r->nslots; i++)
            cf_sema_signal(r->sem_free);         /* wake a worker waiting for a free slot */
        cf_thread_join(r->thread);
        r->thread = -1;
    }
    cf_sema_delete(r->sem_free);
    cf_sema_delete(r->sem_ready);
    r->sem_free = r->sem_ready = -1;
    cf_raw_close(r->fd);
    r->fd = -1;
    for (i = 0; i < CF_RING_MAX; i++) {
        rp_free(r->slot[i].comp);
        rp_free(r->slot[i].out);
        r->slot[i].comp = r->slot[i].out = NULL;
    }
    rp_free(r->arena_mem);
    r->arena_mem = NULL;
}
