/*
 * rp_fixture.h - deterministic, original test data generated on the Vita.
 *
 * Everything written here is synthetic (pseudo-random bytes, pseudo-words
 * from a fixed syllable list, zeros). No game data is used.
 */
#ifndef RP_FIXTURE_H
#define RP_FIXTURE_H

#include "rp_common.h"

#define RP_BASE_DIR "ux0:data/ResProto"
#define RP_WORK_DIR "ux0:data/ResProto/work"

/* PC-generated fixtures shipped in the VPK. Tried in this order. */
#define RP_PC_FIXTURE_DIR_UX0  "ux0:app/VDNG00003/fixtures"
#define RP_PC_FIXTURE_DIR_APP0 "app0:fixtures"

typedef enum { RP_PAY_TEXT = 0, RP_PAY_RANDOM, RP_PAY_ZERO, RP_PAY_KINDS } rp_payload_kind;

typedef struct {
    const char *name;
    uint32_t size;
    uint32_t seed;
    uint32_t crc;          /* computed while writing */
} rp_io_file;

#define RP_IO_FILES 4
#define RP_VALID_ENTRIES 48

typedef struct {
    rp_io_file io[RP_IO_FILES];   /* 4 KB, 1 MB, 8 MB, 16 MB */
    double write_ms;              /* time to write the I/O files */
    uint64_t write_bytes;
    rp_err status;                /* RP_OK if every fixture was written */
    char failed_file[48];
} rp_fixture_set;

typedef void (*rp_progress_fn)(const char *msg);

rp_err rp_fixture_generate(rp_fixture_set *fs, rp_progress_fn progress);
int    rp_fixture_delete(void);           /* returns number of files removed */

/* Returns 1 if every generated work file exists; otherwise 0 and, if
 * missing_name is not NULL, copies the first missing file name into it. */
int    rp_fixture_present(char *missing_name, size_t missing_len);

void rp_gen_random_at(uint32_t seed, uint64_t pos, uint8_t *buf, size_t n);
void rp_gen_text(uint32_t seed, uint8_t *buf, size_t n);
void rp_gen_payload(rp_payload_kind kind, uint32_t seed, uint8_t *buf, size_t n);

/* Builds "<RP_WORK_DIR>/<name>" into buf. */
const char *rp_work_path(const char *name, char *buf, size_t n);

#endif /* RP_FIXTURE_H */
