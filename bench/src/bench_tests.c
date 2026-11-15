/*
 * bench_tests.c - benchmark scenes.
 *
 * Coordinate system: orthographic, top-left origin, 960x544 pixels, z = 0.
 * Depth test is on (GL_LEQUAL) for every scene, as a real 3D game would use.
 * Face culling is off (the Y-flipped ortho reverses winding).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>

#include "bench_tests.h"
#include "graphics/texgen.h"

#define SCREEN_W 960
#define SCREEN_H 544

/* ---- grid mesh: 128 x 128 quads = 32768 triangles, 16641 vertices ---- */
#define GRID        128
#define CHUNK_TRIS  (GRID * GRID * 2)
#define CHUNK_VERTS ((GRID + 1) * (GRID + 1))
#define CHUNK_IDX   (CHUNK_TRIS * 3)
#define GRID_PX     256.0f   /* small on-screen area: vertex-bound, not fill-bound */

typedef struct {
    float x, y, z;
    float u, v;
    unsigned char r, g, b, a;
} BV;

_Static_assert(sizeof(BV) == 24, "BV must be 24 bytes");
_Static_assert(CHUNK_VERTS <= 65535, "GL_UNSIGNED_SHORT indices");

static BV       s_grid_v[CHUNK_VERTS];
static GLushort s_grid_i[CHUNK_IDX];
static GLuint   s_vbo = 0, s_ibo = 0;
static int      s_vbo_ok = 0;

/* full-screen quad (client arrays) */
static BV       s_quad_v[4];
static const GLushort s_quad_i[6] = {0, 1, 2, 0, 2, 3};

static GLuint s_tex8[TEX_COUNT];
static GLuint s_tex565[TEX_COUNT];
static unsigned long s_tex8_vram_kb = 0, s_tex565_vram_kb = 0;
static int s_tex8_ok = 0, s_tex565_ok = 0;

/* ---------------------------------------------------------------------- */
/* Test table                                                              */
/* ---------------------------------------------------------------------- */
/*  id  test       variant              kind         tris    draws lay tex bl fog vbo 565 */
static const BenchTestDef TESTS[] = {
    { 1, "EMPTY",  "CLEAR SWAP",        KIND_EMPTY,       0,    0, 0, 0, 0, 0, 1, 0},
    { 2, "TRIS",   "10K FLAT",          KIND_TRIS,    10000,    0, 0, 0, 0, 0, 1, 0},
    { 3, "TRIS",   "50K FLAT",          KIND_TRIS,    50000,    0, 0, 0, 0, 0, 1, 0},
    { 4, "TRIS",   "100K FLAT",         KIND_TRIS,   100000,    0, 0, 0, 0, 0, 1, 0},
    { 5, "TRIS",   "200K FLAT",         KIND_TRIS,   200000,    0, 0, 0, 0, 0, 1, 0},
    { 6, "TRIS",   "50K TEX",           KIND_TRIS,    50000,    0, 0, 1, 0, 0, 1, 0},
    { 7, "TRIS",   "200K TEX",          KIND_TRIS,   200000,    0, 0, 1, 0, 0, 1, 0},
    { 8, "DRAWS",  "10 X 5000",         KIND_DRAWS,   50000,   10, 0, 0, 0, 0, 1, 0},
    { 9, "DRAWS",  "100 X 500",         KIND_DRAWS,   50000,  100, 0, 0, 0, 0, 1, 0},
    {10, "DRAWS",  "500 X 100",         KIND_DRAWS,   50000,  500, 0, 0, 0, 0, 1, 0},
    {11, "DRAWS",  "1000 X 50",         KIND_DRAWS,   50000, 1000, 0, 0, 0, 0, 1, 0},
    {12, "FILL",   "1 LAYER",           KIND_FILL,        0,    0, 1, 1, 0, 0, 0, 0},
    {13, "FILL",   "2 LAYERS",          KIND_FILL,        0,    0, 2, 1, 0, 0, 0, 0},
    {14, "FILL",   "4 LAYERS",          KIND_FILL,        0,    0, 4, 1, 0, 0, 0, 0},
    {15, "FILL",   "8 LAYERS",          KIND_FILL,        0,    0, 8, 1, 0, 0, 0, 0},
    {16, "BLEND",  "4 LAYERS",          KIND_FILL,        0,    0, 4, 1, 1, 0, 0, 0},
    {17, "BLEND",  "8 LAYERS",          KIND_FILL,        0,    0, 8, 1, 1, 0, 0, 0},
    {18, "PATH",   "CLIENT 50K FLAT",   KIND_TRIS,    50000,    0, 0, 0, 0, 0, 0, 0},
    {19, "PATH",   "VBO 50K FLAT",      KIND_TRIS,    50000,    0, 0, 0, 0, 0, 1, 0},
    {20, "TEX565", "FILL 4 LAYERS",     KIND_FILL,        0,    0, 4, 1, 0, 0, 0, 1},
    {21, "TEX565", "FILL 8 LAYERS",     KIND_FILL,        0,    0, 8, 1, 0, 0, 0, 1},
    {22, "TEX565", "200K TEX",          KIND_TRIS,   200000,    0, 0, 1, 0, 0, 1, 1},
    {23, "FOG",    "FILL 4 LAYERS",     KIND_FILL,        0,    0, 4, 1, 0, 1, 0, 0},
    {24, "FOG",    "200K TEX",          KIND_TRIS,   200000,    0, 0, 1, 0, 1, 1, 0},
    {25, "TEXMEM", "ALLOC 1MB STEPS",   KIND_TEXMEM,      0,    0, 0, 0, 0, 0, 0, 0},
};

int bench_test_count(void) { return (int)(sizeof(TESTS) / sizeof(TESTS[0])); }
const BenchTestDef *bench_test_get(int i) { return &TESTS[i]; }

/* ---------------------------------------------------------------------- */
/* Resources                                                               */
/* ---------------------------------------------------------------------- */
static unsigned long free_kb(vglMemType t) { return (unsigned long)(vglMemFree(t) / 1024); }

static void build_grid(void)
{
    const float x0 = (SCREEN_W - GRID_PX) * 0.5f;
    const float y0 = (SCREEN_H - GRID_PX) * 0.5f;
    const float step = GRID_PX / GRID;
    int gx, gy, n = 0;

    for (gy = 0; gy <= GRID; gy++) {
        for (gx = 0; gx <= GRID; gx++) {
            BV *v = &s_grid_v[gy * (GRID + 1) + gx];
            v->x = x0 + gx * step;
            v->y = y0 + gy * step;
            v->z = 0.0f;
            v->u = (float)gx / 32.0f;   /* 4 texture repeats across the grid */
            v->v = (float)gy / 32.0f;
            v->r = (unsigned char)(120 + (gx & 63));
            v->g = (unsigned char)(140 + (gy & 63));
            v->b = 200;
            v->a = 255;
        }
    }
    for (gy = 0; gy < GRID; gy++) {
        for (gx = 0; gx < GRID; gx++) {
            GLushort a = (GLushort)(gy * (GRID + 1) + gx);
            GLushort b = (GLushort)(a + 1);
            GLushort c = (GLushort)(a + GRID + 1);
            GLushort d = (GLushort)(c + 1);
            s_grid_i[n++] = a; s_grid_i[n++] = c; s_grid_i[n++] = b;
            s_grid_i[n++] = b; s_grid_i[n++] = c; s_grid_i[n++] = d;
        }
    }
}

static void build_quad(void)
{
    /* 1 texel per pixel with 128x128 textures: 960/128 = 7.5, 544/128 = 4.25 */
    const BV q[4] = {
        {0.0f,            0.0f,            0.0f, 0.0f, 0.0f,  255, 255, 255, 128},
        {(float)SCREEN_W, 0.0f,            0.0f, 7.5f, 0.0f,  255, 255, 255, 128},
        {(float)SCREEN_W, (float)SCREEN_H, 0.0f, 7.5f, 4.25f, 255, 255, 255, 128},
        {0.0f,            (float)SCREEN_H, 0.0f, 0.0f, 4.25f, 255, 255, 255, 128},
    };
    memcpy(s_quad_v, q, sizeof(q));
}

int bench_resources_init(char *note, size_t note_len)
{
    unsigned long before;

    build_grid();
    build_quad();

    /* VBO + IBO for the grid */
    while (glGetError() != GL_NO_ERROR) { }
    glGenBuffers(1, &s_vbo);
    glGenBuffers(1, &s_ibo);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(s_grid_v), s_grid_v, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(s_grid_i), s_grid_i, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    s_vbo_ok = (glGetError() == GL_NO_ERROR);

    /* Texture sets; record VRAM cost of each (measured by vglMemFree delta). */
    before = free_kb(VGL_MEM_VRAM);
    s_tex8_ok = (texgen_create_all(s_tex8, 0, 1) == 0);
    s_tex8_vram_kb = before - free_kb(VGL_MEM_VRAM);

    before = free_kb(VGL_MEM_VRAM);
    s_tex565_ok = (texgen_create_all(s_tex565, 1, 1) == 0);
    s_tex565_vram_kb = before - free_kb(VGL_MEM_VRAM);

    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    /* Fog parameters (enabled only by FOG tests). */
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 0.0f);
    glFogf(GL_FOG_END, 1.0f);

    snprintf(note, note_len, "VBO %s  RGBA8 SET %s %luK  RGB565 SET %s %luK",
             s_vbo_ok ? "OK" : "FAILED",
             s_tex8_ok ? "OK" : "ERROR", s_tex8_vram_kb,
             s_tex565_ok ? "OK" : "ERROR", s_tex565_vram_kb);

    return (s_tex8_ok && s_tex565_ok) ? 0 : -1;
}

void bench_resources_free(void)
{
    texgen_delete_all(s_tex8);
    texgen_delete_all(s_tex565);
    if (s_vbo) glDeleteBuffers(1, &s_vbo);
    if (s_ibo) glDeleteBuffers(1, &s_ibo);
    s_vbo = s_ibo = 0;
}

/* ---------------------------------------------------------------------- */
/* Drawing                                                                 */
/* ---------------------------------------------------------------------- */
static void set_ortho(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, SCREEN_W, SCREEN_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void begin_state(const BenchTestDef *t, GLuint tex)
{
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);

    if (t->textured) {
        glEnable(GL_TEXTURE_2D);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glBindTexture(GL_TEXTURE_2D, tex);
    }
    if (t->blend) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    if (t->fog)
        glEnable(GL_FOG);
}

static void end_state(const BenchTestDef *t)
{
    if (t->textured) {
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glDisable(GL_BLEND);
    glDisable(GL_FOG);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

/* Binds grid vertex pointers for VBO (offsets) or client arrays (addresses). */
static int bind_grid(int want_vbo, int textured)
{
    const int vbo = want_vbo && s_vbo_ok;
    const char *base = vbo ? (const char *)0 : (const char *)s_grid_v;
    const GLsizei stride = (GLsizei)sizeof(BV);

    if (vbo) {
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ibo);
    }
    glVertexPointer(3, GL_FLOAT, stride, base + offsetof(BV, x));
    glColorPointer(4, GL_UNSIGNED_BYTE, stride, base + offsetof(BV, r));
    if (textured)
        glTexCoordPointer(2, GL_FLOAT, stride, base + offsetof(BV, u));
    return vbo;
}

/* Draws 'tris' triangles starting at triangle 'first' of the grid. */
static void draw_grid_range(int vbo, int first, int tris)
{
    const void *idx = vbo ? (const void *)(uintptr_t)(first * 3 * sizeof(GLushort))
                          : (const void *)&s_grid_i[first * 3];
    glDrawElements(GL_TRIANGLES, tris * 3, GL_UNSIGNED_SHORT, idx);
}

static void draw_tris(const BenchTestDef *t)
{
    const GLuint *set = t->tex565 ? s_tex565 : s_tex8;
    int vbo, left = t->triangles;

    begin_state(t, set[TEX_WALL]);
    vbo = bind_grid(t->use_vbo, t->textured);
    while (left > 0) {
        int n = left < CHUNK_TRIS ? left : CHUNK_TRIS;
        draw_grid_range(vbo, 0, n);
        left -= n;
    }
    end_state(t);
}

static void draw_draws(const BenchTestDef *t)
{
    const int per = t->triangles / t->draws;
    int vbo, i;

    begin_state(t, s_tex8[TEX_WALL]);
    vbo = bind_grid(t->use_vbo, t->textured);
    for (i = 0; i < t->draws; i++) {
        int n = (i == t->draws - 1) ? t->triangles - per * (t->draws - 1) : per;
        int first = (i * per) % (CHUNK_TRIS - n + 1);
        draw_grid_range(vbo, first, n);
    }
    end_state(t);
}

static void draw_fill(const BenchTestDef *t)
{
    const GLuint *set = t->tex565 ? s_tex565 : s_tex8;
    const GLsizei stride = (GLsizei)sizeof(BV);
    int l;

    begin_state(t, set[TEX_FLOOR]);
    glVertexPointer(3, GL_FLOAT, stride, &s_quad_v[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, stride, &s_quad_v[0].r);
    if (t->textured)
        glTexCoordPointer(2, GL_FLOAT, stride, &s_quad_v[0].u);
    for (l = 0; l < t->layers; l++)
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, s_quad_i);
    end_state(t);
}

void bench_draw(const BenchTestDef *t)
{
    set_ortho();
    switch (t->kind) {
    case KIND_TRIS:  draw_tris(t);  break;
    case KIND_DRAWS: draw_draws(t); break;
    case KIND_FILL:  draw_fill(t);  break;
    default: break;
    }
}

int bench_draw_calls(const BenchTestDef *t)
{
    switch (t->kind) {
    case KIND_TRIS:  return (t->triangles + CHUNK_TRIS - 1) / CHUNK_TRIS;
    case KIND_DRAWS: return t->draws;
    case KIND_FILL:  return t->layers;
    default:         return 0;
    }
}

int bench_triangles(const BenchTestDef *t)
{
    switch (t->kind) {
    case KIND_TRIS:
    case KIND_DRAWS: return t->triangles;
    case KIND_FILL:  return t->layers * 2;
    default:         return 0;
    }
}

void bench_test_note(const BenchTestDef *t, char *out, size_t len)
{
    const char *path = (t->kind == KIND_TRIS || t->kind == KIND_DRAWS)
                           ? ((t->use_vbo && s_vbo_ok) ? "VBO" : "CLIENT")
                           : (t->kind == KIND_FILL ? "CLIENT" : "-");
    snprintf(out, len, "path=%s tex=%s blend=%d fog=%d%s",
             path,
             !t->textured ? "none" : (t->tex565 ? "RGB565" : "RGBA8"),
             t->blend, t->fog,
             (t->use_vbo && !s_vbo_ok) ? " VBO_UNAVAILABLE" : "");
}

/* ---------------------------------------------------------------------- */
/* Texture-memory probe                                                    */
/* ---------------------------------------------------------------------- */
#define TEXMEM_DIM        512                      /* 512*512*4 = 1 MB */
#define TEXMEM_MAX        48                       /* safety cap: 48 MB */
#define TEXMEM_MARGIN_KB  (16UL * 1024UL)          /* keep 16 MB free */

void bench_texmem_run(BenchResult *r)
{
    GLuint ids[TEXMEM_MAX];
    unsigned char *px;
    int count = 0, i;
    const char *stop = "cap reached";
    GLenum err = GL_NO_ERROR;
    unsigned long ram_after_del, vram_after_del;

    r->ram_before_kb  = free_kb(VGL_MEM_RAM);
    r->vram_before_kb = free_kb(VGL_MEM_VRAM);

    px = (unsigned char *)malloc(TEXMEM_DIM * TEXMEM_DIM * 4);
    if (!px) {
        snprintf(r->note, sizeof(r->note), "malloc failed; probe skipped");
        r->ram_after_kb = r->ram_before_kb;
        r->vram_after_kb = r->vram_before_kb;
        return;
    }
    for (i = 0; i < TEXMEM_DIM * TEXMEM_DIM * 4; i++)
        px[i] = (unsigned char)(i * 7);

    while (glGetError() != GL_NO_ERROR) { }
    while (count < TEXMEM_MAX) {
        if (free_kb(VGL_MEM_ALL) < TEXMEM_MARGIN_KB + 1024UL) {
            stop = "margin";
            break;
        }
        glGenTextures(1, &ids[count]);
        glBindTexture(GL_TEXTURE_2D, ids[count]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEXMEM_DIM, TEXMEM_DIM, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, px);
        count++;
        err = glGetError();
        if (err != GL_NO_ERROR) {
            stop = "gl error";
            break;
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    r->ram_after_kb  = free_kb(VGL_MEM_RAM);
    r->vram_after_kb = free_kb(VGL_MEM_VRAM);

    glFinish();
    glDeleteTextures(count, ids);
    free(px);
    glFinish();
    ram_after_del  = free_kb(VGL_MEM_RAM);
    vram_after_del = free_kb(VGL_MEM_VRAM);

    snprintf(r->note, sizeof(r->note),
             "allocated %d x 1024K RGBA8; stop=%s err=0x%X; after delete RAM %luK VRAM %luK",
             (err != GL_NO_ERROR) ? count - 1 : count, stop, (unsigned)err,
             ram_after_del, vram_after_del);
}
