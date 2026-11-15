/*
 * gp_wrap.h - the 128-byte entry header + "BILZ" block (synthetic fixtures).
 *
 * Observed layout (STEAM_ARCHIVE_STRUCTURE.md sections 4.3 and 5.1):
 *   0x00  4-byte tag (00 + three reversed ASCII letters in the sample).
 *         Treated as OPAQUE bytes; never interpreted.
 *   0x04..0x7F  UNKNOWN. Recorded (CRC of the bytes) but never interpreted.
 *   0x80  "BILZ"
 *   0x84  u32 BE uncompressed size
 *   0x88  u32 BE compressed size
 *   0x8C  u32 BE constant, 7 in every sample (meaning unknown)
 *   0x90  stream beginning 78 DA (consistent with zlib)
 *
 * Check order: region < 0x90 -> TRUNCATED; "BILZ" -> BAD_MAGIC;
 * compressed > region - 0x90 -> RANGE; uncompressed > 16 MB -> LIMIT;
 * constant != 7 -> CONST (strict) / observation;
 * compressed < region - 0x90 -> SIZE_RULE (strict) / observation.
 * The size rule (compressed == region - 0x90) was confirmed only for the 37
 * CSH files; for DAT entries it is an ASSUMPTION, hence the lenient mode.
 * Then: compressed == 0 -> TRUNCATED; inflate must give exactly the declared
 * size and consume the whole stream (rp_inflate_exact).
 */
#ifndef GP_WRAP_H
#define GP_WRAP_H

#include "gp_common.h"

typedef struct {
    uint8_t  tag[4];
    uint32_t usize;         /* declared uncompressed size */
    uint32_t csize;         /* declared compressed size */
    uint32_t constant;      /* third BILZ field */
    uint32_t region;        /* bytes available to the wrapper */
    uint32_t unknown_crc;   /* CRC-32 of header bytes 0x04..0x7F (recorded only) */
    uint32_t obs;
} gp_wrap_info;

typedef struct {
    double   read_ms;       /* time in rp_read_at for streams */
    double   inflate_ms;    /* time in rp_inflate_exact */
    uint64_t read_bytes;    /* stream bytes read */
    uint64_t raw_bytes;     /* bytes produced */
} gp_load_timing;

/* Validates a 0x90-byte header for a region of 'region' bytes. */
gp_status gp_wrap_parse(const uint8_t *hdr, uint32_t region, gp_mode mode, gp_wrap_info *w);

/* Reads the stream at stream_off (w->csize bytes) and inflates it into a new
 * rp_malloc buffer of w->usize bytes (*out is a 1-byte buffer when usize==0). */
gp_status gp_wrap_inflate(FILE *fp, uint64_t file_size, uint64_t stream_off, const gp_wrap_info *w,
                          uint8_t **out, size_t *out_len, gp_load_timing *t);

/* Header read + parse + inflate for a region [off, off + size). */
gp_status gp_wrap_load(FILE *fp, uint64_t file_size, uint64_t off, uint32_t size, gp_mode mode,
                       gp_wrap_info *w, uint8_t **out, size_t *out_len, gp_load_timing *t);

/* Reads only the stream (no inflate) - used for the read-throughput test. */
gp_status gp_wrap_read_stream(FILE *fp, uint64_t file_size, uint64_t off, uint32_t size, gp_mode mode,
                              uint8_t **out, size_t *out_len);

#endif /* GP_WRAP_H */
