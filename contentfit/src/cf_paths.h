/*
 * cf_paths.h - ContentFit's file allowlists and no-overwrite file creation.
 *
 * Writes: only below ux0:data/ContentFit/.
 * Reads:  only below app0:fixtures/ and ux0:data/ContentFit/work/.
 * Any path containing ".." is refused. Output files are created only after a
 * check that they do not exist; an existing file is never opened for writing.
 * Nothing is ever deleted.
 */
#ifndef CF_PATHS_H
#define CF_PATHS_H

#include <stdio.h>

#include "cf_common.h"

#define CF_RUN_MAX 999

typedef struct {
    char csv[64];
    char log[64];
    char frames[64];
    char progress[64];
} cf_run_files;

int cf_path_write_ok(const char *path);
int cf_path_read_ok(const char *path);

/* Fills the four output names of run 'run' (1..999). */
void cf_run_files_for(int run, cf_run_files *out);

/* exists(path, ctx) returns 1 if something exists at path. */
typedef int (*cf_exists_fn)(const char *path, void *ctx);

/* First run number 1..999 for which none of the four files exist; 0 if none. */
int cf_run_pick(cf_exists_fn exists, void *ctx);

/* exists() implementation using stat() (rp_file_size). */
int cf_exists_stat(const char *path, void *ctx);

/* Creates a new file for writing: CF_ERR_PATH outside the write allowlist,
 * CF_ERR_EXISTS if anything exists at path, RP_ERR_WRITE if fopen fails. */
gp_status cf_create_new(const char *path, FILE **fp);

/* vitaGL's shader cache. The installed vitaGL uses fixed paths below
 * CF_VGL_CACHE_DEFAULT (its vglSetShaderCachePath is an empty function), so
 * ContentFit redirects them at link time (cf_iowrap.c) into CF_VGL_CACHE_DIR. */
#define CF_VGL_CACHE_DEFAULT "ux0:data/shader_cache"
#define CF_VGL_CACHE_DIR     "ux0:data/ContentFit/shader_cache"

/* If 'path' is CF_VGL_CACHE_DEFAULT or lies below it, writes the same path
 * with that prefix replaced by CF_VGL_CACHE_DIR into out and returns 1.
 * Returns 0 for any other path (out untouched) and -1 if the rewritten path
 * does not fit in n bytes (the caller must then refuse the call). */
int cf_redirect_vgl_cache(const char *path, char *out, size_t n);

#endif /* CF_PATHS_H */
