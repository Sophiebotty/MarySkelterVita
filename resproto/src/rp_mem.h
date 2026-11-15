/*
 * rp_mem.h - tracking allocator and memory snapshots.
 */
#ifndef RP_MEM_H
#define RP_MEM_H

#include "rp_common.h"

typedef struct {
    size_t current;     /* bytes currently allocated through rp_malloc */
    size_t peak;        /* highest 'current' since the last rp_mem_reset_peak */
    size_t allocs;      /* successful allocations */
    size_t frees;
    size_t failed;      /* refused (cap) or failed allocations */
} rp_mem_stats;

typedef struct {
    size_t heap_arena;      /* mallinfo().arena: heap obtained from the system */
    size_t heap_in_use;     /* mallinfo().uordblks */
    long   free_user;       /* sceKernelGetFreeMemorySize: USER_RW, -1 if unavailable */
    long   free_cdram;
    long   free_phycont;
} rp_mem_snapshot;

void *rp_malloc(size_t n);          /* NULL if n == 0, n > RP_MAX_ALLOC or OOM */
void  rp_free(void *p);
rp_mem_stats rp_mem_get(void);
void  rp_mem_reset_peak(void);

/* Implemented in rp_mem.c; the free-memory part uses the Vita kernel API. */
void  rp_mem_snapshot_take(rp_mem_snapshot *s);

/* Largest single malloc() that succeeds (binary search, 1 MB resolution,
 * up to 'limit' bytes). Memory is released immediately. */
size_t rp_mem_probe_max(size_t limit);

#endif /* RP_MEM_H */
