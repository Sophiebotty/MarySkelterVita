/*
 * fake_vgl.c - stand-in for vitaGL's two file-system call sites (tests only).
 *
 * Mirrors what the installed libvitaGL.a does (see CONTENT_FIT_BENCHMARK.md):
 *   vglInitWithCustomSizes: four sceIoMkdir calls for ux0:data/shader_cache,
 *     .../v%d, .../v%d/v and .../v%d/f
 *   reload_ffp_shaders: sceIoOpen on .../v%d/v/%08X-%d.gxp and
 *     .../v%d/f/%08X-%016llX.gxp (the format strings found in ffp.o)
 * It is a separate translation unit, so its calls are undefined references
 * that the linker's --wrap redirects, exactly as for the real library.
 */
#include <stdio.h>

int sceIoOpen(const char *file, int flags, int mode);
int sceIoMkdir(const char *dir, int mode);

#define CACHE_VERSION 3

void fake_vgl_init(void);
int fake_vgl_cache_open(int fragment, unsigned hash, unsigned long long key, int flags);
int fake_other_open(const char *path);
int fake_other_mkdir(const char *path);

void fake_vgl_init(void)
{
    char p[256];
    sceIoMkdir("ux0:data/shader_cache", 0777);
    snprintf(p, sizeof(p), "ux0:data/shader_cache/v%d", CACHE_VERSION);
    sceIoMkdir(p, 0777);
    snprintf(p, sizeof(p), "ux0:data/shader_cache/v%d/v", CACHE_VERSION);
    sceIoMkdir(p, 0777);
    snprintf(p, sizeof(p), "ux0:data/shader_cache/v%d/f", CACHE_VERSION);
    sceIoMkdir(p, 0777);
}

int fake_vgl_cache_open(int fragment, unsigned hash, unsigned long long key, int flags)
{
    char p[256];
    if (fragment)
        snprintf(p, sizeof(p), "ux0:data/shader_cache/v%d/f/%08X-%016llX.gxp", CACHE_VERSION, hash, key);
    else
        snprintf(p, sizeof(p), "ux0:data/shader_cache/v%d/v/%08X-%d.gxp", CACHE_VERSION, hash, (int)key);
    return sceIoOpen(p, flags, 0777);
}

int fake_other_open(const char *path) { return sceIoOpen(path, 1, 0); }
int fake_other_mkdir(const char *path) { return sceIoMkdir(path, 0777); }
