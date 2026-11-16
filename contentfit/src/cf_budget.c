/*
 * cf_budget.c - pre-allocation check, margin guard, spill and baseline tests.
 * Pure arithmetic on pool snapshots; no allocation, no platform calls.
 */
#include <stdio.h>

#include "cf_budget.h"

static const char *const NAMES[CF_POOL_COUNT] = {"VRAM", "GLRAM", "PHYCONT", "CDLG", "HEAP", "KERNEL"};

const char *cf_pool_name(cf_pool_id id)
{
    return ((unsigned)id < CF_POOL_COUNT) ? NAMES[id] : "?";
}

uint64_t cf_pool_margin(cf_pool_id id)
{
    switch (id) {
    case CF_POOL_VRAM:   return CF_MARGIN_VRAM;
    case CF_POOL_GLRAM:  return CF_MARGIN_GLRAM;
    case CF_POOL_HEAP:   return CF_MARGIN_HEAP;
    case CF_POOL_KERNEL: return CF_MARGIN_KERNEL;
    default:             return 0;
    }
}

uint64_t cf_with_allowance(uint64_t need)
{
    return need + (need + 99u) / 100u;
}

static double mib(uint64_t b) { return (double)b / (double)CF_MIB; }

gp_status cf_precheck(const cf_pools *now, const cf_need *need, char *why, size_t why_len)
{
    int i;
    if (why && why_len) why[0] = '\0';
    if (!now || !need)
        return RP_ERR_ARG;
    for (i = 0; i < CF_POOL_COUNT; i++) {
        uint64_t want, margin;
        if (need->b[i] == 0)
            continue;
        if (!now->p[i].known) {
            if (why) snprintf(why, why_len, "%s size unknown; not used", NAMES[i]);
            return CF_ERR_UNKNOWN_SIZE;
        }
        margin = cf_pool_margin((cf_pool_id)i);
        want = cf_with_allowance(need->b[i]);
        if (now->p[i].free < margin || now->p[i].free - margin < want) {
            if (why)
                snprintf(why, why_len, "%s needs %.2f MiB + %.0f MiB margin; %.2f MiB free", NAMES[i],
                         mib(want), mib(margin), mib(now->p[i].free));
            return CF_ERR_SKIPPED;
        }
    }
    return RP_OK;
}

gp_status cf_guard(const cf_pools *now, cf_pool_id id, char *why, size_t why_len)
{
    if (why && why_len) why[0] = '\0';
    if (!now || (unsigned)id >= CF_POOL_COUNT)
        return RP_ERR_ARG;
    if (!now->p[id].known) {
        if (why) snprintf(why, why_len, "%s size unknown", NAMES[id]);
        return CF_ERR_UNKNOWN_SIZE;
    }
    if (now->p[id].free < cf_pool_margin(id)) {
        if (why)
            snprintf(why, why_len, "%s fell to %.2f MiB free, below its %.0f MiB margin", NAMES[id],
                     mib(now->p[id].free), mib(cf_pool_margin(id)));
        return CF_ERR_MARGIN;
    }
    return RP_OK;
}

int cf_spilled(const cf_pools *before, const cf_pools *after, cf_pool_id intended, uint64_t tol,
               cf_pool_id *where)
{
    int i;
    for (i = CF_POOL_VRAM; i <= CF_POOL_CDLG; i++) {
        if (i == (int)intended || !before->p[i].known || !after->p[i].known)
            continue;
        if (after->p[i].free + tol < before->p[i].free) {
            if (where) *where = (cf_pool_id)i;
            return 1;
        }
    }
    return 0;
}

int cf_baseline_ok(const cf_pools *base, const cf_pools *now, uint64_t tol, cf_pool_id *where)
{
    int i;
    for (i = CF_POOL_VRAM; i <= CF_POOL_HEAP; i++) {
        if (!base->p[i].known || !now->p[i].known)
            continue;
        if (now->p[i].free + tol < base->p[i].free) {
            if (where) *where = (cf_pool_id)i;
            return 0;
        }
    }
    return 1;
}
