/*
 * cf_fixdefs.h - type of the expected values that
 * tools/make_contentfit_fixtures.py writes into fixtures/cf_fixtures.h.
 * Field order must match the generator's header rows.
 */
#ifndef CF_FIXDEFS_H
#define CF_FIXDEFS_H

#include <stdint.h>

typedef struct {
    const char *name;
    int         target_mib;     /* payload total in MiB */
    uint32_t    file_size;
    uint32_t    file_crc;
    uint32_t    entries;
    uint32_t    raw_total;      /* sum of payload (uncompressed) sizes */
    uint32_t    tex_count;
    uint32_t    tex_bytes;      /* RGBA8 texel bytes across texture entries */
    uint32_t    data_bytes;     /* body bytes across data entries */
    uint32_t    max_region;     /* largest compressed region (wrapper + stream) */
    uint32_t    max_usize;      /* largest payload */
    uint32_t    list_crc;       /* CRC-32 of the LE u32 body CRCs in index order */
} cf_fix_pack;

/* Accessors (cf_fixinfo.c is the only file that includes cf_fixtures.h). */
int                cf_fix_pack_count(void);
const cf_fix_pack *cf_fix_pack_get(int i);
const char        *cf_fix_io_name(void);
uint32_t           cf_fix_io_size(void);
uint32_t           cf_fix_io_crc(void);
uint32_t           cf_fix_io_half_crc(void);
int                cf_fix_portrait_pack(void);
uint32_t           cf_fix_portrait_entry(void);

#endif /* CF_FIXDEFS_H */
