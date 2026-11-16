/*
 * cf_geom.h - deterministic synthetic geometry and texture content.
 *
 * Everything is generated from integer arithmetic (an integer sine table,
 * integer hashing) followed by plain float +,-,*,/ (compiled with
 * -ffp-contract=off), so the same content and the same CRCs come out on the
 * PC and on the Vita. No libm functions are used here.
 */
#ifndef CF_GEOM_H
#define CF_GEOM_H

#include "cf_common.h"

/* 3D vertex: 24 bytes, same layout as VitaBench's. */
typedef struct {
    float   x, y, z, u, v;
    uint8_t r, g, b, a;
} cf_vtx;

/* 2D vertex (screen pixels): 20 bytes. */
typedef struct {
    float   x, y, u, v;
    uint8_t r, g, b, a;
} cf_v2;

/* Angles in 1/4096 of a turn. Q15 results (32767 = 1.0). */
int32_t cf_isin(int32_t a);
int32_t cf_icos(int32_t a);
float   cf_fsin(int32_t a);     /* cf_isin / 32768 */
float   cf_fcos(int32_t a);

uint32_t cf_hash32(uint32_t x);

/* Tessellated quad: (cols+1) x (rows+1) vertices between corners c0..c3
 * (c0 -> c1 is the first axis, c0 -> c3 the second). */
void cf_geom_quad(cf_vtx *v, int cols, int rows, const float c[4][3], float u_len, float v_len, uint8_t shade);

/* Indices for a cols x rows grid, 6 per cell. flip = 0: (a,b,c)(a,c,d);
 * flip = 1: (a,d,c)(a,c,b). */
void cf_geom_grid_indices(uint16_t *idx, int cols, int rows, int flip);

/* Ellipsoid on a cols x rows longitude/latitude grid, centred at the origin.
 * weight[i] = (row / rows)^2: 0 at the bottom, 1 at the top. */
void cf_geom_ellipsoid(cf_vtx *v, float *weight, int cols, int rows, float rx, float ry, float rz, uint8_t shade);

/* Texture formats. */
enum { CF_TF_RGBA8 = 0, CF_TF_RGB565, CF_TF_DXT1, CF_TF_DXT5, CF_TF_ETC1, CF_TF_PVRTC4, CF_TF_COUNT };
const char *cf_tf_name(int fmt);
int         cf_tf_compressed(int fmt);
uint32_t    cf_tf_level_bytes(int fmt, int w, int h);
uint32_t    cf_tf_chain_bytes(int fmt, int w, int h, int mips);   /* all levels down to 1x1 when mips */

/* RGBA8 content styles. */
enum { CF_STY_TILES = 0, CF_STY_SILHOUETTE, CF_STY_DOT, CF_STY_ATLAS, CF_STY_PANEL, CF_STY_BACKDROP, CF_STY_SKIN };
void cf_tex_rgba(uint8_t *px, int w, int h, uint32_t seed, int style);

/* RGBA8 -> RGB565 (native-endian 16-bit, red in the high bits). */
void cf_tex_565(const uint8_t *rgba, uint16_t *out, int pixels);

/* Synthetic compressed blocks for one level (any byte pattern is a valid
 * block in these formats; the content is structured, not random). */
void cf_tex_blocks(uint8_t *dst, uint32_t bytes, uint32_t seed, int fmt);

#endif /* CF_GEOM_H */
