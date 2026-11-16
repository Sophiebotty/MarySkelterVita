/*
 * test_cf_iowrap.c - link-level test of the shader-cache redirect (PC only).
 *
 * Linked as on the Vita, with -Wl,--wrap=sceIoOpen,--wrap=sceIoMkdir, the
 * unmodified contentfit/src/cf_iowrap.c and fake_vgl.c (vitaGL's two call
 * sites). The "real" sceIoOpen / sceIoMkdir below only record the path they
 * receive, standing in for the Vita kernel. Checks that every vitaGL cache
 * path arrives below ux0:data/ContentFit/shader_cache, that nothing ever
 * arrives at ux0:data/shader_cache, that other paths pass through unchanged,
 * and that an over-long rewritten path is refused without any call.
 */
#include <stdio.h>
#include <string.h>

#include "cf_paths.h"
#include "cf_plat.h"

void fake_vgl_init(void);
int fake_vgl_cache_open(int fragment, unsigned hash, unsigned long long key, int flags);
int fake_other_open(const char *path);
int fake_other_mkdir(const char *path);

#define MAXREC 16
static char g_seen[MAXREC][256];
static int g_nseen, g_bad_default, g_flags_seen;
static int g_fail, g_checks;

#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

/* At or below the default cache folder: the prefix followed by the end of the
 * path or '/'. ("ux0:data/shader_cachex" is a different folder.) */
static int in_default_cache(const char *p)
{
    size_t n = strlen(CF_VGL_CACHE_DEFAULT);
    return strncmp(p, CF_VGL_CACHE_DEFAULT, n) == 0 && (p[n] == '\0' || p[n] == '/');
}

static void record(const char *p)
{
    if (in_default_cache(p)) g_bad_default++;
    if (g_nseen < MAXREC) snprintf(g_seen[g_nseen], sizeof(g_seen[0]), "%s", p);
    g_nseen++;
}

/* stand-ins for the Vita kernel functions (what __real_ resolves to) */
int sceIoOpen(const char *file, int flags, int mode);
int sceIoMkdir(const char *dir, int mode);
int sceIoOpen(const char *file, int flags, int mode) { (void)mode; g_flags_seen = flags; record(file); return 7; }
int sceIoMkdir(const char *dir, int mode) { (void)mode; record(dir); return 0; }

int main(void)
{
    char longp[300];
    int opens, mkdirs, refused, r;

    fake_vgl_init();
    CHECK(g_nseen == 4, "four folder creations reached the kernel (%d)", g_nseen);
    CHECK(strcmp(g_seen[0], "ux0:data/ContentFit/shader_cache") == 0, "root: %s", g_seen[0]);
    CHECK(strcmp(g_seen[1], "ux0:data/ContentFit/shader_cache/v3") == 0, "version folder: %s", g_seen[1]);
    CHECK(strcmp(g_seen[2], "ux0:data/ContentFit/shader_cache/v3/v") == 0, "vertex folder: %s", g_seen[2]);
    CHECK(strcmp(g_seen[3], "ux0:data/ContentFit/shader_cache/v3/f") == 0, "fragment folder: %s", g_seen[3]);

    g_nseen = 0;
    r = fake_vgl_cache_open(1, 0xABCD1234u, 0x0123456789ABCDEFull, 0x0602);
    CHECK(r == 7 && g_nseen == 1 && g_flags_seen == 0x0602, "fragment cache open passed through with its flags");
    CHECK(strcmp(g_seen[0], "ux0:data/ContentFit/shader_cache/v3/f/ABCD1234-0123456789ABCDEF.gxp") == 0,
          "fragment file: %s", g_seen[0]);
    g_nseen = 0;
    fake_vgl_cache_open(0, 0x00000042u, 17, 1);
    CHECK(strcmp(g_seen[0], "ux0:data/ContentFit/shader_cache/v3/v/00000042-17.gxp") == 0, "vertex file: %s", g_seen[0]);
    CHECK(cf_path_write_ok(g_seen[0]), "redirected file is inside ContentFit's write allowlist");

    /* everything else is untouched */
    g_nseen = 0;
    fake_other_open("app0:fixtures/io16.bin");
    fake_other_open("ux0:data/ContentFit/run001.csv");
    fake_other_mkdir("ux0:data/ContentFit/work");
    fake_other_open("ux0:data/shader_cachex/a");
    CHECK(g_nseen == 4 && strcmp(g_seen[0], "app0:fixtures/io16.bin") == 0 &&
          strcmp(g_seen[1], "ux0:data/ContentFit/run001.csv") == 0 && strcmp(g_seen[2], "ux0:data/ContentFit/work") == 0 &&
          strcmp(g_seen[3], "ux0:data/shader_cachex/a") == 0, "other paths pass through unchanged");

    /* over-long cache path: refused, nothing reaches the kernel */
    memset(longp, 'a', sizeof(longp));
    memcpy(longp, "ux0:data/shader_cache/", 22);
    longp[sizeof(longp) - 1] = '\0';
    g_nseen = 0;
    r = fake_other_open(longp);
    CHECK(r == (int)0x8001005B && g_nseen == 0, "over-long cache file refused (%#x, %d calls)", (unsigned)r, g_nseen);
    r = fake_other_mkdir(longp);
    CHECK(r == (int)0x8001005B && g_nseen == 0, "over-long cache folder refused");

    cf_plat_redirect_counts(&opens, &mkdirs, &refused);
    CHECK(mkdirs == 4 && opens == 2 && refused == 2, "counters: %d folders, %d opens, %d refused", mkdirs, opens,
          refused);
    CHECK(g_bad_default == 0, "nothing reached the kernel at ux0:data/shader_cache (%d)", g_bad_default);
    printf("iowrap checks: %d, failures: %d\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
