/*
 * rs_host_pipe.h - a serial, single-threaded run of RESOURCE_PIPELINE.md
 * section 6.1 for the stage 10.3 PC tests.
 *
 * Steps A1-A5, then B1-B6 and C for each entry, in the document's order:
 * the unmodified GDAT Proto (gp_gdat_open, gp_gdat_load, gp_wrap_parse),
 * Phase 8 (rp_inflate_exact via gp_gdat_load) and ContentFit
 * (cf_zinflate_exact, cf_payload_check) modules, and src/resource/rs_format.c.
 * The worker steps B1-B6 are run on the calling thread, in the order of
 * contentfit/src/cf_pack.c's worker; the background reader itself and memory
 * pre-checks (A6, A7) belong to stage 10.4 and are not used here.
 * Slot sizes follow A7 literally: R = largest region, U = largest
 * table-of-contents payload_size + 32.
 */
#ifndef RS_HOST_PIPE_H
#define RS_HOST_PIPE_H

#include <stddef.h>
#include <stdint.h>

#include "rs_format.h"

typedef struct {
    int         code;           /* 0 = every entry accepted */
    int         entry;          /* GDAT entry of the failure; -1 = the archive itself, or none */
    const char *stage;          /* "open" (6.1 A), "entry" (6.1 B/C), "" if accepted */
    char        check[112];     /* the failing check (rs_format diagnostic, CFP1 field, or module) */
    /* filled once A4 passed */
    uint32_t    n, vram, heap_total, max_region, max_inflated, toc_region, toc_inflated;
    uint64_t    heap_need;      /* section 8.2 with 3 slots */
} rs_pipe_result;

/* Called after an entry passes every check: region = its region bytes,
 * raw = the inflated entry, res = the checked resource (NULL for entry 0). */
typedef void (*rs_pipe_cb)(void *ctx, uint32_t entry, const uint8_t *region, uint32_t region_size,
                           const uint8_t *raw, size_t raw_len, const rs_resource *res);

/* Runs 'path' (inside app0:fixtures/) through section 6.1. first_entry > 0
 * starts B/C at that entry (entries before it must be unchanged from a pack
 * already checked); every A step always runs. Returns result.code. */
int rs_pipe_run(const char *path, int apply_policy, uint32_t first_entry, rs_pipe_cb cb, void *ctx,
                rs_pipe_result *result);

/* Readable name of any status (Phase 8, GDAT Proto, ContentFit, ResPipe). */
const char *rs_pipe_status_name(int code);

#endif /* RS_HOST_PIPE_H */
