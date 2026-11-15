/*
 * gp_common.c - status names, observation names, path allowlist.
 */
#include <string.h>

#include "gp_common.h"
#include "rp_io.h"

static const char *const GP_NAMES[GP_ERR_END - GP_ERR_BASE] = {
    "PATH", "IN_INDEX", "UNSORTED", "GAP", "TRAILING", "EMPTY",
    "SIZE_RULE", "CONST", "CSH_FIELD", "CSH_PAIR",
};

static const char *const OBS_NAMES[8] = {
    "GAP", "TRAILING", "UNSORTED", "EMPTY", "SIZE_RULE", "CONST", "CSH_FIELD", "CSH_PAIR",
};

const char *gp_status_name(gp_status s)
{
    if (s == GP_EXPECT_ANY)
        return "ANY";
    if (s >= GP_ERR_BASE && s < GP_ERR_END)
        return GP_NAMES[s - GP_ERR_BASE];
    return rp_err_name((rp_err)s);
}

gp_status gp_status_from_name(const char *name, int *ok)
{
    int i;
    *ok = 1;
    if (strcmp(name, "ANY") == 0)
        return GP_EXPECT_ANY;
    for (i = 0; i < (int)RP_ERR_COUNT_OF_CODES; i++)
        if (strcmp(name, rp_err_name((rp_err)i)) == 0)
            return i;
    for (i = GP_ERR_BASE; i < GP_ERR_END; i++)
        if (strcmp(name, GP_NAMES[i - GP_ERR_BASE]) == 0)
            return i;
    *ok = 0;
    return RP_ERR_ARG;
}

void gp_obs_names(uint32_t obs, char *buf, size_t n)
{
    size_t used = 0;
    int i;
    if (n == 0)
        return;
    buf[0] = '\0';
    if (obs == 0) {
        snprintf(buf, n, "-");
        return;
    }
    for (i = 0; i < 8; i++) {
        if (obs & (1u << i)) {
            int w = snprintf(buf + used, n - used, "%s%s", used ? "+" : "", OBS_NAMES[i]);
            if (w < 0 || (size_t)w >= n - used)
                return;
            used += (size_t)w;
        }
    }
}

gp_status gp_path_check(const char *path)
{
    if (!path)
        return RP_ERR_ARG;
    if (strstr(path, ".."))
        return GP_ERR_PATH;
    if (strncmp(path, GP_FIX_DIR_UX0, strlen(GP_FIX_DIR_UX0)) == 0 ||
        strncmp(path, GP_FIX_DIR_APP0, strlen(GP_FIX_DIR_APP0)) == 0)
        return RP_OK;
    return GP_ERR_PATH;
}

gp_status gp_open_checked(const char *path, FILE **fp, uint64_t *size)
{
    gp_status s = gp_path_check(path);
    if (s != RP_OK) {
        if (fp) *fp = NULL;
        return s;
    }
    return rp_open(path, fp, size);
}
