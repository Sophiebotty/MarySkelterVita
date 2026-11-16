/*
 * rs_format.c - Vita ResPipe resource checks (RESOURCE_PIPELINE.md, VRS1
 * version 1). See rs_format.h for the scope and the rules every function
 * follows (no allocation, no GL, no I/O, byte loads, 64-bit sizes).
 *
 * The check order inside each function is the document's order (sections
 * 6.1-6.3); the first failure wins. Comments name the step they implement.
 */
#include <string.h>
#include <zlib.h>

#include "rs_format.h"
#include "gameplay/room_map.h"

static const char *const RS_NAMES[RS_ERR_END - RS_ERR_BASE] = {
    "RS_ERR_MAGIC", "RS_ERR_VERSION", "RS_ERR_KIND", "RS_ERR_HEADER_CRC", "RS_ERR_BODY_SIZE",
    "RS_ERR_BODY_CRC", "RS_ERR_RESERVED", "RS_ERR_ENUM", "RS_ERR_RANGE", "RS_ERR_ALIGN",
    "RS_ERR_DIMENSION", "RS_ERR_MIP_CHAIN", "RS_ERR_LEVEL_SIZE", "RS_ERR_INDEX", "RS_ERR_FLOAT",
    "RS_ERR_TOC", "RS_ERR_DUP_ID", "RS_ERR_REF", "RS_ERR_LIMIT", "RS_ERR_LEVEL_MISMATCH",
    "RS_ERR_ENVELOPE",
};

const char *rs_status_name(int code)
{
    if (code >= RS_ERR_BASE && code < RS_ERR_END)
        return RS_NAMES[code - RS_ERR_BASE];
    return NULL;
}

/* ------------------------------------------------------------------ helpers */
static uint32_t rd16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* CRC-32 (zlib / ISO-HDLC) of n bytes; n is at most 8 MiB here. */
static uint32_t crc(const uint8_t *p, uint64_t n)
{
    return (uint32_t)crc32(crc32(0uL, Z_NULL, 0), p, (uInt)n);
}

static gp_status fail(rs_diag *d, gp_status code, const char *layer, const char *check,
                      int has_offset, uint64_t offset)
{
    if (d) {
        d->layer = layer;
        d->check = check;
        d->has_offset = has_offset;
        d->offset = has_offset ? (uint32_t)offset : 0u;
    }
    return code;
}

/* Offset of the first non-zero byte in [lo, hi), or hi if all are zero. */
static uint64_t first_nonzero(const uint8_t *p, uint64_t lo, uint64_t hi)
{
    uint64_t i;
    for (i = lo; i < hi; i++)
        if (p[i] != 0)
            return i;
    return hi;
}

static int is_pow2(uint32_t v)
{
    return v != 0u && (v & (v - 1u)) == 0u;
}

static uint32_t log2_pow2(uint32_t v)
{
    uint32_t n = 0;
    while (v > 1u) {
        v >>= 1;
        n++;
    }
    return n;
}

static uint64_t up16(uint64_t v)
{
    return (v + 15u) & ~(uint64_t)15u;
}

/* Bytes in a w x h level of a texture format (5.4). */
static uint64_t level_bytes(uint32_t fmt, uint32_t w, uint32_t h)
{
    const uint64_t blocks = (uint64_t)((w + 3u) / 4u) * ((h + 3u) / 4u);
    switch (fmt) {
    case RS_FMT_RGBA8:  return 4u * (uint64_t)w * h;
    case RS_FMT_RGB565: return 2u * (uint64_t)w * h;
    case RS_FMT_DXT1:   return 8u * blocks;
    case RS_FMT_DXT5:   return 16u * blocks;
    case RS_FMT_ETC1:   return 8u * blocks;
    default:            return (uint64_t)(w > 8u ? w : 8u) * (h > 8u ? h : 8u) / 2u;   /* PVRTC4 */
    }
}

static uint32_t dim_at(uint32_t side, uint32_t level)
{
    side >>= level;
    return side ? side : 1u;
}

/* binary32 bit patterns: finite = exponent not all ones; > 0 = finite, sign
 * clear, not zero. Decided on the bits, so no floating-point state is used. */
static int f32_finite(uint32_t bits)
{
    return ((bits >> 23) & 0xFFu) != 0xFFu;
}

static int f32_positive(uint32_t bits)
{
    return f32_finite(bits) && (bits & 0x80000000u) == 0u && (bits & 0x7FFFFFFFu) != 0u;
}

static float f32_from_bits(uint32_t bits)
{
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

/* ------------------------------------------------------------------ transport rules */
gp_status rs_check_entry_count(uint32_t n, rs_diag *d)
{
    if (n < RS_MIN_ENTRIES || n > RS_MAX_ENTRIES)                                    /* A1 */
        return fail(d, RS_ERR_LIMIT, "archive", "entry count", 0, 0);
    return RP_OK;
}

gp_status rs_check_wrapper_record(const gp_wrap_info *w, rs_diag *d)
{
    if (!w)
        return RP_ERR_ARG;
    if (w->tag[0] | w->tag[1] | w->tag[2] | w->tag[3])                               /* A2 */
        return fail(d, RS_ERR_RESERVED, "wrapper", "header bytes 0x00-0x03 (tag)", 1, 0);
    if (w->unknown_crc != RS_ZERO124_CRC)
        return fail(d, RS_ERR_RESERVED, "wrapper", "header bytes 0x04-0x7F", 1, 4);
    return RP_OK;
}

gp_status rs_check_wrapper_header(const uint8_t *hdr, rs_diag *d)
{
    uint64_t at;
    if (!hdr)
        return RP_ERR_ARG;
    at = first_nonzero(hdr, 0, RS_WRAP_HDR_BYTES);                                     /* C.3.1 */
    if (at != RS_WRAP_HDR_BYTES)
        return fail(d, RS_ERR_RESERVED, "wrapper", "header bytes 0x00-0x7F", 1, at);
    return RP_OK;
}

const char *rs_cfp1_field(const uint8_t *raw, size_t len, uint32_t index)
{
    uint32_t kind, w, h, body_len;
    if (!raw || len < RS_ENVELOPE_BYTES)
        return "length";
    if (memcmp(raw, "CFP1", 4) != 0)
        return "magic";
    if (rd32(raw + 28) != 0u)
        return "reserved";
    if (rd32(raw + 24) != index)
        return "index";
    body_len = rd32(raw + 16);
    if ((uint64_t)body_len != (uint64_t)len - RS_ENVELOPE_BYTES)
        return "body length";
    kind = rd32(raw + 4);
    w = rd32(raw + 8);
    h = rd32(raw + 12);
    if (kind == 1u) {
        if (!is_pow2(w) || !is_pow2(h) || w > 4096u || h > 4096u || 4u * (uint64_t)w * h != body_len)
            return "kind";
    } else if (kind == 2u) {
        if (w != 0u || h != 0u)
            return "kind";
    } else {
        return "kind";
    }
    if (crc(raw + RS_ENVELOPE_BYTES, body_len) != rd32(raw + 20))
        return "body CRC";
    return NULL;
}

gp_status rs_check_envelope(const uint8_t *raw, size_t len, rs_diag *d)
{
    if (!raw || len < RS_ENVELOPE_BYTES)
        return RP_ERR_ARG;
    if (rd32(raw + 4) != 2u)                                                           /* A3, C.3.2 */
        return fail(d, RS_ERR_ENVELOPE, "CFP1", "kind (must be 2)", 1, 4);
    return RP_OK;
}

gp_status rs_check_toc_again(const uint8_t *raw, size_t len, uint32_t open_crc, rs_diag *d)
{
    if (!raw || len < RS_ENVELOPE_BYTES)
        return RP_ERR_ARG;
    if (rd32(raw + 20) != open_crc)                                                    /* C.2 */
        return fail(d, RS_ERR_TOC, "CFP1", "body CRC differs from the one checked at open", 1, 20);
    return RP_OK;
}

/* ------------------------------------------------------------------ 6.2 common header */
static gp_status check_header(const uint8_t *p, uint64_t len, uint32_t *kind, uint32_t *id, rs_diag *d)
{
    static const char L[] = "VRS1 header";
    if (len < RS_HEADER_BYTES)                                                          /* 6.2.1 */
        return fail(d, RS_ERR_BODY_SIZE, L, "payload shorter than 32 bytes", 0, 0);
    if (memcmp(p, "VRS1", 4) != 0)                                                      /* 6.2.2 */
        return fail(d, RS_ERR_MAGIC, L, "magic", 1, 0);
    if (rd16(p + 4) != RS_VERSION)                                                      /* 6.2.3 */
        return fail(d, RS_ERR_VERSION, L, "version", 1, 4);
    if (crc(p, 28) != rd32(p + 28))                                                     /* 6.2.4 */
        return fail(d, RS_ERR_HEADER_CRC, L, "header CRC", 1, 28);
    *kind = rd16(p + 6);
    if (*kind < RS_KIND_TOC || *kind > RS_KIND_LEVEL)                                   /* 6.2.5 */
        return fail(d, RS_ERR_KIND, L, "kind", 1, 6);
    if (first_nonzero(p, 20, 28) != 28u)                                                /* 6.2.6 */
        return fail(d, RS_ERR_RESERVED, L, "reserved bytes 20-27", 1, first_nonzero(p, 20, 28));
    if ((uint64_t)rd32(p + 12) != len - RS_HEADER_BYTES)                                /* 6.2.7 */
        return fail(d, RS_ERR_BODY_SIZE, L, "body_size", 1, 12);
    if (crc(p + RS_HEADER_BYTES, len - RS_HEADER_BYTES) != rd32(p + 16))                /* 6.2.8 */
        return fail(d, RS_ERR_BODY_CRC, L, "body CRC", 1, 16);
    *id = rd32(p + 8);
    return RP_OK;
}

/* ------------------------------------------------------------------ 6.3 table of contents */
gp_status rs_check_toc(const uint8_t *payload, size_t len, uint32_t n_archive, rs_toc *out, rs_diag *d)
{
    static const char L[] = "table of contents";
    uint32_t kind = 0, id = 0, n, i, j;
    uint64_t sum_vram = 0, sum_heap = 0, blen;
    const uint8_t *b;
    gp_status st;

    if (!payload || !out)
        return RP_ERR_ARG;
    st = check_header(payload, len, &kind, &id, d);                                      /* A4: 6.2 */
    if (st != RP_OK)
        return st;
    if (kind != RS_KIND_TOC || id != 0u)                                                 /* 6.3 TOC 1 */
        return fail(d, RS_ERR_TOC, L, "entry 0 is not a table of contents", 1, 6);
    b = payload + RS_HEADER_BYTES;
    blen = (uint64_t)len - RS_HEADER_BYTES;
    if (blen < 16u)                                                                      /* 6.3 TOC 2 */
        return fail(d, RS_ERR_RANGE, L, "body under 16 bytes", 0, 0);
    n = rd32(b);
    if (blen != 16u + 32u * (uint64_t)n)
        return fail(d, RS_ERR_RANGE, L, "body size != 16 + 32 x entry_count", 1, RS_HEADER_BYTES);
    if (first_nonzero(b, 12, 16) != 16u)                                                 /* 6.3 TOC 3 */
        return fail(d, RS_ERR_RESERVED, L, "reserved", 1, RS_HEADER_BYTES + 12u);
    if (n < 1u || n > RS_MAX_RESOURCES || (uint64_t)n + 1u != n_archive)                 /* 6.3 TOC 4 */
        return fail(d, RS_ERR_TOC, L, "entry_count vs archive", 1, RS_HEADER_BYTES);
    for (i = 0; i < n; i++) {                                                            /* 6.3 TOC 5 */
        const uint8_t *row = b + 16u + 32u * (uint64_t)i;
        const uint64_t at = RS_HEADER_BYTES + 16u + 32u * (uint64_t)i;
        const uint32_t rid = rd32(row), rkind = rd16(row + 4), rfmt = rd16(row + 6);
        const uint32_t rsize = rd32(row + 8);
        if (first_nonzero(row, 24, 32) != 32u)
            return fail(d, RS_ERR_RESERVED, L, "row reserved", 1, at + 24u);
        if (rid == 0u || rkind < RS_KIND_TEXTURE || rkind > RS_KIND_LEVEL)
            return fail(d, RS_ERR_TOC, L, "row id or kind", 1, at);
        if (rkind == RS_KIND_TEXTURE ? (rfmt < RS_FMT_RGBA8 || rfmt > RS_FMT_PVRTC4) : rfmt != 0u)
            return fail(d, RS_ERR_ENUM, L, "row format", 1, at + 6u);
        if (rsize < RS_MIN_PAYLOAD || rsize > RS_MAX_PAYLOAD)
            return fail(d, RS_ERR_LIMIT, L, "row payload_size", 1, at + 8u);
        for (j = 0; j < i; j++)
            if (rd32(b + 16u + 32u * (uint64_t)j) == rid)
                return fail(d, RS_ERR_DUP_ID, L, "row resource_id used twice", 1, at);
        sum_vram += rd32(row + 16);
        sum_heap += rd32(row + 20);
    }
    if (sum_vram != rd32(b + 4) || sum_heap != rd32(b + 8))                              /* 6.3 TOC 6 */
        return fail(d, RS_ERR_TOC, L, "totals", 1, RS_HEADER_BYTES + 4u);
    out->payload = payload;
    out->payload_size = (uint32_t)len;
    out->entry_count = n;
    out->total_vram_bytes = rd32(b + 4);
    out->total_heap_bytes = rd32(b + 8);
    return RP_OK;
}

void rs_toc_row(const rs_toc *t, uint32_t i, rs_row *out)
{
    const uint8_t *row = t->payload + RS_HEADER_BYTES + 16u + 32u * (uint64_t)i;
    out->resource_id = rd32(row);
    out->kind = rd16(row + 4);
    out->format = rd16(row + 6);
    out->payload_size = rd32(row + 8);
    out->payload_crc = rd32(row + 12);
    out->vram_bytes = rd32(row + 16);
    out->heap_bytes = rd32(row + 20);
}

gp_status rs_check_region(const rs_toc *t, uint32_t entry, uint32_t region_size, rs_diag *d)
{
    rs_row row;
    uint64_t u;
    if (!t || entry == 0u || entry > t->entry_count)
        return RP_ERR_ARG;
    rs_toc_row(t, entry - 1u, &row);
    u = (uint64_t)row.payload_size + RS_ENVELOPE_BYTES;
    if (region_size > RS_MAX_REGION)                                                     /* A5 */
        return fail(d, RS_ERR_LIMIT, "archive", "region over 8 MiB", 0, 0);
    if (region_size >= GP_WRAP_TOTAL && (uint64_t)region_size - GP_WRAP_TOTAL > u + (u >> 10) + 64u)
        return fail(d, RS_ERR_LIMIT, "archive", "compressed size over U + (U >> 10) + 64", 0, 0);
    return RP_OK;
}

/* ------------------------------------------------------------------ 6.3 texture */
static gp_status check_texture(const uint8_t *b, uint64_t len, rs_resource *r, rs_diag *d)
{
    static const char L[] = "texture";
    const uint64_t H = RS_HEADER_BYTES;
    uint32_t fmt, w, h, n, flags, chain, i, count;
    uint64_t end, vram = 0;

    if (len < 16u)                                                                       /* 1 */
        return fail(d, RS_ERR_RANGE, L, "body under 16 bytes", 0, 0);
    fmt = rd16(b);
    w = rd16(b + 2);
    h = rd16(b + 4);
    n = rd16(b + 6);
    flags = rd32(b + 8);
    if (fmt < RS_FMT_RGBA8 || fmt > RS_FMT_PVRTC4)                                       /* 2 */
        return fail(d, RS_ERR_ENUM, L, "format", 1, H);
    if ((flags & ~7u) != 0u || ((flags & RS_TEX_GENMIP) && fmt > RS_FMT_RGB565))          /* 3 */
        return fail(d, RS_ERR_ENUM, L, "flags", 1, H + 8u);
    if (first_nonzero(b, 12, 16) != 16u)                                                 /* 4 */
        return fail(d, RS_ERR_RESERVED, L, "reserved", 1, H + 12u);
    if (!is_pow2(w) || !is_pow2(h) || w > RS_MAX_TEX_SIDE || h > RS_MAX_TEX_SIDE ||        /* 5 */
        (fmt == RS_FMT_PVRTC4 && w != h))
        return fail(d, RS_ERR_DIMENSION, L, "width or height", 1, H + 2u);
    chain = log2_pow2(w > h ? w : h) + 1u;
    if ((n != 1u && n != chain) || ((flags & RS_TEX_GENMIP) && n != 1u))                  /* 6 */
        return fail(d, RS_ERR_MIP_CHAIN, L, "mip_count", 1, H + 6u);
    if (fmt == RS_FMT_RGB565)                                                            /* 7 */
        for (i = 0; i < n; i++)
            if (dim_at(w, i) == 1u && dim_at(h, i) != 1u)
                return fail(d, RS_ERR_DIMENSION, L, "RGB565 level one texel wide", 1, H + 2u);
    if (16u + 8u * (uint64_t)n > len)                                                    /* 8 */
        return fail(d, RS_ERR_RANGE, L, "level table outside the body", 1, H + 16u);
    end = 16u + 8u * (uint64_t)n;
    for (i = 0; i < n; i++) {                                                            /* 9 */
        const uint64_t off = rd32(b + 16u + 8u * i), size = rd32(b + 20u + 8u * i);
        const uint64_t at = H + 16u + 8u * i;
        if (off % 16u != 0u)
            return fail(d, RS_ERR_ALIGN, L, "level offset", 1, at);
        if (off != up16(end))
            return fail(d, RS_ERR_RANGE, L, "level position", 1, at);
        if (size != level_bytes(fmt, dim_at(w, i), dim_at(h, i)))
            return fail(d, RS_ERR_LEVEL_SIZE, L, "level size", 1, at + 4u);
        if (off + size > len)
            return fail(d, RS_ERR_RANGE, L, "level outside the body", 1, at);
        if (first_nonzero(b, end, off) != off)
            return fail(d, RS_ERR_RESERVED, L, "padding before a level", 1, H + first_nonzero(b, end, off));
        end = off + size;
    }
    if (end != len)                                                                      /* 10 */
        return fail(d, RS_ERR_RANGE, L, "body does not end at the last level", 1, H + end);
    count = (flags & RS_TEX_GENMIP) ? chain : n;
    for (i = 0; i < count; i++)
        vram += level_bytes(fmt, dim_at(w, i), dim_at(h, i));
    r->format = fmt;
    r->vram_bytes = (uint32_t)vram;          /* at most 4/3 x 16 MiB: fits */
    r->heap_bytes = 0;
    r->tex.width = w;
    r->tex.height = h;
    r->tex.mip_count = n;
    r->tex.flags = flags;
    return RP_OK;
}

/* ------------------------------------------------------------------ 6.3 mesh */
static gp_status check_mesh(const uint8_t *b, uint64_t len, rs_resource *r, rs_diag *d)
{
    static const char L[] = "mesh";
    const uint64_t H = RS_HEADER_BYTES;
    uint32_t vc, ic, vfmt, stride, nr, prim, ro, vo, io, i, k;
    uint64_t next = 0;

    if (len < 32u)                                                                       /* 1 */
        return fail(d, RS_ERR_RANGE, L, "body under 32 bytes", 0, 0);
    vc = rd32(b);
    ic = rd32(b + 4);
    vfmt = rd16(b + 8);
    stride = rd16(b + 10);
    nr = rd16(b + 12);
    prim = rd16(b + 14);
    ro = rd32(b + 16);
    vo = rd32(b + 20);
    io = rd32(b + 24);
    if (vfmt != 1u || stride != 24u || prim != 1u)                                       /* 2 */
        return fail(d, RS_ERR_ENUM, L, "vertex format, stride or primitive", 1, H + 8u);
    if (rd32(b + 28) != 0u)                                                              /* 3 */
        return fail(d, RS_ERR_RESERVED, L, "reserved", 1, H + 28u);
    if (vc < 3u || vc > 65536u || ic < 3u || ic > 393216u || ic % 3u != 0u ||             /* 4 */
        nr < 1u || nr > RS_MAX_RANGES)
        return fail(d, RS_ERR_LIMIT, L, "counts", 1, H);
    if (ro % 16u != 0u || vo % 16u != 0u || io % 16u != 0u)                               /* 5 */
        return fail(d, RS_ERR_ALIGN, L, "block offsets", 1, H + 16u);
    if (ro != 32u || (uint64_t)vo != up16(32u + 16u * (uint64_t)nr) ||                    /* 6 */
        (uint64_t)io != up16((uint64_t)vo + 24u * (uint64_t)vc) || len != (uint64_t)io + 2u * (uint64_t)ic)
        return fail(d, RS_ERR_RANGE, L, "block positions or body end", 1, H + 16u);
    if (first_nonzero(b, 32u + 16u * (uint64_t)nr, vo) != vo)                             /* 7 */
        return fail(d, RS_ERR_RESERVED, L, "padding before the vertices", 1,
                    H + first_nonzero(b, 32u + 16u * (uint64_t)nr, vo));
    if (first_nonzero(b, (uint64_t)vo + 24u * (uint64_t)vc, io) != io)
        return fail(d, RS_ERR_RESERVED, L, "padding before the indices", 1,
                    H + first_nonzero(b, (uint64_t)vo + 24u * (uint64_t)vc, io));
    for (i = 0; i < nr; i++) {                                                           /* 8 */
        const uint8_t *rg = b + 32u + 16u * i;
        const uint32_t first = rd32(rg), count = rd32(rg + 4);
        if (rd32(rg + 12) != 0u)
            return fail(d, RS_ERR_RESERVED, L, "range reserved", 1, H + 32u + 16u * i + 12u);
        if ((uint64_t)first != next || count == 0u || count % 3u != 0u)
            return fail(d, RS_ERR_RANGE, L, "range not contiguous or count", 1, H + 32u + 16u * i);
        next = (uint64_t)first + count;     /* 64 bits: cannot wrap */
    }
    if (next != ic)                                                                      /* 9 */
        return fail(d, RS_ERR_RANGE, L, "ranges do not cover the indices", 1, H + 32u);
    for (i = 0; i < vc; i++)                                                             /* 10 */
        for (k = 0; k < 5u; k++)
            if (!f32_finite(rd32(b + vo + 24u * (uint64_t)i + 4u * k)))
                return fail(d, RS_ERR_FLOAT, L, "vertex float not finite", 1, H + vo + 24u * (uint64_t)i + 4u * k);
    for (i = 0; i < ic; i++)                                                             /* 11 */
        if (rd16(b + io + 2u * (uint64_t)i) >= vc)
            return fail(d, RS_ERR_INDEX, L, "index >= vertex_count", 1, H + io + 2u * (uint64_t)i);
    r->format = 0;
    r->vram_bytes = 24u * vc + 2u * ic;
    r->heap_bytes = 0;
    r->mesh.vertex_count = vc;
    r->mesh.index_count = ic;
    r->mesh.range_count = nr;
    r->mesh.vertex_offset = vo;
    r->mesh.index_offset = io;
    return RP_OK;
}

/* ------------------------------------------------------------------ 6.3 level */
static gp_status check_level(const uint8_t *b, uint64_t len, rs_resource *r, rs_diag *d)
{
    static const char L[] = "level";
    const uint64_t H = RS_HEADER_BYTES;
    uint32_t w, dep, sx, sz, cs, wh;
    uint64_t i;

    if (len < 16u)                                                                       /* 1 */
        return fail(d, RS_ERR_RANGE, L, "body under 16 bytes", 0, 0);
    w = rd16(b);
    dep = rd16(b + 2);
    sx = rd16(b + 4);
    sz = rd16(b + 6);
    cs = rd32(b + 8);
    wh = rd32(b + 12);
    if (w < 1u || w > 64u || dep < 1u || dep > 64u)                                      /* 2 */
        return fail(d, RS_ERR_LIMIT, L, "width or depth", 1, H);
    if (len != 16u + (uint64_t)w * dep)                                                  /* 3 */
        return fail(d, RS_ERR_RANGE, L, "body size != 16 + width x depth", 0, 0);
    if (!f32_positive(cs) || !f32_positive(wh))                                          /* 4 */
        return fail(d, RS_ERR_FLOAT, L, "cell_size or wall_height", 1, H + 8u);
    for (i = 16; i < len; i++)                                                           /* 5 */
        if (b[i] > 1u)
            return fail(d, RS_ERR_ENUM, L, "cell value", 1, H + i);
    if (sx >= w || sz >= dep || b[16u + (uint64_t)sz * w + sx] != 0u)                    /* 6 */
        return fail(d, RS_ERR_RANGE, L, "start cell", 1, H + 4u);
    r->format = 0;
    r->vram_bytes = 0;
    r->heap_bytes = (uint32_t)len;
    r->level.width = w;
    r->level.depth = dep;
    r->level.start_x = sx;
    r->level.start_z = sz;
    r->level.cell_size = f32_from_bits(cs);
    r->level.wall_height = f32_from_bits(wh);
    return RP_OK;
}

/* ------------------------------------------------------------------ 5.8 application policy */
static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_f32(uint8_t *p, float f)
{
    uint32_t v;
    memcpy(&v, &f, sizeof v);
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* Every level must equal the reference room byte for byte: room_map.c's grid
 * and sizes, and the main app's start cell. */
static gp_status check_policy(const uint8_t *b, uint64_t len, rs_diag *d)
{
    static const char L[] = "policy";
    uint8_t head[16];
    int x, z;

    put16(head, ROOM_W);
    put16(head + 2, ROOM_D);
    put16(head + 4, RS_ROOM_START_X);
    put16(head + 6, RS_ROOM_START_Z);
    put_f32(head + 8, ROOM_CELL_SIZE);
    put_f32(head + 12, ROOM_WALL_HEIGHT);
    if (len != 16u + (uint64_t)ROOM_W * ROOM_D || memcmp(b, head, sizeof head) != 0)
        return fail(d, RS_ERR_LEVEL_MISMATCH, L, "level is not the reference room (size or header)", 1,
                    RS_HEADER_BYTES);
    for (z = 0; z < ROOM_D; z++)
        for (x = 0; x < ROOM_W; x++)
            if (b[16 + z * ROOM_W + x] != (room_is_solid(x, z) ? 1u : 0u))
                return fail(d, RS_ERR_LEVEL_MISMATCH, L, "level is not the reference room (cells)", 1,
                            RS_HEADER_BYTES + 16u + (uint64_t)z * ROOM_W + (uint64_t)x);
    return RP_OK;
}

/* ------------------------------------------------------------------ C.3 */
static int toc_has_texture(const rs_toc *t, uint32_t id)
{
    uint32_t i;
    rs_row row;
    for (i = 0; i < t->entry_count; i++) {
        rs_toc_row(t, i, &row);
        if (row.kind == RS_KIND_TEXTURE && row.resource_id == id)
            return 1;
    }
    return 0;
}

gp_status rs_check_entry(const uint8_t *wrapper_hdr, const uint8_t *raw, size_t len, const rs_toc *t,
                         uint32_t entry, int apply_policy, rs_resource *out, rs_diag *d)
{
    rs_resource r;
    rs_row row;
    uint32_t kind = 0, id = 0, i;
    const uint8_t *p, *b;
    uint64_t plen;
    gp_status st;

    if (!wrapper_hdr || !raw || !t || len < RS_ENVELOPE_BYTES || entry == 0u || entry > t->entry_count)
        return RP_ERR_ARG;
    memset(&r, 0, sizeof r);
    rs_toc_row(t, entry - 1u, &row);
    st = rs_check_wrapper_header(wrapper_hdr, d);                                        /* C.3.1 */
    if (st != RP_OK)
        return st;
    st = rs_check_envelope(raw, len, d);                                                 /* C.3.2 */
    if (st != RP_OK)
        return st;
    if ((uint64_t)len != (uint64_t)row.payload_size + RS_ENVELOPE_BYTES ||               /* C.3.3 */
        rd32(raw + 20) != row.payload_crc)
        return fail(d, RS_ERR_TOC, "CFP1", "size or body CRC vs table of contents", 1, 16);
    p = raw + RS_ENVELOPE_BYTES;
    plen = (uint64_t)len - RS_ENVELOPE_BYTES;
    st = check_header(p, plen, &kind, &id, d);                                           /* C.3.4 */
    if (st != RP_OK)
        return st;
    if (id != row.resource_id || kind != row.kind)                                       /* C.3.5 */
        return fail(d, RS_ERR_TOC, "VRS1 header", "resource_id or kind vs table of contents", 1, 6);
    b = p + RS_HEADER_BYTES;
    if (kind == RS_KIND_TEXTURE)                                                         /* C.3.6 */
        st = check_texture(b, plen - RS_HEADER_BYTES, &r, d);
    else if (kind == RS_KIND_MESH)
        st = check_mesh(b, plen - RS_HEADER_BYTES, &r, d);
    else
        st = check_level(b, plen - RS_HEADER_BYTES, &r, d);   /* kind 2-4 here: C.3.5 matched a row */
    if (st != RP_OK)
        return st;
    if (r.format != row.format || r.vram_bytes != row.vram_bytes || r.heap_bytes != row.heap_bytes) /* C.3.7 */
        return fail(d, RS_ERR_TOC, "table of contents", "format, vram_bytes or heap_bytes", 0, 0);
    if (kind == RS_KIND_MESH)                                                            /* C.3.8 */
        for (i = 0; i < r.mesh.range_count; i++) {
            const uint32_t tex = rd32(b + 32u + 16u * i + 8u);
            if (tex != 0u && !toc_has_texture(t, tex))
                return fail(d, RS_ERR_REF, "mesh", "range texture_id not in the table of contents", 1,
                            RS_HEADER_BYTES + 32u + 16u * i + 8u);
        }
    if (kind == RS_KIND_LEVEL && apply_policy) {                                         /* C.3.9 */
        st = check_policy(b, plen - RS_HEADER_BYTES, d);
        if (st != RP_OK)
            return st;
    }
    r.resource_id = id;
    r.kind = kind;
    r.payload = p;
    r.payload_size = (uint32_t)plen;
    r.body = b;
    r.body_size = (uint32_t)(plen - RS_HEADER_BYTES);
    if (out)
        *out = r;
    return RP_OK;
}

/* ------------------------------------------------------------------ accessors */
int rs_texture_level(const rs_resource *r, uint32_t i, rs_tex_level *out)
{
    if (!r || !out || r->kind != RS_KIND_TEXTURE || i >= r->tex.mip_count)
        return -1;
    out->width = dim_at(r->tex.width, i);
    out->height = dim_at(r->tex.height, i);
    out->size = rd32(r->body + 20u + 8u * i);
    out->data = r->body + rd32(r->body + 16u + 8u * i);
    return 0;
}

int rs_mesh_range_get(const rs_resource *r, uint32_t i, rs_mesh_range *out)
{
    const uint8_t *rg;
    if (!r || !out || r->kind != RS_KIND_MESH || i >= r->mesh.range_count)
        return -1;
    rg = r->body + 32u + 16u * i;
    out->first_index = rd32(rg);
    out->index_count = rd32(rg + 4);
    out->texture_id = rd32(rg + 8);
    return 0;
}
