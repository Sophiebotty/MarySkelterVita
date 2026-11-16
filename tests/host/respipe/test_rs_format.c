/*
 * test_rs_format.c - stage 10.3 PC tests of src/resource/rs_format.c
 * (RESOURCE_PIPELINE.md, VRS1 version 1).
 *
 * Run from a work directory whose app0:fixtures/ holds the stage 10.2
 * fixtures and is writable (tests/host/run_respipe_host_tests.sh sets this
 * up). Built with MAIN_START_X / MAIN_START_Z taken from src/main.c.
 *
 *   1. unit checks: codes and names, CRC constants, A1 and A5 limits, the
 *      start cell against src/main.c, argument checks, accessors;
 *   2. every valid fixture pack through section 6.1 (rs_host_pipe.c);
 *   3. all 50 section 10 cases: code and GDAT entry;
 *   4. every room pack's mesh against level_mesh_build()'s own output
 *      (src/graphics/level_mesh.c, unmodified, through a GL stand-in);
 *   5. every header byte of every pack flipped in the file: the GDAT index
 *      and each entry's 0x90-byte wrapper; each must be rejected;
 *   6. every header byte of every entry flipped after inflation (CFP1
 *      envelope, VRS1 header, the kind's body header): "raw" (must be
 *      rejected) and "fix" (CRCs and the table-of-contents row made
 *      consistent, so the structural checks are reached);
 *   7. synthetic resources at each limit of sections 5.3-5.8 and one step
 *      past it (sizes, counts, floats, start cell, policy, totals);
 *   8. synthetic packs for the section 8.2 slot rule (rs_synth.c): a table
 *      of contents larger than every resource, the one-byte tie boundary,
 *      and rows declaring 32-byte payloads, with and without the room rule.
 * Every result is also written to argv[1] for compare_rs_results.py, which
 * compares it with expected.json, RESOURCE_PIPELINE.md and the independent
 * Python checker. Exit status 0 only when every check passed.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "gameplay/room_map.h"
#include "graphics/level_mesh.h"
#include "cf_pack.h"
#include "rs_fixtures.h"
#include "rs_format.h"
#include "rs_gl_capture.h"
#include "rs_host_pipe.h"
#include "rs_host_plat.h"
#include "rs_synth.h"

#if !defined(MAIN_START_X) || !defined(MAIN_START_Z)
#error "build with -DMAIN_START_X / -DMAIN_START_Z taken from src/main.c"
#endif

#define FIX "app0:fixtures/"
#define FLIP_PATH FIX "_flip.gdat"

static int g_pass, g_fail;
static FILE *g_out;

static void check(int ok, const char *fmt, ...)
{
    va_list ap;
    if (ok) {
        g_pass++;
        return;
    }
    g_fail++;
    printf("FAIL: ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t crc(const uint8_t *p, size_t n)
{
    return (uint32_t)crc32(crc32(0uL, Z_NULL, 0), p, (uInt)n);
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf = NULL;
    long n;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        buf = (uint8_t *)malloc(n ? (size_t)n : 1u);
        if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
            free(buf);
            buf = NULL;
        }
        *len = (size_t)n;
    }
    fclose(f);
    return buf;
}

static const char *stage_or_dash(const rs_pipe_result *r)
{
    return r->stage && r->stage[0] ? r->stage : "-";
}

/* ------------------------------------------------------------------ collecting entries */
typedef struct {
    uint32_t  n;
    uint8_t  *raw[RS_MAX_ENTRIES];
    size_t    len[RS_MAX_ENTRIES];
    /* the mesh, if any */
    int       mesh_found;
    uint8_t  *verts, *idx;
    size_t    vsize, isize;
    uint32_t  ranges;
    rs_mesh_range rg[RS_MAX_RANGES];
} collected;

static void collect(void *ctx, uint32_t e, const uint8_t *region, uint32_t region_size, const uint8_t *raw,
                    size_t len, const rs_resource *r)
{
    collected *c = (collected *)ctx;
    uint32_t i;
    (void)region;
    (void)region_size;
    if (e < RS_MAX_ENTRIES) {
        c->raw[e] = (uint8_t *)malloc(len);
        memcpy(c->raw[e], raw, len);
        c->len[e] = len;
        if (e + 1u > c->n)
            c->n = e + 1u;
    }
    if (r && r->kind == RS_KIND_MESH && !c->mesh_found) {
        c->mesh_found = 1;
        c->vsize = 24u * (size_t)r->mesh.vertex_count;
        c->isize = 2u * (size_t)r->mesh.index_count;
        c->verts = (uint8_t *)malloc(c->vsize);
        c->idx = (uint8_t *)malloc(c->isize);
        memcpy(c->verts, r->body + r->mesh.vertex_offset, c->vsize);
        memcpy(c->idx, r->body + r->mesh.index_offset, c->isize);
        c->ranges = r->mesh.range_count;
        for (i = 0; i < r->mesh.range_count; i++)
            rs_mesh_range_get(r, i, &c->rg[i]);
    }
}

static void collected_free(collected *c)
{
    uint32_t i;
    for (i = 0; i < c->n; i++)
        free(c->raw[i]);
    free(c->verts);
    free(c->idx);
    memset(c, 0, sizeof *c);
}

/* ------------------------------------------------------------------ 1. unit checks */
static void unit_checks(void)
{
    static const char *const SPEC_NAMES[21] = {
        "RS_ERR_MAGIC", "RS_ERR_VERSION", "RS_ERR_KIND", "RS_ERR_HEADER_CRC", "RS_ERR_BODY_SIZE",
        "RS_ERR_BODY_CRC", "RS_ERR_RESERVED", "RS_ERR_ENUM", "RS_ERR_RANGE", "RS_ERR_ALIGN",
        "RS_ERR_DIMENSION", "RS_ERR_MIP_CHAIN", "RS_ERR_LEVEL_SIZE", "RS_ERR_INDEX", "RS_ERR_FLOAT",
        "RS_ERR_TOC", "RS_ERR_DUP_ID", "RS_ERR_REF", "RS_ERR_LIMIT", "RS_ERR_LEVEL_MISMATCH",
        "RS_ERR_ENVELOPE"};
    static const uint8_t zeros[124];
    uint8_t env[32];
    rs_diag d;
    rs_resource r;
    rs_tex_level tl;
    rs_mesh_range mr;
    int c;

    for (c = 299; c <= 321; c++) {
        const char *n = rs_status_name(c);
        if (c < RS_ERR_BASE || c >= RS_ERR_END)
            check(n == NULL, "rs_status_name(%d) should be NULL", c);
        else {
            check(n && strcmp(n, SPEC_NAMES[c - RS_ERR_BASE]) == 0, "name of %d", c);
            fprintf(g_out, "CODE %d %s\n", c, n ? n : "?");
        }
    }
    check(RS_ERR_MAGIC == 300 && RS_ERR_TOC == 315 && RS_ERR_LEVEL_MISMATCH == 319 && RS_ERR_ENVELOPE == 320,
          "code numbers");
    check(crc((const uint8_t *)"123456789", 9) == 0xCBF43926u, "CRC-32 check value");
    check(crc(zeros, sizeof zeros) == RS_ZERO124_CRC, "CRC-32 of 124 zero bytes");
    check(RS_MAX_PAYLOAD == 8388576u && RS_MAX_REGION == 8388608u && RS_MAX_INFLATED == 8388608u, "limits");
    check(RS_ROOM_START_X == MAIN_START_X && RS_ROOM_START_Z == MAIN_START_Z,
          "start cell %u,%u differs from src/main.c %d,%d", RS_ROOM_START_X, RS_ROOM_START_Z, MAIN_START_X,
          MAIN_START_Z);
    check(rs_check_entry_count(0, &d) == RS_ERR_LIMIT && rs_check_entry_count(1, &d) == RS_ERR_LIMIT &&
          rs_check_entry_count(2, &d) == RP_OK && rs_check_entry_count(256, &d) == RP_OK &&
          rs_check_entry_count(257, &d) == RS_ERR_LIMIT, "A1 boundaries");
    memset(env, 0, sizeof env);
    memcpy(env, "CFP1", 4);
    env[4] = 2;
    check(rs_check_envelope(NULL, 32, &d) == RP_ERR_ARG && rs_check_envelope(env, 31, &d) == RP_ERR_ARG &&
          rs_check_envelope(env, 32, &d) == RP_OK, "rs_check_envelope arguments");
    env[4] = 1;
    check(rs_check_envelope(env, 32, &d) == RS_ERR_ENVELOPE, "rs_check_envelope kind 1");
    check(rs_check_toc_again(NULL, 32, 0, &d) == RP_ERR_ARG && rs_check_toc_again(env, 31, 0, &d) == RP_ERR_ARG,
          "rs_check_toc_again arguments");
    check(rs_check_wrapper_record(NULL, &d) == RP_ERR_ARG && rs_check_wrapper_header(NULL, &d) == RP_ERR_ARG,
          "wrapper arguments");
    check(rs_check_toc(NULL, 48, 2, NULL, &d) == RP_ERR_ARG, "rs_check_toc arguments");
    check(rs_cfp1_field(NULL, 0, 0) != NULL && strcmp(rs_cfp1_field(env, 31, 0), "length") == 0,
          "rs_cfp1_field length");
    {   /* section 4.5's order, one field at a time and with two bad fields at once */
        static const struct { const char *want; int magic, reserved, index, length, kind, crc_bad, kind1; } F[] = {
            {NULL, 0, 0, 0, 0, 0, 0, 0},        {NULL, 0, 0, 0, 0, 0, 0, 1},
            {"magic", 1, 0, 0, 0, 0, 0, 0},      {"magic", 1, 1, 0, 0, 0, 0, 0},
            {"reserved", 0, 1, 0, 0, 0, 0, 0},   {"reserved", 0, 1, 1, 0, 0, 0, 0},
            {"index", 0, 0, 1, 0, 0, 0, 0},      {"index", 0, 0, 1, 1, 0, 0, 0},
            {"body length", 0, 0, 0, 1, 0, 0, 0}, {"body length", 0, 0, 0, 1, 1, 0, 0},
            {"kind", 0, 0, 0, 0, 1, 0, 0},       {"kind", 0, 0, 0, 0, 1, 1, 0},
            {"body CRC", 0, 0, 0, 0, 0, 1, 0},
        };
        size_t k;
        for (k = 0; k < sizeof F / sizeof F[0]; k++) {
            uint8_t e2[36];
            const char *got;
            memset(e2, 0, sizeof e2);
            memcpy(e2, F[k].magic ? "CFP2" : "CFP1", 4);
            wr32(e2 + 4, F[k].kind ? 3u : F[k].kind1 ? 1u : 2u);
            if (F[k].kind1) { wr32(e2 + 8, 1); wr32(e2 + 12, 1); }   /* kind 1: 1x1 RGBA8 = 4 bytes */
            wr32(e2 + 16, F[k].length ? 5u : 4u);
            memcpy(e2 + 32, "body", 4);
            wr32(e2 + 20, crc(e2 + 32, 4) ^ (F[k].crc_bad ? 1u : 0u));
            wr32(e2 + 24, F[k].index ? 2u : 1u);
            wr32(e2 + 28, F[k].reserved ? 1u : 0u);
            got = rs_cfp1_field(e2, sizeof e2, 1);
            check(F[k].want ? (got && strcmp(got, F[k].want) == 0) : got == NULL,
                  "rs_cfp1_field case %u: %s, expected %s", (unsigned)k, got ? got : "none",
                  F[k].want ? F[k].want : "none");
            check((cf_payload_check(e2, sizeof e2, 1, 1, NULL) == RP_OK) == (F[k].want == NULL),
                  "rs_cfp1_field case %u disagrees with cf_payload_check", (unsigned)k);
        }
    }
    memset(&r, 0, sizeof r);
    check(rs_texture_level(&r, 0, &tl) == -1 && rs_mesh_range_get(&r, 0, &mr) == -1 &&
          rs_texture_level(NULL, 0, &tl) == -1, "accessors refuse other kinds");
}

/* A5 and C.3 argument checks on the real example table of contents. */
static void toc_unit_checks(void)
{
    collected c;
    rs_pipe_result res;
    rs_toc toc;
    rs_diag d;
    rs_row row;
    static const uint8_t hdr[0x80];

    memset(&c, 0, sizeof c);
    rs_pipe_run(FIX "example.gdat", 0, 0, collect, &c, &res);
    check(res.code == 0 && c.n == 4, "example.gdat format-only run: %d", res.code);
    if (res.code != 0 || c.n != 4) {
        collected_free(&c);
        return;
    }
    check(rs_check_toc(c.raw[0] + 32, c.len[0] - 32, 4, &toc, &d) == RP_OK, "example table of contents");
    check(rs_check_toc(c.raw[0] + 32, c.len[0] - 32, 5, &toc, &d) == RS_ERR_TOC, "entry_count vs archive");
    check(rs_check_toc(c.raw[0] + 32, c.len[0] - 32, 4, &toc, &d) == RP_OK, "re-check");
    rs_toc_row(&toc, 0, &row);
    check(row.resource_id == 1 && row.kind == 2 && row.format == 3 && row.payload_size == 152, "row 0");
    /* A5: U = 152 + 32 = 184, compressed limit 184 + 0 + 64 = 248 */
    check(rs_check_region(&toc, 1, 0x90 + 248, &d) == RP_OK, "A5 at the limit");
    check(rs_check_region(&toc, 1, 0x90 + 249, &d) == RS_ERR_LIMIT, "A5 one over");
    check(rs_check_region(&toc, 1, RS_MAX_REGION + 1u, &d) == RS_ERR_LIMIT, "A5 region over 8 MiB");
    check(rs_check_region(&toc, 1, 0x8F, &d) == RP_OK, "A5 leaves short regions to the wrapper check");
    check(rs_check_region(&toc, 0, 0x90, &d) == RP_ERR_ARG && rs_check_region(&toc, 4, 0x90, &d) == RP_ERR_ARG,
          "A5 entry arguments");
    check(rs_check_entry(hdr, c.raw[1], c.len[1], &toc, 0, 1, NULL, &d) == RP_ERR_ARG &&
          rs_check_entry(hdr, c.raw[1], c.len[1], &toc, 4, 1, NULL, &d) == RP_ERR_ARG &&
          rs_check_entry(hdr, c.raw[1], 31, &toc, 1, 1, NULL, &d) == RP_ERR_ARG,
          "rs_check_entry arguments");
    check(rs_check_entry(hdr, c.raw[1], c.len[1], &toc, 1, 1, NULL, &d) == RP_OK, "entry 1 with out == NULL");
    check(rs_check_entry(hdr, c.raw[3], c.len[3], &toc, 3, 0, NULL, &d) == RP_OK &&
          rs_check_entry(hdr, c.raw[3], c.len[3], &toc, 3, 1, NULL, &d) == RS_ERR_LEVEL_MISMATCH,
          "3x3 level: valid VRS1, refused by the policy");
    collected_free(&c);
}

/* ------------------------------------------------------------------ 2. valid packs */
typedef struct {
    uint32_t textures, levels_seen, meshes, levels;
    uint64_t level_bytes;
    int bad;
} resource_tally;

static void tally(void *ctx, uint32_t e, const uint8_t *region, uint32_t region_size, const uint8_t *raw,
                  size_t len, const rs_resource *r)
{
    resource_tally *t = (resource_tally *)ctx;
    rs_tex_level tl;
    uint32_t i;
    (void)e; (void)region; (void)region_size; (void)raw; (void)len;
    if (!r)
        return;
    if (r->kind == RS_KIND_TEXTURE) {
        t->textures++;
        for (i = 0; i < r->tex.mip_count; i++) {
            if (rs_texture_level(r, i, &tl) != 0 || tl.data < r->body || tl.data + tl.size > r->body + r->body_size)
                t->bad = 1;
            t->levels_seen++;
            t->level_bytes += tl.size;
        }
        if (rs_texture_level(r, r->tex.mip_count, &tl) != -1)
            t->bad = 1;
    } else if (r->kind == RS_KIND_MESH) {
        t->meshes++;
    } else if (r->kind == RS_KIND_LEVEL) {
        t->levels++;
    }
}

static void valid_packs(void)
{
    int i;
    for (i = 0; i < RS_FIX_PACK_COUNT; i++) {
        const rs_fix_pack *p = &RS_FIX_PACKS[i];
        char path[160];
        rs_pipe_result r;
        resource_tally t;
        int code, entry;
        const char *stage;
        memset(&t, 0, sizeof t);
        snprintf(path, sizeof path, FIX "%s", p->path);
        rs_pipe_run(path, 1, 0, tally, &t, &r);
        code = r.code;
        entry = r.entry;
        stage = stage_or_dash(&r);          /* a string literal: stays valid */
        check(r.code == p->expect_code && r.entry == p->expect_entry,
              "%s: %d %s entry %d (%s), expected %d entry %d", p->path, r.code, rs_pipe_status_name(r.code),
              r.entry, r.check, p->expect_code, p->expect_entry);
        if (r.code != 0)        /* example.gdat: its totals come from the format-only run */
            rs_pipe_run(path, 0, 0, NULL, NULL, &r);
        check(r.code == 0 && r.n == p->entries && r.vram == p->vram_bytes && r.heap_need == p->heap_need,
              "%s: entries %u vram %u heap need %llu (fixture %u %u %u)", p->path, r.n, r.vram,
              (unsigned long long)r.heap_need, p->entries, p->vram_bytes, p->heap_need);
        check(r.max_inflated == (r.row_inflated > r.toc_inflated ? r.row_inflated : r.toc_inflated),
              "%s: U %u is not max(largest row %u, table of contents %u) (section 8.2)", p->path, r.max_inflated,
              r.row_inflated, r.toc_inflated);
        check(!t.bad, "%s: texture level accessors", p->path);
        fprintf(g_out, "PACK %s %d %d %s %u %u %llu\n", p->path, code, entry, stage, r.n, r.vram,
                (unsigned long long)r.heap_need);
        printf("  %-18s %s; %u entries, %u textures (%u levels), %u mesh, %u level\n", p->path,
               code ? rs_pipe_status_name(code) : "accepted", r.n, t.textures, t.levels_seen, t.meshes, t.levels);
    }
}

/* ------------------------------------------------------------------ 3. the 50 cases */
static void cases(void)
{
    int i, ok = 0;
    for (i = 0; i < RS_FIX_CASE_COUNT; i++) {
        const rs_fix_pack *c = &RS_FIX_CASES[i];
        char path[160];
        rs_pipe_result r;
        snprintf(path, sizeof path, FIX "%s", c->path);
        rs_pipe_run(path, 1, 0, NULL, NULL, &r);
        check(r.code == c->expect_code && r.entry == c->expect_entry, "%s: %d %s entry %d (%s), expected %d entry %d",
              c->id, r.code, rs_pipe_status_name(r.code), r.entry, r.check, c->expect_code, c->expect_entry);
        ok += r.code == c->expect_code && r.entry == c->expect_entry;
        fprintf(g_out, "CASE %s %s %d %d %s %s\n", c->id, c->path, r.code, r.entry, stage_or_dash(&r), r.check);
        printf("  %s %3d %-22s %-5s entry %2d  %s\n", c->id, r.code, rs_pipe_status_name(r.code), stage_or_dash(&r),
               r.entry, r.check);
    }
    printf("  cases: %d of %d give the expected code at the expected entry\n", ok, RS_FIX_CASE_COUNT);
}

/* ------------------------------------------------------------------ 4. the room mesh */
static void room_mesh(void)
{
    static const GLuint TEX[TEX_COUNT] = {101, 102, 103};
    static const uint32_t PACK_TEX_ID[TEX_COUNT] = {1, 2, 4};   /* wall, floor, ceiling in the room packs */
    const rs_gl_capture *g;
    int vc, i, m, packs = 0;

    rs_gl_reset();
    vc = level_mesh_build();
    check(vc == 976 && level_mesh_vertex_count() == 976 && level_mesh_triangle_count() == 488,
          "level_mesh_build: %d vertices, %d triangles", vc, level_mesh_triangle_count());
    check(level_mesh_upload_vbo() == 0 && level_mesh_vbo_available(), "level_mesh_upload_vbo");
    level_mesh_draw(TEX, 1, 1);
    g = rs_gl_get();
    check(g->vertex_size == 976u * 24u && g->index_size == 1464u * 2u && g->draws == TEX_COUNT && !g->overflow,
          "captured %zu vertex bytes, %zu index bytes, %d draws", g->vertex_size, g->index_size, g->draws);
    for (i = 0; i < RS_FIX_PACK_COUNT; i++) {
        const rs_fix_pack *p = &RS_FIX_PACKS[i];
        char path[160];
        collected c;
        rs_pipe_result r;
        int same;
        if (strncmp(p->path, "room", 4) && strncmp(p->path, "fmt_", 4) && strncmp(p->path, "area8", 5))
            continue;
        memset(&c, 0, sizeof c);
        snprintf(path, sizeof path, FIX "%s", p->path);
        rs_pipe_run(path, 1, 0, collect, &c, &r);
        check(r.code == 0 && c.mesh_found, "%s: no mesh collected", p->path);
        if (!c.mesh_found) {
            collected_free(&c);
            continue;
        }
        same = c.vsize == g->vertex_size && memcmp(c.verts, g->vertex_data, c.vsize) == 0 &&
               c.isize == g->index_size && memcmp(c.idx, g->index_data, c.isize) == 0 &&
               c.ranges == (uint32_t)g->draws;
        for (m = 0; same && m < g->draws; m++) {
            const rs_gl_draw *dr = &g->draw[m];
            same = dr->mode == GL_TRIANGLES && dr->type == GL_UNSIGNED_SHORT && dr->vbo_bound &&
                   dr->texture == TEX[m] && (uint32_t)dr->count == c.rg[m].index_count &&
                   dr->offset == 2u * (uintptr_t)c.rg[m].first_index && c.rg[m].texture_id == PACK_TEX_ID[m];
        }
        check(same, "%s: room mesh differs from level_mesh_build()", p->path);
        packs += same;
        collected_free(&c);
    }
    printf("  level_mesh_build(): 976 vertices (%zu bytes), 1464 indices (%zu bytes), %d draw ranges;\n"
           "  identical byte for byte in %d room packs (vertices, indices, ranges, texture order)\n",
           g->vertex_size, g->index_size, g->draws, packs);
    level_mesh_free();
    rs_gl_reset();
}

/* ------------------------------------------------------------------ 7. boundaries */
/* Synthetic resources built here, each at a limit of sections 5.3-5.7 or one
 * step past it, checked as entry 1 of a two-entry pack whose table of contents
 * describes it. Expected codes come from the document's rules. */
static uint64_t spec_level_size(uint32_t fmt, uint32_t w, uint32_t h)
{
    const uint64_t bw = (w + 3u) / 4u, bh = (h + 3u) / 4u;
    switch (fmt) {
    case 1: return 4u * (uint64_t)w * h;
    case 2: return 2u * (uint64_t)w * h;
    case 3: case 5: return 8u * bw * bh;
    case 4: return 16u * bw * bh;
    default: return (uint64_t)(w < 8u ? 8u : w) * (h < 8u ? 8u : h) / 2u;
    }
}

static uint8_t *vrs1_payload(uint32_t kind, uint32_t id, const uint8_t *body, size_t blen, size_t *plen)
{
    uint8_t *p = (uint8_t *)calloc(1, 32 + blen);
    memcpy(p, "VRS1", 4);
    p[4] = 1;
    p[6] = (uint8_t)kind;
    wr32(p + 8, id);
    wr32(p + 12, (uint32_t)blen);
    if (blen)
        memcpy(p + 32, body, blen);
    wr32(p + 16, crc(p + 32, blen));
    wr32(p + 28, crc(p, 28));
    *plen = 32 + blen;
    return p;
}

/* Table of contents with 'n' rows of 32 bytes (already filled in rows). */
static uint8_t *toc_payload(const uint8_t *rows, uint32_t n, uint32_t total_vram, uint32_t total_heap, size_t *len)
{
    uint8_t *body = (uint8_t *)calloc(1, 16u + 32u * (size_t)n), *p;
    wr32(body, n);
    wr32(body + 4, total_vram);
    wr32(body + 8, total_heap);
    memcpy(body + 16, rows, 32u * (size_t)n);
    p = vrs1_payload(RS_KIND_TOC, 0, body, 16u + 32u * (size_t)n, len);
    free(body);
    return p;
}

static void put_row(uint8_t *r, uint32_t id, uint32_t kind, uint32_t fmt, uint32_t size, uint32_t pcrc, uint32_t vram,
                    uint32_t heap)
{
    memset(r, 0, 32);
    wr32(r, id);
    r[4] = (uint8_t)kind;
    r[6] = (uint8_t)fmt;
    wr32(r + 8, size);
    wr32(r + 12, pcrc);
    wr32(r + 16, vram);
    wr32(r + 20, heap);
}

/* Runs a VRS1 payload through B6 and C.3 as entry 1 of a two-entry pack: the CFP1 envelope matches
 * the payload; the table-of-contents row carries row_crc. -1000 if that table itself is refused. */
static int entry_check(uint32_t kind, uint32_t fmt, const uint8_t *pay, size_t plen, uint32_t row_crc,
                       uint32_t vram, uint32_t heap, int policy)
{
    static const uint8_t zero_hdr[0x80];
    size_t tlen;
    uint8_t *raw, *tp, row[32];
    rs_toc t;
    rs_diag d;
    int st;

    raw = (uint8_t *)malloc(32 + plen);
    memset(raw, 0, 32);
    memcpy(raw, "CFP1", 4);
    raw[4] = 2;
    wr32(raw + 16, (uint32_t)plen);
    wr32(raw + 20, crc(pay, plen));
    wr32(raw + 24, 1);
    memcpy(raw + 32, pay, plen);
    put_row(row, 1, kind, fmt, (uint32_t)plen, row_crc, vram, heap);
    tp = toc_payload(row, 1, vram, heap, &tlen);
    st = rs_check_toc(tp, tlen, 2, &t, &d);
    if (st == RP_OK) {
        st = cf_payload_check(raw, 32 + plen, 1, 1, NULL);
        if (st == RP_OK)
            st = rs_check_entry(zero_hdr, raw, 32 + plen, &t, 1, policy, NULL, &d);
    } else {
        st = -1000;
    }
    free(raw);
    free(tp);
    return st;
}

static int as_entry(uint32_t kind, uint32_t fmt, const uint8_t *body, size_t blen, uint32_t vram, uint32_t heap,
                    int policy)
{
    size_t plen;
    uint8_t *pay = vrs1_payload(kind, 1, body, blen, &plen);
    int st = entry_check(kind, fmt, pay, plen, crc(pay, plen), vram, heap, policy);
    free(pay);
    return st;
}

/* Two defects at once: the result shows which check runs first (sections 6.2 and 6.1 C.3).
 * A 1x1 level (body 17 bytes) unless stated. */
static int order_case(char variant)
{
    uint8_t b[17];
    size_t plen;
    uint8_t *pay;
    uint32_t row_crc;
    int st;
    memset(b, 0, sizeof b);
    b[0] = 1; b[2] = 1;
    wr32(b + 8, 0x40000000u);
    wr32(b + 12, 0x40200000u);
    pay = vrs1_payload(RS_KIND_LEVEL, 1, b, sizeof b, &plen);
    row_crc = crc(pay, plen);
    switch (variant) {
    case 'b': pay[3] = '2'; pay[4] = 2; wr32(pay + 28, crc(pay, 28)); break;       /* magic, version */
    case 'c': pay[4] = 2; break;                                                    /* version, header CRC */
    case 'd': pay[6] = 9; break;                                                    /* header CRC, kind */
    case 'e': pay[6] = 9; pay[20] = 1; wr32(pay + 28, crc(pay, 28)); break;         /* kind, reserved */
    case 'f': pay[20] = 1; wr32(pay + 12, 18); wr32(pay + 28, crc(pay, 28)); break; /* reserved, body_size */
    case 'g': wr32(pay + 12, 18); pay[48] ^= 1; wr32(pay + 28, crc(pay, 28)); break; /* body_size, body CRC */
    /* C.3.3 row CRC vs C.3.4 magic. The header CRC is left stale on purpose: a header whose own
     * CRC-32 is valid always gives the same payload CRC (CRC-32's fixed residue), so recomputing it
     * would leave the row CRC matching and C.3.3 passing. */
    case 'i': pay[0] = 'X'; break;
    default: break;
    }
    if (variant != 'i')
        row_crc = crc(pay, plen);
    st = entry_check(RS_KIND_LEVEL, 0, pay, plen, row_crc, 0, 17, 0);
    free(pay);
    return st;
}

/* Texture body at the defined layout; n levels of the formula's size (zero texels). */
static uint8_t *tex_body(uint32_t fmt, uint32_t w, uint32_t h, uint32_t n, uint32_t flags, size_t tail, size_t *len)
{
    uint64_t end = 16u + 8u * (uint64_t)n, size = end;
    uint32_t i;
    uint8_t *b;
    for (i = 0; i < n; i++)
        size = ((size + 15u) & ~(uint64_t)15u) + spec_level_size(fmt, w >> i ? w >> i : 1u, h >> i ? h >> i : 1u);
    b = (uint8_t *)calloc(1, (size_t)size + tail);
    b[0] = (uint8_t)fmt;
    b[2] = (uint8_t)w; b[3] = (uint8_t)(w >> 8);
    b[4] = (uint8_t)h; b[5] = (uint8_t)(h >> 8);
    b[6] = (uint8_t)n;
    wr32(b + 8, flags);
    for (i = 0; i < n; i++) {
        const uint64_t ls = spec_level_size(fmt, w >> i ? w >> i : 1u, h >> i ? h >> i : 1u);
        end = (end + 15u) & ~(uint64_t)15u;
        wr32(b + 16 + 8 * i, (uint32_t)end);
        wr32(b + 20 + 8 * i, (uint32_t)ls);
        end += ls;
    }
    *len = (size_t)size + tail;
    return b;
}

static int tex_case(uint32_t fmt, uint32_t w, uint32_t h, uint32_t n, uint32_t flags, size_t tail, uint32_t vram)
{
    size_t len;
    uint8_t *b = tex_body(fmt, w, h, n, flags, tail, &len);
    int st = as_entry(RS_KIND_TEXTURE, fmt >= 1 && fmt <= 6 ? fmt : 1, b, len, vram, 0, 1);
    free(b);
    return st;
}

/* Mesh body at the defined layout: vc zero vertices, ic zero indices, nr equal ranges of 3. */
static uint8_t *mesh_body(uint32_t vc, uint32_t ic, uint32_t nr, size_t *len)
{
    const uint64_t vo = (32u + 16u * (uint64_t)nr + 15u) & ~(uint64_t)15u;
    const uint64_t io = (vo + 24u * (uint64_t)vc + 15u) & ~(uint64_t)15u;
    uint8_t *b = (uint8_t *)calloc(1, (size_t)(io + 2u * (uint64_t)ic));
    uint32_t i;
    wr32(b, vc);
    wr32(b + 4, ic);
    b[8] = 1;
    b[10] = 24;
    b[12] = (uint8_t)nr;
    b[13] = (uint8_t)(nr >> 8);
    b[14] = 1;
    wr32(b + 16, 32);
    wr32(b + 20, (uint32_t)vo);
    wr32(b + 24, (uint32_t)io);
    for (i = 0; i < nr; i++) {
        wr32(b + 32 + 16 * i, 3u * i);
        wr32(b + 36 + 16 * i, i + 1u < nr ? 3u : ic - 3u * i);
    }
    *len = (size_t)(io + 2u * (uint64_t)ic);
    return b;
}

/* float_bits (if non-zero) is written at byte float_at of the vertex block. */
static int mesh_case_at(uint32_t vc, uint32_t ic, uint32_t nr, uint32_t float_bits, uint32_t float_at, int last_index_vc)
{
    size_t len;
    uint8_t *b = mesh_body(vc, ic, nr, &len);
    const uint32_t vo = le32(b + 20), io = le32(b + 24);
    int st;
    if (float_bits && vc)
        wr32(b + vo + float_at, float_bits);
    if (last_index_vc && ic) {
        b[io + 2u * (ic - 1u)] = (uint8_t)vc;
        b[io + 2u * (ic - 1u) + 1u] = (uint8_t)(vc >> 8);
    }
    st = as_entry(RS_KIND_MESH, 0, b, len, 24u * vc + 2u * ic, 0, 1);
    free(b);
    return st;
}

static int mesh_case(uint32_t vc, uint32_t ic, uint32_t nr, uint32_t first_float_bits, int last_index_vc)
{
    return mesh_case_at(vc, ic, nr, first_float_bits, 0, last_index_vc);
}

/* Level body: w x d open cells (or room_map's grid when room != 0). */
static int level_case(uint32_t w, uint32_t d, uint32_t sx, uint32_t sz, uint32_t cs_bits, uint32_t wh_bits,
                      int start_solid, int bad_cell, int room, int policy)
{
    const size_t len = 16u + (size_t)w * d;
    uint8_t *b = (uint8_t *)calloc(1, len + 1u);
    uint32_t x, z;
    int st;
    b[0] = (uint8_t)w; b[1] = (uint8_t)(w >> 8);
    b[2] = (uint8_t)d; b[3] = (uint8_t)(d >> 8);
    b[4] = (uint8_t)sx; b[6] = (uint8_t)sz;
    wr32(b + 8, cs_bits);
    wr32(b + 12, wh_bits);
    if (room)
        for (z = 0; z < d; z++)
            for (x = 0; x < w; x++)
                b[16 + z * w + x] = (uint8_t)room_is_solid((int)x, (int)z);
    if (start_solid && sx < w && sz < d)
        b[16 + sz * w + sx] = 1;
    if (bad_cell && len > 16u)
        b[len - 1u] = 2;
    st = as_entry(RS_KIND_LEVEL, 0, b, len, 0, (uint32_t)len, policy);
    free(b);
    return st;
}

/* Table of contents with n_rows rows (ids 1..n); its total is the 32-bit sum, which wraps if the
 * rows' vram_bytes add up past 32 bits. */
static int toc_case(uint32_t n_rows, uint32_t n_archive, uint32_t kind, uint32_t fmt, uint32_t size, uint32_t vram_each)
{
    uint8_t *rows = (uint8_t *)calloc(n_rows ? n_rows : 1u, 32), *p;
    uint64_t total = 0;
    uint32_t i;
    size_t len;
    rs_toc t;
    rs_diag d;
    int st;
    for (i = 0; i < n_rows; i++) {
        put_row(rows + 32 * i, i + 1u, kind, fmt, size, 0, vram_each, 0);
        total += vram_each;
    }
    p = toc_payload(rows, n_rows, (uint32_t)total, 0, &len);
    st = rs_check_toc(p, len, n_archive, &t, &d);
    free(rows);
    free(p);
    return st;
}

#define F_ONE 0x3F800000u
#define F_TWO 0x40000000u
#define F_2_5 0x40200000u

static void boundaries(void)
{
    struct { const char *what; int got, want; } c[80];
    int n = 0, i, ok = 0;
#define B(W_, G_, E_) do { c[n].what = (W_); c[n].got = (G_); c[n].want = (E_); n++; } while (0)
    /* textures (5.4, 6.3) */
    B("RGBA8 1x1, one level", tex_case(1, 1, 1, 1, 0, 0, 4), RP_OK);
    B("DXT1 2048x2048, one level", tex_case(3, 2048, 2048, 1, 3, 0, 2097152), RP_OK);
    {   /* header and level table only: a real 4096x4096 level would exceed the 8 MiB payload limit */
        uint8_t b[24];
        memset(b, 0, sizeof b);
        b[0] = 3; b[3] = 16; b[5] = 16; b[6] = 1; b[8] = 3;      /* DXT1 4096x4096, 1 level, flags 3 */
        b[16] = 32;                                              /* level 0 offset 32 */
        wr32(b + 20, 8388608u);                                  /* its formula size */
        B("DXT1 4096x4096", as_entry(RS_KIND_TEXTURE, 3, b, sizeof b, 0, 0, 1), RS_ERR_DIMENSION);
    }
    B("RGBA8 3x4", tex_case(1, 3, 4, 1, 0, 0, 0), RS_ERR_DIMENSION);
    B("RGBA8 0x4", tex_case(1, 0, 4, 1, 0, 0, 0), RS_ERR_DIMENSION);
    B("PVRTC4 4x4, one level (32 bytes)", tex_case(6, 4, 4, 1, 0, 0, 32), RP_OK);
    B("PVRTC4 8x4 (not square)", tex_case(6, 8, 4, 1, 0, 0, 0), RS_ERR_DIMENSION);
    B("RGB565 4x1 full chain", tex_case(2, 4, 1, 3, 0, 0, 8 + 4 + 2), RP_OK);
    B("RGB565 1x4 (one texel wide, 4 high)", tex_case(2, 1, 4, 1, 0, 0, 0), RS_ERR_DIMENSION);
    B("RGBA8 4x4 mip_count 3 (full chain)", tex_case(1, 4, 4, 3, 0, 0, 64 + 16 + 4), RP_OK);
    B("RGBA8 4x4 mip_count 2", tex_case(1, 4, 4, 2, 0, 0, 0), RS_ERR_MIP_CHAIN);
    B("RGBA8 4x4 mip_count 0", tex_case(1, 4, 4, 0, 0, 0, 0), RS_ERR_MIP_CHAIN);
    B("RGBA8 4x4 generated mipmaps, mip_count 1", tex_case(1, 4, 4, 1, 4, 0, 64 + 16 + 4), RP_OK);
    B("RGBA8 4x4 generated mipmaps, mip_count 3", tex_case(1, 4, 4, 3, 4, 0, 0), RS_ERR_MIP_CHAIN);
    B("DXT5 generated mipmaps", tex_case(4, 4, 4, 1, 4, 0, 0), RS_ERR_ENUM);
    B("flags bit 3", tex_case(1, 4, 4, 1, 8, 0, 0), RS_ERR_ENUM);
    B("format 0", tex_case(0, 4, 4, 1, 0, 0, 0), RS_ERR_ENUM);
    B("format 7", tex_case(7, 4, 4, 1, 0, 0, 0), RS_ERR_ENUM);
    B("16 bytes after the last level", tex_case(1, 4, 4, 1, 0, 16, 0), RS_ERR_RANGE);
    {   /* level table larger than the body */
        uint8_t b[16];
        memset(b, 0, sizeof b);
        b[0] = 1; b[3] = 8; b[5] = 8; b[6] = 12;   /* RGBA8 2048x2048, 12 levels, no table bytes */
        B("level table outside the body", as_entry(RS_KIND_TEXTURE, 1, b, sizeof b, 0, 0, 1), RS_ERR_RANGE);
    }
    /* meshes (5.5, 6.3) */
    B("mesh 3 vertices, 3 indices, 1 range", mesh_case(3, 3, 1, 0, 0), RP_OK);
    B("mesh 2 vertices", mesh_case(2, 3, 1, 0, 0), RS_ERR_LIMIT);
    B("mesh 4 indices (not a multiple of 3)", mesh_case(3, 4, 1, 0, 0), RS_ERR_LIMIT);
    B("mesh 16 ranges", mesh_case(3, 48, 16, 0, 0), RP_OK);
    B("mesh 17 ranges", mesh_case(3, 51, 17, 0, 0), RS_ERR_LIMIT);
    B("mesh 65536 vertices", mesh_case(65536, 3, 1, 0, 0), RP_OK);
    B("mesh 65537 vertices", mesh_case(65537, 3, 1, 0, 0), RS_ERR_LIMIT);
    B("mesh 393216 indices", mesh_case(3, 393216, 1, 0, 0), RP_OK);
    B("mesh 393219 indices", mesh_case(3, 393219, 1, 0, 0), RS_ERR_LIMIT);
    B("mesh vertex float +infinity", mesh_case(3, 3, 1, 0x7F800000u, 0), RS_ERR_FLOAT);
    B("mesh vertex float smallest denormal", mesh_case(3, 3, 1, 0x00000001u, 0), RP_OK);
    B("mesh index = vertex_count", mesh_case(3, 3, 1, 0, 1), RS_ERR_INDEX);
    B("mesh vertex 1 y = -infinity", mesh_case_at(3, 3, 1, 0xFF800000u, 24 + 4, 0), RS_ERR_FLOAT);
    B("mesh vertex 2 v = NaN (last float)", mesh_case_at(3, 3, 1, 0x7FC00000u, 48 + 16, 0), RS_ERR_FLOAT);
    B("mesh vertex 2 colour bytes 0xFFFFFFFF (not floats)", mesh_case_at(3, 3, 1, 0xFFFFFFFFu, 48 + 20, 0), RP_OK);
    /* order: two defects, the document's first check must win */
    B("order 6.2: magic before version", order_case('b'), RS_ERR_MAGIC);
    B("order 6.2: version before header CRC", order_case('c'), RS_ERR_VERSION);
    B("order 6.2: header CRC before kind", order_case('d'), RS_ERR_HEADER_CRC);
    B("order 6.2: kind before reserved", order_case('e'), RS_ERR_KIND);
    B("order 6.2: reserved before body_size", order_case('f'), RS_ERR_RESERVED);
    B("order 6.2: body_size before body CRC", order_case('g'), RS_ERR_BODY_SIZE);
    B("order C.3: row CRC (C.3.3) before magic (C.3.4)", order_case('i'), RS_ERR_TOC);
    {   /* C.3.5 before C.3.6: payload id 9 against row id 1, and texture format 7 */
        size_t len, plen;
        uint8_t *tb = tex_body(7, 4, 4, 1, 0, 0, &len);
        uint8_t *pay = vrs1_payload(RS_KIND_TEXTURE, 9, tb, len, &plen);
        B("order C.3: id (C.3.5) before format (C.3.6)", entry_check(RS_KIND_TEXTURE, 1, pay, plen, crc(pay, plen), 0, 0, 1),
          RS_ERR_TOC);
        free(pay);
        free(tb);
    }
    /* levels (5.6, 6.3), policy off */
    B("level 1x1", level_case(1, 1, 0, 0, F_TWO, F_2_5, 0, 0, 0, 0), RP_OK);
    B("level 64x64", level_case(64, 64, 63, 63, F_TWO, F_2_5, 0, 0, 0, 0), RP_OK);
    B("level 65x1", level_case(65, 1, 0, 0, F_TWO, F_2_5, 0, 0, 0, 0), RS_ERR_LIMIT);
    B("level 0x1", level_case(0, 1, 0, 0, F_TWO, F_2_5, 0, 0, 0, 0), RS_ERR_LIMIT);
    B("level start x = width", level_case(4, 4, 4, 0, F_TWO, F_2_5, 0, 0, 0, 0), RS_ERR_RANGE);
    B("level start z = depth", level_case(4, 4, 0, 4, F_TWO, F_2_5, 0, 0, 0, 0), RS_ERR_RANGE);
    B("level start cell solid", level_case(4, 4, 1, 1, F_TWO, F_2_5, 1, 0, 0, 0), RS_ERR_RANGE);
    B("level cell value 2", level_case(4, 4, 0, 0, F_TWO, F_2_5, 0, 1, 0, 0), RS_ERR_ENUM);
    B("level cell_size +0.0", level_case(4, 4, 0, 0, 0x00000000u, F_2_5, 0, 0, 0, 0), RS_ERR_FLOAT);
    B("level cell_size -0.0", level_case(4, 4, 0, 0, 0x80000000u, F_2_5, 0, 0, 0, 0), RS_ERR_FLOAT);
    B("level cell_size -1.0", level_case(4, 4, 0, 0, 0xBF800000u, F_2_5, 0, 0, 0, 0), RS_ERR_FLOAT);
    B("level cell_size NaN", level_case(4, 4, 0, 0, 0x7FC00000u, F_2_5, 0, 0, 0, 0), RS_ERR_FLOAT);
    B("level wall_height +infinity", level_case(4, 4, 0, 0, F_ONE, 0x7F800000u, 0, 0, 0, 0), RS_ERR_FLOAT);
    B("level cell_size smallest denormal", level_case(4, 4, 0, 0, 0x00000001u, F_2_5, 0, 0, 0, 0), RP_OK);
    /* the policy (5.8) */
    B("room grid, start (6,6): policy", level_case(12, 12, 6, 6, F_TWO, F_2_5, 0, 0, 1, 1), RP_OK);
    B("room grid, start (5,6): policy", level_case(12, 12, 5, 6, F_TWO, F_2_5, 0, 0, 1, 1), RS_ERR_LEVEL_MISMATCH);
    B("room grid, wall height 2.0: policy", level_case(12, 12, 6, 6, F_TWO, F_TWO, 0, 0, 1, 1), RS_ERR_LEVEL_MISMATCH);
    B("room grid, start (5,6), no policy", level_case(12, 12, 5, 6, F_TWO, F_2_5, 0, 0, 1, 0), RP_OK);
    /* table of contents (5.3, 6.3) */
    B("table with 255 rows, 256 entries", toc_case(255, 256, 4, 0, 49, 0), RP_OK);
    B("table with 255 rows, 255 entries", toc_case(255, 255, 4, 0, 49, 0), RS_ERR_TOC);
    B("table with 0 rows", toc_case(0, 1, 4, 0, 49, 0), RS_ERR_TOC);
    B("row payload_size 32", toc_case(1, 2, 4, 0, 32, 0), RP_OK);
    B("row payload_size 31", toc_case(1, 2, 4, 0, 31, 0), RS_ERR_LIMIT);
    B("row payload_size 8,388,576", toc_case(1, 2, 4, 0, 8388576, 0), RP_OK);
    B("mesh row with format 1", toc_case(1, 2, 3, 1, 49, 0), RS_ERR_ENUM);
    B("texture row with format 0", toc_case(1, 2, 2, 0, 49, 0), RS_ERR_ENUM);
    B("vram total that wraps past 32 bits", toc_case(2, 3, 2, 1, 49, 0xFFFFFFFFu), RS_ERR_TOC);
#undef B
    for (i = 0; i < n; i++) {
        check(c[i].got == c[i].want, "boundary \"%s\": %d %s, expected %d %s", c[i].what, c[i].got,
              rs_pipe_status_name(c[i].got), c[i].want, rs_pipe_status_name(c[i].want));
        ok += c[i].got == c[i].want;
        fprintf(g_out, "BOUND %d %d %s\n", c[i].got, c[i].want, c[i].what);
    }
    printf("  boundaries: %d of %d give the expected result\n", ok, n);
}

/* ------------------------------------------------------------------ 8. synthetic packs */
/* The section 8.2 slot rule: built here (rs_synth.c), run with and without the room rule. */
static void synth_packs(void)
{
    rs_synth_case sc[RS_SYNTH_CASES];
    int i, mode, ok = 0, runs = 0;
    if (rs_synth_build_all(sc) != 0) {
        check(0, "building the synthetic packs failed");
        return;
    }
    for (i = 0; i < RS_SYNTH_CASES; i++) {
        for (mode = 1; mode >= 0; mode--) {
            rs_pipe_result r;
            const int want_code = mode ? sc[i].policy_code : sc[i].format_code;
            const int want_entry = mode ? sc[i].policy_entry : sc[i].format_entry;
            int good;
            rs_pipe_run(sc[i].path, mode, 0, NULL, NULL, &r);
            good = r.code == want_code && r.entry == want_entry && r.max_inflated == sc[i].expect_u &&
                   r.row_inflated == sc[i].rows_only_u && r.toc_inflated == sc[i].toc_inflated;
            check(good, "%s (%s): %d %s entry %d (%s), U %u rows %u Ut %u; expected %d entry %d, U %u", sc[i].name,
                  mode ? "policy" : "format", r.code, rs_pipe_status_name(r.code), r.entry, r.check, r.max_inflated,
                  r.row_inflated, r.toc_inflated, want_code, want_entry, sc[i].expect_u);
            ok += good;
            runs++;
            fprintf(g_out, "SYNTH %s %s %d %d %s %u\n", sc[i].path, mode ? "policy" : "format", r.code, r.entry,
                    stage_or_dash(&r), r.max_inflated);
            printf("  %-15s %-6s %3d %-22s entry %2d  U %4u (rows alone %4u, table %4u)\n", sc[i].name,
                   mode ? "policy" : "format", r.code, r.code ? rs_pipe_status_name(r.code) : "OK", r.entry,
                   r.max_inflated, r.row_inflated, r.toc_inflated);
        }
    }
    printf("  synthetic packs: %d of %d runs as expected\n", ok, runs);
}

/* ------------------------------------------------------------------ 5. file-level flips */
typedef struct { unsigned runs, rejected, codes[512]; } flip_tally;

static void file_flips(const char *rel, flip_tally *ft)
{
    char path[160];
    size_t len = 0;
    uint8_t *data;
    FILE *f;
    uint32_t n, k, o, index_end;

    snprintf(path, sizeof path, FIX "%s", rel);
    data = read_file(path, &len);
    check(data != NULL, "%s: cannot read", rel);
    if (!data)
        return;
    f = fopen(FLIP_PATH, "wb");
    check(f && fwrite(data, 1, len, f) == len, "cannot write " FLIP_PATH);
    if (f)
        fclose(f);
    f = fopen(FLIP_PATH, "r+b");
    if (!f) {
        free(data);
        return;
    }
    n = le32(data + 4);
    index_end = 8u + 8u * n;
    for (k = 0; k <= n; k++) {
        /* k == n: the GDAT index; otherwise entry k's wrapper (0x90 bytes) */
        const uint32_t lo = k == n ? 0u : le32(data + 8 + 8 * k);
        const uint32_t hi = k == n ? index_end : lo + 0x90u;
        const uint32_t first = (k == n || k == 0) ? 0u : k;
        for (o = lo; o < hi; o++) {
            rs_pipe_result r;
            fseek(f, (long)o, SEEK_SET);
            fputc(data[o] ^ 0xFF, f);
            fflush(f);
            rs_pipe_run(FLIP_PATH, 1, first, NULL, NULL, &r);
            fseek(f, (long)o, SEEK_SET);
            fputc(data[o], f);
            fflush(f);
            ft->runs++;
            ft->rejected += r.code != 0;
            if (r.code >= 0 && r.code < 512)
                ft->codes[r.code]++;
            check(r.code != 0, "%s: file byte %u flipped was accepted", rel, o);
            fprintf(g_out, "FLIPF %s %u %u %d %d %s\n", rel, o, first, r.code, r.entry, stage_or_dash(&r));
        }
    }
    fclose(f);
    remove(FLIP_PATH);
    free(data);
}

/* ------------------------------------------------------------------ 6. entry-level flips */
static uint32_t body_header_bytes(const uint8_t *raw)
{
    const uint8_t *p = raw + 32, *b = raw + 64;
    switch (le16(p + 6)) {
    case RS_KIND_TOC:     return 16u + 32u * le32(b);
    case RS_KIND_TEXTURE: return 16u + 8u * le16(b + 6);
    case RS_KIND_MESH:    return 32u + 16u * le16(b + 12);
    default:              return 16u;
    }
}

/* "fix": recompute the CRCs the flipped byte is not part of: the VRS1 body CRC
 * (body bytes only), the VRS1 header CRC (unless the byte is in it), then the
 * CFP1 body CRC. Envelope bytes (o < 32) are never fixed. */
static void make_consistent(uint8_t *m, size_t len, uint32_t o)
{
    if (o < 32u)
        return;
    if (o >= 64u)
        wr32(m + 32 + 16, crc(m + 64, len - 64));
    if (!(o >= 60u && o < 64u))
        wr32(m + 32 + 28, crc(m + 32, 28));
    wr32(m + 20, crc(m + 32, len - 32));
}

static void entry_flips(const char *rel, flip_tally *ft)
{
    static const uint8_t zero_hdr[0x80];
    char path[160];
    collected c;
    rs_pipe_result res;
    rs_toc toc;
    rs_diag d;
    uint32_t e, o, mode;

    memset(&c, 0, sizeof c);
    snprintf(path, sizeof path, FIX "%s", rel);
    rs_pipe_run(path, 0, 0, collect, &c, &res);           /* format only: every entry is delivered */
    check(res.code == 0, "%s: format-only run %d", rel, res.code);
    if (res.code != 0 || rs_check_toc(c.raw[0] + 32, c.len[0] - 32, c.n, &toc, &d) != RP_OK) {
        collected_free(&c);
        return;
    }
    for (e = 0; e < c.n; e++) {
        const size_t len = c.len[e];
        const uint32_t hi = 64u + body_header_bytes(c.raw[e]);
        uint8_t *m = (uint8_t *)malloc(len);
        uint8_t *tocp = (uint8_t *)malloc(c.len[0] - 32);
        for (o = 0; o < hi && o < len; o++) {
            for (mode = 0; mode < 2; mode++) {
                rs_toc t2 = toc;
                rs_resource r;
                gp_status st;
                if (mode == 1 && o < 32u)
                    continue;               /* envelope bytes: "fix" would be the same as "raw" */
                memcpy(m, c.raw[e], len);
                m[o] ^= 0xFF;
                if (mode == 1) {
                    make_consistent(m, len, o);
                    if (e > 0) {            /* the row must carry the new CRC, and the table its own */
                        const size_t tl = c.len[0] - 32;
                        memcpy(tocp, c.raw[0] + 32, tl);
                        wr32(tocp + 32 + 16 + 32 * (e - 1) + 12, le32(m + 20));
                        wr32(tocp + 16, crc(tocp + 32, tl - 32));
                        wr32(tocp + 28, crc(tocp, 28));
                        check(rs_check_toc(tocp, tl, c.n, &t2, &d) == RP_OK, "%s: adjusted table rejected", rel);
                    }
                }
                st = cf_payload_check(m, len, e, 1, NULL);
                if (st == RP_OK) {
                    if (e == 0) {
                        rs_toc t3;
                        st = rs_check_envelope(m, len, &d);
                        if (st == RP_OK)
                            st = rs_check_toc(m + 32, len - 32, c.n, &t3, &d);
                    } else {
                        st = rs_check_entry(zero_hdr, m, len, &t2, e, 1, &r, &d);
                    }
                }
                ft->runs++;
                ft->rejected += st != RP_OK;
                if (st >= 0 && st < 512)
                    ft->codes[st]++;
                check(st == RP_OK || st == CF_ERR_PAYLOAD || (st >= RS_ERR_BASE && st < RS_ERR_END),
                      "%s entry %u byte %u: unexpected status %d", rel, e, o, st);
                if (mode == 0)
                    check(st != RP_OK, "%s entry %u byte %u flipped (raw) was accepted", rel, e, o);
                fprintf(g_out, "FLIPE %s %u %u %s %d\n", rel, e, o, mode ? "fix" : "raw", st);
            }
        }
        free(m);
        free(tocp);
    }
    collected_free(&c);
}

static void print_tally(const char *what, const flip_tally *ft)
{
    int code;
    printf("  %s: %u flips, %u rejected, %u accepted; codes:", what, ft->runs, ft->rejected, ft->runs - ft->rejected);
    for (code = 0; code < 512; code++)
        if (ft->codes[code])
            printf(" %s=%u", code ? rs_pipe_status_name(code) : "OK", ft->codes[code]);
    printf("\n");
}

int main(int argc, char **argv)
{
    flip_tally f_file, f_entry;
    int i;

    if (argc != 2) {
        fprintf(stderr, "usage: %s RESULTS_FILE\n", argv[0]);
        return 2;
    }
    g_out = fopen(argv[1], "w");
    if (!g_out) {
        perror(argv[1]);
        return 2;
    }
    memset(&f_file, 0, sizeof f_file);
    memset(&f_entry, 0, sizeof f_entry);

    printf("### 1. unit checks\n");
    unit_checks();
    toc_unit_checks();
    printf("### 2. valid packs\n");
    valid_packs();
    printf("### 3. section 10 cases\n");
    cases();
    printf("### 4. room mesh vs level_mesh_build()\n");
    room_mesh();
    printf("### 5. file-level header flips (GDAT index, every wrapper)\n");
    for (i = 0; i < RS_FIX_PACK_COUNT; i++)
        file_flips(RS_FIX_PACKS[i].path, &f_file);
    print_tally("file level", &f_file);
    printf("### 6. entry-level header flips (CFP1, VRS1 header, body headers)\n");
    for (i = 0; i < RS_FIX_PACK_COUNT; i++)
        entry_flips(RS_FIX_PACKS[i].path, &f_entry);
    print_tally("entry level", &f_entry);
    printf("### 7. boundaries of sections 5.3-5.8\n");
    boundaries();
    printf("### 8. synthetic packs: the section 8.2 slot rule\n");
    synth_packs();

    check(rs_host_plat_calls() == 0, "the inert platform stand-ins were called %d times", rs_host_plat_calls());
    fclose(g_out);
    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
