/*
 * gp_manifest.h - reads fixtures/expected.txt, the PC-generated expected
 * results (written by tools/make_gdatproto_fixtures.py from its own
 * construction intent, and cross-checked by tools/check_gdatproto_fixtures.py).
 *
 * Line format (space-separated, one record per line):
 *   GPX 1
 *   FILE  <name> <size> <crc32 hex> <entry count or -1>
 *   ENTRY <file index> <i> <offset> <size> <usize or -1> <csize or -1> <payload crc32 hex>
 *   CASE  <id> <name> <op> <strict|lenient> <file|-> <arg|-> <expected status> <obs hex> <count|-1> <check hex|->
 *   END
 */
#ifndef GP_MANIFEST_H
#define GP_MANIFEST_H

#include "gp_common.h"

#define GP_MAX_FILES 64
#define GP_MAX_CASES 128

typedef struct {
    char     name[40];
    uint32_t size;
    uint32_t crc;
    int32_t  n_entries;     /* -1: no per-entry expectations */
    uint32_t first_entry;   /* index into gp_manifest.entries */
} gp_mfile;

typedef struct {
    uint32_t offset, size;
    int64_t  usize, csize;  /* -1 when the entry is shorter than its header */
    uint32_t crc;           /* CRC-32 of the original (uncompressed) payload */
} gp_mentry;

typedef struct {
    char      id[12];
    char      name[40];
    char      op[12];
    gp_mode   mode;
    char      file[40];
    char      arg[64];
    gp_status expect;
    uint32_t  obs;
    int32_t   count;
    int       has_check;
    uint32_t  check;
} gp_mcase;

typedef struct {
    char       dir[48];     /* fixture directory that worked (with trailing '/') */
    gp_mfile   files[GP_MAX_FILES];
    int        n_files;
    gp_mentry *entries;     /* rp_malloc'd */
    uint32_t   n_entries;
    gp_mcase   cases[GP_MAX_CASES];
    int        n_cases;
} gp_manifest;

/* Tries GP_FIX_DIR_UX0 then GP_FIX_DIR_APP0. On failure 'why' says which line. */
gp_status gp_manifest_load(gp_manifest *m, char *why, size_t why_len);
void      gp_manifest_free(gp_manifest *m);

const gp_mfile *gp_manifest_file(const gp_manifest *m, const char *name);
void gp_manifest_path(const gp_manifest *m, const char *name, char *buf, size_t n);

#endif /* GP_MANIFEST_H */
