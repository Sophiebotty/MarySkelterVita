/*
 * rp_io.h - defensive file I/O with standard C (fopen/fread/fseek/stat).
 * On the Vita, newlib maps these to sceIo* calls, so ux0: paths work.
 */
#ifndef RP_IO_H
#define RP_IO_H

#include <stdio.h>

#include "rp_common.h"

/* Size of a file via stat(). RP_ERR_NOT_FOUND if stat() fails. */
rp_err rp_file_size(const char *path, uint64_t *out_size);

/* Opens for binary reading and returns the stat() size. */
rp_err rp_open(const char *path, FILE **out_fp, uint64_t *out_size);

/* Reads exactly n bytes at offset off. Rejects ranges outside file_size
 * (computed in 64-bit, so offset + n cannot wrap). */
rp_err rp_read_at(FILE *fp, uint64_t file_size, uint64_t off, void *buf, size_t n);

/* Reads a whole file (<= max_bytes) into a new rp_malloc buffer. */
rp_err rp_read_all(const char *path, size_t max_bytes, uint8_t **out, size_t *out_len);

/* Streams a file in 'chunk'-byte reads, returning its CRC-32 and length. */
rp_err rp_crc_file_chunked(const char *path, size_t chunk, uint32_t *out_crc, uint64_t *out_len);

/* Writes a buffer to a file (truncating). */
rp_err rp_write_file(const char *path, const void *data, size_t n);

/* Creates a directory; an already-existing directory is not an error. */
void rp_mkdir(const char *path);

#endif /* RP_IO_H */
