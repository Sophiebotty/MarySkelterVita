/*
 * rp_io.c - defensive file I/O.
 */
#include <limits.h>
#include <sys/stat.h>
#include <zlib.h>

#include "rp_io.h"
#include "rp_mem.h"

static const char *const ERR_NAMES[RP_ERR_COUNT_OF_CODES] = {
    "OK", "ARG", "NOT_FOUND", "OPEN", "SEEK", "SHORT_READ", "WRITE",
    "TOO_LARGE", "NOMEM", "TRUNCATED", "SIZE_MISMATCH", "BAD_MAGIC",
    "BAD_VERSION", "BAD_HEADER_CRC", "BAD_INDEX", "BAD_INDEX_CRC", "COUNT",
    "RANGE", "OVERLAP", "LIMIT", "CRC", "ZLIB_DATA", "ZLIB",
};

const char *rp_err_name(rp_err e)
{
    return ((unsigned)e < (unsigned)RP_ERR_COUNT_OF_CODES) ? ERR_NAMES[e] : "?";
}

rp_err rp_file_size(const char *path, uint64_t *out_size)
{
    struct stat st;
    if (!path || !out_size)
        return RP_ERR_ARG;
    if (stat(path, &st) != 0 || st.st_size < 0)
        return RP_ERR_NOT_FOUND;
    *out_size = (uint64_t)st.st_size;
    return RP_OK;
}

rp_err rp_open(const char *path, FILE **out_fp, uint64_t *out_size)
{
    rp_err e;
    if (!out_fp)
        return RP_ERR_ARG;
    *out_fp = NULL;
    e = rp_file_size(path, out_size);
    if (e != RP_OK)
        return e;
    *out_fp = fopen(path, "rb");
    return *out_fp ? RP_OK : RP_ERR_OPEN;
}

rp_err rp_read_at(FILE *fp, uint64_t file_size, uint64_t off, void *buf, size_t n)
{
    size_t got;
    if (!fp || (!buf && n))
        return RP_ERR_ARG;
    if (off > file_size || (uint64_t)n > file_size - off)
        return RP_ERR_RANGE;
    if (off > (uint64_t)LONG_MAX)
        return RP_ERR_RANGE;
    if (fseek(fp, (long)off, SEEK_SET) != 0)
        return RP_ERR_SEEK;
    got = fread(buf, 1, n, fp);
    return got == n ? RP_OK : RP_ERR_SHORT_READ;
}

rp_err rp_read_all(const char *path, size_t max_bytes, uint8_t **out, size_t *out_len)
{
    FILE *fp;
    uint64_t size;
    uint8_t *buf;
    rp_err e;

    if (!out || !out_len)
        return RP_ERR_ARG;
    *out = NULL; *out_len = 0;
    e = rp_open(path, &fp, &size);
    if (e != RP_OK)
        return e;
    if (size > (uint64_t)max_bytes) {
        fclose(fp);
        return RP_ERR_TOO_LARGE;
    }
    if (size == 0) {           /* valid but empty: no allocation */
        fclose(fp);
        return RP_OK;
    }
    buf = (uint8_t *)rp_malloc((size_t)size);
    if (!buf) {
        fclose(fp);
        return RP_ERR_NOMEM;
    }
    e = rp_read_at(fp, size, 0, buf, (size_t)size);
    fclose(fp);
    if (e != RP_OK) {
        rp_free(buf);
        return e;
    }
    *out = buf;
    *out_len = (size_t)size;
    return RP_OK;
}

rp_err rp_crc_file_chunked(const char *path, size_t chunk, uint32_t *out_crc, uint64_t *out_len)
{
    FILE *fp;
    uint64_t size, done = 0;
    uint8_t *buf;
    uLong crc = crc32(0L, Z_NULL, 0);
    rp_err e;

    if (!chunk || !out_crc || !out_len)
        return RP_ERR_ARG;
    e = rp_open(path, &fp, &size);
    if (e != RP_OK)
        return e;
    buf = (uint8_t *)rp_malloc(chunk);
    if (!buf) {
        fclose(fp);
        return RP_ERR_NOMEM;
    }
    while (done < size) {
        size_t want = (size - done) < chunk ? (size_t)(size - done) : chunk;
        size_t got = fread(buf, 1, want, fp);
        if (got != want) {
            e = RP_ERR_SHORT_READ;
            break;
        }
        crc = crc32(crc, buf, (uInt)got);
        done += got;
    }
    rp_free(buf);
    fclose(fp);
    if (e != RP_OK)
        return e;
    *out_crc = (uint32_t)crc;
    *out_len = done;
    return RP_OK;
}

rp_err rp_write_file(const char *path, const void *data, size_t n)
{
    FILE *fp = fopen(path, "wb");
    size_t put;
    if (!fp)
        return RP_ERR_WRITE;
    put = n ? fwrite(data, 1, n, fp) : 0;
    if (fclose(fp) != 0 || put != n)
        return RP_ERR_WRITE;
    return RP_OK;
}

void rp_mkdir(const char *path)
{
    mkdir(path, 0777);   /* errors (e.g. already exists) are deliberately ignored */
}
