/*
 * rp_inflate.c - strict zlib wrappers (zlib from the VitaSDK package).
 */
#include <string.h>
#include <zlib.h>

#include "rp_inflate.h"
#include "rp_mem.h"

rp_err rp_inflate_exact(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len)
{
    z_stream zs;
    int ret;
    rp_err e;

    if ((!src && src_len) || (!dst && dst_len))
        return RP_ERR_ARG;
    if (src_len > 0xFFFFFFFFu || dst_len > 0xFFFFFFFFu)
        return RP_ERR_LIMIT;

    memset(&zs, 0, sizeof(zs));
    zs.next_in = (Bytef *)src;
    zs.avail_in = (uInt)src_len;
    zs.next_out = dst;
    zs.avail_out = (uInt)dst_len;
    if (inflateInit(&zs) != Z_OK)
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
    return e;
}

rp_err rp_deflate_alloc(const uint8_t *src, size_t src_len, int level,
                        uint8_t **out, size_t *out_len)
{
    uLongf bound, dlen;
    uint8_t *buf;

    if (!out || !out_len || (!src && src_len))
        return RP_ERR_ARG;
    bound = compressBound((uLong)src_len);
    buf = (uint8_t *)rp_malloc(bound);
    if (!buf)
        return RP_ERR_NOMEM;
    dlen = bound;
    if (compress2(buf, &dlen, src, (uLong)src_len, level) != Z_OK) {
        rp_free(buf);
        return RP_ERR_ZLIB;
    }
    *out = buf;
    *out_len = (size_t)dlen;
    return RP_OK;
}

uint32_t rp_crc32(const uint8_t *data, size_t len)
{
    return (uint32_t)crc32(crc32(0L, Z_NULL, 0), data, (uInt)len);
}
