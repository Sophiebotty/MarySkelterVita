/*
 * cf_stats.c - frame-time statistics (see cf_stats.h for the definitions).
 */
#include <stdlib.h>
#include <string.h>

#include "cf_stats.h"

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

/* Nearest rank: the smallest value with at least p% of samples at or below it. */
static double rank(const float *sorted, int n, int pct)
{
    long r = ((long)pct * n + 99) / 100;    /* ceil(pct * n / 100), integer arithmetic */
    if (r < 1) r = 1;
    if (r > n) r = n;
    return sorted[r - 1];
}

void cf_stats_compute(const float *ms, int n, float *scratch, cf_frame_stats *out)
{
    double sum = 0.0;
    int i;

    memset(out, 0, sizeof(*out));
    if (n <= 0 || !ms || !scratch)
        return;
    out->n = n;
    out->min = out->max = ms[0];
    for (i = 0; i < n; i++) {
        double v = ms[i];
        sum += v;
        if (v < out->min) out->min = v;
        if (v > out->max) out->max = v;
        if (v > CF_OVER_60_MS) out->over_16_7++;
        if (v > CF_LOCK_LIMIT_MS) out->over_34++;
        if (v > CF_LOCK_MAX_MS) out->over_51++;
    }
    out->avg = sum / n;
    memcpy(scratch, ms, (size_t)n * sizeof(float));
    qsort(scratch, (size_t)n, sizeof(float), cmp_float);
    out->p50 = rank(scratch, n, 50);
    out->p95 = rank(scratch, n, 95);
    out->p99 = rank(scratch, n, 99);
}

int cf_stable30(const cf_frame_stats *f)
{
    if (!f || f->n <= 0)
        return 0;
    /* over_34 <= 1% of n, compared in integers: 100 * over <= pct * n */
    return 100L * f->over_34 <= (long)CF_LOCK_MISS_PCT * f->n && f->p95 <= CF_LOCK_LIMIT_MS &&
           f->max <= CF_LOCK_MAX_MS;
}

cf_fit cf_classify(const cf_frame_stats *locked, const cf_frame_stats *off)
{
    if (!locked || !off || locked->n <= 0 || off->n <= 0)
        return CF_FIT_SKIPPED;
    if (!cf_stable30(locked) || off->p95 > CF_TIGHT_COST_MS)
        return CF_NOT_30;
    return off->p95 <= CF_FIT_COST_MS ? CF_FIT_30 : CF_TIGHT_30;
}

int cf_60_candidate(const cf_frame_stats *off)
{
    return off && off->n > 0 && off->p95 <= CF_60_COST_MS;
}

const char *cf_fit_name(cf_fit f)
{
    switch (f) {
    case CF_FIT_30:   return "FITS_30";
    case CF_TIGHT_30: return "TIGHT_30";
    case CF_NOT_30:   return "NOT_30";
    default:          return "SKIPPED";
    }
}
