/*
 * cf_pack.h - CFP1 payload checks and the background ring reader used by the
 * pipelined area loads (C2) and the streaming test (C4-STRM).
 *
 * Thread rules: every buffer is allocated by the main thread before the
 * worker starts. The worker only reads (cf_raw_pread), parses the BILZ header
 * (gp_wrap_parse, unmodified, no allocation), optionally inflates with an
 * arena (cf_zinflate_exact) and checks CRCs. It never calls rp_malloc, malloc
 * or stdio. Slots are handed over with two counting semaphores, in order.
 */
#ifndef CF_PACK_H
#define CF_PACK_H

#include "cf_zinflate.h"
#include "gp_gdat.h"

#define CF_PAYLOAD_HDR 32u
enum { CF_KIND_TEX = 1, CF_KIND_DATA = 2 };

typedef struct {
    uint32_t kind, w, h, body_len, body_crc, index;
    const uint8_t *body;
} cf_payload;

/* Validates a CFP1 payload of 'len' bytes that should be entry 'index'.
 * verify_crc: also recompute the body CRC. RP_OK or CF_ERR_PAYLOAD. */
gp_status cf_payload_check(const uint8_t *p, size_t len, uint32_t index, int verify_crc, cf_payload *out);

#define CF_RING_MAX 4

typedef struct {
    uint8_t     *comp;          /* region bytes: 0x90-byte wrapper header + stream */
    uint8_t     *out;           /* inflated payload (worker-inflate mode only) */
    uint32_t     entry, region;
    gp_status    st;            /* worker result for this slot */
    double       read_ms, inflate_ms;
    gp_wrap_info w;             /* filled in worker-inflate mode */
    cf_payload   pay;           /* filled in worker-inflate mode */
} cf_slot;

typedef struct {
    const gp_gdat *a;
    int        fd, thread, sem_free, sem_ready;
    int        nslots, inflate_in_worker, loop;
    uint32_t   count, comp_cap, out_cap, taken;
    int        stop;            /* accessed with __atomic builtins */
    uint8_t   *arena_mem;
    cf_zarena  arena;
    cf_slot    slot[CF_RING_MAX];
} cf_ring;

/* Bytes cf_ring_start allocates on the heap for these settings. */
uint64_t cf_ring_heap_bytes(int nslots, uint32_t comp_cap, uint32_t out_cap, int inflate_in_worker);
#define CF_RING_STACK (128u * 1024u)

/* Starts a worker that delivers the entries of the already-open archive 'a'
 * (path must pass cf_path_read_ok) in index order; loop = 1 starts again at
 * entry 0 after the last. */
gp_status cf_ring_start(cf_ring *r, const char *path, const gp_gdat *a, int nslots, uint32_t comp_cap,
                        uint32_t out_cap, int inflate_in_worker, int loop);

/* Next filled slot in order, or NULL if none is ready and wait == 0. */
cf_slot *cf_ring_take(cf_ring *r, int wait);
void     cf_ring_release(cf_ring *r, cf_slot *s);

/* Stops the worker, waits for it, and frees everything. Safe to call twice. */
void     cf_ring_stop(cf_ring *r);

#endif /* CF_PACK_H */
