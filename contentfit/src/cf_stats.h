/*
 * cf_stats.h - frame-time statistics and the 30 fps fit classification.
 *
 * Percentiles use the nearest-rank method: Pp = sorted[ceil(p/100 * n) - 1].
 * tools/analyze_contentfit_results.py implements the same definitions and
 * recomputes every value from the raw frame file.
 *
 * Primary target: stable 30 fps. A locked run is "stable" when
 *   - at most 1% of frame intervals exceed 34.0 ms (33.3 ms + 0.7 ms timer slack),
 *   - P95 <= 34.0 ms, and
 *   - no interval exceeds 51.0 ms (never two missed refreshes in a row).
 * Classes (rendering cost = vsync-off P95):
 *   FITS_30   stable and cost <= 16.7 ms (half the frame left for game work)
 *   TIGHT_30  stable and 16.7 < cost <= 25 ms
 *   NOT_30    not stable, or cost > 25 ms
 * 60 fps is diagnostic only: "60 candidate" when the vsync-off P95 <= 8.3 ms.
 */
#ifndef CF_STATS_H
#define CF_STATS_H

#include "cf_common.h"

#define CF_LOCK_LIMIT_MS   34.0
#define CF_LOCK_MAX_MS     51.0
#define CF_LOCK_MISS_PCT   1          /* at most 1% of intervals over the limit */
#define CF_FIT_COST_MS     16.7
#define CF_TIGHT_COST_MS   25.0
#define CF_60_COST_MS      8.3
#define CF_OVER_60_MS      16.7

typedef enum { CF_FIT_SKIPPED = 0, CF_FIT_30, CF_TIGHT_30, CF_NOT_30 } cf_fit;

/* ms: n samples (unchanged). scratch: room for n floats (sorted copy). */
void cf_stats_compute(const float *ms, int n, float *scratch, cf_frame_stats *out);

/* 1 if the locked run meets every stability rule above (n must be > 0). */
int cf_stable30(const cf_frame_stats *locked);

/* Fit class from a locked run and a vsync-off run of the same scene. */
cf_fit cf_classify(const cf_frame_stats *locked, const cf_frame_stats *off);

int cf_60_candidate(const cf_frame_stats *off);

const char *cf_fit_name(cf_fit f);

#endif /* CF_STATS_H */
