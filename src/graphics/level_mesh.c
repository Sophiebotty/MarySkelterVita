/*
 * level_mesh.c - builds and draws the textured room geometry.
 *
 * Winding rule used for every quad (a, b, c, d): triangles (a,b,c) and
 * (a,c,d), with (b-a) x (c-a) pointing INTO the room. Verified on hardware
 * in phase 1 with glFrontFace(GL_CCW) + glCullFace(GL_BACK).
 *
 * UVs are world-space based (1 texture repeat per ROOM_CELL_SIZE), so texel
 * density is identical on walls, floor and ceiling.
 */
#include <stddef.h>
#include <stdint.h>
#include <vitaGL.h>

#include "gameplay/room_map.h"
#include "graphics/level_mesh.h"

_Static_assert(sizeof(LevelVertex) == 24, "LevelVertex must be 24 bytes");

/* Worst case: every cell is floor with 4 walls + floor + ceiling. */
#define MAX_QUADS   (ROOM_W * ROOM_D * 6)
#define MAX_VERTS   (MAX_QUADS * 4)
#define MAX_INDICES (MAX_QUADS * 6)

_Static_assert(MAX_VERTS <= 65535, "indices are GL_UNSIGNED_SHORT");

static LevelVertex s_verts[MAX_VERTS];
static GLushort    s_indices[MAX_INDICES];
static int s_vcount = 0;
static int s_icount = 0;

/* Index range per material (TEX_WALL, TEX_FLOOR, TEX_CEILING). */
static struct { int first, count; } s_range[TEX_COUNT];

static GLuint s_vbo = 0, s_ibo = 0;

typedef struct { float x, y, z, u, v; } P5;

static int emit_quad(P5 a, P5 b, P5 c, P5 d, unsigned char shade)
{
    const P5 *p[4] = {&a, &b, &c, &d};
    int i, base = s_vcount;

    if (s_vcount + 4 > MAX_VERTS || s_icount + 6 > MAX_INDICES)
        return -1;

    for (i = 0; i < 4; i++) {
        LevelVertex *v = &s_verts[s_vcount++];
        v->x = p[i]->x; v->y = p[i]->y; v->z = p[i]->z;
        v->u = p[i]->u; v->v = p[i]->v;
        v->r = v->g = v->b = shade;
        v->a = 255;
    }
    s_indices[s_icount++] = (GLushort)(base + 0);
    s_indices[s_icount++] = (GLushort)(base + 1);
    s_indices[s_icount++] = (GLushort)(base + 2);
    s_indices[s_icount++] = (GLushort)(base + 0);
    s_indices[s_icount++] = (GLushort)(base + 2);
    s_indices[s_icount++] = (GLushort)(base + 3);
    return 0;
}

/* Baked brightness per face direction (0..255). */
#define SHADE_FLOOR   235
#define SHADE_CEILING 170
#define SHADE_POS_X   220
#define SHADE_NEG_X   170
#define SHADE_POS_Z   195
#define SHADE_NEG_Z   150

static int build_material(int mat)
{
    const float S = ROOM_CELL_SIZE;
    const float H = ROOM_WALL_HEIGHT;
    const float vt = H / S; /* wall texture V at the top edge */
    int cx, cz;

    s_range[mat].first = s_icount;

    for (cz = 0; cz < ROOM_D; cz++) {
        for (cx = 0; cx < ROOM_W; cx++) {
            float x0, x1, z0, z1, u0, u1, w0, w1;

            if (room_is_solid(cx, cz))
                continue;

            x0 = cx * S; x1 = x0 + S;
            z0 = cz * S; z1 = z0 + S;
            u0 = (float)cx; u1 = u0 + 1.0f; /* world-space UVs */
            w0 = (float)cz; w1 = w0 + 1.0f;

            if (mat == TEX_FLOOR) {             /* normal +Y */
                P5 a = {x0, 0, z0, u0, w0}, b = {x0, 0, z1, u0, w1};
                P5 c = {x1, 0, z1, u1, w1}, d = {x1, 0, z0, u1, w0};
                if (emit_quad(a, b, c, d, SHADE_FLOOR) < 0) return -1;
            } else if (mat == TEX_CEILING) {    /* normal -Y */
                P5 a = {x0, H, z0, u0, w0}, b = {x1, H, z0, u1, w0};
                P5 c = {x1, H, z1, u1, w1}, d = {x0, H, z1, u0, w1};
                if (emit_quad(a, b, c, d, SHADE_CEILING) < 0) return -1;
            } else {                            /* walls; V runs 0 (top) .. vt (floor) */
                if (room_is_solid(cx - 1, cz)) {       /* plane x0, normal +X */
                    P5 a = {x0, 0, z1, w0, vt}, b = {x0, 0, z0, w1, vt};
                    P5 c = {x0, H, z0, w1, 0},  d = {x0, H, z1, w0, 0};
                    if (emit_quad(a, b, c, d, SHADE_POS_X) < 0) return -1;
                }
                if (room_is_solid(cx + 1, cz)) {       /* plane x1, normal -X */
                    P5 a = {x1, 0, z0, w0, vt}, b = {x1, 0, z1, w1, vt};
                    P5 c = {x1, H, z1, w1, 0},  d = {x1, H, z0, w0, 0};
                    if (emit_quad(a, b, c, d, SHADE_NEG_X) < 0) return -1;
                }
                if (room_is_solid(cx, cz - 1)) {       /* plane z0, normal +Z */
                    P5 a = {x0, 0, z0, u0, vt}, b = {x1, 0, z0, u1, vt};
                    P5 c = {x1, H, z0, u1, 0},  d = {x0, H, z0, u0, 0};
                    if (emit_quad(a, b, c, d, SHADE_POS_Z) < 0) return -1;
                }
                if (room_is_solid(cx, cz + 1)) {       /* plane z1, normal -Z */
                    P5 a = {x1, 0, z1, u0, vt}, b = {x0, 0, z1, u1, vt};
                    P5 c = {x0, H, z1, u1, 0},  d = {x1, H, z1, u0, 0};
                    if (emit_quad(a, b, c, d, SHADE_NEG_Z) < 0) return -1;
                }
            }
        }
    }

    s_range[mat].count = s_icount - s_range[mat].first;
    return 0;
}

int level_mesh_build(void)
{
    int m;
    s_vcount = s_icount = 0;
    for (m = 0; m < TEX_COUNT; m++) {
        if (build_material(m) < 0)
            return -1;
    }
    return s_vcount;
}

int level_mesh_upload_vbo(void)
{
    GLenum err;

    while (glGetError() != GL_NO_ERROR) { /* clear stale errors */ }

    glGenBuffers(1, &s_vbo);
    glGenBuffers(1, &s_ibo);

    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(s_vcount * sizeof(LevelVertex)),
                 s_verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)(s_icount * sizeof(GLushort)),
                 s_indices, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    err = glGetError();
    if (err != GL_NO_ERROR) {
        level_mesh_free();
        return -1;
    }
    return 0;
}

int level_mesh_vbo_available(void)
{
    return s_vbo != 0 && s_ibo != 0;
}

void level_mesh_draw(const GLuint tex[TEX_COUNT], int textured, int use_vbo)
{
    const int vbo = use_vbo && level_mesh_vbo_available();
    const GLsizei stride = (GLsizei)sizeof(LevelVertex);
    const char *vbase;
    int m;

    if (s_icount == 0)
        return;

    if (vbo) {
        glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_ibo);
        vbase = (const char *)0;        /* offsets into the bound VBO */
    } else {
        vbase = (const char *)s_verts;  /* client memory */
    }

    glVertexPointer(3, GL_FLOAT, stride, vbase + offsetof(LevelVertex, x));
    glColorPointer(4, GL_UNSIGNED_BYTE, stride, vbase + offsetof(LevelVertex, r));

    if (textured) {
        glEnable(GL_TEXTURE_2D);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, stride, vbase + offsetof(LevelVertex, u));
    }

    for (m = 0; m < TEX_COUNT; m++) {
        const void *idx;
        if (s_range[m].count == 0)
            continue;
        if (textured)
            glBindTexture(GL_TEXTURE_2D, tex[m]);
        idx = vbo ? (const void *)(uintptr_t)(s_range[m].first * sizeof(GLushort))
                  : (const void *)&s_indices[s_range[m].first];
        glDrawElements(GL_TRIANGLES, s_range[m].count, GL_UNSIGNED_SHORT, idx);
    }

    if (textured) {
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    if (vbo) {
        /* Unbind so later client-array pointers (HUD) are not treated as
         * offsets into this buffer. */
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }
}

int level_mesh_vertex_count(void)   { return s_vcount; }
int level_mesh_triangle_count(void) { return s_icount / 3; }

void level_mesh_free(void)
{
    if (s_vbo) glDeleteBuffers(1, &s_vbo);
    if (s_ibo) glDeleteBuffers(1, &s_ibo);
    s_vbo = s_ibo = 0;
}
