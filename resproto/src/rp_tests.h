/*
 * rp_tests.h - the ResProto test suite.
 */
#ifndef RP_TESTS_H
#define RP_TESTS_H

#include "rp_common.h"
#include "rp_fixture.h"
#include "rp_mem.h"

#define RP_EXPECT_ANY_ERROR (-1)   /* any non-OK result counts as a pass */
#define RP_MAX_RESULTS 48

typedef struct {
    char   id[8];
    char   name[48];
    int    expect;        /* rp_err, or RP_EXPECT_ANY_ERROR */
    rp_err actual;
    int    pass;
    double ms;            /* primary timing (0 if not timed) */
    double mbps;          /* primary throughput (0 if n/a) */
    uint32_t check;       /* deterministic value compared across iterations */
    char   detail[96];
} rp_result;

typedef struct {
    rp_result r[RP_MAX_RESULTS];
    int count;
    int passed;
    rp_mem_snapshot mem_before, mem_after;
    rp_mem_stats stats_end;
    size_t probe_max_bytes;
    char pc_fixture_dir[64];
} rp_suite;

void rp_suite_run(rp_suite *s, const rp_fixture_set *fx, rp_progress_fn progress);

/* T14: compares pass flags and check values of several runs.
 * Returns 1 if identical; fills 'detail' with a summary. */
int rp_suite_compare(const rp_suite *runs, int n, char *detail, size_t detail_len,
                     double *ms_min, double *ms_max);

#endif /* RP_TESTS_H */
