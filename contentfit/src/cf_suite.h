/*
 * cf_suite.h - the ContentFit test run: context, shared helpers and groups.
 *
 * One iteration runs, in this order:
 *   C0  environment (passive)            C1  storage reads
 *   C2  area loads                        C3-P 30 fps pacing calibration
 *   C3  scenes: dungeon, battle, dialogue, menu, combined; 60 fps diagnostic
 *   C4  memory: VRAM tiers, heap tiers, area cycles, streaming, soak
 *   C3-T texture formats (last: compressed uploads are the least proven step)
 * After the last iteration C5 compares the iterations.
 */
#ifndef CF_SUITE_H
#define CF_SUITE_H

#include "cf_budget.h"
#include "cf_plat.h"
#include "cf_report.h"
#include "cf_stats.h"

#define CF_MAX_ITER     3
#define CF_MAX_RESULTS  128
#define CF_MAX_SAMPLES  16384

enum { CF_M_OFF = 0, CF_M_LOCK, CF_M_V1 };
enum { CF_LOCK_NONE = 0, CF_LOCK_INTERVAL2, CF_LOCK_VBLANK2 };

typedef struct cf_ctx cf_ctx;

/* Draws a progress frame between tests (may be NULL). */
typedef void (*cf_ui_fn)(cf_ctx *c, const char *line1, const char *line2);
/* Called once per frame during a measurement, before drawing. */
typedef void (*cf_frame_hook)(cf_ctx *c, void *user, uint32_t frame, int measuring);

struct cf_ctx {
    int        iterations, iter;          /* iter is 0-based */
    int        aborted;
    int        mem_abort;                 /* memory did not return to baseline in C4 */
    cf_ui_fn   ui;
    cf_report  rep;
    cf_result  res[CF_MAX_ITER][CF_MAX_RESULTS];
    int        nres[CF_MAX_ITER];
    int        lock_method;
    int        work_state;                /* 0 unchecked, 1 ready, -1 unavailable */
    char       work_note[96];
    int        best_vram_tier;            /* largest VRAM tier that fitted this iteration (MiB) */
    float      samples[CF_MAX_SAMPLES];
    float      scratch[CF_MAX_SAMPLES];
    int        nsamples;
    cf_env     env_start;
    int        c5_pass;
    char       c5_note[sizeof(((cf_result *)0)->detail)];   /* copy of the C5 row's detail */
    double     t_start_ms;
};

/* Whole run: opens the report, runs 'iterations' iterations, compares them,
 * writes the summary and closes the files. */
void cf_suite_run(cf_ctx *c, int iterations, cf_ui_fn ui);

/* ---- helpers shared by the groups ---- */
cf_result *cf_add(cf_ctx *c, const char *id, const char *name, const char *mode);
void       cf_done(cf_ctx *c, cf_result *r);           /* writes the row and a log line */
void       cf_progress(cf_ctx *c, const char *id, const char *what);
int        cf_should_stop(cf_ctx *c);                  /* polls START; sets c->aborted */

/* Frees nothing itself: waits (glFinish + blank frames, up to 30) until the
 * pools are back within 64 KiB of 'base'. 1 if they are. */
int        cf_settle(cf_ctx *c, const cf_pools *base, cf_pools *now, char *why, size_t why_len);

/* Measures the current scene. Samples go to c->samples / c->nsamples.
 * Returns RP_OK or CF_ERR_ABORTED. */
gp_status  cf_measure(cf_ctx *c, const char *label, int mode, double warm_s, double meas_s, cf_frame_hook hook,
                      void *user);

/* Copies c->samples into r (stats) and the frames file. */
void       cf_take_stats(cf_ctx *c, cf_result *r);

double     cf_mib(uint64_t bytes);
int64_t    cf_kib_delta(const cf_pools *before, const cf_pools *after, cf_pool_id id);

/* Groups (cf_io.c, cf_load.c, cf_scenes.c, cf_memtest.c). */
void cf_group_io(cf_ctx *c);
void cf_group_loads(cf_ctx *c);
void cf_group_pacing(cf_ctx *c);
void cf_group_scenes(cf_ctx *c);
void cf_group_memory(cf_ctx *c);
void cf_group_formats(cf_ctx *c);

#endif /* CF_SUITE_H */
