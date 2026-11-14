/*
 * level_mesh.h - static room geometry built once from the room grid.
 *
 * Phase 2: textured, indexed quads grouped by material (wall / floor /
 * ceiling) so a frame needs one draw call per material. Vertex colours carry
 * baked per-direction shading and are modulated with the texture.
 *
 * Two draw paths:
 *   - client arrays (default, proven on hardware in phase 1)
 *   - static VBO + IBO (GL_STATIC_DRAW), selectable at runtime for testing
 *
 * All faces wind counter-clockwise when seen from inside the room.
 */
#ifndef LEVEL_MESH_H
#define LEVEL_MESH_H

#include <vitaGL.h>

#include "graphics/texgen.h"

/* Interleaved vertex: 24 bytes. */
typedef struct {
    float x, y, z;
    float u, v;
    unsigned char r, g, b, a;
} LevelVertex;

/* Builds the mesh from room_map. Returns vertex count, or -1 on overflow. */
int  level_mesh_build(void);

/* Uploads vertices and indices into GL buffers. Returns 0 on success,
 * -1 if GL reported an error (VBO path then stays unavailable). */
int  level_mesh_upload_vbo(void);
int  level_mesh_vbo_available(void);

/* Draws the mesh with the current camera matrices.
 * Requires GL_VERTEX_ARRAY and GL_COLOR_ARRAY enabled. Enables and then
 * disables GL_TEXTURE_2D / GL_TEXTURE_COORD_ARRAY itself when textured.
 * Leaves GL_ARRAY_BUFFER / GL_ELEMENT_ARRAY_BUFFER unbound on return. */
void level_mesh_draw(const GLuint tex[TEX_COUNT], int textured, int use_vbo);

int  level_mesh_vertex_count(void);
int  level_mesh_triangle_count(void);

void level_mesh_free(void);

#endif /* LEVEL_MESH_H */
