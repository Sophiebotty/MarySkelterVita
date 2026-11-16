/*
 * Host stand-in for vitaGL.h (ContentFit PC tests only; never used on the
 * Vita). Declares just the GL subset ContentFit and hud2d use. The matching
 * implementation, cf_gl_stub.c, records what is submitted: draw calls,
 * triangles, texture binds, blended pixel area, back-facing triangles and the
 * bytes of every texture and buffer (as a simulated VRAM pool).
 */
#ifndef CF_STUB_VITAGL_H
#define CF_STUB_VITAGL_H

#include <stddef.h>
#include <stdint.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef double GLdouble;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef unsigned char GLubyte;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
typedef float GLclampf;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_VALUE 0x0501
#define GL_OUT_OF_MEMORY 0x0505
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_LEQUAL 0x0203
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_BACK 0x0405
#define GL_CCW 0x0901
#define GL_CULL_FACE 0x0B44
#define GL_FOG 0x0B60
#define GL_FOG_START 0x0B63
#define GL_FOG_END 0x0B64
#define GL_FOG_MODE 0x0B65
#define GL_FOG_COLOR 0x0B66
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_MODULATE 0x2100
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#define GL_VERTEX_ARRAY 0x8074
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG 0x8C00
#define GL_ETC1_RGB8_OES 0x8D64

typedef enum { VGL_MEM_VRAM, VGL_MEM_RAM, VGL_MEM_PHYCONT, VGL_MEM_BUDGET, VGL_MEM_EXTERNAL, VGL_MEM_ALL } vglMemType;

void glClear(GLbitfield mask);
void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glClearDepthf(GLfloat d);
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void glEnable(GLenum cap);
void glDisable(GLenum cap);
void glEnableClientState(GLenum a);
void glDisableClientState(GLenum a);
void glDepthFunc(GLenum f);
void glDepthMask(GLboolean m);
void glCullFace(GLenum m);
void glFrontFace(GLenum m);
void glBlendFunc(GLenum s, GLenum d);
void glFogi(GLenum p, const GLint v);
void glFogf(GLenum p, GLfloat v);
void glFogfv(GLenum p, const GLfloat *v);
void glTexEnvi(GLenum t, GLenum p, GLint v);
void glMatrixMode(GLenum m);
void glLoadIdentity(void);
void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zn, GLdouble zf);
void glRotatef(GLfloat a, GLfloat x, GLfloat y, GLfloat z);
void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const void *p);
void glColorPointer(GLint size, GLenum type, GLsizei stride, const void *p);
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void *p);
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glGenTextures(GLsizei n, GLuint *t);
void glBindTexture(GLenum target, GLuint t);
void glTexParameteri(GLenum target, GLenum p, GLint v);
void glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum fmt,
                  GLenum type, const void *data);
void glCompressedTexImage2D(GLenum target, GLint level, GLenum ifmt, GLsizei w, GLsizei h, GLint border,
                            GLsizei size, const void *data);
void glGenerateMipmap(GLenum target);
void glDeleteTextures(GLsizei n, const GLuint *t);
void glGenBuffers(GLsizei n, GLuint *b);
void glBindBuffer(GLenum target, GLuint b);
void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
void glDeleteBuffers(GLsizei n, const GLuint *b);
GLenum glGetError(void);
void glFinish(void);
size_t vglMemFree(vglMemType t);
size_t vglMemTotal(vglMemType t);

/* ---- stand-in controls and counters (tests only) ---- */
typedef struct {
    long   draws, triangles, binds;
    double blended_px;          /* blended triangle area in screen pixels (not clipped) */
    long   backfacing;          /* triangles that face away while culling is on */
    long   behind;              /* triangles with a vertex behind the camera (w <= 0) */
    long   tex_live;            /* live texture objects */
    uint64_t tex_bytes;         /* bytes held by live textures (all levels) */
    uint64_t buf_bytes;         /* bytes held by live buffers */
    long   gl_errors_raised;
} stub_counts;

void stub_reset_counts(void);   /* draws, triangles, binds, area, backfacing */
void stub_get_counts(stub_counts *out);
void stub_set_analysis(int on); /* transform every triangle (slow); off = count only */
void stub_reject_format(GLenum ifmt);          /* glCompressedTexImage2D with ifmt raises GL_INVALID_ENUM */
void stub_set_pools(uint64_t vram_total, uint64_t vram_reserved, uint64_t ram_total, uint64_t ram_free,
                    uint64_t phycont_total);

#endif /* CF_STUB_VITAGL_H */
