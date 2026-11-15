/*
 * gp_gdat.c - GDAT-like archive index parsing and validation.
 */
#include <stdlib.h>
#include <string.h>

#include "gp_gdat.h"
#include <zlib.h>

#include "rp_inflate.h"
#include "rp_io.h"
#include "rp_mem.h"

typedef struct {
    uint32_t offset;
    uint32_t pos;       /* index position, keeps the sort stable */
} gp_sort_key;

static int cmp_key(const void *x, const void *y)
{
    const gp_sort_key *a = (const gp_sort_key *)x, *b = (const gp_sort_key *)y;
    if (a->offset != b->offset) return a->offset < b->offset ? -1 : 1;
    return a->pos < b->pos ? -1 : (a->pos > b->pos);
}

/* Order / overlap / gap / trailing checks over entries in offset order. */
static gp_status check_layout(gp_gdat *a, uint64_t index_end)
{
    gp_sort_key *keys = NULL;
    uint64_t prev_end = index_end;
    uint32_t i, unsorted = 0;
    gp_status st = RP_OK;

    for (i = 1; i < a->count; i++)
        if (a->idx[i].offset < a->idx[i - 1].offset) { unsorted = 1; break; }
    if (unsorted) {
        if (a->mode == GP_STRICT)
            return GP_ERR_UNSORTED;
        a->obs |= GP_OBS_UNSORTED;
        keys = (gp_sort_key *)rp_malloc((size_t)a->count * sizeof(*keys));
        if (!keys)
            return RP_ERR_NOMEM;
        for (i = 0; i < a->count; i++) { keys[i].offset = a->idx[i].offset; keys[i].pos = i; }
        qsort(keys, a->count, sizeof(*keys), cmp_key);
    }
    for (i = 0; i < a->count; i++) {
        const gp_entry *e = &a->idx[keys ? keys[i].pos : i];
        if ((uint64_t)e->offset < prev_end) { st = RP_ERR_OVERLAP; break; }
        if ((uint64_t)e->offset > prev_end) {
            if (a->mode == GP_STRICT) { st = GP_ERR_GAP; break; }
            a->obs |= GP_OBS_GAP;
        }
        prev_end = (uint64_t)e->offset + e->size;
    }
    rp_free(keys);
    if (st != RP_OK)
        return st;
    if (prev_end < a->file_size) {
        if (a->mode == GP_STRICT)
            return GP_ERR_TRAILING;
        a->obs |= GP_OBS_TRAILING;
    }
    return RP_OK;
}

gp_status gp_gdat_open(gp_gdat *a, const char *path, gp_mode mode)
{
    uint8_t head[8];
    uint8_t *raw = NULL;
    uint64_t index_end;
    uint32_t i;
    gp_status st;

    memset(a, 0, sizeof(*a));
    a->mode = mode;
    st = gp_open_checked(path, &a->fp, &a->file_size);
    if (st != RP_OK)
        return st;

    if (a->file_size < 8) { st = RP_ERR_TRUNCATED; goto fail; }
    st = rp_read_at(a->fp, a->file_size, 0, head, 8);
    if (st != RP_OK) goto fail;
    if (memcmp(head, "GDAT", 4) != 0) { st = RP_ERR_BAD_MAGIC; goto fail; }
    a->count = gp_le32(head + 4);
    if (a->count > GP_MAX_ENTRIES) { st = RP_ERR_COUNT; goto fail; }
    index_end = 8u + 8u * (uint64_t)a->count;
    if (index_end > a->file_size) { st = RP_ERR_TRUNCATED; goto fail; }

    if (a->count == 0) {
        if (mode == GP_STRICT) { st = GP_ERR_EMPTY; goto fail; }
        a->obs |= GP_OBS_EMPTY;
        if (a->file_size > 8) a->obs |= GP_OBS_TRAILING;
        return RP_OK;
    }

    /* Index fits in the file and count <= cap: at most 512 KB. */
    raw = (uint8_t *)rp_malloc((size_t)a->count * 8u);
    a->idx = (gp_entry *)rp_malloc((size_t)a->count * sizeof(gp_entry));
    if (!raw || !a->idx) { st = RP_ERR_NOMEM; goto fail; }
    st = rp_read_at(a->fp, a->file_size, 8, raw, (size_t)a->count * 8u);
    if (st != RP_OK) goto fail;
    for (i = 0; i < a->count; i++) {
        gp_entry *e = &a->idx[i];
        e->offset = gp_le32(raw + 8u * i);
        e->size = gp_le32(raw + 8u * i + 4u);
        if ((uint64_t)e->offset + (uint64_t)e->size > a->file_size) { st = RP_ERR_RANGE; goto fail; }
        if ((uint64_t)e->offset < index_end) { st = GP_ERR_IN_INDEX; goto fail; }
    }
    rp_free(raw);
    raw = NULL;

    st = check_layout(a, index_end);
    if (st != RP_OK) goto fail;
    return RP_OK;

fail:
    rp_free(raw);
    gp_gdat_close(a);
    return st;
}

void gp_gdat_close(gp_gdat *a)
{
    if (a->fp) fclose(a->fp);
    rp_free(a->idx);
    a->fp = NULL;
    a->idx = NULL;
}

uint32_t gp_gdat_index_crc(const gp_gdat *a)
{
    uint8_t pair[8];
    uint32_t i;
    uLong c = crc32(0L, Z_NULL, 0);
    for (i = 0; i < a->count; i++) {
        gp_wr_le32(pair, a->idx[i].offset);
        gp_wr_le32(pair + 4, a->idx[i].size);
        c = crc32(c, pair, 8);
    }
    return (uint32_t)c;
}

gp_status gp_gdat_load(gp_gdat *a, uint32_t i, gp_wrap_info *w, uint8_t **out, size_t *out_len,
                       gp_load_timing *t)
{
    if (!a->fp || i >= a->count)
        return RP_ERR_ARG;
    return gp_wrap_load(a->fp, a->file_size, a->idx[i].offset, a->idx[i].size, a->mode, w, out, out_len, t);
}
