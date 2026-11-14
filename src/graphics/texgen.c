/*
 * texgen.c - procedural texture generation (original designs).
 *
 * All patterns are tileable at 128x128 so GL_REPEAT shows no seams.
 * A single static scratch buffer is reused for every texture; vitaGL copies
 * the pixels into GPU memory in glTexImage2D, so no heap allocation is needed.
 */
#include <math.h>

#include "engine/vmath.h"
#include "graphics/texgen.h"

#define N TEXGEN_SIZE

static unsigned char  s_pixels[N * N * 4];
static unsigned short s_pixels565[N * N];

/* ---- deterministic integer hash noise ---- */

static unsigned hash2(unsigned x, unsigned y, unsigned seed)
{
    unsigned h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

/* 0..1 */
static float noise1(unsigned x, unsigned y, unsigned seed)
{
    return (float)(hash2(x, y, seed) & 0xFFFF) / 65535.0f;
}

/* Smooth value noise on a wrapping lattice of 'cells' per texture side. */
static float value_noise(int x, int y, int cells, unsigned seed)
{
    float fx = (float)x * cells / N, fy = (float)y * cells / N;
    int x0 = (int)fx, y0 = (int)fy;
    float tx = fx - x0, ty = fy - y0;
    int x1 = (x0 + 1) % cells, y1 = (y0 + 1) % cells;
    float a = noise1(x0 % cells, y0 % cells, seed), b = noise1(x1, y0 % cells, seed);
    float c = noise1(x0 % cells, y1, seed),         d = noise1(x1, y1, seed);
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

static void put(int x, int y, float r, float g, float b)
{
    unsigned char *p = &s_pixels[(y * N + x) * 4];
    p[0] = (unsigned char)(vm_clampf(r, 0.0f, 1.0f) * 255.0f);
    p[1] = (unsigned char)(vm_clampf(g, 0.0f, 1.0f) * 255.0f);
    p[2] = (unsigned char)(vm_clampf(b, 0.0f, 1.0f) * 255.0f);
    p[3] = 255;
}

/* ---- patterns ---- */

/* Running-bond stone bricks: 4 courses of 32 px, bricks 64 px wide. */
static void gen_wall(void)
{
    const int course_h = 32, brick_w = 64, mortar = 3;
    int x, y;
    for (y = 0; y < N; y++) {
        int course = y / course_h;
        int ly = y % course_h;
        int shift = (course & 1) ? brick_w / 2 : 0;
        for (x = 0; x < N; x++) {
            int bx = ((x + shift) % N) / brick_w;
            int lx = (x + shift) % brick_w;
            float grain = value_noise(x, y, 16, 11u) * 0.12f + noise1(x, y, 12u) * 0.06f;
            if (ly < mortar || lx < mortar) {
                float m = 0.22f + grain * 0.5f;
                put(x, y, m, m * 0.97f, m * 0.92f);
            } else {
                float tint = noise1(bx, course, 13u) * 0.18f;
                float edge = (ly < mortar + 2 || lx < mortar + 2) ? -0.06f : 0.0f;
                float v = 0.58f + tint + grain + edge;
                put(x, y, v, v * 0.94f, v * 0.86f);
            }
        }
    }
}

/* 2x2 square flagstones with bevelled edges. */
static void gen_floor(void)
{
    const int tile = 64, gap = 2, bevel = 4;
    int x, y;
    for (y = 0; y < N; y++) {
        for (x = 0; x < N; x++) {
            int lx = x % tile, ly = y % tile;
            int tx = x / tile, ty = y / tile;
            float grain = value_noise(x, y, 8, 21u) * 0.16f + noise1(x, y, 22u) * 0.05f;
            float v;
            if (lx < gap || ly < gap) {
                v = 0.12f;
            } else {
                int dmin = lx - gap;
                if (ly - gap < dmin) dmin = ly - gap;
                if (tile - 1 - lx < dmin) dmin = tile - 1 - lx;
                if (tile - 1 - ly < dmin) dmin = tile - 1 - ly;
                v = 0.42f + noise1(tx, ty, 23u) * 0.10f + grain;
                if (dmin < bevel)
                    v -= (bevel - dmin) * 0.03f;
            }
            put(x, y, v * 1.02f, v * 0.96f, v * 0.90f);
        }
    }
}

/* Wooden planks running along V, 32 px wide, with grain and dark seams. */
static void gen_ceiling(void)
{
    const int plank_w = 32, seam = 2;
    int x, y;
    for (y = 0; y < N; y++) {
        for (x = 0; x < N; x++) {
            int p = x / plank_w, lx = x % plank_w;
            float rings = sinf((float)x * 0.9f + value_noise(x, y, 4, 31u + p) * 6.0f) * 0.5f + 0.5f;
            float v = 0.30f + noise1(p, 0, 32u) * 0.08f + rings * 0.10f + noise1(x, y, 33u) * 0.04f;
            if (lx < seam)
                v = 0.08f;
            put(x, y, v * 1.10f, v * 0.82f, v * 0.58f);
        }
    }
}

/* ---- GL upload ---- */

/* Packs s_pixels (RGBA8) into s_pixels565 (R5 G6 B5, alpha dropped). */
static void convert_to_565(void)
{
    int i;
    for (i = 0; i < N * N; i++) {
        const unsigned char *p = &s_pixels[i * 4];
        s_pixels565[i] = (unsigned short)(((p[0] >> 3) << 11) |
                                          ((p[1] >> 2) << 5) |
                                           (p[2] >> 3));
    }
}

static void upload(GLuint tex, int use_565)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    if (use_565) {
        convert_to_565();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, N, N, 0,
                     GL_RGB, GL_UNSIGNED_SHORT_5_6_5, s_pixels565);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, N, N, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, s_pixels);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);
}

int texgen_create_all(GLuint out[TEX_COUNT], int use_565, int trilinear)
{
    while (glGetError() != GL_NO_ERROR) { /* clear stale errors */ }

    glGenTextures(TEX_COUNT, out);

    gen_wall();    upload(out[TEX_WALL], use_565);
    gen_floor();   upload(out[TEX_FLOOR], use_565);
    gen_ceiling(); upload(out[TEX_CEILING], use_565);

    texgen_set_filter(out, trilinear);
    glBindTexture(GL_TEXTURE_2D, 0);

    return glGetError() == GL_NO_ERROR ? 0 : -1;
}

void texgen_set_filter(const GLuint tex[TEX_COUNT], int trilinear)
{
    int i;
    for (i = 0; i < TEX_COUNT; i++) {
        glBindTexture(GL_TEXTURE_2D, tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        trilinear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                        trilinear ? GL_LINEAR : GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void texgen_delete_all(GLuint tex[TEX_COUNT])
{
    glDeleteTextures(TEX_COUNT, tex);
}

unsigned texgen_bytes_per_texture(int use_565)
{
    /* base level + ~1/3 for the mip chain */
    return (unsigned)(N * N * (use_565 ? 2 : 4)) * 4u / 3u;
}
