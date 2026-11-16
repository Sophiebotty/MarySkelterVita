/*
 * cf_zinflate.c - inflate with a caller-owned arena (no malloc).
 */
#include <string.h>
#include <zlib.h>

#include "cf_zinflate.h"

static voidpf arena_alloc(voidpf opaque, uInt items, uInt size)
{
    cf_zarena *a = (cf_zarena *)opaque;
    uint64_t want = (uint64_t)items * size;
    size_t at = (a->used + 15u) & ~(size_t)15u;      /* 16-byte alignment */
    if (want > a->size || at > a->size - (size_t)want)
        return Z_NULL;
    a->used = at + (size_t)want;
    return a->base + at;
}

static void arena_free(voidpf opaque, voidpf p)
{
    (void)opaque;
    (void)p;                                          /* released all at once by the caller */
}

gp_status cf_zinflate_exact(cf_zarena *arena, const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len)
{
    z_stream zs;
    int ret;
    gp_status e;

    if (!arena || !arena->base || (!src && src_len) || (!dst && dst_len))
        return RP_ERR_ARG;
    if (src_len > 0xFFFFFFFFu || dst_len > 0xFFFFFFFFu)
        return RP_ERR_LIMIT;
    arena->used = 0;
    memset(&zs, 0, sizeof(zs));
    zs.zalloc = arena_alloc;
    zs.zfree = arena_free;
    zs.opaque = arena;
    zs.next_in = (Bytef *)src;
    zs.avail_in = (uInt)src_len;
    zs.next_out = dst;
    zs.avail_out = (uInt)dst_len;
    ret = inflateInit(&zs);
    if (ret == Z_MEM_ERROR)
        return RP_ERR_NOMEM;
    if (ret != Z_OK)
        return RP_ERR_ZLIB;

    ret = inflate(&zs, Z_FINISH);
    if (ret == Z_STREAM_END)
        e = (zs.total_out == dst_len && zs.avail_in == 0) ? RP_OK : RP_ERR_SIZE_MISMATCH;
    else if (ret == Z_DATA_ERROR || ret == Z_NEED_DICT)
        e = RP_ERR_ZLIB_DATA;
    else if (ret == Z_MEM_ERROR)
        e = RP_ERR_NOMEM;
    else if (ret == Z_BUF_ERROR || ret == Z_OK)
        e = (zs.avail_out == 0) ? RP_ERR_SIZE_MISMATCH : RP_ERR_TRUNCATED;
    else
        e = RP_ERR_ZLIB;

    inflateEnd(&zs);
    arena->used = 0;
    return e;
}
