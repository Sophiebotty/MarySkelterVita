/*
 * test_cf_scenes.c - scene check on the PC with the stand-in GL.
 *
 * For every scene tier: creates it, draws several frames with full triangle
 * analysis, and compares what was submitted with the Stage 4 design tables
 * (draw calls, triangles, texture binds, blended screen area), checks that the
 * dungeon geometry faces the camera, that the VRAM estimate equals what was
 * uploaded, that the heap estimate covers the real peak, that content CRCs
 * repeat, and that everything is released afterwards. Also checks a rejected
 * compressed format. Prints the content CRC table for comparison with the Vita.
 */
#include <stdio.h>
#include <string.h>
#include <vitaGL.h>

#include "cf_scene.h"
#include "rp_mem.h"

static int g_fail, g_checks;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct { const char *id; long draws, tris, binds; double blended_px; } expect;

/* Values derived by hand from the design tables (see cf_scene.h). */
static const expect EXP[] = {
    {"C3-P", 0, 0, 0, 0.0},
    {"C3-D-S", 20, 10000, 4, 0.0},
    {"C3-D-M", 60, 30000, 8, 0.0},
    {"C3-D-L", 150, 60000, 16, 0.0},
    {"C3-D-XL", 300, 100800, 32, 0.0},
    {"C3-B-S", 25, 12008, 9, 131072.0},
    {"C3-B-M", 28, 25208, 12, 156672.0},
    {"C3-B-L", 31, 50610, 14, 730112.0},
    {"C3-B-XL", 31, 71210, 14, 806912.0},
    {"C3-V-S", 3, 6, 3, 285472.0},
    {"C3-V-M", 4, 8, 4, 442144.0},
    {"C3-V-L", 5, 10, 5, 598816.0},
    {"C3-V-XL", 6, 12, 6, 755488.0},
    {"C3-U-S", 2, 1002, 1, 72000.0},
    {"C3-U-M", 3, 3002, 1, 216000.0},
    {"C3-U-L", 4, 6002, 1, 432000.0},
    {"C3-U-XL", 7, 12002, 1, 864000.0},
    {"C3-X", 31, 25214, 15, 598816.0},
    {"C3-T-RGBA8", 60, 30000, 8, 0.0},
    {"C3-T-RGB565", 60, 30000, 8, 0.0},
    {"C3-T-DXT1", 60, 30000, 8, 0.0},
    {"C3-T-DXT5", 60, 30000, 8, 0.0},
    {"C3-T-ETC1", 60, 30000, 8, 0.0},
    {"C3-T-PVRTC4", 60, 30000, 8, 0.0},
};

static double absd(double v) { return v < 0 ? -v : v; }

int main(void)
{
    static const uint32_t FRAMES[] = {0, 37, 60, 119, 240, 1234};
    unsigned i, f;
    stub_counts c0, c;
    char why[160];

    stub_set_analysis(1);
    glEnableClientState(GL_VERTEX_ARRAY);          /* as cf_main.c and cf_suite_run do */
    glEnableClientState(GL_COLOR_ARRAY);
    CHECK((int)(sizeof(EXP) / sizeof(EXP[0])) == cf_scene_count(), "every scene has an expectation (%d)",
          cf_scene_count());
    printf("%-12s %6s %8s %6s %8s %10s %10s %10s  %s\n", "scene", "draws", "tris", "binds", "blend", "VRAM_KiB",
           "heapNeed", "heapPeak", "content CRC");
    for (i = 0; i < sizeof(EXP) / sizeof(EXP[0]); i++) {
        const cf_scene_def *d = cf_scene_find(EXP[i].id);
        cf_need need;
        uint32_t crc1, crc2;
        size_t peak;
        gp_status st;
        if (!d) { CHECK(0, "scene %s missing", EXP[i].id); continue; }
        cf_scene_need(d, &need);
        stub_get_counts(&c0);
        rp_mem_reset_peak();
        st = cf_scene_create(d, why, sizeof(why));
        peak = rp_mem_get().peak;
        CHECK(st == RP_OK, "%s create: %d %s", d->id, st, why);
        if (st != RP_OK) continue;
        crc1 = cf_scene_crc();
        stub_get_counts(&c);
        CHECK(c.tex_bytes + c.buf_bytes == need.b[CF_POOL_VRAM], "%s VRAM estimate %llu vs uploaded %llu", d->id,
              (unsigned long long)need.b[CF_POOL_VRAM], (unsigned long long)(c.tex_bytes + c.buf_bytes));
        CHECK(c.tex_bytes == cf_scene_texture_bytes(d), "%s texture bytes", d->id);
        CHECK(peak <= need.b[CF_POOL_HEAP], "%s heap estimate %llu below peak %lu", d->id,
              (unsigned long long)need.b[CF_POOL_HEAP], (unsigned long)peak);
        CHECK(c.gl_errors_raised == c0.gl_errors_raised, "%s raised GL errors", d->id);
        for (f = 0; f < sizeof(FRAMES) / sizeof(FRAMES[0]); f++) {
            stub_get_counts(&c0);
            stub_reset_counts();
            cf_scene_draw(FRAMES[f]);
            stub_get_counts(&c);
            CHECK(c.gl_errors_raised == c0.gl_errors_raised, "%s frame %u made invalid GL calls", d->id,
                  (unsigned)FRAMES[f]);
            CHECK(c.draws == EXP[i].draws && c.triangles == EXP[i].tris && c.binds == EXP[i].binds,
                  "%s frame %u: draws %ld/%ld tris %ld/%ld binds %ld/%ld", d->id, (unsigned)FRAMES[f], c.draws,
                  EXP[i].draws, c.triangles, EXP[i].tris, c.binds, EXP[i].binds);
            CHECK(absd(c.blended_px - EXP[i].blended_px) <= 1.0, "%s frame %u blended %.1f px, expected %.1f",
                  d->id, (unsigned)FRAMES[f], c.blended_px, EXP[i].blended_px);
            if (d->kind == CF_SC_DUNGEON)
                CHECK(c.backfacing == 0, "%s frame %u: %ld dungeon triangles face away", d->id,
                      (unsigned)FRAMES[f], c.backfacing);
            if (d->kind == CF_SC_BATTLE || d->kind == CF_SC_COMBINED)
                CHECK(c.backfacing > 0 && c.backfacing < (long)(d->enemies * d->ecols * d->erows * 2),
                      "%s frame %u: back-facing %ld only on the enemies' far sides", d->id, (unsigned)FRAMES[f],
                      c.backfacing);
            if (f == 0 && i > 0)
                printf("%-12s %6ld %8ld %6ld %8.3f %10llu %10llu %10lu  %08x%s\n", d->id, c.draws, c.triangles,
                       c.binds, c.blended_px / (960.0 * 544.0),
                       (unsigned long long)need.b[CF_POOL_VRAM] / 1024ull,
                       (unsigned long long)need.b[CF_POOL_HEAP] / 1024ull, (unsigned long)peak / 1024ul,
                       (unsigned)crc1, c.behind ? " (some triangles behind the camera)" : "");
        }
        cf_scene_destroy();
        stub_get_counts(&c);
        CHECK(c.tex_live == 0 && c.tex_bytes == 0 && c.buf_bytes == 0, "%s released (tex %ld, %llu bytes)", d->id,
              c.tex_live, (unsigned long long)(c.tex_bytes + c.buf_bytes));
        CHECK(rp_mem_get().current == 0, "%s left %lu tracked bytes", d->id, (unsigned long)rp_mem_get().current);
        st = cf_scene_create(d, why, sizeof(why));
        crc2 = cf_scene_crc();
        cf_scene_destroy();
        CHECK(st == RP_OK && crc1 == crc2, "%s content CRC repeats (%08x %08x)", d->id, (unsigned)crc1,
              (unsigned)crc2);
    }

    /* a compressed format the GPU rejects: reported, nothing left behind */
    stub_reject_format(GL_ETC1_RGB8_OES);
    {
        gp_status st = cf_scene_create(cf_scene_find("C3-T-ETC1"), why, sizeof(why));
        stub_get_counts(&c);
        CHECK(st == CF_ERR_UNSUPPORTED && strstr(why, "ETC1"), "rejected format reported: %d %s", st, why);
        CHECK(c.tex_live == 0 && c.buf_bytes == 0 && rp_mem_get().current == 0, "rejected format cleaned up");
        st = cf_scene_create(cf_scene_find("C3-T-DXT1"), why, sizeof(why));
        CHECK(st == RP_OK, "other formats unaffected");
        cf_scene_destroy();
    }
    printf("scene checks: %d, failures: %d\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
