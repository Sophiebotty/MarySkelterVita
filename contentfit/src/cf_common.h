/*
 * cf_common.h - shared constants, status codes and the result record for
 * Vita ContentFit (Stage 4 content-fit benchmark, title VDNG00005).
 *
 * Everything ContentFit draws, reads or allocates is synthetic. It reads no
 * Mary Skelter file and its results say nothing about Finale itself; they
 * describe what fits on this Vita with this project's SDL2 + vitaGL stack.
 *
 * Status values: below 100 are the unmodified Phase 8 rp_err codes, 100-199
 * the GDAT Proto codes (gp_common.h), 200+ the ContentFit codes below.
 */
#ifndef CF_COMMON_H
#define CF_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include "gp_common.h"

#define CF_MIB (1024u * 1024u)
#define CF_KIB 1024u

#define CF_SCREEN_W 960
#define CF_SCREEN_H 544
#define CF_SCREEN_PIXELS (960.0 * 544.0)

#define CF_TITLE_ID     "VDNG00005"
#define CF_OUT_DIR      "ux0:data/ContentFit"
#define CF_OUT_PREFIX   "ux0:data/ContentFit/"
#define CF_WORK_DIR     "ux0:data/ContentFit/work"
#define CF_WORK_PREFIX  "ux0:data/ContentFit/work/"
#define CF_FIX_PREFIX   "app0:fixtures/"

enum {
    CF_ERR_BASE = 200,
    CF_ERR_PATH = CF_ERR_BASE,  /* path outside ContentFit's read or write allowlist */
    CF_ERR_EXISTS,              /* output file already exists: never overwritten */
    CF_ERR_SKIPPED,             /* memory pre-check refused the step before allocating */
    CF_ERR_UNKNOWN_SIZE,        /* a pool the step needs could not be measured: skipped */
    CF_ERR_MARGIN,              /* second-line check: a pool fell below its margin; stopped */
    CF_ERR_SPILL,               /* an allocation landed in a pool other than the intended one */
    CF_ERR_BASELINE,            /* memory did not return to the starting level after freeing */
    CF_ERR_GL,                  /* OpenGL reported an error */
    CF_ERR_UNSUPPORTED,         /* texture format upload rejected */
    CF_ERR_THREAD,              /* thread or semaphore creation failed */
    CF_ERR_ABORTED,             /* START pressed */
    CF_ERR_PAYLOAD,             /* CFP1 payload header or body CRC wrong */
    CF_ERR_NO_SPACE,            /* not enough free space on ux0 to write */
    CF_ERR_END
};

/* Name for any status: CF codes here, GDAT Proto and Phase 8 codes otherwise. */
const char *cf_status_name(gp_status s);

/* Appends sep + s to the string in dst (capacity n) when s is not empty,
 * cutting the text short if it does not fit. dst stays terminated. */
void cf_append(char *dst, size_t n, const char *sep, const char *s);

/* Frame-time statistics of one measured run (cf_stats.c). */
typedef struct {
    int    n;
    double avg, min, max, p50, p95, p99;
    int    over_16_7, over_34, over_51;
} cf_frame_stats;

/* One CSV row. */
typedef struct {
    char      id[16];
    char      name[40];
    char      mode[8];          /* "-", "OFF", "LOCK30", "V1", "VERDICT" */
    gp_status status;
    int       ok;               /* functional result: data verified, no error, memory returned */
    char      verdict[16];      /* fit class, target result, or "-" */
    int       has_frames;
    cf_frame_stats fs;
    double    ms;               /* duration of the measured operation */
    double    mbytes;           /* MiB moved (I/O, loads) */
    double    mbps;             /* MiB/s */
    int64_t   vram_kib, glram_kib, heap_kib;  /* memory figures (see detail) */
    uint32_t  check;            /* deterministic value compared across iterations */
    char      detail[192];
} cf_result;

#endif /* CF_COMMON_H */
