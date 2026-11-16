/*
 * cf_common.c - status names and bounded text appending.
 */
#include <string.h>

#include "cf_common.h"

void cf_append(char *dst, size_t n, const char *sep, const char *s)
{
    const char *parts[2];
    size_t used;
    int k;
    if (!dst || n == 0 || !s || !s[0])
        return;
    parts[0] = sep ? sep : "";
    parts[1] = s;
    used = strlen(dst);
    for (k = 0; k < 2; k++) {
        const char *p = parts[k];
        while (*p && used + 1 < n)
            dst[used++] = *p++;
    }
    dst[used] = '\0';
}

static const char *const CF_NAMES[CF_ERR_END - CF_ERR_BASE] = {
    "PATH", "EXISTS", "SKIPPED", "UNKNOWN_SIZE", "MARGIN", "SPILL", "BASELINE",
    "GL", "UNSUPPORTED", "THREAD", "ABORTED", "PAYLOAD", "NO_SPACE",
};

const char *cf_status_name(gp_status s)
{
    if (s >= CF_ERR_BASE && s < CF_ERR_END)
        return CF_NAMES[s - CF_ERR_BASE];
    return gp_status_name(s);
}
