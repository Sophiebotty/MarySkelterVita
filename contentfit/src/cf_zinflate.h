/*
 * cf_zinflate.h - strict inflate that never calls malloc.
 *
 * The Phase 8 allocator (rp_malloc) keeps unsynchronised counters, and zlib's
 * default allocator calls malloc, so neither may be used on a worker thread.
 * cf_zinflate_exact gives zlib a caller-owned arena instead. Its results are
 * the same as rp_inflate_exact's (Phase 8, unmodified), which the host tests
 * check on every fixture entry:
 *   RP_OK                 stream ended, exactly dst_len bytes, all input used
 *   RP_ERR_SIZE_MISMATCH  output longer/shorter than dst_len, or input left over
 *   RP_ERR_TRUNCATED      input ended before the stream was complete
 *   RP_ERR_ZLIB_DATA      corrupt data (includes Adler-32 failure)
 *   RP_ERR_NOMEM          arena too small; RP_ERR_ZLIB other zlib failures
 */
#ifndef CF_ZINFLATE_H
#define CF_ZINFLATE_H

#include "cf_common.h"

/* Enough for zlib's inflate state plus a 32 KiB window. */
#define CF_ZARENA_BYTES (64u * 1024u)

typedef struct {
    uint8_t *base;
    size_t   size;
    size_t   used;
} cf_zarena;

gp_status cf_zinflate_exact(cf_zarena *arena, const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len);

#endif /* CF_ZINFLATE_H */
