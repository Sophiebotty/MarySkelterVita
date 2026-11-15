/*
 * rp_mem.c - tracking allocator (size header in front of each block).
 */
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include "rp_mem.h"

#ifdef __vita__
#include <psp2/kernel/sysmem.h>
#endif

/* 16-byte header in front of each block. Because 16 is a multiple of the
 * alignment malloc() guarantees, the caller receives a pointer with the same
 * alignment malloc() itself provides (8 bytes on newlib). It does NOT make
 * the block 16-byte aligned. */
#define RP_HDR 16u
#define RP_HDR_MAGIC 0x52504D4Du   /* 'RPMM' */

static rp_mem_stats s_st;

void *rp_malloc(size_t n)
{
    unsigned char *p;
    if (n == 0 || n > RP_MAX_ALLOC) {
        s_st.failed++;
        return NULL;
    }
    p = (unsigned char *)malloc(n + RP_HDR);
    if (!p) {
        s_st.failed++;
        return NULL;
    }
    ((size_t *)p)[0] = n;
    ((uint32_t *)p)[2] = RP_HDR_MAGIC;
    s_st.current += n;
    s_st.allocs++;
    if (s_st.current > s_st.peak)
        s_st.peak = s_st.current;
    return p + RP_HDR;
}

void rp_free(void *ptr)
{
    unsigned char *p;
    if (!ptr)
        return;
    p = (unsigned char *)ptr - RP_HDR;
    if (((uint32_t *)p)[2] != RP_HDR_MAGIC)
        abort();                   /* not ours: programming error */
    s_st.current -= ((size_t *)p)[0];
    s_st.frees++;
    ((uint32_t *)p)[2] = 0;
    free(p);
}

rp_mem_stats rp_mem_get(void) { return s_st; }
void rp_mem_reset_peak(void)  { s_st.peak = s_st.current; }

void rp_mem_snapshot_take(rp_mem_snapshot *s)
{
    struct mallinfo mi = mallinfo();
    memset(s, 0, sizeof(*s));
    s->heap_arena = mi.arena;
    s->heap_in_use = mi.uordblks;
    s->free_user = s->free_cdram = s->free_phycont = -1;
#ifdef __vita__
    {
        SceKernelFreeMemorySizeInfo info;
        memset(&info, 0, sizeof(info));
        info.size = sizeof(info);
        if (sceKernelGetFreeMemorySize(&info) >= 0) {
            s->free_user = info.size_user;
            s->free_cdram = info.size_cdram;
            s->free_phycont = info.size_phycont;
        }
    }
#endif
}

size_t rp_mem_probe_max(size_t limit)
{
    const size_t step = 1024u * 1024u;
    size_t lo = 0, hi = limit / step;   /* in MB units: lo succeeds, hi+1 fails */
    void *p;

    /* quick check of the top */
    p = malloc(hi * step);
    if (p) { free(p); return hi * step; }
    while (lo < hi) {
        size_t mid = lo + (hi - lo + 1) / 2;
        p = malloc(mid * step);
        if (p) { free(p); lo = mid; } else { hi = mid - 1; }
    }
    return lo * step;
}
