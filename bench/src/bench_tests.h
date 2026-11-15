/*
 * bench_tests.h - benchmark test table and scene resources.
 *
 * All content is original and synthetic: a procedural triangle grid, a
 * full-screen quad and the project's procedural textures (texgen).
 * No immediate-mode GL is used (SDL2 initialises vitaGL with legacy pool 0).
 */
#ifndef BENCH_TESTS_H
#define BENCH_TESTS_H

#include <stddef.h>

#include "bench_report.h"

typedef enum {
    KIND_EMPTY = 0,   /* clear + swap only */
    KIND_TRIS,        /* N triangles from the grid mesh */
    KIND_DRAWS,       /* fixed triangles split into D draw calls */
    KIND_FILL,        /* L full-screen quads (overdraw / fill rate) */
    KIND_TEXMEM       /* texture-memory allocation probe (not timed) */
} BenchKind;

typedef struct {
    int         id;
    const char *test;      /* uppercase: rendered with the 5x7 HUD font */
    const char *variant;
    BenchKind   kind;
    int         triangles; /* KIND_TRIS / KIND_DRAWS: triangles per frame */
    int         draws;     /* KIND_DRAWS: number of draw calls */
    int         layers;    /* KIND_FILL: full-screen layers */
    int         textured;
    int         blend;
    int         fog;
    int         use_vbo;   /* KIND_TRIS / KIND_DRAWS: 1 = VBO, 0 = client arrays */
    int         tex565;    /* textured: 1 = RGB565 set, 0 = RGBA8 set */
} BenchTestDef;

int                 bench_test_count(void);
const BenchTestDef *bench_test_get(int i);

/* Creates meshes, VBO/IBO and both texture sets. Writes a short status
 * (VRAM cost of each texture set, VBO status) into note. 0 = OK. */
int  bench_resources_init(char *note, size_t note_len);
void bench_resources_free(void);

/* Renders one frame of the test scene (no clear, no swap). Leaves GL state
 * reset: texture/blend/fog off, buffers unbound, vertex+color arrays on. */
void bench_draw(const BenchTestDef *t);

/* Draw calls issued per frame by bench_draw for this test. */
int  bench_draw_calls(const BenchTestDef *t);
/* Triangles submitted per frame. */
int  bench_triangles(const BenchTestDef *t);

/* Runs the texture-memory probe and fills r (frames = 0). */
void bench_texmem_run(BenchResult *r);

/* Static notes for a test (draw path, texture set cost, ...). */
void bench_test_note(const BenchTestDef *t, char *out, size_t len);

#endif /* BENCH_TESTS_H */
