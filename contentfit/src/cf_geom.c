/*
 * cf_geom.c - deterministic synthetic content (see cf_geom.h).
 * Built with -ffp-contract=off. Uses no libm functions.
 */
#include <string.h>

#include "cf_geom.h"

/* sin(pi * t), t = a / 2048 in [0, 1), by Bhaskara I's rational
 * approximation 16p / (5 - 4p) with p = t(1 - t), in 64-bit integers. */
static int32_t half_sine(int32_t a)
{
    int64_t p = (int64_t)a * (2048 - a);
    int64_t v = (16 * p * 32768) / (5LL * 2048 * 2048 - 4 * p);
    return v > 32767 ? 32767 : (int32_t)v;
}

int32_t cf_isin(int32_t a)
{
    a &= 4095;
    return a < 2048 ? half_sine(a) : -half_sine(a - 2048);
}

int32_t cf_icos(int32_t a) { return cf_isin(a + 1024); }
float cf_fsin(int32_t a) { return (float)cf_isin(a) / 32768.0f; }
float cf_fcos(int32_t a) { return (float)cf_icos(a) / 32768.0f; }

uint32_t cf_hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

void cf_geom_quad(cf_vtx *v, int cols, int rows, const float c[4][3], float u_len, float v_len, uint8_t shade)
{
    int i, j;
    for (j = 0; j <= rows; j++) {
        float t = (float)j / (float)rows;
        for (i = 0; i <= cols; i++) {
            float s = (float)i / (float)cols;
            cf_vtx *o = &v[j * (cols + 1) + i];
            float b[3], tp[3];
            int k;
            for (k = 0; k < 3; k++) {
                b[k] = c[0][k] + (c[1][k] - c[0][k]) * s;
                tp[k] = c[3][k] + (c[2][k] - c[3][k]) * s;
            }
            o->x = b[0] + (tp[0] - b[0]) * t;
            o->y = b[1] + (tp[1] - b[1]) * t;
            o->z = b[2] + (tp[2] - b[2]) * t;
            o->u = s * u_len;
            o->v = t * v_len;
            o->r = o->g = o->b = shade;
            o->a = 255;
        }
    }
}

void cf_geom_grid_indices(uint16_t *idx, int cols, int rows, int flip)
{
    int i, j, n = 0;
    for (j = 0; j < rows; j++) {
        for (i = 0; i < cols; i++) {
            uint16_t a = (uint16_t)(j * (cols + 1) + i);
            uint16_t b = (uint16_t)(a + 1);
            uint16_t d = (uint16_t)(a + cols + 1);
            uint16_t c = (uint16_t)(d + 1);
            if (!flip) {
                idx[n++] = a; idx[n++] = b; idx[n++] = c;
                idx[n++] = a; idx[n++] = c; idx[n++] = d;
            } else {
                idx[n++] = a; idx[n++] = d; idx[n++] = c;
                idx[n++] = a; idx[n++] = c; idx[n++] = b;
            }
        }
    }
}

void cf_geom_ellipsoid(cf_vtx *v, float *weight, int cols, int rows, float rx, float ry, float rz, uint8_t shade)
{
    int i, j;
    for (j = 0; j <= rows; j++) {
        int32_t lat = -1024 + (j * 2048) / rows;
        float cl = cf_fcos(lat), sl = cf_fsin(lat);
        float w = (float)(j * j) / (float)(rows * rows);
        for (i = 0; i <= cols; i++) {
            int32_t lon = (i * 4096) / cols;
            cf_vtx *o = &v[j * (cols + 1) + i];
            o->x = rx * cl * cf_fcos(lon);
            o->y = ry * sl;
            o->z = rz * cl * cf_fsin(lon);
            o->u = (float)i / (float)cols;
            o->v = (float)j / (float)rows;
            o->r = o->g = o->b = shade;
            o->a = 255;
            weight[j * (cols + 1) + i] = w;
        }
    }
}

/* ------------------------------------------------------------ formats -- */
const char *cf_tf_name(int fmt)
{
    static const char *const N[CF_TF_COUNT] = {"RGBA8", "RGB565", "DXT1", "DXT5", "ETC1", "PVRTC4"};
    return (fmt >= 0 && fmt < CF_TF_COUNT) ? N[fmt] : "?";
}

int cf_tf_compressed(int fmt) { return fmt >= CF_TF_DXT1 && fmt < CF_TF_COUNT; }

uint32_t cf_tf_level_bytes(int fmt, int w, int h)
{
    uint32_t bw = (uint32_t)((w + 3) / 4), bh = (uint32_t)((h + 3) / 4);
    switch (fmt) {
    case CF_TF_RGBA8:  return (uint32_t)w * (uint32_t)h * 4u;
    case CF_TF_RGB565: return (uint32_t)w * (uint32_t)h * 2u;
    case CF_TF_DXT1:
    case CF_TF_ETC1:   return bw * bh * 8u;
    case CF_TF_DXT5:   return bw * bh * 16u;
    case CF_TF_PVRTC4: return (uint32_t)(w < 8 ? 8 : w) * (uint32_t)(h < 8 ? 8 : h) / 2u;
    default:           return 0;
    }
}

uint32_t cf_tf_chain_bytes(int fmt, int w, int h, int mips)
{
    uint32_t total = 0;
    for (;;) {
        total += cf_tf_level_bytes(fmt, w, h);
        if (!mips || (w == 1 && h == 1))
            break;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    return total;
}

/* ----------------------------------------------------------- textures -- */
static void put(uint8_t *p, uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    p[0] = (uint8_t)(r > 255 ? 255 : r);
    p[1] = (uint8_t)(g > 255 ? 255 : g);
    p[2] = (uint8_t)(b > 255 ? 255 : b);
    p[3] = (uint8_t)(a > 255 ? 255 : a);
}

static int64_t sq(int64_t v) { return v * v; }

void cf_tex_rgba(uint8_t *px, int w, int h, uint32_t seed, int style)
{
    int x, y;
    uint32_t tint = cf_hash32(seed * 2654435761u + 17u);
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            uint8_t *p = &px[((size_t)y * (size_t)w + (size_t)x) * 4u];
            uint32_t n = cf_hash32(seed ^ ((uint32_t)x * 7919u + (uint32_t)y * 104729u)) & 15u;
            switch (style) {
            case CF_STY_TILES: {
                int bw = w / 4, bh = h / 8, row = y / bh;
                int xo = (x + ((row & 1) ? bw / 2 : 0)) % w;
                int col = xo / bw, mortar = (xo % bw) < 3 || (y % bh) < 3;
                uint32_t t = cf_hash32(tint ^ (uint32_t)(row * 31 + col));
                if (mortar)
                    put(p, 60 + n, 58 + n, 55 + n, 255);
                else
                    put(p, 110 + (t & 63) + n, 95 + ((t >> 8) & 47) + n, 80 + ((t >> 16) & 47) + n, 255);
                break;
            }
            case CF_STY_SILHOUETTE: {
                int64_t cx = w / 2, by = (h * 62) / 100, rx = (w * 36) / 100, ry = (h * 36) / 100;
                int64_t hy = (h * 20) / 100, hr = (w * 17) / 100;
                int64_t body = sq(x - cx) * sq(ry) + sq(y - by) * sq(rx);
                int64_t lim = sq(rx) * sq(ry);
                int64_t head = sq(x - cx) + sq(y - hy);
                if (body <= lim || head <= sq(hr))
                    put(p, 120 + (tint & 63) + (uint32_t)(y * 64 / h), 90 + ((tint >> 8) & 63), 140 + n, 255);
                else if (body <= lim + lim / 8 || head <= sq(hr + 3))
                    put(p, 60, 40, 70, 128);
                else
                    put(p, 0, 0, 0, 0);
                break;
            }
            case CF_STY_DOT: {
                int64_t r2 = sq(w / 2), d2 = sq(x - w / 2) + sq(y - h / 2);
                uint32_t a = d2 >= r2 ? 0u : (uint32_t)(255 - (255 * d2) / r2);
                put(p, 255, 220, 160, a);
                break;
            }
            case CF_STY_ATLAS: {
                int cx = x % 16, cy = y % 16, cell = (y / 16) * (w / 16) + x / 16;
                uint32_t t = cf_hash32(tint ^ (uint32_t)cell);
                int inside = (t & 1) ? (sq(cx - 8) + sq(cy - 8) <= 36) : (cx >= 3 && cx < 13 && cy >= 3 && cy < 13);
                put(p, 80 + (t & 127), 80 + ((t >> 8) & 127), 80 + ((t >> 16) & 127), inside ? 255u : 0u);
                break;
            }
            case CF_STY_PANEL: {
                int border = x < 4 || y < 4 || x >= w - 4 || y >= h - 4;
                put(p, 20 + n, 30 + n, 70 + (uint32_t)(y * 40 / h), border ? 255u : 200u);
                break;
            }
            case CF_STY_BACKDROP: {
                uint32_t band = (uint32_t)(y * 8 / h);
                uint32_t blk = cf_hash32(tint ^ (uint32_t)((y / 32) * 64 + x / 32)) & 31u;
                put(p, 30 + band * 10 + blk + n, 25 + band * 8 + n, 50 + band * 6 + blk, 255);
                break;
            }
            default: { /* CF_STY_SKIN */
                uint32_t cell = cf_hash32(tint ^ (uint32_t)((y / 8) * 512 + x / 8));
                put(p, 90 + (cell & 63) + n, 60 + ((cell >> 6) & 31) + n, 70 + ((cell >> 12) & 63), 255);
                break;
            }
            }
        }
    }
}

void cf_tex_565(const uint8_t *rgba, uint16_t *out, int pixels)
{
    int i;
    for (i = 0; i < pixels; i++) {
        const uint8_t *p = &rgba[(size_t)i * 4u];
        out[i] = (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
    }
}

static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v); wr16(p + 2, v >> 16); }

void cf_tex_blocks(uint8_t *dst, uint32_t bytes, uint32_t seed, int fmt)
{
    uint32_t off, i = 0;
    const uint32_t bsz = fmt == CF_TF_DXT5 ? 16u : 8u;
    for (off = 0; off + bsz <= bytes; off += bsz, i++) {
        uint32_t h1 = cf_hash32(seed ^ (i * 2654435761u));
        uint32_t h2 = cf_hash32(h1 + 0x9E3779B9u);
        uint8_t *b = dst + off;
        switch (fmt) {
        case CF_TF_DXT1:   /* color0 > color1: four-colour block; index rows 0,1,2,3 */
            wr16(b, 0x8000u | (h1 & 0x7FFFu));
            wr16(b + 2, h2 & 0x7FFFu);
            wr32(b + 4, 0xE4E4E4E4u ^ (h1 & 0x01010101u));
            break;
        case CF_TF_DXT5:   /* alpha block, then a DXT1-style colour block */
            b[0] = 255;
            b[1] = (uint8_t)(h1 & 127u);
            wr32(b + 2, 0x49249249u ^ (h2 & 0x00FF0000u));
            wr16(b + 6, (h2 >> 8) & 0xFFFFu);
            wr16(b + 8, 0x8000u | (h1 & 0x7FFFu));
            wr16(b + 10, h2 & 0x7FFFu);
            wr32(b + 12, 0xE4E4E4E4u ^ (h2 & 0x02020202u));
            break;
        default:           /* ETC1 and PVRTC4: every 64-bit word decodes to some colours */
            wr32(b, (h1 & 0xF8F8F8FCu) | 0x02000000u);
            wr32(b + 4, 0xE4E4E4E4u ^ (h2 & 0x01010101u));
            break;
        }
    }
    for (; off < bytes; off++)
        dst[off] = 0;
}
