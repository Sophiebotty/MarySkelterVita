/*
 * rp_common.h - error codes and limits shared by the ResProto modules.
 *
 * The core modules (rp_io, rp_container, rp_inflate, rp_mem) use only
 * standard C + zlib so they can later be tested on a PC as well.
 */
#ifndef RP_COMMON_H
#define RP_COMMON_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    RP_OK = 0,
    RP_ERR_ARG,            /* invalid argument */
    RP_ERR_NOT_FOUND,      /* stat() failed: file absent or inaccessible */
    RP_ERR_OPEN,           /* fopen() failed although stat() succeeded */
    RP_ERR_SEEK,
    RP_ERR_SHORT_READ,     /* fread returned fewer bytes than requested */
    RP_ERR_WRITE,
    RP_ERR_TOO_LARGE,      /* file larger than the caller's limit */
    RP_ERR_NOMEM,          /* allocation failed or exceeded the allocation cap */
    RP_ERR_TRUNCATED,      /* file or stream shorter than declared */
    RP_ERR_SIZE_MISMATCH,  /* sizes disagree (declared vs actual) */
    RP_ERR_BAD_MAGIC,
    RP_ERR_BAD_VERSION,
    RP_ERR_BAD_HEADER_CRC,
    RP_ERR_BAD_INDEX,      /* structural index error */
    RP_ERR_BAD_INDEX_CRC,
    RP_ERR_COUNT,          /* entry count above RP_MAX_ENTRIES */
    RP_ERR_RANGE,          /* offset/size outside the file */
    RP_ERR_OVERLAP,        /* entries overlap or are out of order */
    RP_ERR_LIMIT,          /* declared size above a safety cap */
    RP_ERR_CRC,            /* payload CRC-32 mismatch */
    RP_ERR_ZLIB_DATA,      /* zlib reported corrupt data */
    RP_ERR_ZLIB,           /* other zlib failure */
    RP_ERR_COUNT_OF_CODES
} rp_err;

const char *rp_err_name(rp_err e);

/* ---- safety limits ---- */
#define RP_MAX_ALLOC        (64u * 1024u * 1024u)  /* single allocation cap */
#define RP_MAX_READ_ALL     (32u * 1024u * 1024u)  /* whole-file read cap */
#define RP_MAX_ENTRIES      65536u                  /* container entry cap */
#define RP_MAX_ENTRY_RAW    (16u * 1024u * 1024u)  /* per-entry decompressed cap */

#endif /* RP_COMMON_H */
