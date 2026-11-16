/*
 * Host stand-in for vitaGL.h (ResPipe PC tests only; never used on the Vita).
 * Declares just what src/graphics/level_mesh.c and texgen.h need, so the
 * unmodified level_mesh.c can be compiled on a PC. The implementation,
 * rs_gl_capture.c, records the buffers and draw calls it receives.
 */
#ifndef RS_STUB_VITAGL_H
#define RS_STUB_VITAGL_H

#include <stddef.h>
#include <stdint.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef unsigned short GLushort;
typedef ptrdiff_t GLsizeiptr;

#define GL_NO_ERROR 0
#define GL_TRIANGLES 0x0004
#define GL_BACK 0x0405
#define GL_CCW 0x0901
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define GL_RGB 0x1907
#define GL_NEAREST 0x2600
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4

GLenum glGetError(void);
void glGenBuffers(GLsizei n, GLuint *buffers);
void glDeleteBuffers(GLsizei n, const GLuint *buffers);
void glBindBuffer(GLenum target, GLuint buffer);
void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
void glColorPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *pointer);
void glEnable(GLenum cap);
void glDisable(GLenum cap);
void glEnableClientState(GLenum array);
void glDisableClientState(GLenum array);
void glBindTexture(GLenum target, GLuint texture);
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);

#endif /* RS_STUB_VITAGL_H */
