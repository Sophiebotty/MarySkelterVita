/*
 * rs_plat_pc.h - PC stand-in for the platform services the ResPipe loader
 * uses through ContentFit's background reader (stage 10.4 tests only; the
 * Vita implementation belongs to respipe/ in stage 10.6).
 *
 * It implements the ten functions of contentfit/src/cf_plat.h that cf_pack.c
 * calls (raw reads, threads, semaphores) plus Phase 8's rp_now_ms, with the
 * Vita's behaviour where it matters to the loader:
 *   - raw reads only below app0:fixtures/ (no ".."), relative to the current
 *     directory; handles below 0 are ignored on close, as on the Vita;
 *   - a semaphore never goes above its maximum: extra signals are dropped
 *     and counted (sceKernelSignalSema refuses them);
 *   - threads get 8x the requested stack, for the sanitizers' larger frames
 *     (ContentFit's PC stand-in does the same).
 * Tests can inject faults, hold a read, read the live and total counters,
 * set the memory snapshot the loader sees, and drive the clock.
 * Faults and the snapshot are set only while no worker is running.
 */
#ifndef RS_PLAT_PC_H
#define RS_PLAT_PC_H

#include <stddef.h>
#include <stdint.h>

#include "cf_budget.h"

typedef struct {
    int open_files, threads, semaphores;        /* currently alive */
} rs_pc_live;

typedef struct {
    unsigned opens, open_refused, threads_started, semas_created, preads, short_reads, held_reads;
    unsigned sema_overflows;                     /* signals dropped at a semaphore's maximum */
} rs_pc_totals;

/* Clears faults and totals; real clock; default (M1-like) memory snapshot. */
void rs_pc_reset(void);
rs_pc_live rs_pc_live_now(void);
rs_pc_totals rs_pc_totals_now(void);

/* Faults (one-shot unless stated). */
void rs_pc_fail_next_open(void);
void rs_pc_fail_sema_create(int nth);            /* the nth creation from now fails; 0 = none */
void rs_pc_fail_next_thread(void);
void rs_pc_short_read_at(int64_t offset);        /* a read at this file offset returns one byte short; -1 = off */
void rs_pc_hold_read_at(int64_t offset, int ms); /* a read at this offset first waits ms; -1 = off */
/* A read at this offset returns these bytes instead of the file's (the file
 * changed between two reads); offset -1 = off. The bytes are not copied. */
void rs_pc_substitute_read(int64_t offset, const uint8_t *data, size_t len);
int  rs_pc_read_held(void);                      /* 1 once the held read has started */

/* Memory snapshot handed to the loader (matches rs_pools_fn). */
void rs_pc_default_pools(cf_pools *p);
void rs_pc_set_pools(const cf_pools *p);
void rs_pc_pools(void *ctx, cf_pools *out);

/* Clock behind rp_now_ms. */
void rs_pc_clock_manual(int on);
void rs_pc_clock_set(double ms);
void rs_pc_clock_advance(double ms);

#endif /* RS_PLAT_PC_H */
