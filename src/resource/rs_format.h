/*
 * rs_format.h - Vita ResPipe resource checks (Phase 10, VDNG00006).
 *
 * Implements the ResPipe-specific checks of RESOURCE_PIPELINE.md (VRS1
 * version 1), section 6.1:
 *   A1  archive entry count                 rs_check_entry_count
 *   A2  entry 0's wrapper header is zero    rs_check_wrapper_record
 *   A3  entry 0's CFP1 envelope is kind 2   rs_check_envelope
 *   A4  common header + table of contents   rs_check_toc
 *   A5  region and compressed-size limits   rs_check_region
 *   C.2 entry 0 seen again by the worker    rs_check_toc_again
 *   C.3 steps 1-9 for every other entry     rs_check_entry
 *       (wrapper header, envelope rule, table-of-contents match, common
 *       header, id and kind, kind-specific checks, format / vram / heap,
 *       texture references, application policy 5.8)
 * The transport checks stay in the unmodified modules: gp_gdat_open and
 * gp_wrap_parse (GDAT Proto), rp_inflate_exact / cf_zinflate_exact and
 * cf_payload_check (ContentFit). rs_cfp1_field names the CFP1 field that
 * cf_payload_check rejected, for the run log (section 4.5).
 *
 * Validation only: no allocation, no GL, no I/O, and no global state is
 * written, so every function may be called from any thread. Inputs are only
 * read, field by field with byte loads (no alignment needed), and every size
 * is computed in 64 bits. The first failure wins; there is no lenient mode.
 *
 * Results are gp_status values: RP_OK, an RS_ERR_* code (300-320), or
 * RP_ERR_ARG when a caller breaks a documented precondition (a NULL pointer,
 * or an entry that cf_payload_check has not accepted).
 */
#ifndef RS_FORMAT_H
#define RS_FORMAT_H

#include <stddef.h>
#include <stdint.h>

#include "gp_common.h"
#include "gp_wrap.h"

/* RESOURCE_PIPELINE.md 6.4 */
enum {
    RS_ERR_BASE = 300,
    RS_ERR_MAGIC = RS_ERR_BASE,     /* 300 not "VRS1" */
    RS_ERR_VERSION,                 /* 301 version not supported */
    RS_ERR_KIND,                    /* 302 unknown kind */
    RS_ERR_HEADER_CRC,              /* 303 header CRC wrong */
    RS_ERR_BODY_SIZE,               /* 304 payload under 32 bytes, or body_size disagrees */
    RS_ERR_BODY_CRC,                /* 305 body CRC wrong */
    RS_ERR_RESERVED,                /* 306 reserved field, padding or wrapper header byte not zero */
    RS_ERR_ENUM,                    /* 307 unknown or disallowed value */
    RS_ERR_RANGE,                   /* 308 block or field outside or misplaced */
    RS_ERR_ALIGN,                   /* 309 offset not a multiple of 16 */
    RS_ERR_DIMENSION,               /* 310 texture size not allowed */
    RS_ERR_MIP_CHAIN,               /* 311 mip count not allowed */
    RS_ERR_LEVEL_SIZE,              /* 312 level size does not match its formula */
    RS_ERR_INDEX,                   /* 313 index >= vertex_count */
    RS_ERR_FLOAT,                   /* 314 NaN or infinite float, or a level size <= 0 */
    RS_ERR_TOC,                     /* 315 table of contents disagrees */
    RS_ERR_DUP_ID,                  /* 316 resource id used twice */
    RS_ERR_REF,                     /* 317 mesh names a texture not in the pack */
    RS_ERR_LIMIT,                   /* 318 count or size above the limits of 5.7 */
    RS_ERR_LEVEL_MISMATCH,          /* 319 application policy: not the reference room */
    RS_ERR_ENVELOPE,                /* 320 CFP1 envelope that is not kind 2 */
    RS_ERR_END
};

#define RS_VERSION          1u
#define RS_HEADER_BYTES     32u                         /* VRS1 common header */
#define RS_ENVELOPE_BYTES   32u                         /* CFP1 envelope */
#define RS_WRAP_HDR_BYTES   0x80u                       /* wrapper header that must be zero */
#define RS_MAX_INFLATED     (8u * 1024u * 1024u)        /* U: CFP1 envelope + VRS1 */
#define RS_MAX_REGION       (8u * 1024u * 1024u)        /* wrapper + compressed stream */
#define RS_MIN_PAYLOAD      32u
#define RS_MAX_PAYLOAD      (RS_MAX_INFLATED - RS_ENVELOPE_BYTES)
#define RS_MIN_ENTRIES      2u
#define RS_MAX_ENTRIES      256u
#define RS_MAX_RESOURCES    255u
#define RS_MAX_TEX_SIDE     2048u
#define RS_MAX_RANGES       16u
#define RS_ZERO124_CRC      0x15D70E0Cu                 /* CRC-32 of 124 zero bytes */
/* The reference room's start cell: START_CELL_X / START_CELL_Z in src/main.c. */
#define RS_ROOM_START_X     6u
#define RS_ROOM_START_Z     6u

enum { RS_KIND_TOC = 1, RS_KIND_TEXTURE = 2, RS_KIND_MESH = 3, RS_KIND_LEVEL = 4 };
enum { RS_FMT_RGBA8 = 1, RS_FMT_RGB565, RS_FMT_DXT1, RS_FMT_DXT5, RS_FMT_ETC1, RS_FMT_PVRTC4 };
#define RS_TEX_REPEAT 1u
#define RS_TEX_LINEAR 2u
#define RS_TEX_GENMIP 4u

/* Where a check failed, for the run log (section 6.6). Static strings only.
 * offset is relative to the buffer the layer names: the region for
 * "wrapper", the inflated entry for "CFP1", the VRS1 payload otherwise. */
typedef struct {
    const char *layer;      /* "archive", "wrapper", "CFP1", "VRS1 header", "table of contents",
                               "texture", "mesh", "level", "policy" */
    const char *check;
    uint32_t    offset;
    int         has_offset;
} rs_diag;

typedef struct {
    const uint8_t *payload;     /* the table of contents' VRS1 payload (caller-owned) */
    uint32_t       payload_size;
    uint32_t       entry_count;
    uint32_t       total_vram_bytes;
    uint32_t       total_heap_bytes;
} rs_toc;

typedef struct {
    uint32_t resource_id;
    uint32_t kind;
    uint32_t format;
    uint32_t payload_size;
    uint32_t payload_crc;
    uint32_t vram_bytes;
    uint32_t heap_bytes;
} rs_row;

/* A resource that passed rs_check_entry. Pointers point into the caller's
 * buffer and are valid only while it is. */
typedef struct {
    uint32_t       resource_id, kind, format;   /* format: texture code, else 0 */
    uint32_t       vram_bytes, heap_bytes;
    const uint8_t *payload;                     /* VRS1 header + body */
    uint32_t       payload_size;
    const uint8_t *body;
    uint32_t       body_size;
    struct { uint32_t width, height, mip_count, flags; } tex;
    struct { uint32_t vertex_count, index_count, range_count, vertex_offset, index_offset; } mesh;
    struct { uint32_t width, depth, start_x, start_z; float cell_size, wall_height; } level;
} rs_resource;

typedef struct {
    uint32_t       width, height, size;
    const uint8_t *data;
} rs_tex_level;

typedef struct {
    uint32_t first_index, index_count, texture_id;
} rs_mesh_range;

/* Name of an RS_ERR_* code ("RS_ERR_MAGIC" ...), or NULL for other values. */
const char *rs_status_name(int code);

/* A1: 2 <= n <= 256, otherwise RS_ERR_LIMIT. */
gp_status rs_check_entry_count(uint32_t n, rs_diag *d);

/* A2: entry 0's wrapper header bytes 0x00-0x7F are zero, judged from GDAT
 * Proto's record of them (tag 0, CRC-32 of 0x04-0x7F = RS_ZERO124_CRC). */
gp_status rs_check_wrapper_record(const gp_wrap_info *w, rs_diag *d);

/* C.3.1: the 0x80 header bytes of a region are zero. */
gp_status rs_check_wrapper_header(const uint8_t *hdr, rs_diag *d);

/* The first CFP1 field that fails, in cf_payload_check's order ("length",
 * "magic", "reserved", "index", "body length", "kind", "body CRC"), or NULL
 * if the envelope passes. For the run log only; reads, never writes. */
const char *rs_cfp1_field(const uint8_t *raw, size_t len, uint32_t index);

/* A3 / C.3.2: a CFP1 envelope that cf_payload_check accepted must be kind 2
 * (RS_ERR_ENVELOPE). len must be >= 32. */
gp_status rs_check_envelope(const uint8_t *raw, size_t len, rs_diag *d);

/* A4: the common header (6.2) and the table-of-contents checks (6.3) of entry
 * 0's VRS1 payload (the inflated entry after its envelope), for an archive of
 * n_archive entries. On RP_OK fills *out, which then refers to 'payload'. */
gp_status rs_check_toc(const uint8_t *payload, size_t len, uint32_t n_archive, rs_toc *out, rs_diag *d);

/* Row i (0-based; row i describes GDAT entry i + 1) of a checked table. */
void rs_toc_row(const rs_toc *t, uint32_t i, rs_row *out);

/* A5 for GDAT entry 'entry' (1 .. entry_count): region <= 8 MiB and
 * region - 0x90 <= U + (U >> 10) + 64 with U = payload_size + 32. */
gp_status rs_check_region(const rs_toc *t, uint32_t entry, uint32_t region_size, rs_diag *d);

/* C.2: entry 0 delivered again must carry the CFP1 body CRC checked at A3.
 * len must be >= 32. */
gp_status rs_check_toc_again(const uint8_t *raw, size_t len, uint32_t open_crc, rs_diag *d);

/* C.3.1-C.3.9 for GDAT entry 'entry' (1 .. entry_count) after
 * cf_payload_check accepted it: wrapper_hdr is the region's first 0x80 bytes,
 * raw the inflated entry (envelope + VRS1, len bytes). apply_policy != 0
 * applies section 5.8. On RP_OK fills *out (which may be NULL). */
gp_status rs_check_entry(const uint8_t *wrapper_hdr, const uint8_t *raw, size_t len, const rs_toc *t,
                         uint32_t entry, int apply_policy, rs_resource *out, rs_diag *d);

/* Level i of a checked texture; 0 on success, -1 if i is out of range. */
int rs_texture_level(const rs_resource *r, uint32_t i, rs_tex_level *out);

/* Range i of a checked mesh; 0 on success, -1 if i is out of range. */
int rs_mesh_range_get(const rs_resource *r, uint32_t i, rs_mesh_range *out);

#endif /* RS_FORMAT_H */
