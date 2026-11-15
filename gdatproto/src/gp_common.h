/*
 * gp_common.h - status codes, observation flags and the path allowlist for
 * Vita GDAT Proto.
 *
 * gp_status values below GP_ERR_BASE are the unmodified Phase 8 rp_err codes
 * (rp_common.h); GP_ERR_* codes add the GDAT/BILZ/CSH-specific outcomes.
 *
 * Everything this prototype reads is synthetic (gdatproto/fixtures/). The
 * layouts follow observations in STEAM_ARCHIVE_STRUCTURE.md; passing tests
 * show agreement with that interpretation, NOT compatibility with real
 * Mary Skelter Finale archives.
 */
#ifndef GP_COMMON_H
#define GP_COMMON_H

#include <stdio.h>

#include "rp_common.h"

typedef int gp_status;

#define GP_EXPECT_ANY (-1)          /* manifest "ANY": any non-OK result */

enum {
    GP_ERR_BASE = 100,
    GP_ERR_PATH = GP_ERR_BASE,      /* path outside the fixture allowlist */
    GP_ERR_IN_INDEX,                /* entry data starts inside the index */
    GP_ERR_UNSORTED,                /* strict: offsets not ascending */
    GP_ERR_GAP,                     /* strict: bytes between entries / after index */
    GP_ERR_TRAILING,                /* strict: bytes after the last entry */
    GP_ERR_EMPTY,                   /* strict: zero entries */
    GP_ERR_SIZE_RULE,               /* strict: compressed size != region - 0x90 */
    GP_ERR_CONST,                   /* strict: BILZ third field != 7 */
    GP_ERR_CSH_FIELD,               /* strict: CSH header differs from observation */
    GP_ERR_CSH_PAIR,                /* strict: CSH size != 12 + 28 x paired count */
    GP_ERR_END
};

/* Observation flags (lenient mode records a deviation instead of failing).
 * Bit order matches OBS_BITS in tools/make_gdatproto_fixtures.py. */
enum {
    GP_OBS_GAP       = 1u << 0,
    GP_OBS_TRAILING  = 1u << 1,
    GP_OBS_UNSORTED  = 1u << 2,
    GP_OBS_EMPTY     = 1u << 3,
    GP_OBS_SIZE_RULE = 1u << 4,
    GP_OBS_CONST     = 1u << 5,
    GP_OBS_CSH_FIELD = 1u << 6,
    GP_OBS_CSH_PAIR  = 1u << 7
};

typedef enum { GP_STRICT = 0, GP_LENIENT = 1 } gp_mode;

/* Header sizes taken from the observations. */
#define GP_WRAP_HDR     0x80u       /* 128-byte entry header */
#define GP_WRAP_TOTAL   0x90u       /* header + 16-byte BILZ block */
#define GP_MAX_ENTRIES  RP_MAX_ENTRIES
#define GP_MAX_RAW      RP_MAX_ENTRY_RAW

/* The only directories this prototype may open files in. */
#define GP_FIX_DIR_UX0  "ux0:app/VDNG00004/fixtures/"
#define GP_FIX_DIR_APP0 "app0:fixtures/"

const char *gp_status_name(gp_status s);
gp_status   gp_status_from_name(const char *name, int *ok);
void        gp_obs_names(uint32_t obs, char *buf, size_t n);

/* GP_ERR_PATH unless 'path' starts with one of the fixture directories and
 * contains no "..". */
gp_status gp_path_check(const char *path);

/* gp_path_check + rp_open. */
gp_status gp_open_checked(const char *path, FILE **fp, uint64_t *size);

static inline uint32_t gp_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint32_t gp_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline void gp_wr_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

#endif /* GP_COMMON_H */
