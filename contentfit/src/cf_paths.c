/*
 * cf_paths.c - allowlists and no-overwrite file creation.
 */
#include <string.h>

#include "cf_paths.h"
#include "rp_io.h"

static int starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* A path must name something below the prefix, not the prefix itself. */
static int below(const char *path, const char *prefix)
{
    return starts_with(path, prefix) && path[strlen(prefix)] != '\0';
}

int cf_path_write_ok(const char *path)
{
    return path && !strstr(path, "..") && below(path, CF_OUT_PREFIX);
}

int cf_path_read_ok(const char *path)
{
    return path && !strstr(path, "..") && (below(path, CF_FIX_PREFIX) || below(path, CF_WORK_PREFIX));
}

void cf_run_files_for(int run, cf_run_files *out)
{
    snprintf(out->csv, sizeof(out->csv), "%s/run%03d.csv", CF_OUT_DIR, run);
    snprintf(out->log, sizeof(out->log), "%s/run%03d.log", CF_OUT_DIR, run);
    snprintf(out->frames, sizeof(out->frames), "%s/run%03d_frames.csv", CF_OUT_DIR, run);
    snprintf(out->progress, sizeof(out->progress), "%s/run%03d_progress.txt", CF_OUT_DIR, run);
}

int cf_run_pick(cf_exists_fn exists, void *ctx)
{
    int run;
    for (run = 1; run <= CF_RUN_MAX; run++) {
        cf_run_files f;
        cf_run_files_for(run, &f);
        if (!exists(f.csv, ctx) && !exists(f.log, ctx) && !exists(f.frames, ctx) && !exists(f.progress, ctx))
            return run;
    }
    return 0;
}

int cf_exists_stat(const char *path, void *ctx)
{
    uint64_t size;
    (void)ctx;
    return rp_file_size(path, &size) == RP_OK;
}

int cf_redirect_vgl_cache(const char *path, char *out, size_t n)
{
    const size_t plen = sizeof(CF_VGL_CACHE_DEFAULT) - 1, dlen = sizeof(CF_VGL_CACHE_DIR) - 1;
    size_t rest;
    if (!path || strncmp(path, CF_VGL_CACHE_DEFAULT, plen) != 0 || (path[plen] != '\0' && path[plen] != '/'))
        return 0;
    rest = strlen(path + plen);
    if (!out || dlen + rest + 1 > n)
        return -1;
    memcpy(out, CF_VGL_CACHE_DIR, dlen);
    memcpy(out + dlen, path + plen, rest + 1);
    return 1;
}

gp_status cf_create_new(const char *path, FILE **fp)
{
    if (fp) *fp = NULL;
    if (!fp || !cf_path_write_ok(path))
        return CF_ERR_PATH;
    if (cf_exists_stat(path, NULL))
        return CF_ERR_EXISTS;
    *fp = fopen(path, "wb");
    return *fp ? RP_OK : RP_ERR_WRITE;
}
