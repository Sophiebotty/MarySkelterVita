/*
 * rs_gl_capture.c - host GL stand-in for the unmodified src/graphics/
 * level_mesh.c (ResPipe PC tests). glBufferData keeps a copy of the bytes
 * uploaded to each target; glDrawElements records each draw with the texture
 * bound at the time. Nothing is rendered.
 */
#include <stdlib.h>
#include <string.h>

#include "vitaGL.h"
#include "rs_gl_capture.h"

static rs_gl_capture g_cap;
static GLuint g_next = 1, g_array, g_element, g_texture;

const rs_gl_capture *rs_gl_get(void)
{
    return &g_cap;
}

void rs_gl_reset(void)
{
    free(g_cap.vertex_data);
    free(g_cap.index_data);
    memset(&g_cap, 0, sizeof g_cap);
    g_array = g_element = g_texture = 0;
}

GLenum glGetError(void)
{
    return GL_NO_ERROR;
}

void glGenBuffers(GLsizei n, GLuint *buffers)
{
    GLsizei i;
    for (i = 0; i < n; i++)
        buffers[i] = g_next++;
}

void glDeleteBuffers(GLsizei n, const GLuint *buffers)
{
    (void)n;
    (void)buffers;
}

void glBindBuffer(GLenum target, GLuint buffer)
{
    if (target == GL_ARRAY_BUFFER)
        g_array = buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER)
        g_element = buffer;
}

static uint8_t *copy_of(const void *data, GLsizeiptr size)
{
    uint8_t *p = (uint8_t *)malloc(size > 0 ? (size_t)size : 1u);
    if (p && size > 0)
        memcpy(p, data, (size_t)size);
    return p;
}

void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    if (target == GL_ARRAY_BUFFER && g_array) {
        free(g_cap.vertex_data);
        g_cap.vertex_data = copy_of(data, size);
        g_cap.vertex_size = (size_t)size;
        g_cap.usage_vertex = usage;
    } else if (target == GL_ELEMENT_ARRAY_BUFFER && g_element) {
        free(g_cap.index_data);
        g_cap.index_data = copy_of(data, size);
        g_cap.index_size = (size_t)size;
        g_cap.usage_index = usage;
    }
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *pointer)
{
    (void)size; (void)type; (void)stride; (void)pointer;
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, const void *pointer)
{
    (void)size; (void)type; (void)stride; (void)pointer;
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *pointer)
{
    (void)size; (void)type; (void)stride; (void)pointer;
}

void glEnable(GLenum cap) { (void)cap; }
void glDisable(GLenum cap) { (void)cap; }
void glEnableClientState(GLenum array) { (void)array; }
void glDisableClientState(GLenum array) { (void)array; }

void glBindTexture(GLenum target, GLuint texture)
{
    if (target == GL_TEXTURE_2D)
        g_texture = texture;
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    rs_gl_draw *d;
    if (g_cap.draws >= RS_GL_MAX_DRAWS) {
        g_cap.overflow = 1;
        return;
    }
    d = &g_cap.draw[g_cap.draws++];
    d->mode = mode;
    d->type = type;
    d->count = count;
    d->texture = g_texture;
    d->offset = (uintptr_t)indices;
    d->vbo_bound = g_element != 0;
}
