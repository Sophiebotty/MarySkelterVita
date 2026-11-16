/*
 * cf_iowrap.c - keeps vitaGL's shader cache inside ux0:data/ContentFit/.
 *
 * The installed vitaGL creates its cache folders with sceIoMkdir (in
 * vglInitWithCustomSizes) and opens cache files with sceIoOpen (in
 * reload_ffp_shaders), always below the fixed path "ux0:data/shader_cache".
 * Its vglSetShaderCachePath is an empty function, so ContentFit is linked with
 *     -Wl,--wrap=sceIoOpen  -Wl,--wrap=sceIoMkdir
 * Every call to those two functions in the program (vitaGL, newlib and
 * ContentFit itself) then goes through the wrappers below:
 *   - a path at or below ux0:data/shader_cache is rewritten to the same path
 *     below ux0:data/ContentFit/shader_cache (cf_redirect_vgl_cache);
 *   - every other path is passed on unchanged;
 *   - a rewritten path that would not fit is refused with an error, so nothing
 *     is ever created at the default location.
 * Nothing in vitaGL or any other module is modified.
 *
 * SceUID and SceMode are both 'int' (psp2common/types.h), so this file needs
 * no Vita headers and the PC tests link the same file with --wrap.
 */
#include "cf_paths.h"
#include "cf_plat.h"

/* SCE_ERROR_ERRNO_ENAMETOOLONG: 0x80010000 | ENAMETOOLONG (91) */
#define CF_SCE_ENAMETOOLONG ((int)0x8001005B)
#define CF_WRAP_PATH_MAX 192

int __real_sceIoOpen(const char *file, int flags, int mode);
int __real_sceIoMkdir(const char *dir, int mode);
int __wrap_sceIoOpen(const char *file, int flags, int mode);
int __wrap_sceIoMkdir(const char *dir, int mode);

static int s_opens, s_mkdirs, s_refused;

int __wrap_sceIoOpen(const char *file, int flags, int mode)
{
    char p[CF_WRAP_PATH_MAX];
    int r = cf_redirect_vgl_cache(file, p, sizeof(p));
    if (r < 0) {
        s_refused++;
        return CF_SCE_ENAMETOOLONG;
    }
    if (r > 0) s_opens++;
    return __real_sceIoOpen(r > 0 ? p : file, flags, mode);
}

int __wrap_sceIoMkdir(const char *dir, int mode)
{
    char p[CF_WRAP_PATH_MAX];
    int r = cf_redirect_vgl_cache(dir, p, sizeof(p));
    if (r < 0) {
        s_refused++;
        return CF_SCE_ENAMETOOLONG;
    }
    if (r > 0) s_mkdirs++;
    return __real_sceIoMkdir(r > 0 ? p : dir, mode);
}

void cf_plat_redirect_counts(int *opens, int *mkdirs, int *refused)
{
    if (opens) *opens = s_opens;
    if (mkdirs) *mkdirs = s_mkdirs;
    if (refused) *refused = s_refused;
}
