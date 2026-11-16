/*
 * rs_gl_capture.h - what the host GL stand-in recorded (ResPipe PC tests).
 */
#ifndef RS_GL_CAPTURE_H
#define RS_GL_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

#define RS_GL_MAX_DRAWS 16

typedef struct {
    unsigned       mode, type, texture;     /* texture bound when the draw was issued */
    int            count;
    uintptr_t      offset;                  /* byte offset into the bound index buffer */
    int            vbo_bound;               /* an element buffer was bound */
} rs_gl_draw;

typedef struct {
    uint8_t   *vertex_data;                 /* copy of the GL_ARRAY_BUFFER upload */
    size_t     vertex_size;
    uint8_t   *index_data;                  /* copy of the GL_ELEMENT_ARRAY_BUFFER upload */
    size_t     index_size;
    unsigned   usage_vertex, usage_index;
    rs_gl_draw draw[RS_GL_MAX_DRAWS];
    int        draws;
    int        overflow;                    /* more draws than RS_GL_MAX_DRAWS */
} rs_gl_capture;

const rs_gl_capture *rs_gl_get(void);
void rs_gl_reset(void);                     /* frees the copies, clears everything */

#endif /* RS_GL_CAPTURE_H */
