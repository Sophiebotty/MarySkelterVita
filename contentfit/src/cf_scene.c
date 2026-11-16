/*
 * cf_scene.c - synthetic scenes for the content-fit benchmark (see cf_scene.h).
 *
 * Static geometry uses VBO/IBO; per-frame data (animated enemies, particles,
 * menu icons, 2D quads) uses client arrays, the proven path for dynamic data.
 * No immediate-mode GL (SDL2 starts vitaGL with legacy_pool_size = 0).
 */
#include <stddef.h>
#include <string.h>
#include <vitaGL.h>
#include <zlib.h>

#include "cf_scene.h"
#include "rp_mem.h"

#define MAX_PIECES 300
#define MAX_ENEMIES 5
#define MAX_LAYERS 4
#define QUAD_BATCH 1000

enum { S_ = 0, M_ = 1, L_ = 2, XL_ = 3 };

#define DUN(id, nm, t, f, segs, pps, c, r, m, dim) \
    {id, nm, CF_SC_DUNGEON, t, f, segs, pps, c, r, m, dim, 0, 0, 0, 0, 0, 0, 0, 0}
#define BAT(id, nm, t, ne, ec, er, np, fl) \
    {id, nm, CF_SC_BATTLE, t, CF_TF_RGBA8, 5, 4, 25, 10, 4, 256, ne, ec, er, np, fl, 0, 0, 0}
#define DLG(id, nm, t, nl) {id, nm, CF_SC_DIALOGUE, t, CF_TF_RGBA8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, nl, 1, 0}
#define MENU(id, nm, t, ni) {id, nm, CF_SC_MENU, t, CF_TF_RGBA8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, ni}

static const cf_scene_def SCENES[] = {
    {"C3-P", "PACING_EMPTY", CF_SC_EMPTY, -1, CF_TF_RGBA8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    DUN("C3-D-S", "DUNGEON_S", S_, CF_TF_RGBA8, 5, 4, 25, 10, 4, 256),
    DUN("C3-D-M", "DUNGEON_M", M_, CF_TF_RGBA8, 10, 6, 25, 10, 8, 256),
    DUN("C3-D-L", "DUNGEON_L", L_, CF_TF_RGBA8, 10, 15, 20, 10, 16, 512),
    DUN("C3-D-XL", "DUNGEON_XL", XL_, CF_TF_RGBA8, 10, 30, 14, 12, 32, 512),
    BAT("C3-B-S", "BATTLE_S", S_, 1, 50, 20, 0, 0),
    BAT("C3-B-M", "BATTLE_M", M_, 3, 50, 50, 100, 0),
    BAT("C3-B-L", "BATTLE_L", L_, 5, 80, 50, 300, 1),
    BAT("C3-B-XL", "BATTLE_XL", XL_, 5, 100, 60, 600, 1),
    DLG("C3-V-S", "DIALOGUE_S", S_, 1),
    DLG("C3-V-M", "DIALOGUE_M", M_, 2),
    DLG("C3-V-L", "DIALOGUE_L", L_, 3),
    DLG("C3-V-XL", "DIALOGUE_XL", XL_, 4),
    MENU("C3-U-S", "MENU_S", S_, 500),
    MENU("C3-U-M", "MENU_M", M_, 1500),
    MENU("C3-U-L", "MENU_L", L_, 3000),
    MENU("C3-U-XL", "MENU_XL", XL_, 6000),
    {"C3-X", "COMBINED", CF_SC_COMBINED, -1, CF_TF_RGBA8, 5, 4, 25, 10, 4, 256, 3, 50, 50, 100, 0, 2, 0, 0},
    DUN("C3-T-RGBA8", "FORMAT_RGBA8", M_, CF_TF_RGBA8, 10, 6, 25, 10, 8, 256),
    DUN("C3-T-RGB565", "FORMAT_RGB565", M_, CF_TF_RGB565, 10, 6, 25, 10, 8, 256),
    DUN("C3-T-DXT1", "FORMAT_DXT1", M_, CF_TF_DXT1, 10, 6, 25, 10, 8, 256),
    DUN("C3-T-DXT5", "FORMAT_DXT5", M_, CF_TF_DXT5, 10, 6, 25, 10, 8, 256),
    DUN("C3-T-ETC1", "FORMAT_ETC1", M_, CF_TF_ETC1, 10, 6, 25, 10, 8, 256),
    DUN("C3-T-PVRTC4", "FORMAT_PVRTC4", M_, CF_TF_PVRTC4, 10, 6, 25, 10, 8, 256),
};
#define N_SCENES ((int)(sizeof(SCENES) / sizeof(SCENES[0])))

int cf_scene_count(void) { return N_SCENES; }
const cf_scene_def *cf_scene_get(int i) { return (i >= 0 && i < N_SCENES) ? &SCENES[i] : NULL; }

const cf_scene_def *cf_scene_find(const char *id)
{
    int i;
    for (i = 0; i < N_SCENES; i++)
        if (strcmp(SCENES[i].id, id) == 0) return &SCENES[i];
    return NULL;
}

/* ------------------------------------------------------- sizes / needs -- */
static int has_corridor(const cf_scene_def *d)
{
    return d->kind == CF_SC_DUNGEON || d->kind == CF_SC_BATTLE || d->kind == CF_SC_COMBINED;
}
static int has_battle(const cf_scene_def *d) { return d->kind == CF_SC_BATTLE || d->kind == CF_SC_COMBINED; }
static int has_dialogue(const cf_scene_def *d) { return d->kind == CF_SC_DIALOGUE || d->kind == CF_SC_COMBINED; }

uint64_t cf_scene_texture_bytes(const cf_scene_def *d)
{
    uint64_t t = 0;
    if (has_corridor(d))
        t += (uint64_t)d->materials * cf_tf_chain_bytes(d->texfmt, d->mat_dim, d->mat_dim, 1);
    if (has_battle(d)) {
        t += (uint64_t)d->enemies * cf_tf_chain_bytes(CF_TF_RGBA8, 512, 512, 1);
        if (d->particles) t += 64u * 64u * 4u;
        t += 4u * 256u * 512u * 4u;                       /* portraits */
    }
    if (has_dialogue(d)) {
        if (d->bg) t += 1024u * 1024u * 4u;
        t += (uint64_t)d->layers * 512u * 1024u * 4u + 256u * 64u * 4u;
    }
    if (d->kind == CF_SC_MENU) t += 256u * 256u * 4u;
    return t;
}

static uint32_t scratch_bytes(const cf_scene_def *d)
{
    uint32_t m = 0;
#define TAKE(v) do { uint32_t v_ = (v); if (v_ > m) m = v_; } while (0)
    if (has_corridor(d)) {
        uint32_t l0 = cf_tf_level_bytes(CF_TF_RGBA8, d->mat_dim, d->mat_dim);
        TAKE(d->texfmt == CF_TF_RGB565 ? l0 + l0 / 2 : l0);
    }
    if (has_battle(d)) TAKE(512u * 512u * 4u);
    if (has_dialogue(d)) TAKE(d->bg ? 1024u * 1024u * 4u : 512u * 1024u * 4u);
    if (d->kind == CF_SC_MENU) TAKE(256u * 256u * 4u);
#undef TAKE
    return m;
}

void cf_scene_need(const cf_scene_def *d, cf_need *need)
{
    uint64_t heap = scratch_bytes(d), vbo = 0;
    memset(need, 0, sizeof(*need));
    if (has_corridor(d)) {
        uint64_t pv = (uint64_t)(d->cols + 1) * (uint64_t)(d->rows + 1);
        uint64_t vb = (uint64_t)d->segs * d->pps * pv * sizeof(cf_vtx);
        uint64_t ib = (uint64_t)d->cols * d->rows * 6u * 2u;
        vbo = vb + ib;
        heap += vb + ib;                               /* CPU copies until uploaded */
    }
    if (has_battle(d)) {
        uint64_t ev = (uint64_t)(d->ecols + 1) * (uint64_t)(d->erows + 1);
        heap += (uint64_t)d->enemies * ev * (2u * sizeof(cf_vtx) + sizeof(float));
        heap += (uint64_t)d->ecols * d->erows * 6u * 2u;
        heap += (uint64_t)d->particles * 4u * sizeof(cf_v2);
    }
    if (d->kind == CF_SC_MENU) heap += (uint64_t)d->icons * 4u * sizeof(cf_v2);
    if (d->kind != CF_SC_EMPTY) heap += QUAD_BATCH * 6u * 2u;
    need->b[CF_POOL_VRAM] = cf_scene_texture_bytes(d) + vbo;
    need->b[CF_POOL_HEAP] = heap;
}

/* ---------------------------------------------------------------- state -- */
static struct {
    const cf_scene_def *d;
    uint32_t crc;
    uint8_t *scratch;
    /* corridor */
    GLuint vbo, ibo;
    int pieces, piece_verts, piece_idx;
    int base[MAX_PIECES], mat[MAX_PIECES], order[MAX_PIECES];
    GLuint mtex[32];
    /* enemies */
    int ne, everts, eidx_n;
    cf_vtx *estat[MAX_ENEMIES], *edyn[MAX_ENEMIES];
    float *ew[MAX_ENEMIES];
    float ex[MAX_ENEMIES];
    uint16_t *eidx;
    GLuint etex[MAX_ENEMIES];
    /* 2D */
    uint16_t *qidx;
    cf_v2 *pv;
    GLuint ptex, port[4], bgtex, ltex[MAX_LAYERS], boxtex, atlas;
    cf_v2 portv[16], flashv[4], bgv[4], lv[MAX_LAYERS * 4], boxv[4], panelv[4];
    cf_v2 *iv;
} S;

static void crc_add(const void *p, size_t n)
{
    S.crc = (uint32_t)crc32(S.crc, (const Bytef *)p, (uInt)n);
}

uint32_t cf_scene_crc(void) { return S.crc; }

static void clear_gl_errors(void)
{
    int guard = 64;
    while (glGetError() != GL_NO_ERROR && --guard > 0) { }
}

static void quad2d(cf_v2 *v, float x, float y, float w, float h, float u1, float v1, uint8_t r, uint8_t g,
                   uint8_t b, uint8_t a)
{
    const float xs[4] = {x, x + w, x + w, x};
    const float ys[4] = {y, y, y + h, y + h};
    const float us[4] = {0.0f, u1, u1, 0.0f};
    const float vs[4] = {0.0f, 0.0f, v1, v1};
    int i;
    for (i = 0; i < 4; i++) {
        v[i].x = xs[i]; v[i].y = ys[i]; v[i].u = us[i]; v[i].v = vs[i];
        v[i].r = r; v[i].g = g; v[i].b = b; v[i].a = a;
    }
}

/* Creates one texture from generated content. Returns 0 and sets *err on
 * failure (CF_ERR_UNSUPPORTED for a rejected compressed format). */
static GLuint make_texture(int w, int h, int fmt, int mips, int repeat, uint32_t seed, int style, gp_status *err,
                           char *why, size_t why_len)
{
    GLuint t = 0;
    GLenum e;
    clear_gl_errors();
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (!cf_tf_compressed(fmt)) {
        cf_tex_rgba(S.scratch, w, h, seed, style);
        if (fmt == CF_TF_RGB565) {
            uint16_t *p565 = (uint16_t *)(void *)(S.scratch + (size_t)w * (size_t)h * 4u);
            cf_tex_565(S.scratch, p565, w * h);
            crc_add(p565, (size_t)w * (size_t)h * 2u);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, p565);
        } else {
            crc_add(S.scratch, (size_t)w * (size_t)h * 4u);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, S.scratch);
        }
        if (mips) glGenerateMipmap(GL_TEXTURE_2D);
        e = glGetError();
        if (e != GL_NO_ERROR) {
            if (why) snprintf(why, why_len, "%s %dx%d upload: GL error 0x%X", cf_tf_name(fmt), w, h, (unsigned)e);
            *err = CF_ERR_GL;
        }
    } else {
        static const GLenum IFMT[CF_TF_COUNT] = {0, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT,
                                                 GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_ETC1_RGB8_OES,
                                                 GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG};
        int lw = w, lh = h, level = 0;
        e = GL_NO_ERROR;
        for (;;) {
            uint32_t bytes = cf_tf_level_bytes(fmt, lw, lh);
            cf_tex_blocks(S.scratch, bytes, seed ^ ((uint32_t)level * 0x01000193u), fmt);
            crc_add(S.scratch, bytes);
            glCompressedTexImage2D(GL_TEXTURE_2D, level, IFMT[fmt], lw, lh, 0, (GLsizei)bytes, S.scratch);
            e = glGetError();
            if (e != GL_NO_ERROR) {
                if (why)
                    snprintf(why, why_len, "%s level %d (%dx%d) rejected: GL error 0x%X", cf_tf_name(fmt), level,
                             lw, lh, (unsigned)e);
                *err = CF_ERR_UNSUPPORTED;
                break;
            }
            if (!mips || (lw == 1 && lh == 1)) break;
            lw = lw > 1 ? lw / 2 : 1;
            lh = lh > 1 ? lh / 2 : 1;
            level++;
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    if (*err != RP_OK) {
        glDeleteTextures(1, &t);
        return 0;
    }
    return t;
}

/* ------------------------------------------------------------- corridor -- */
static void piece_corners(int s, int k, int pps, float c[4][3], float *ul, float *vl)
{
    const float z0 = 2.0f - 4.0f * (float)s, z1 = z0 - 4.0f;
    float q[4][3];
    memset(q, 0, sizeof(q));
    *ul = 2.0f;
    *vl = 2.0f;
#define SET(i, X, Y, Z) do { q[i][0] = (X); q[i][1] = (Y); q[i][2] = (Z); } while (0)
    switch (k) {
    case 0: SET(0, -2.0f, 0.0f, z0); SET(1, 2.0f, 0.0f, z0); SET(2, 2.0f, 0.0f, z1); SET(3, -2.0f, 0.0f, z1); break;
    case 1: SET(0, -2.0f, 3.0f, z1); SET(1, 2.0f, 3.0f, z1); SET(2, 2.0f, 3.0f, z0); SET(3, -2.0f, 3.0f, z0); break;
    case 2: SET(0, -2.0f, 0.0f, z0); SET(1, -2.0f, 0.0f, z1); SET(2, -2.0f, 3.0f, z1); SET(3, -2.0f, 3.0f, z0);
            *vl = 1.5f; break;
    case 3: SET(0, 2.0f, 0.0f, z1); SET(1, 2.0f, 0.0f, z0); SET(2, 2.0f, 3.0f, z0); SET(3, 2.0f, 3.0f, z1);
            *vl = 1.5f; break;
    default: {
        /* inset wall panels, alternating left and right */
        int j = k - 4, slots = pps - 4, per_side = (slots + 1) / 2, slot = j >> 1;
        float len = 4.0f / (float)per_side;
        float za = z0 - (float)slot * len - 0.05f, zb = za - len * 0.9f;
        if ((j & 1) == 0) {
            SET(0, -1.9f, 0.3f, za); SET(1, -1.9f, 0.3f, zb); SET(2, -1.9f, 2.7f, zb); SET(3, -1.9f, 2.7f, za);
        } else {
            SET(0, 1.9f, 0.3f, zb); SET(1, 1.9f, 0.3f, za); SET(2, 1.9f, 2.7f, za); SET(3, 1.9f, 2.7f, zb);
        }
        *ul = 1.0f;
        *vl = 1.0f;
        break;
    }
    }
#undef SET
    memcpy(c, q, sizeof(q));
}

static gp_status build_corridor(const cf_scene_def *d, char *why, size_t why_len)
{
    const int pv = (d->cols + 1) * (d->rows + 1);
    const int pieces = d->segs * d->pps;
    cf_vtx *v;
    uint16_t *idx;
    gp_status err = RP_OK;
    int s, k, m, n;

    if (pieces > MAX_PIECES || d->materials > 32 || pv > 65535)
        return RP_ERR_ARG;
    v = (cf_vtx *)rp_malloc((size_t)pieces * (size_t)pv * sizeof(cf_vtx));
    idx = (uint16_t *)rp_malloc((size_t)d->cols * (size_t)d->rows * 6u * sizeof(uint16_t));
    if (!v || !idx) {
        rp_free(v);
        rp_free(idx);
        return RP_ERR_NOMEM;
    }
    for (s = 0; s < d->segs; s++) {
        for (k = 0; k < d->pps; k++) {
            int p = s * d->pps + k;
            float c[4][3], ul, vl;
            piece_corners(s, k, d->pps, c, &ul, &vl);
            cf_geom_quad(&v[(size_t)p * (size_t)pv], d->cols, d->rows, (const float(*)[3])c, ul, vl,
                         (uint8_t)(190 + (cf_hash32((uint32_t)p) & 63u)));
            S.base[p] = p * pv;
            S.mat[p] = (p * 5 + 1) % d->materials;
        }
    }
    cf_geom_grid_indices(idx, d->cols, d->rows, 0);
    crc_add(v, (size_t)pieces * (size_t)pv * sizeof(cf_vtx));
    crc_add(idx, (size_t)d->cols * (size_t)d->rows * 6u * sizeof(uint16_t));
    S.pieces = pieces;
    S.piece_verts = pv;
    S.piece_idx = d->cols * d->rows * 6;
    /* draw order: grouped by material (stable), so each material is bound once */
    n = 0;
    for (m = 0; m < d->materials; m++)
        for (k = 0; k < pieces; k++)
            if (S.mat[k] == m) S.order[n++] = k;

    clear_gl_errors();
    glGenBuffers(1, &S.vbo);
    glGenBuffers(1, &S.ibo);
    glBindBuffer(GL_ARRAY_BUFFER, S.vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)((size_t)pieces * (size_t)pv * sizeof(cf_vtx)), v, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, S.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)((size_t)S.piece_idx * sizeof(uint16_t)), idx, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    rp_free(v);
    rp_free(idx);
    if (glGetError() != GL_NO_ERROR) {
        if (why) snprintf(why, why_len, "vertex/index buffer upload failed");
        return CF_ERR_GL;
    }
    for (m = 0; m < d->materials && err == RP_OK; m++)
        S.mtex[m] = make_texture(d->mat_dim, d->mat_dim, d->texfmt, 1, 1, 1000u + (uint32_t)m, CF_STY_TILES, &err,
                                 why, why_len);
    return err;
}

/* -------------------------------------------------------------- battle -- */
static gp_status build_battle(const cf_scene_def *d, char *why, size_t why_len)
{
    const float rx = d->enemies == 1 ? 0.9f : (d->enemies == 3 ? 0.7f : 0.55f);
    const float step = d->enemies == 1 ? 0.0f : (d->enemies == 3 ? 1.2f : 0.8f);
    gp_status err = RP_OK;
    int e, i;

    if (d->enemies > MAX_ENEMIES || d->particles > QUAD_BATCH)
        return RP_ERR_ARG;
    S.ne = d->enemies;
    S.everts = (d->ecols + 1) * (d->erows + 1);
    S.eidx_n = d->ecols * d->erows * 6;
    S.eidx = (uint16_t *)rp_malloc((size_t)S.eidx_n * sizeof(uint16_t));
    if (!S.eidx) return RP_ERR_NOMEM;
    cf_geom_grid_indices(S.eidx, d->ecols, d->erows, 1);
    crc_add(S.eidx, (size_t)S.eidx_n * sizeof(uint16_t));
    for (e = 0; e < S.ne; e++) {
        S.estat[e] = (cf_vtx *)rp_malloc((size_t)S.everts * sizeof(cf_vtx));
        S.edyn[e] = (cf_vtx *)rp_malloc((size_t)S.everts * sizeof(cf_vtx));
        S.ew[e] = (float *)rp_malloc((size_t)S.everts * sizeof(float));
        if (!S.estat[e] || !S.edyn[e] || !S.ew[e]) return RP_ERR_NOMEM;
        cf_geom_ellipsoid(S.estat[e], S.ew[e], d->ecols, d->erows, rx, 1.2f, rx, (uint8_t)(200 + 10 * e));
        crc_add(S.estat[e], (size_t)S.everts * sizeof(cf_vtx));
        memcpy(S.edyn[e], S.estat[e], (size_t)S.everts * sizeof(cf_vtx));
        S.ex[e] = ((float)e - (float)(S.ne - 1) * 0.5f) * step;
        S.etex[e] = make_texture(512, 512, CF_TF_RGBA8, 1, 1, 2000u + (uint32_t)e, CF_STY_SKIN, &err, why, why_len);
        if (err != RP_OK) return err;
    }
    if (d->particles > 0) {
        S.pv = (cf_v2 *)rp_malloc((size_t)d->particles * 4u * sizeof(cf_v2));
        if (!S.pv) return RP_ERR_NOMEM;
        S.ptex = make_texture(64, 64, CF_TF_RGBA8, 0, 0, 2100u, CF_STY_DOT, &err, why, why_len);
        if (err != RP_OK) return err;
    }
    for (i = 0; i < 4 && err == RP_OK; i++) {
        S.port[i] = make_texture(256, 512, CF_TF_RGBA8, 0, 0, 2200u + (uint32_t)i, CF_STY_SILHOUETTE, &err, why,
                                 why_len);
        quad2d(&S.portv[i * 4], 24.0f + 136.0f * (float)i, 280.0f, 128.0f, 256.0f, 1.0f, 1.0f, 255, 255, 255, 255);
    }
    quad2d(S.flashv, 0.0f, 0.0f, (float)CF_SCREEN_W, (float)CF_SCREEN_H, 1.0f, 1.0f, 255, 255, 255, 64);
    crc_add(S.portv, sizeof(S.portv));
    return err;
}

/* ------------------------------------------------------------ dialogue -- */
static gp_status build_dialogue(const cf_scene_def *d, char *why, size_t why_len)
{
    gp_status err = RP_OK;
    int i;
    if (d->layers > MAX_LAYERS)
        return RP_ERR_ARG;
    if (d->bg) {
        S.bgtex = make_texture(1024, 1024, CF_TF_RGBA8, 0, 0, 3000u, CF_STY_BACKDROP, &err, why, why_len);
        quad2d(S.bgv, 0.0f, 0.0f, (float)CF_SCREEN_W, (float)CF_SCREEN_H, 960.0f / 1024.0f, 544.0f / 1024.0f, 255,
               255, 255, 255);
    }
    for (i = 0; i < d->layers && err == RP_OK; i++) {
        S.ltex[i] = make_texture(512, 1024, CF_TF_RGBA8, 0, 0, 3100u + (uint32_t)i, CF_STY_SILHOUETTE, &err, why,
                                 why_len);
        quad2d(&S.lv[i * 4], 40.0f + 210.0f * (float)i, 0.0f, 288.0f, 544.0f, 1.0f, 1.0f, 255, 255, 255, 255);
    }
    if (err == RP_OK) {
        S.boxtex = make_texture(256, 64, CF_TF_RGBA8, 0, 0, 3200u, CF_STY_PANEL, &err, why, why_len);
        quad2d(S.boxv, 20.0f, 394.0f, 920.0f, 140.0f, 1.0f, 1.0f, 255, 255, 255, 255);
    }
    crc_add(S.lv, sizeof(S.lv));
    return err;
}

/* ---------------------------------------------------------------- menu -- */
static gp_status build_menu(const cf_scene_def *d, char *why, size_t why_len)
{
    gp_status err = RP_OK;
    S.iv = (cf_v2 *)rp_malloc((size_t)d->icons * 4u * sizeof(cf_v2));
    if (!S.iv) return RP_ERR_NOMEM;
    S.atlas = make_texture(256, 256, CF_TF_RGBA8, 0, 0, 4000u, CF_STY_ATLAS, &err, why, why_len);
    quad2d(S.panelv, 0.0f, 0.0f, (float)CF_SCREEN_W, (float)CF_SCREEN_H, 1.0f, 1.0f, 24, 28, 52, 255);
    return err;
}

gp_status cf_scene_create(const cf_scene_def *d, char *why, size_t why_len)
{
    gp_status err = RP_OK;
    uint32_t sb;
    int q;

    if (why && why_len) why[0] = '\0';
    cf_scene_destroy();
    if (!d) return RP_ERR_ARG;
    S.d = d;
    S.crc = (uint32_t)crc32(0L, Z_NULL, 0);
    if (d->kind == CF_SC_EMPTY)
        return RP_OK;
    sb = scratch_bytes(d);
    S.scratch = (uint8_t *)rp_malloc(sb ? sb : 16u);
    S.qidx = (uint16_t *)rp_malloc(QUAD_BATCH * 6u * sizeof(uint16_t));
    if (!S.scratch || !S.qidx) err = RP_ERR_NOMEM;
    if (err == RP_OK) {
        for (q = 0; q < QUAD_BATCH; q++) {
            uint16_t b = (uint16_t)(q * 4);
            uint16_t *o = &S.qidx[q * 6];
            o[0] = b; o[1] = (uint16_t)(b + 1); o[2] = (uint16_t)(b + 2);
            o[3] = b; o[4] = (uint16_t)(b + 2); o[5] = (uint16_t)(b + 3);
        }
        crc_add(S.qidx, QUAD_BATCH * 6u * sizeof(uint16_t));
    }
    if (err == RP_OK && has_corridor(d)) err = build_corridor(d, why, why_len);
    if (err == RP_OK && has_battle(d)) err = build_battle(d, why, why_len);
    if (err == RP_OK && has_dialogue(d)) err = build_dialogue(d, why, why_len);
    if (err == RP_OK && d->kind == CF_SC_MENU) err = build_menu(d, why, why_len);
    rp_free(S.scratch);
    S.scratch = NULL;
    if (err != RP_OK) {
        if (err == RP_ERR_NOMEM && why && why[0] == '\0') snprintf(why, why_len, "CPU-side allocation failed");
        cf_scene_destroy();
    }
    return err;
}

void cf_scene_destroy(void)
{
    int i;
    if (!S.d) {
        memset(&S, 0, sizeof(S));
        return;
    }
    if (S.vbo) glDeleteBuffers(1, &S.vbo);
    if (S.ibo) glDeleteBuffers(1, &S.ibo);
    for (i = 0; i < 32; i++)
        if (S.mtex[i]) glDeleteTextures(1, &S.mtex[i]);
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (S.etex[i]) glDeleteTextures(1, &S.etex[i]);
        rp_free(S.estat[i]);
        rp_free(S.edyn[i]);
        rp_free(S.ew[i]);
    }
    for (i = 0; i < 4; i++)
        if (S.port[i]) glDeleteTextures(1, &S.port[i]);
    for (i = 0; i < MAX_LAYERS; i++)
        if (S.ltex[i]) glDeleteTextures(1, &S.ltex[i]);
    if (S.ptex) glDeleteTextures(1, &S.ptex);
    if (S.bgtex) glDeleteTextures(1, &S.bgtex);
    if (S.boxtex) glDeleteTextures(1, &S.boxtex);
    if (S.atlas) glDeleteTextures(1, &S.atlas);
    rp_free(S.eidx);
    rp_free(S.pv);
    rp_free(S.iv);
    rp_free(S.qidx);
    rp_free(S.scratch);
    memset(&S, 0, sizeof(S));
}

/* ------------------------------------------------------------- drawing -- */
static void set_persp(int32_t yaw, float zoff)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0, (double)CF_SCREEN_W / (double)CF_SCREEN_H, 0.1, 60.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef((float)yaw * (360.0f / 4096.0f), 0.0f, 1.0f, 0.0f);
    glTranslatef(0.0f, -1.5f, zoff);
}

static void set_ortho(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, CF_SCREEN_W, CF_SCREEN_H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void state_3d(void)
{
    static const GLfloat fog_col[4] = {0.02f, 0.02f, 0.04f, 1.0f};
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_BLEND);
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 6.0f);
    glFogf(GL_FOG_END, 40.0f);
    glFogfv(GL_FOG_COLOR, fog_col);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
}

static void state_2d(void)
{
    set_ortho();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FOG);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void draw_corridor(uint32_t frame, int moving)
{
    const GLsizei stride = (GLsizei)sizeof(cf_vtx);
    int32_t yaw = moving ? (cf_isin((int32_t)(frame * 17u)) * 91) >> 15 : (cf_isin((int32_t)(frame * 9u)) * 23) >> 15;
    float zoff = moving ? (float)(frame % 120u) * (4.0f / 120.0f) : 0.0f;
    int i, bound = -1;

    set_persp(yaw, zoff);
    state_3d();
    glBindBuffer(GL_ARRAY_BUFFER, S.vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, S.ibo);
    for (i = 0; i < S.pieces; i++) {
        int p = S.order[i];
        uintptr_t b = (uintptr_t)S.base[p] * sizeof(cf_vtx);   /* byte offset into the bound VBO */
        if (S.mat[p] != bound) {
            bound = S.mat[p];
            glBindTexture(GL_TEXTURE_2D, S.mtex[bound]);
        }
        glVertexPointer(3, GL_FLOAT, stride, (const void *)(b + offsetof(cf_vtx, x)));
        glTexCoordPointer(2, GL_FLOAT, stride, (const void *)(b + offsetof(cf_vtx, u)));
        glColorPointer(4, GL_UNSIGNED_BYTE, stride, (const void *)(b + offsetof(cf_vtx, r)));
        glDrawElements(GL_TRIANGLES, S.piece_idx, GL_UNSIGNED_SHORT, (const void *)0);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

/* CPU skinning: two rotations blended by the vertex weight, every frame. */
static void animate_enemies(uint32_t frame)
{
    int e, i;
    for (e = 0; e < S.ne; e++) {
        int32_t ph = e * 600;
        int32_t a0 = (cf_isin((int32_t)(frame * 23u) + ph) * 68) >> 15;
        int32_t a1 = (cf_isin((int32_t)(frame * 46u) + ph) * 284) >> 15;
        int32_t tl = (cf_isin((int32_t)(frame * 31u) + ph) * 57) >> 15;
        float c0 = cf_fcos(a0), s0 = cf_fsin(a0), c1 = cf_fcos(a1), s1 = cf_fsin(a1);
        float ct = cf_fcos(tl), st = cf_fsin(tl);
        float bob = 0.05f * cf_fsin((int32_t)(frame * 40u) + ph);
        /* R0 = Ry(a0); R1 = Rx(tl) * Ry(a1) */
        const float r0[9] = {c0, 0.0f, s0, 0.0f, 1.0f, 0.0f, -s0, 0.0f, c0};
        const float r1[9] = {c1, 0.0f, s1, st * s1, ct, -st * c1, -ct * s1, st, ct * c1};
        const cf_vtx *in = S.estat[e];
        cf_vtx *out = S.edyn[e];
        const float *w = S.ew[e];
        const float cx = S.ex[e];
        for (i = 0; i < S.everts; i++) {
            float x = in[i].x, y = in[i].y, z = in[i].z;
            float x0 = r0[0] * x + r0[1] * y + r0[2] * z;
            float y0 = r0[3] * x + r0[4] * y + r0[5] * z;
            float z0 = r0[6] * x + r0[7] * y + r0[8] * z;
            float x1 = r1[0] * x + r1[1] * y + r1[2] * z;
            float y1 = r1[3] * x + r1[4] * y + r1[5] * z;
            float z1 = r1[6] * x + r1[7] * y + r1[8] * z;
            float k = w[i];
            out[i].x = x0 + (x1 - x0) * k + cx;
            out[i].y = y0 + (y1 - y0) * k + 1.3f + bob * k;
            out[i].z = z0 + (z1 - z0) * k - 6.0f;
        }
    }
}

static void draw_enemies(void)
{
    const GLsizei stride = (GLsizei)sizeof(cf_vtx);
    int e;
    for (e = 0; e < S.ne; e++) {
        glBindTexture(GL_TEXTURE_2D, S.etex[e]);
        glVertexPointer(3, GL_FLOAT, stride, &S.edyn[e][0].x);
        glTexCoordPointer(2, GL_FLOAT, stride, &S.edyn[e][0].u);
        glColorPointer(4, GL_UNSIGNED_BYTE, stride, &S.edyn[e][0].r);
        glDrawElements(GL_TRIANGLES, S.eidx_n, GL_UNSIGNED_SHORT, S.eidx);
    }
}

/* 2D quads, textured or not; 'quads' may exceed one batch. */
static void draw_quads(const cf_v2 *v, int quads, GLuint tex)
{
    const GLsizei stride = (GLsizei)sizeof(cf_v2);
    int done = 0;
    if (tex) {
        glEnable(GL_TEXTURE_2D);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glBindTexture(GL_TEXTURE_2D, tex);
    } else {
        glDisable(GL_TEXTURE_2D);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }
    while (done < quads) {
        int n = quads - done > QUAD_BATCH ? QUAD_BATCH : quads - done;
        const cf_v2 *b = v + (size_t)done * 4u;
        glVertexPointer(2, GL_FLOAT, stride, &b->x);
        if (tex) glTexCoordPointer(2, GL_FLOAT, stride, &b->u);
        glColorPointer(4, GL_UNSIGNED_BYTE, stride, &b->r);
        glDrawElements(GL_TRIANGLES, n * 6, GL_UNSIGNED_SHORT, S.qidx);
        done += n;
    }
}

static void update_particles(uint32_t frame, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        uint32_t h = cf_hash32((uint32_t)i + 77u);
        int32_t ang = (int32_t)(((uint32_t)i * 397u + frame * (8u + (uint32_t)(i % 5))) & 4095u);
        float rad = 60.0f + (float)(h % 160u);
        float cx = 480.0f + rad * cf_fcos(ang), cy = 230.0f + rad * 0.6f * cf_fsin(ang);
        quad2d(&S.pv[i * 4], cx - 8.0f, cy - 8.0f, 16.0f, 16.0f, 1.0f, 1.0f, 255, 255, 255, 255);
    }
}

static void update_icons(uint32_t frame, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        int col = i % 64, row = i / 64;
        uint32_t cell = ((uint32_t)i * 7u) & 255u;
        float x = (float)(col * 15), y = (float)((row * 15 + (int)(frame * 2u)) % CF_SCREEN_H);
        float u0 = (float)(cell & 15u) / 16.0f, v0 = (float)(cell >> 4) / 16.0f;
        cf_v2 *q = &S.iv[i * 4];
        quad2d(q, x, y, 12.0f, 12.0f, 1.0f, 1.0f, 255, 255, 255, 255);
        q[0].u = q[3].u = u0;
        q[1].u = q[2].u = u0 + 1.0f / 16.0f;
        q[0].v = q[1].v = v0;
        q[2].v = q[3].v = v0 + 1.0f / 16.0f;
    }
}

static void draw_dialogue(int layers, int bg)
{
    if (bg) {
        glDisable(GL_BLEND);
        draw_quads(S.bgv, 1, S.bgtex);
    }
    glEnable(GL_BLEND);
    {
        int i;
        for (i = 0; i < layers; i++)
            draw_quads(&S.lv[i * 4], 1, S.ltex[i]);
    }
    draw_quads(S.boxv, 1, S.boxtex);
}

void cf_scene_draw(uint32_t frame)
{
    const cf_scene_def *d = S.d;
    int i;
    if (!d || d->kind == CF_SC_EMPTY)
        return;
    switch (d->kind) {
    case CF_SC_DUNGEON:
        draw_corridor(frame, 1);
        break;
    case CF_SC_BATTLE:
    case CF_SC_COMBINED:
        draw_corridor(frame, 0);
        animate_enemies(frame);
        draw_enemies();
        state_2d();
        glEnable(GL_BLEND);
        if (d->particles) {
            update_particles(frame, d->particles);
            draw_quads(S.pv, d->particles, S.ptex);
        }
        for (i = 0; i < 4; i++)
            draw_quads(&S.portv[i * 4], 1, S.port[i]);
        if (d->flash) draw_quads(S.flashv, 1, 0);
        if (d->kind == CF_SC_COMBINED) draw_dialogue(d->layers, 0);
        break;
    case CF_SC_DIALOGUE:
        state_2d();
        draw_dialogue(d->layers, d->bg);
        break;
    case CF_SC_MENU:
        state_2d();
        glDisable(GL_BLEND);
        draw_quads(S.panelv, 1, 0);
        glEnable(GL_BLEND);
        update_icons(frame, d->icons);
        draw_quads(S.iv, d->icons, S.atlas);
        break;
    default:
        break;
    }
    /* leave the state hud2d expects */
    glDisable(GL_TEXTURE_2D);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    glDisable(GL_FOG);
    glDisable(GL_CULL_FACE);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}
