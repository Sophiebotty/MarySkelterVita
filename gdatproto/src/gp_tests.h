/*
 * gp_tests.h - Vita GDAT Proto test suite.
 *
 * G0-01 checks every fixture file against the PC manifest (size + CRC-32).
 * G0-02.. G5 are the PC-defined cases from expected.txt (path allowlist,
 * valid/invalid GDAT, wrapper, CSH, loading). G6 are on-device performance
 * measurements (also verified against the manifest). G7 (gp_suite_compare)
 * compares three runs.
 */
#ifndef GP_TESTS_H
#define GP_TESTS_H

#include "gp_common.h"
#include "rp_mem.h"

#define GP_MAX_RESULTS 112

typedef struct {
    char      id[12];
    char      name[40];
    char      mode[2];      /* "S", "L" or "-" */
    gp_status expect;
    gp_status actual;
    int       pass;
    uint32_t  expect_obs;
    uint32_t  obs;
    double    ms;
    double    mbps;
    uint32_t  check;        /* deterministic value compared across runs */
    char      detail[112];
} gp_result;

typedef struct {
    gp_result r[GP_MAX_RESULTS];
    int count;
    int passed;
    /* Memory: passive measurements only. No allocation probe is performed. */
    rp_mem_snapshot mem_before, mem_after;  /* heap + free-memory snapshots around the iteration;
                                               free_* fields are -1 when unavailable */
    rp_mem_stats stats_end;      /* allocator counters at the end (allocs/frees/refused count
                                    since app start; current = bytes still tracked) */
    size_t peak_suite_bytes;     /* tracked rp_malloc peak over the whole iteration */
    size_t peak_load_bytes;      /* tracked rp_malloc peak during the G6 perf loads */
    char fixture_dir[48];
    char manifest_note[96];
} gp_suite;

typedef void (*gp_progress_fn)(const char *msg);

void gp_suite_run(gp_suite *s, gp_progress_fn progress);

/* G7: 1 if all runs agree on pass, status, observations and check values. */
int gp_suite_compare(const gp_suite *runs, int n, char *detail, size_t detail_len);

#endif /* GP_TESTS_H */
