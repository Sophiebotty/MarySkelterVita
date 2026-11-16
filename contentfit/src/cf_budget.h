/*
 * cf_budget.h - memory pools, margins and the pre-allocation check.
 *
 * Every step that allocates states what it needs from each pool. It runs
 * only if, for every pool it needs,
 *     free_now - margin >= need * 1.01        (1% allocator allowance)
 * computed in integers as  free_now >= margin + need + ceil(need / 100).
 * A pool whose size could not be measured is never used (UNKNOWN_SIZE).
 * Nothing is allocated before this check passes.
 *
 * Margins (approved Stage 4 plan): video memory 16 MiB, vitaGL RAM pool
 * 16 MiB, C library heap 24 MiB, kernel free memory 8 MiB.
 */
#ifndef CF_BUDGET_H
#define CF_BUDGET_H

#include "cf_common.h"

typedef enum {
    CF_POOL_VRAM = 0,   /* vitaGL CDRAM pool (vglMemFree(VGL_MEM_VRAM)) */
    CF_POOL_GLRAM,      /* vitaGL USER_RW pool (VGL_MEM_RAM) */
    CF_POOL_PHYCONT,    /* vitaGL PHYCONT pool */
    CF_POOL_CDLG,       /* vitaGL CDLG pool (VGL_MEM_BUDGET) */
    CF_POOL_HEAP,       /* C library heap: memblock size minus bytes in use */
    CF_POOL_KERNEL,     /* sceKernelGetFreeMemorySize USER_RW (thread stacks come from here) */
    CF_POOL_COUNT
} cf_pool_id;

typedef struct {
    int      known;
    uint64_t total;
    uint64_t free;
} cf_pool;

typedef struct {
    cf_pool p[CF_POOL_COUNT];
} cf_pools;

typedef struct {
    uint64_t b[CF_POOL_COUNT];
} cf_need;

#define CF_MARGIN_VRAM    (16ull * CF_MIB)
#define CF_MARGIN_GLRAM   (16ull * CF_MIB)
#define CF_MARGIN_HEAP    (24ull * CF_MIB)
#define CF_MARGIN_KERNEL  (8ull * CF_MIB)
#define CF_BASELINE_TOL   (64ull * CF_KIB)

const char *cf_pool_name(cf_pool_id id);
uint64_t    cf_pool_margin(cf_pool_id id);

/* need + ceil(need / 100) */
uint64_t cf_with_allowance(uint64_t need);

/* RP_OK, CF_ERR_SKIPPED or CF_ERR_UNKNOWN_SIZE; 'why' names the first pool
 * that refused (may be NULL). */
gp_status cf_precheck(const cf_pools *now, const cf_need *need, char *why, size_t why_len);

/* Second-line check during allocation: CF_ERR_MARGIN if the pool is now below
 * its margin, CF_ERR_UNKNOWN_SIZE if unknown, RP_OK otherwise. */
gp_status cf_guard(const cf_pools *now, cf_pool_id id, char *why, size_t why_len);

/* 1 if any vitaGL pool other than 'intended' lost more than 'tol' bytes of
 * free space between the two snapshots; *where gets the first such pool. */
int cf_spilled(const cf_pools *before, const cf_pools *after, cf_pool_id intended, uint64_t tol,
               cf_pool_id *where);

/* 1 if every known pool among VRAM, GLRAM, PHYCONT, CDLG and HEAP has at
 * least (baseline free - tol) free again; *where gets the first that has not. */
int cf_baseline_ok(const cf_pools *base, const cf_pools *now, uint64_t tol, cf_pool_id *where);

#endif /* CF_BUDGET_H */
