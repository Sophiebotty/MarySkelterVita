/*
 * rp_container.h - "VRP1", the project's own test container format.
 *
 * This is an original format designed to exercise validation paths. It is
 * not, and does not imitate, any game archive format.
 *
 * All integers little-endian.
 *   Header (32 bytes):
 *     0  char[4] "VRP1"
 *     4  u16     version (1)
 *     6  u16     flags (0)
 *     8  u32     entry_count
 *    12  u32     index_offset (must be 32)
 *    16  u32     file_size (declared total size)
 *    20  u32     data_offset (must be 32 + 24 * entry_count)
 *    24  u32     index_crc32 (CRC-32 of the index bytes)
 *    28  u32     header_crc32 (CRC-32 of header bytes 0..27)
 *   Index entry (24 bytes) x entry_count:
 *     u32 offset, u32 stored_size, u32 raw_size, u32 flags (bit0 = zlib),
 *     u32 crc32 (CRC-32 of the raw/uncompressed payload), u32 reserved (0)
 *   Data: entries in ascending offset order, non-overlapping.
 */
#ifndef RP_CONTAINER_H
#define RP_CONTAINER_H

#include <stdio.h>

#include "rp_common.h"

#define RP_VRP_VERSION      1u
#define RP_VRP_HEADER_SIZE  32u
#define RP_VRP_ENTRY_SIZE   24u
#define RP_VRP_FLAG_ZLIB    1u

typedef struct {
    uint32_t offset, stored_size, raw_size, flags, crc32, reserved;
} rp_entry;

typedef struct {
    FILE    *fp;
    uint64_t file_size;
    uint32_t count;
    uint32_t data_offset;
    rp_entry *entries;      /* rp_malloc'd, NULL when count == 0 */
} rp_container;

/* Opens and fully validates header + index. On error nothing stays open. */
rp_err rp_container_open(rp_container *c, const char *path);

/* Loads entry i into a new rp_malloc buffer (decompressing if flagged) and
 * verifies its CRC-32. */
rp_err rp_container_load(rp_container *c, uint32_t i, uint8_t **out, uint32_t *out_len);

void rp_container_close(rp_container *c);

/* ---- builder (used by the on-device fixture generator) ---- */
typedef struct {
    const uint8_t *data;
    uint32_t size;
    int compress;           /* 1 = store zlib-compressed */
} rp_item;

rp_err rp_container_build(const rp_item *items, uint32_t n, uint8_t **out, size_t *out_len);

/* Recomputes index CRC and header CRC from the fields in a built buffer
 * (used after deliberately patching fields to isolate one error). */
void rp_container_fix_crcs(uint8_t *buf, size_t len);

/* Little-endian helpers, exposed for fixture patching. */
uint32_t rp_rd32(const uint8_t *p);
void     rp_wr32(uint8_t *p, uint32_t v);

#endif /* RP_CONTAINER_H */
