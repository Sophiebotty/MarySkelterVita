/*
 * rs_loader.h - Vita ResPipe pack loader (Phase 10, VDNG00006).
 *
 * Runs RESOURCE_PIPELINE.md section 6.1 for one pack:
 *   A1-A7  rs_load_open: strict GDAT open and entry count, entry 0 (the table
 *          of contents) with its wrapper record and CFP1 envelope, the table's
 *          checks, every region limit, the memory pre-check with section 8.2's
 *          needs, and ContentFit's background reader (cf_ring, unmodified)
 *          with S slots of R + U bytes, U as corrected in section 8.2;
 *   B      on the reader's worker thread (unmodified ContentFit code);
 *   C, D   rs_load_step: each ready slot in order - the worker's status
 *          (C.1), entry 0's second CRC check (C.2), rs_check_entry (C.3), the
 *          caller's consumer (C.4, the GL upload in stage 10.5), the slot's
 *          release (C.5); after the last entry the reader is stopped and the
 *          archive closed (D). The table of contents is kept.
 * Release (section 7.5) for what the loader owns: rs_load_cancel (any time;
 * section 7.6) and rs_load_release (after the caller freed its own parts)
 * stop the reader before closing the archive, free the table of contents,
 * and rs_load_release then checks that tracked rp_malloc bytes are back
 * exactly and the memory pools are back within 64 KiB (cf_baseline_ok).
 *
 * Main thread only: every function, the consumer and every allocation. The
 * worker runs only cf_ring's own code. No GL, SDL or platform call is made
 * here; free memory comes from the caller's snapshot function. There is no
 * global state, so a pending and an active load can coexist. An rs_load must
 * not be moved or copied while it is loading (the reader points into it).
 */
#ifndef RS_LOADER_H
#define RS_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include "cf_budget.h"
#include "cf_pack.h"
#include "gp_gdat.h"
#include "rs_format.h"

/* Free-memory snapshot (platform: vitaGL pools, heap, kernel). */
typedef void (*rs_pools_fn)(void *ctx, cf_pools *out);

/* C.4: called on the main thread for every resource that passed C.3, while
 * its slot is held; res points into the slot and is valid only during the
 * call. RP_OK to continue; any other status fails the load with it. */
typedef gp_status (*rs_consume_fn)(void *ctx, uint32_t entry, const rs_resource *res);

typedef struct {
    int           slots;            /* 1..CF_RING_MAX: 3 on the loading screen, 2 streaming (7.3) */
    int           apply_policy;     /* section 5.8; 1 in ResPipe Phase 10 */
    rs_pools_fn   pools;            /* required */
    void         *pools_ctx;
    rs_consume_fn consume;          /* may be NULL */
    void         *consume_ctx;
} rs_load_cfg;

/* Section 8.2 for a pack (the CFP1 envelope included). */
typedef struct {
    uint32_t n;                     /* GDAT entries */
    uint32_t slots;                 /* S */
    uint32_t max_region;            /* R: largest region, entry 0 included */
    uint32_t max_inflated;          /* U = max(largest payload_size, 48 + 32 x entry_count) + 32 */
    uint32_t row_inflated;          /* largest payload_size + 32 (the resources alone) */
    uint32_t toc_region;            /* Rt */
    uint32_t toc_inflated;          /* Ut = 80 + 32 x entry_count */
    uint64_t heap;                  /* 16N + Rt + Ut + S(R + U) + 65,536 + 64N + total_heap_bytes */
    uint64_t vram;                  /* total_vram_bytes */
    uint64_t kernel;                /* the worker's stack */
} rs_load_needs;

/* The first failure, for the run log (section 6.6). Strings are static. */
typedef struct {
    int         code;               /* 0 = none */
    int         entry;              /* GDAT entry; -1 = the archive, or none */
    const char *stage;              /* "open" (6.1 A), "entry" (6.1 B/C), "" */
    const char *layer;
    const char *check;
    const char *cfp1_field;         /* for CF_ERR_PAYLOAD (section 4.5), else NULL */
    uint32_t    resource_id;
    int         has_resource_id;
    uint32_t    offset;
    int         has_offset;
    char        why[96];            /* cf_precheck's explanation, if it refused */
} rs_load_error;

typedef enum {
    RS_LOAD_EMPTY = 0,              /* not opened (or open refused its arguments) */
    RS_LOAD_RUNNING,                /* A passed; entries being delivered */
    RS_LOAD_DONE,                   /* every entry passed (D); the table of contents is kept */
    RS_LOAD_FAILED,                 /* the first failure is recorded; loader parts released */
    RS_LOAD_STOPPED,                /* cancelled; loader parts released */
    RS_LOAD_RELEASED
} rs_load_state;

typedef struct {
    rs_load_state state;
    rs_load_cfg   cfg;
    gp_gdat       gdat;
    int           gdat_open;
    uint32_t      count;            /* GDAT entries */
    uint8_t      *toc_raw;          /* entry 0 inflated: CFP1 envelope + table of contents */
    size_t        toc_raw_len;
    rs_toc        toc;
    uint32_t      open_crc;         /* entry 0's CFP1 body CRC at A3 */
    cf_ring       ring;
    int           ring_started;
    uint32_t      next;             /* next GDAT entry to take */
    rs_load_needs needs;
    size_t        tracked_before;   /* rp_malloc bytes before the load */
    cf_pools      pools_before;
    rs_load_error err;
} rs_load;

/* Section 8.2 for an open archive and its checked table (pure arithmetic). */
gp_status rs_load_needs_for(const gp_gdat *a, const rs_toc *t, uint32_t slots, rs_load_needs *out);

/* A1-A7. On failure everything is released and the error recorded. */
gp_status rs_load_open(rs_load *l, const char *path, const rs_load_cfg *cfg);

/* C and D for ready entries: at least one if available, then until
 * max_entries or budget_ms (rp_now_ms) is reached. wait != 0 blocks for each
 * entry. *processed (may be NULL) counts entries taken. Returns RP_OK while
 * running or done, the first failure's code, or CF_ERR_ABORTED if cancelled. */
gp_status rs_load_step(rs_load *l, uint32_t max_entries, double budget_ms, int wait, uint32_t *processed);

/* 1 once every entry passed (state RS_LOAD_DONE). */
int rs_load_done(const rs_load *l);

/* Stop a running load (7.6): the reader is stopped (a read or inflation in
 * progress finishes first), the archive closed, the table freed. No effect
 * once done, failed or stopped. */
void rs_load_cancel(rs_load *l);

/* Release what the loader still owns (the table of contents after D), then
 * check the baseline: RP_OK, or CF_ERR_BASELINE if tracked bytes or the
 * pools have not come back. Call after the caller freed its own parts. */
gp_status rs_load_release(rs_load *l);

const rs_load_error *rs_load_error_of(const rs_load *l);

/* The checked table of contents (RS_LOAD_RUNNING or RS_LOAD_DONE), else NULL. */
const rs_toc *rs_load_toc(const rs_load *l);

#endif /* RS_LOADER_H */
