/*
 * gp_csh.h - checks for synthetic CSH-like files (wrapper and size relation ONLY).
 *
 * Observed layout (STEAM_ARCHIVE_STRUCTURE.md section 5):
 *   0x00 "\0hsc"; 0x04/0x08/0x0C u32 BE 2, 13, 2; 0x10..0x7F zero except
 *   byte 0x2B = 01; 0x80 BILZ block (same wrapper as DAT entries);
 *   compressed size == file size - 0x90 in all 37 real files;
 *   uncompressed size == 12 + 28 x (entry count of the paired archive).
 *
 * The CSH LOOKUP FORMAT IS NOT UNDERSTOOD. This module never decodes,
 * indexes or searches the records: it inflates them only to prove the
 * stream is intact and reports their CRC-32. In the fixtures the record
 * bytes are pseudo-random placeholders.
 *
 * Check order: size < 0x90 -> TRUNCATED; magic -> BAD_MAGIC;
 * header fields / zero region / byte 0x2B -> CSH_FIELD (strict) / observation;
 * wrapper checks (gp_wrap_parse, region = whole file);
 * paired archive opened with gp_gdat_open in the same mode (its error, e.g.
 * NOT_FOUND, is returned); 12 + 28n mismatch -> CSH_PAIR (strict) /
 * observation; then the strict inflate.
 */
#ifndef GP_CSH_H
#define GP_CSH_H

#include "gp_common.h"
#include "gp_wrap.h"

typedef struct {
    uint32_t     field[3];      /* BE fields at 0x04, 0x08, 0x0C */
    uint8_t      byte_2b;
    gp_wrap_info w;
    uint32_t     pair_count;    /* entry count of the paired archive */
    uint32_t     records_size;  /* bytes after inflating (opaque) */
    uint32_t     records_crc;
    uint32_t     obs;
} gp_csh_info;

gp_status gp_csh_check(const char *csh_path, const char *pair_path, gp_mode mode, gp_csh_info *ci);

#endif /* GP_CSH_H */
