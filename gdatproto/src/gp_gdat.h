/*
 * gp_gdat.h - reader for synthetic GDAT-like archives.
 *
 * Layout (observed, STEAM_ARCHIVE_STRUCTURE.md section 4.1):
 *   0   char[4] "GDAT"
 *   4   u32 LE  entry count
 *   8   count x { u32 LE offset, u32 LE size }
 * Observed in the real archives: entry 0 starts at 8 + 8 x count, entries are
 * sorted, non-overlapping and gap-free, and the last ends exactly at EOF.
 *
 * Check order (first failure wins; nothing large is allocated before the
 * index is known to fit in the file):
 *   size < 8 -> TRUNCATED; magic -> BAD_MAGIC; count > cap -> COUNT;
 *   index past EOF -> TRUNCATED; count == 0 -> EMPTY (strict) / observation;
 *   per entry: offset + size > file (64-bit) -> RANGE, offset inside the
 *   index -> IN_INDEX; unsorted -> UNSORTED (strict) / observation;
 *   in offset order: overlap -> OVERLAP (always), gap -> GAP (strict) /
 *   observation; bytes after the last entry -> TRAILING (strict) / observation.
 */
#ifndef GP_GDAT_H
#define GP_GDAT_H

#include "gp_common.h"
#include "gp_wrap.h"

typedef struct {
    uint32_t offset;
    uint32_t size;
} gp_entry;

typedef struct {
    FILE     *fp;
    uint64_t  file_size;
    uint32_t  count;
    gp_entry *idx;          /* rp_malloc'd, count entries (NULL when count == 0) */
    uint32_t  obs;          /* GP_OBS_* recorded in lenient mode */
    gp_mode   mode;
} gp_gdat;

gp_status gp_gdat_open(gp_gdat *a, const char *path, gp_mode mode);
void      gp_gdat_close(gp_gdat *a);

/* CRC-32 of the parsed (offset, size) pairs re-packed little-endian. */
uint32_t  gp_gdat_index_crc(const gp_gdat *a);

/* Loads entry i through the BILZ wrapper (gp_wrap_load). */
gp_status gp_gdat_load(gp_gdat *a, uint32_t i, gp_wrap_info *w, uint8_t **out, size_t *out_len,
                       gp_load_timing *t);

#endif /* GP_GDAT_H */
