/*
 * rs_synth.c - synthetic packs for the worker-slot rule (see rs_synth.h).
 *
 * Each pack is a GDAT archive whose entries are wrapped exactly as
 * RESOURCE_PIPELINE.md section 4 describes: a 128-byte zero header, the
 * "BILZ" block (big-endian sizes, constant 7), and a zlib stream (Phase 8's
 * rp_deflate_alloc, level 9) of a CFP1 envelope (kind 2, index i) followed by
 * the VRS1 payload. Entry 0 is the table of contents.
 *
 *   toc_big       255 one-texel RGBA8 textures (P = 68): the table's payload
 *                 (8,208) is larger than every resource. U = 8,240.
 *   tie_minus1    four one-texel textures and one level of P = 207 (3 x 53
 *   tie_equal     cells), 208 (10 x 16) or 209 (7 x 23); the table's payload
 *   tie_plus1     is 208. U = 240, 240, 241.
 *   decl32_*      three bare 32-byte resources (header only, valid CRCs; the
 *                 rows declare payload_size 32); the named kind is entry 1.
 *                 U = 176, and entry 1 must fail its own check (RS_ERR_RANGE),
 *                 not for lack of buffer space.
 * The expected values below were worked out by hand from sections 5 and 8.2;
 * the builder also recomputes U and refuses to write a pack that disagrees.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

#include "rp_inflate.h"
#include "rp_mem.h"
#include "rs_synth.h"

#define SYNTH_DIR "app0:fixtures/synth"

static void wr16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void wrbe32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static uint32_t crc(const uint8_t *p, size_t n)
{
    return (uint32_t)crc32(crc32(0uL, Z_NULL, 0), p, (uInt)n);
}

static uint8_t *payload(uint32_t kind, uint32_t id, const uint8_t *body, size_t blen, size_t *plen)
{
    uint8_t *p = (uint8_t *)calloc(1, 32 + blen + 1);
    if (!p)
        return NULL;
    memcpy(p, "VRS1", 4);
    wr16(p + 4, 1);
    wr16(p + 6, kind);
    wr32(p + 8, id);
    wr32(p + 12, (uint32_t)blen);
    if (blen)
        memcpy(p + 32, body, blen);
    wr32(p + 16, crc(p + 32, blen));
    wr32(p + 28, crc(p, 28));
    *plen = 32 + blen;
    return p;
}

typedef struct {
    uint32_t kind, fmt, vram, heap;
    const uint8_t *body;
    size_t blen;
} res_spec;

/* One-texel RGBA8 texture, one level, repeat + linear: body 36 bytes (P = 68). */
static const uint8_t TEX1[36] = {1, 0, 1, 0, 1, 0, 1, 0, 3, 0, 0, 0, 0, 0, 0, 0,
                                 32, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                 0x80, 0x80, 0x80, 0xFF};

/* A level of w x d open cells, start (0, 0), cell 2.0, wall 2.5: body 16 + w x d. */
static uint8_t *level_body(uint32_t w, uint32_t d, size_t *len)
{
    uint8_t *b = (uint8_t *)calloc(1, 16u + (size_t)w * d);
    if (!b)
        return NULL;
    wr16(b, w);
    wr16(b + 2, d);
    wr32(b + 8, 0x40000000u);
    wr32(b + 12, 0x40200000u);
    *len = 16u + (size_t)w * d;
    return b;
}

static int write_pack(const char *path, const res_spec *res, uint32_t n, uint32_t *u_out, uint32_t *rows_u_out,
                      uint32_t *ut_out)
{
    const uint32_t count = n + 1u;
    uint8_t **pay = (uint8_t **)calloc(count, sizeof *pay);
    uint8_t **reg = (uint8_t **)calloc(count, sizeof *reg);
    size_t *plen = (size_t *)calloc(count, sizeof *plen), *rlen = (size_t *)calloc(count, sizeof *rlen);
    uint8_t *tocb = (uint8_t *)calloc(1, 16u + 32u * (size_t)n), head[8];
    uint64_t tv = 0, th = 0, off;
    uint32_t i, max_p = 0;
    FILE *f = NULL;
    int ok = pay && reg && plen && rlen && tocb;

    for (i = 0; ok && i < n; i++) {
        uint8_t *row = tocb + 16u + 32u * i;
        pay[i + 1] = payload(res[i].kind, i + 1, res[i].body, res[i].blen, &plen[i + 1]);
        ok = pay[i + 1] != NULL;
        if (!ok)
            break;
        wr32(row, i + 1);
        wr16(row + 4, res[i].kind);
        wr16(row + 6, res[i].fmt);
        wr32(row + 8, (uint32_t)plen[i + 1]);
        wr32(row + 12, crc(pay[i + 1], plen[i + 1]));
        wr32(row + 16, res[i].vram);
        wr32(row + 20, res[i].heap);
        tv += res[i].vram;
        th += res[i].heap;
        if (plen[i + 1] > max_p)
            max_p = (uint32_t)plen[i + 1];
    }
    if (ok) {
        wr32(tocb, n);
        wr32(tocb + 4, (uint32_t)tv);
        wr32(tocb + 8, (uint32_t)th);
        pay[0] = payload(1, 0, tocb, 16u + 32u * (size_t)n, &plen[0]);
        ok = pay[0] != NULL;
    }
    for (i = 0; ok && i < count; i++) {          /* CFP1 envelope + payload, zlib, wrapper */
        const size_t raw_len = 32 + plen[i];
        uint8_t *raw = (uint8_t *)calloc(1, raw_len), *z = NULL;
        size_t zlen = 0;
        ok = raw != NULL;
        if (ok) {
            memcpy(raw, "CFP1", 4);
            wr32(raw + 4, 2);
            wr32(raw + 16, (uint32_t)plen[i]);
            wr32(raw + 20, crc(pay[i], plen[i]));
            wr32(raw + 24, i);
            memcpy(raw + 32, pay[i], plen[i]);
            ok = rp_deflate_alloc(raw, raw_len, 9, &z, &zlen) == RP_OK;
        }
        if (ok) {
            reg[i] = (uint8_t *)calloc(1, 0x90 + zlen);
            ok = reg[i] != NULL;
        }
        if (ok) {
            memcpy(reg[i] + 0x80, "BILZ", 4);
            wrbe32(reg[i] + 0x84, (uint32_t)raw_len);
            wrbe32(reg[i] + 0x88, (uint32_t)zlen);
            wrbe32(reg[i] + 0x8C, 7);
            memcpy(reg[i] + 0x90, z, zlen);
            rlen[i] = 0x90 + zlen;
        }
        rp_free(z);
        free(raw);
    }
    if (ok) {
        f = fopen(path, "wb");
        ok = f != NULL;
    }
    if (ok) {
        memcpy(head, "GDAT", 4);
        wr32(head + 4, count);
        ok = fwrite(head, 1, 8, f) == 8;
        off = 8u + 8u * (uint64_t)count;
        for (i = 0; ok && i < count; i++) {
            uint8_t pair[8];
            wr32(pair, (uint32_t)off);
            wr32(pair + 4, (uint32_t)rlen[i]);
            ok = fwrite(pair, 1, 8, f) == 8;
            off += rlen[i];
        }
        for (i = 0; ok && i < count; i++)
            ok = fwrite(reg[i], 1, rlen[i], f) == rlen[i];
    }
    if (f && fclose(f) != 0)
        ok = 0;
    if (ok) {
        const uint32_t toc_p = (uint32_t)plen[0];
        *u_out = (max_p > toc_p ? max_p : toc_p) + 32u;       /* section 8.2 */
        *rows_u_out = max_p + 32u;
        *ut_out = toc_p + 32u;
    }
    for (i = 0; pay && reg && i < count; i++) {
        free(pay[i]);
        free(reg[i]);
    }
    free(pay);
    free(reg);
    free(plen);
    free(rlen);
    free(tocb);
    return ok ? 0 : -1;
}

int rs_synth_build_all(rs_synth_case *cases)
{
    static const struct {
        const char *name;
        int pc, pe, fc, fe;
        uint32_t u, rows_u, ut;
    } EXPECT[RS_SYNTH_CASES] = {
        {"toc_big", 0, -1, 0, -1, 8240, 100, 8240},
        {"tie_minus1", 319, 5, 0, -1, 240, 239, 240},
        {"tie_equal", 319, 5, 0, -1, 240, 240, 240},
        {"tie_plus1", 319, 5, 0, -1, 241, 241, 240},
        {"decl32_texture", 308, 1, 308, 1, 176, 64, 176},
        {"decl32_mesh", 308, 1, 308, 1, 176, 64, 176},
        {"decl32_level", 308, 1, 308, 1, 176, 64, 176},
    };
    static const uint32_t TIE[3][2] = {{3, 53}, {10, 16}, {7, 23}};
    res_spec *res = (res_spec *)calloc(255, sizeof *res);
    uint32_t i, k;
    int ok = res != NULL;

    if (mkdir(SYNTH_DIR, 0755) != 0 && errno != EEXIST)
        ok = 0;
    for (k = 0; ok && k < RS_SYNTH_CASES; k++) {
        rs_synth_case *c = &cases[k];
        uint8_t *lvl = NULL;
        size_t lvl_len = 0;
        uint32_t n = 0, u = 0, rows_u = 0, ut = 0;
        memset(c, 0, sizeof *c);
        c->name = EXPECT[k].name;
        snprintf(c->path, sizeof c->path, SYNTH_DIR "/%s.gdat", c->name);
        c->policy_code = EXPECT[k].pc;
        c->policy_entry = EXPECT[k].pe;
        c->format_code = EXPECT[k].fc;
        c->format_entry = EXPECT[k].fe;
        c->expect_u = EXPECT[k].u;
        c->rows_only_u = EXPECT[k].rows_u;
        c->toc_inflated = EXPECT[k].ut;
        if (k == 0) {
            for (i = 0; i < 255; i++) {
                res[i].kind = 2; res[i].fmt = 1; res[i].vram = 4; res[i].heap = 0;
                res[i].body = TEX1; res[i].blen = sizeof TEX1;
            }
            n = 255;
        } else if (k <= 3) {
            for (i = 0; i < 4; i++) {
                res[i].kind = 2; res[i].fmt = 1; res[i].vram = 4; res[i].heap = 0;
                res[i].body = TEX1; res[i].blen = sizeof TEX1;
            }
            lvl = level_body(TIE[k - 1][0], TIE[k - 1][1], &lvl_len);
            ok = lvl != NULL;
            res[4].kind = 4; res[4].fmt = 0; res[4].vram = 0; res[4].heap = (uint32_t)lvl_len;
            res[4].body = lvl; res[4].blen = lvl_len;
            n = 5;
        } else {
            static const uint32_t ORDER[3][3] = {{2, 3, 4}, {3, 4, 2}, {4, 2, 3}};
            for (i = 0; i < 3; i++) {
                const uint32_t kind = ORDER[k - 4][i];
                res[i].kind = kind; res[i].fmt = kind == 2 ? 1u : 0u; res[i].vram = 0; res[i].heap = 0;
                res[i].body = NULL; res[i].blen = 0;
            }
            n = 3;
        }
        if (ok)
            ok = write_pack(c->path, res, n, &u, &rows_u, &ut) == 0;
        if (ok && (u != c->expect_u || rows_u != c->rows_only_u || ut != c->toc_inflated)) {
            fprintf(stderr, "rs_synth: %s computed U %u rows %u Ut %u, expected %u %u %u\n", c->name, u, rows_u, ut,
                    c->expect_u, c->rows_only_u, c->toc_inflated);
            ok = 0;
        }
        free(lvl);
    }
    free(res);
    return ok ? 0 : -1;
}
