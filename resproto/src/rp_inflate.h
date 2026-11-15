/*
 * rp_inflate.h - strict zlib (RFC 1950) helpers.
 */
#ifndef RP_INFLATE_H
#define RP_INFLATE_H

#include "rp_common.h"

/* Inflates a complete zlib stream into dst, which must be exactly the
 * declared uncompressed size. Results:
 *   RP_OK              stream ended, produced exactly dst_len, consumed all input
 *   RP_ERR_SIZE_MISMATCH  output longer or shorter than dst_len, or trailing input
 *   RP_ERR_TRUNCATED   input ended before the stream was complete
 *   RP_ERR_ZLIB_DATA   corrupt data (includes Adler-32 failure)
 *   RP_ERR_NOMEM / RP_ERR_ZLIB  other zlib failures */
rp_err rp_inflate_exact(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len);

/* Compresses src with zlib at 'level' into a new rp_malloc buffer. */
rp_err rp_deflate_alloc(const uint8_t *src, size_t src_len, int level,
                        uint8_t **out, size_t *out_len);

uint32_t rp_crc32(const uint8_t *data, size_t len);

#endif /* RP_INFLATE_H */
