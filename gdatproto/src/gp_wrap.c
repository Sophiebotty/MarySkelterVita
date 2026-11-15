/*
 * gp_wrap.c - BILZ wrapper validation and strict inflate.
 */
#include <string.h>
#include <zlib.h>

#include "gp_wrap.h"
#include "rp_inflate.h"
#include "rp_io.h"
#include "rp_mem.h"
#include "rp_time.h"

gp_status gp_wrap_parse(const uint8_t *hdr, uint32_t region, gp_mode mode, gp_wrap_info *w)
{
    uint32_t avail;

    memset(w, 0, sizeof(*w));
    w->region = region;
    if (region < GP_WRAP_TOTAL)
        return RP_ERR_TRUNCATED;
    memcpy(w->tag, hdr, 4);
    w->unknown_crc = (uint32_t)crc32(0L, hdr + 4, GP_WRAP_HDR - 4);
    if (memcmp(hdr + GP_WRAP_HDR, "BILZ", 4) != 0)
        return RP_ERR_BAD_MAGIC;
    w->usize = gp_be32(hdr + 0x84);
    w->csize = gp_be32(hdr + 0x88);
    w->constant = gp_be32(hdr + 0x8C);
    avail = region - GP_WRAP_TOTAL;
    if (w->csize > avail)
        return RP_ERR_RANGE;
    if (w->usize > GP_MAX_RAW)
        return RP_ERR_LIMIT;
    if (w->constant != 7u) {
        if (mode == GP_STRICT)
            return GP_ERR_CONST;
        w->obs |= GP_OBS_CONST;
    }
    if (w->csize < avail) {
        if (mode == GP_STRICT)
            return GP_ERR_SIZE_RULE;
        w->obs |= GP_OBS_SIZE_RULE;
    }
    return RP_OK;
}

gp_status gp_wrap_inflate(FILE *fp, uint64_t file_size, uint64_t stream_off, const gp_wrap_info *w,
                          uint8_t **out, size_t *out_len, gp_load_timing *t)
{
    uint8_t *src, *dst;
    double t0, t1;
    gp_status st;

    *out = NULL;
    *out_len = 0;
    if (w->csize == 0)
        return RP_ERR_TRUNCATED;          /* an empty stream cannot be complete */
    src = (uint8_t *)rp_malloc(w->csize);
    if (!src)
        return RP_ERR_NOMEM;
    t0 = rp_now_ms();
    st = rp_read_at(fp, file_size, stream_off, src, w->csize);
    t1 = rp_now_ms();
    if (st != RP_OK) {
        rp_free(src);
        return st;
    }
    dst = (uint8_t *)rp_malloc(w->usize ? w->usize : 1u);
    if (!dst) {
        rp_free(src);
        return RP_ERR_NOMEM;
    }
    st = rp_inflate_exact(src, w->csize, dst, w->usize);
    if (t) {
        double t2 = rp_now_ms();
        t->read_ms += t1 - t0;
        t->inflate_ms += t2 - t1;
        t->read_bytes += w->csize;
        if (st == RP_OK) t->raw_bytes += w->usize;
    }
    rp_free(src);
    if (st != RP_OK) {
        rp_free(dst);
        return st;
    }
    *out = dst;
    *out_len = w->usize;
    return RP_OK;
}

static gp_status read_header(FILE *fp, uint64_t file_size, uint64_t off, uint32_t size, gp_mode mode,
                             gp_wrap_info *w)
{
    uint8_t hdr[GP_WRAP_TOTAL];
    gp_status st;
    if (size < GP_WRAP_TOTAL) {
        memset(w, 0, sizeof(*w));
        w->region = size;
        return RP_ERR_TRUNCATED;
    }
    st = rp_read_at(fp, file_size, off, hdr, sizeof(hdr));
    if (st != RP_OK)
        return st;
    return gp_wrap_parse(hdr, size, mode, w);
}

gp_status gp_wrap_load(FILE *fp, uint64_t file_size, uint64_t off, uint32_t size, gp_mode mode,
                       gp_wrap_info *w, uint8_t **out, size_t *out_len, gp_load_timing *t)
{
    gp_status st;
    *out = NULL;
    *out_len = 0;
    st = read_header(fp, file_size, off, size, mode, w);
    if (st != RP_OK)
        return st;
    return gp_wrap_inflate(fp, file_size, off + GP_WRAP_TOTAL, w, out, out_len, t);
}

gp_status gp_wrap_read_stream(FILE *fp, uint64_t file_size, uint64_t off, uint32_t size, gp_mode mode,
                              uint8_t **out, size_t *out_len)
{
    gp_wrap_info w;
    uint8_t *src;
    gp_status st;
    *out = NULL;
    *out_len = 0;
    st = read_header(fp, file_size, off, size, mode, &w);
    if (st != RP_OK)
        return st;
    if (w.csize == 0)
        return RP_ERR_TRUNCATED;
    src = (uint8_t *)rp_malloc(w.csize);
    if (!src)
        return RP_ERR_NOMEM;
    st = rp_read_at(fp, file_size, off + GP_WRAP_TOTAL, src, w.csize);
    if (st != RP_OK) {
        rp_free(src);
        return st;
    }
    *out = src;
    *out_len = w.csize;
    return RP_OK;
}
