/*
 * cf_gl_stub.c - host stand-in for the GL subset ContentFit uses (tests only).
 *
 * It does not render. It records what would be submitted and, with analysis
 * on, transforms every triangle with the current matrices to measure blended
 * pixel area and count back-facing triangles. Texture uploads read every
 * byte of the caller's buffer, so AddressSanitizer catches undersized
 * buffers. Textures and buffers are charged to a simulated VRAM pool.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <vitaGL.h>

#define MAX_TEX 8192
#define MAX_BUF 64

typedef struct { int live; uint64_t bytes; int w, h, bpp4; } tex_t;
typedef struct { int live; uint8_t *data; size_t size; } buf_t;
typedef struct { GLint size; GLenum type; GLsizei stride; const void *p; } ptr_t;

static tex_t T[MAX_TEX];
static buf_t B[MAX_BUF];
static GLuint next_tex = 1, next_buf = 1, bound_tex, bound_array, bound_elem;
static int en_blend, en_tex, en_fog, en_depth, en_cull, cs_vertex, cs_color, cs_tex;
static GLenum err_pending, rejected[8];
static int n_rejected, analysis;
static double proj[16], mv[16];
static int mode_proj;
static ptr_t vp, cp, tp;
static stub_counts C;
static uint64_t vram_total = 90ull << 20, vram_reserved = 7ull << 20, ram_total = 64ull << 20,
                ram_free = 63ull << 20, phy_total = 26ull << 20;
static volatile uint32_t sink;

static void ident(double *m) { memset(m, 0, 16 * sizeof(double)); m[0] = m[5] = m[10] = m[15] = 1.0; }
static double *cur(void) { return mode_proj ? proj : mv; }

static void mul(double *m, const double *r)   /* m = m * r (column-major) */
{
    double o[16];
    int i, j, k;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            double s = 0.0;
            for (k = 0; k < 4; k++) s += m[k * 4 + j] * r[i * 4 + k];
            o[i * 4 + j] = s;
        }
    memcpy(m, o, sizeof(o));
}

static void raise_err(GLenum e) { if (err_pending == GL_NO_ERROR) err_pending = e; C.gl_errors_raised++; }

void stub_reset_counts(void)
{
    C.draws = C.triangles = C.binds = C.backfacing = C.behind = 0;
    C.blended_px = 0.0;
}
void stub_get_counts(stub_counts *out) { *out = C; }
void stub_set_analysis(int on) { analysis = on; }
void stub_reject_format(GLenum f) { if (n_rejected < 8) rejected[n_rejected++] = f; }
void stub_set_pools(uint64_t vt, uint64_t vr, uint64_t rt, uint64_t rf, uint64_t pt)
{
    vram_total = vt; vram_reserved = vr; ram_total = rt; ram_free = rf; phy_total = pt;
}

void glClear(GLbitfield m) { (void)m; }
void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { (void)r; (void)g; (void)b; (void)a; }
void glClearDepthf(GLfloat d) { (void)d; }
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { (void)x; (void)y; (void)w; (void)h; }

static void set_cap(GLenum cap, int on)
{
    switch (cap) {
    case GL_BLEND: en_blend = on; break;
    case GL_TEXTURE_2D: en_tex = on; break;
    case GL_FOG: en_fog = on; break;
    case GL_DEPTH_TEST: en_depth = on; break;
    case GL_CULL_FACE: en_cull = on; break;
    default: raise_err(GL_INVALID_ENUM); break;
    }
}
void glEnable(GLenum cap) { set_cap(cap, 1); }
void glDisable(GLenum cap) { set_cap(cap, 0); }
static void set_cs(GLenum a, int on)
{
    if (a == GL_VERTEX_ARRAY) cs_vertex = on;
    else if (a == GL_COLOR_ARRAY) cs_color = on;
    else if (a == GL_TEXTURE_COORD_ARRAY) cs_tex = on;
    else raise_err(GL_INVALID_ENUM);
}
void glEnableClientState(GLenum a) { set_cs(a, 1); }
void glDisableClientState(GLenum a) { set_cs(a, 0); }
void glDepthFunc(GLenum f) { (void)f; }
void glDepthMask(GLboolean m) { (void)m; }
void glCullFace(GLenum m) { (void)m; }
void glFrontFace(GLenum m) { (void)m; }
void glBlendFunc(GLenum s, GLenum d) { (void)s; (void)d; }
void glFogi(GLenum p, const GLint v) { (void)p; (void)v; }
void glFogf(GLenum p, GLfloat v) { (void)p; (void)v; }
void glFogfv(GLenum p, const GLfloat *v) { (void)p; sink += v ? (uint32_t)v[3] : 0u; }
void glTexEnvi(GLenum t, GLenum p, GLint v) { (void)t; (void)p; (void)v; }
void glMatrixMode(GLenum m) { mode_proj = m == GL_PROJECTION; }
void glLoadIdentity(void) { ident(cur()); }

void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    double m[16];
    ident(m);
    m[0] = 2.0 / (r - l); m[5] = 2.0 / (t - b); m[10] = -2.0 / (f - n);
    m[12] = -(r + l) / (r - l); m[13] = -(t + b) / (t - b); m[14] = -(f + n) / (f - n);
    mul(cur(), m);
}

void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zn, GLdouble zf)
{
    double m[16], f = 1.0 / tan(fovy * 3.14159265358979323846 / 360.0);
    memset(m, 0, sizeof(m));
    m[0] = f / aspect; m[5] = f; m[10] = (zf + zn) / (zn - zf); m[11] = -1.0; m[14] = 2.0 * zf * zn / (zn - zf);
    mul(cur(), m);
}

void glRotatef(GLfloat a, GLfloat x, GLfloat y, GLfloat z)
{
    double m[16], r = a * 3.14159265358979323846 / 180.0, c = cos(r), s = sin(r), l = sqrt(x * x + y * y + z * z);
    double X = x / l, Y = y / l, Z = z / l;
    ident(m);
    m[0] = X * X * (1 - c) + c;     m[4] = X * Y * (1 - c) - Z * s; m[8] = X * Z * (1 - c) + Y * s;
    m[1] = Y * X * (1 - c) + Z * s; m[5] = Y * Y * (1 - c) + c;     m[9] = Y * Z * (1 - c) - X * s;
    m[2] = X * Z * (1 - c) - Y * s; m[6] = Y * Z * (1 - c) + X * s; m[10] = Z * Z * (1 - c) + c;
    mul(cur(), m);
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    double m[16];
    ident(m);
    m[12] = x; m[13] = y; m[14] = z;
    mul(cur(), m);
}

void glVertexPointer(GLint s, GLenum t, GLsizei st, const void *p) { vp.size = s; vp.type = t; vp.stride = st; vp.p = p; }
void glColorPointer(GLint s, GLenum t, GLsizei st, const void *p) { cp.size = s; cp.type = t; cp.stride = st; cp.p = p; }
void glTexCoordPointer(GLint s, GLenum t, GLsizei st, const void *p) { tp.size = s; tp.type = t; tp.stride = st; tp.p = p; }

/* Address of attribute data: an offset into the bound array buffer, or a pointer. */
static const uint8_t *attr_base(const ptr_t *a)
{
    if (bound_array) {
        size_t off = (size_t)(uintptr_t)a->p;
        if (!B[bound_array].live || off > B[bound_array].size) { raise_err(GL_INVALID_VALUE); return NULL; }
        return B[bound_array].data + off;
    }
    return (const uint8_t *)a->p;
}

static int position(const uint8_t *base, unsigned idx, double out[4])
{
    const float *f = (const float *)(const void *)(base + (size_t)idx * (size_t)vp.stride);
    out[0] = f[0];
    out[1] = f[1];
    out[2] = vp.size >= 3 ? f[2] : 0.0;
    out[3] = 1.0;
    return 1;
}

static void xform(const double *m, const double in[4], double out[4])
{
    int i;
    for (i = 0; i < 4; i++) out[i] = m[i] * in[0] + m[4 + i] * in[1] + m[8 + i] * in[2] + m[12 + i] * in[3];
}

static void triangle(const uint8_t *vb, const uint8_t *cb, const uint8_t *tb, unsigned a, unsigned b, unsigned c)
{
    double p[3][4], q[4], mvp[16], ndc[3][2], area;
    unsigned ids[3] = {a, b, c};
    int k;
    /* touch colour and texcoord data so out-of-range indices are caught */
    for (k = 0; k < 3; k++) {
        if (cb) sink += cb[(size_t)ids[k] * (size_t)cp.stride];
        if (tb) sink += tb[(size_t)ids[k] * (size_t)tp.stride];
    }
    if (!analysis) {
        for (k = 0; k < 3; k++) { position(vb, ids[k], p[k]); sink += (uint32_t)p[k][0]; }
        return;
    }
    memcpy(mvp, proj, sizeof(mvp));
    mul(mvp, mv);
    for (k = 0; k < 3; k++) {
        position(vb, ids[k], q);
        xform(mvp, q, p[k]);
        if (p[k][3] <= 1e-9) { C.behind++; return; }
        ndc[k][0] = p[k][0] / p[k][3];
        ndc[k][1] = p[k][1] / p[k][3];
    }
    area = (ndc[1][0] - ndc[0][0]) * (ndc[2][1] - ndc[0][1]) - (ndc[2][0] - ndc[0][0]) * (ndc[1][1] - ndc[0][1]);
    if (en_cull && area <= 0.0) C.backfacing++;
    if (en_blend) C.blended_px += fabs(area) * 0.5 * 480.0 * 272.0;
}

static void draw(GLsizei count, const uint16_t *idx, GLint first)
{
    const uint8_t *vb = cs_vertex ? attr_base(&vp) : NULL;
    const uint8_t *cb = cs_color ? attr_base(&cp) : NULL;
    const uint8_t *tb = cs_tex ? attr_base(&tp) : NULL;
    GLsizei i;
    if (!vb) { raise_err(GL_INVALID_VALUE); return; }
    C.draws++;
    C.triangles += count / 3;
    for (i = 0; i + 2 < count; i += 3) {
        if (idx) triangle(vb, cb, tb, idx[i], idx[i + 1], idx[i + 2]);
        else triangle(vb, cb, tb, (unsigned)(first + i), (unsigned)(first + i + 1), (unsigned)(first + i + 2));
    }
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    const uint16_t *idx;
    if (mode != GL_TRIANGLES || type != GL_UNSIGNED_SHORT) { raise_err(GL_INVALID_ENUM); return; }
    if (bound_elem) {
        size_t off = (size_t)(uintptr_t)indices;
        if (!B[bound_elem].live || off + (size_t)count * 2u > B[bound_elem].size) { raise_err(GL_INVALID_VALUE); return; }
        idx = (const uint16_t *)(const void *)(B[bound_elem].data + off);
    } else {
        idx = (const uint16_t *)indices;
    }
    draw(count, idx, 0);
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    if (mode != GL_TRIANGLES) { raise_err(GL_INVALID_ENUM); return; }
    draw(count, NULL, first);
}

void glGenTextures(GLsizei n, GLuint *t)
{
    GLsizei i;
    for (i = 0; i < n; i++) {
        GLuint id = 0, k;
        for (k = 0; k < MAX_TEX - 1 && !id; k++) {
            GLuint cand = next_tex;
            next_tex = next_tex % (MAX_TEX - 1) + 1;
            if (!T[cand].live && T[cand].w == 0) id = cand;
        }
        if (!id) { raise_err(GL_OUT_OF_MEMORY); t[i] = 0; continue; }
        memset(&T[id], 0, sizeof(T[id]));
        T[id].w = -1;            /* name allocated, no storage yet */
        t[i] = id;
    }
}

void glBindTexture(GLenum target, GLuint t)
{
    (void)target;
    if (t >= MAX_TEX) { raise_err(GL_INVALID_VALUE); return; }
    bound_tex = t;
    if (t) C.binds++;
}

void glTexParameteri(GLenum target, GLenum p, GLint v) { (void)target; (void)p; (void)v; }

static void charge(GLuint t, uint64_t bytes, int reset)
{
    if (!t || !T[t].w) { raise_err(GL_INVALID_VALUE); return; }
    if (reset && T[t].live) { C.tex_bytes -= T[t].bytes; T[t].bytes = 0; C.tex_live--; T[t].live = 0; }
    if (!T[t].live) { T[t].live = 1; C.tex_live++; }
    T[t].bytes += bytes;
    C.tex_bytes += bytes;
}

static void read_all(const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t s = 0;
    size_t i;
    for (i = 0; i < n; i++) s += p[i];
    sink += s;
}

void glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum fmt,
                  GLenum type, const void *data)
{
    size_t bpp = type == GL_UNSIGNED_SHORT_5_6_5 ? 2u : 4u;
    (void)target; (void)border;
    if (level != 0 || w <= 0 || h <= 0 || (GLint)fmt != ifmt) { raise_err(GL_INVALID_VALUE); return; }
    if (data) read_all(data, (size_t)w * (size_t)h * bpp);
    charge(bound_tex, (uint64_t)w * (uint64_t)h * bpp, 1);
    T[bound_tex].w = w; T[bound_tex].h = h; T[bound_tex].bpp4 = (int)bpp;
}

void glCompressedTexImage2D(GLenum target, GLint level, GLenum ifmt, GLsizei w, GLsizei h, GLint border,
                            GLsizei size, const void *data)
{
    int i;
    (void)target; (void)border; (void)w; (void)h;
    for (i = 0; i < n_rejected; i++)
        if (rejected[i] == ifmt) { raise_err(GL_INVALID_ENUM); return; }
    read_all(data, (size_t)size);
    charge(bound_tex, (uint64_t)size, level == 0);
    if (level == 0) { T[bound_tex].w = w; T[bound_tex].h = h; }
}

void glGenerateMipmap(GLenum target)
{
    int w, h;
    uint64_t extra = 0;
    (void)target;
    if (!bound_tex || !T[bound_tex].live) { raise_err(GL_INVALID_VALUE); return; }
    w = T[bound_tex].w; h = T[bound_tex].h;
    while (w > 1 || h > 1) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        extra += (uint64_t)w * (uint64_t)h * (uint64_t)T[bound_tex].bpp4;
    }
    charge(bound_tex, extra, 0);
}

void glDeleteTextures(GLsizei n, const GLuint *t)
{
    GLsizei i;
    for (i = 0; i < n; i++) {
        GLuint id = t[i];
        if (!id || id >= MAX_TEX) continue;
        if (T[id].live) { C.tex_bytes -= T[id].bytes; C.tex_live--; }
        memset(&T[id], 0, sizeof(T[id]));
        if (bound_tex == id) bound_tex = 0;
    }
}

void glGenBuffers(GLsizei n, GLuint *b)
{
    GLsizei i;
    for (i = 0; i < n; i++) {
        GLuint k, id = 0;
        for (k = 1; k < MAX_BUF && !id; k++)
            if (!B[k].live && !B[k].data) id = k;
        if (!id) { raise_err(GL_OUT_OF_MEMORY); b[i] = 0; continue; }
        B[id].live = 1;
        B[id].size = 0;
        b[i] = id;
    }
    (void)next_buf;
}

void glBindBuffer(GLenum target, GLuint b)
{
    if (b >= MAX_BUF) { raise_err(GL_INVALID_VALUE); return; }
    if (target == GL_ARRAY_BUFFER) bound_array = b;
    else bound_elem = b;
}

void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage)
{
    GLuint b = target == GL_ARRAY_BUFFER ? bound_array : bound_elem;
    (void)usage;
    if (!b || !B[b].live || size < 0) { raise_err(GL_INVALID_VALUE); return; }
    if (B[b].data) { C.buf_bytes -= B[b].size; free(B[b].data); }
    B[b].data = (uint8_t *)malloc((size_t)size ? (size_t)size : 1u);
    if (!B[b].data) { raise_err(GL_OUT_OF_MEMORY); B[b].size = 0; return; }
    if (data) memcpy(B[b].data, data, (size_t)size);
    B[b].size = (size_t)size;
    C.buf_bytes += (uint64_t)size;
}

void glDeleteBuffers(GLsizei n, const GLuint *b)
{
    GLsizei i;
    for (i = 0; i < n; i++) {
        GLuint id = b[i];
        if (!id || id >= MAX_BUF) continue;
        if (B[id].data) { C.buf_bytes -= B[id].size; free(B[id].data); }
        memset(&B[id], 0, sizeof(B[id]));
        if (bound_array == id) bound_array = 0;
        if (bound_elem == id) bound_elem = 0;
    }
}

GLenum glGetError(void) { GLenum e = err_pending; err_pending = GL_NO_ERROR; return e; }
void glFinish(void) { }

size_t vglMemTotal(vglMemType t)
{
    switch (t) {
    case VGL_MEM_VRAM: return (size_t)vram_total;
    case VGL_MEM_RAM: return (size_t)ram_total;
    case VGL_MEM_PHYCONT: return (size_t)phy_total;
    default: return 0;
    }
}

size_t vglMemFree(vglMemType t)
{
    uint64_t used;
    switch (t) {
    case VGL_MEM_VRAM:
        used = vram_reserved + C.tex_bytes + C.buf_bytes;
        return used >= vram_total ? 0u : (size_t)(vram_total - used);
    case VGL_MEM_RAM: return (size_t)ram_free;
    case VGL_MEM_PHYCONT: return (size_t)phy_total;
    default: return 0;
    }
}
