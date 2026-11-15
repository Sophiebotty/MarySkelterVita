/*
 * rp_container.c - VRP1 reader with strict validation, and builder.
 *
 * Validation order (each failure returns before any large allocation):
 *   size >= header -> magic -> version -> header CRC -> declared size vs
 *   actual -> entry count cap -> index bounds -> index CRC -> per-entry
 *   range / order / flags / size caps.
 */
#include <string.h>

#include "rp_container.h"
#include "rp_inflate.h"
#include "rp_io.h"
#include "rp_mem.h"

uint32_t rp_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
void rp_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static void fail_close(rp_container *c)
{
    if (c->entries) rp_free(c->entries);
    if (c->fp) fclose(c->fp);
    memset(c, 0, sizeof(*c));
}

rp_err rp_container_open(rp_container *c, const char *path)
{
    uint8_t h[RP_VRP_HEADER_SIZE];
    uint8_t *idx = NULL;
    uint32_t count, index_off, declared, data_off, idx_crc;
    uint64_t idx_end, prev_end;
    uint32_t i;
    rp_err e;

    if (!c || !path)
        return RP_ERR_ARG;
    memset(c, 0, sizeof(*c));

    e = rp_open(path, &c->fp, &c->file_size);
    if (e != RP_OK) { fail_close(c); return e; }
    if (c->file_size < RP_VRP_HEADER_SIZE) { fail_close(c); return RP_ERR_TRUNCATED; }

    e = rp_read_at(c->fp, c->file_size, 0, h, sizeof(h));
    if (e != RP_OK) { fail_close(c); return e; }

    if (memcmp(h, "VRP1", 4) != 0)                  { fail_close(c); return RP_ERR_BAD_MAGIC; }
    if (rd16(h + 4) != RP_VRP_VERSION)              { fail_close(c); return RP_ERR_BAD_VERSION; }
    if (rp_crc32(h, 28) != rp_rd32(h + 28))         { fail_close(c); return RP_ERR_BAD_HEADER_CRC; }

    count     = rp_rd32(h + 8);
    index_off = rp_rd32(h + 12);
    declared  = rp_rd32(h + 16);
    data_off  = rp_rd32(h + 20);
    idx_crc   = rp_rd32(h + 24);

    if (c->file_size < declared)                    { fail_close(c); return RP_ERR_TRUNCATED; }
    if (c->file_size > declared)                    { fail_close(c); return RP_ERR_SIZE_MISMATCH; }
    if (count > RP_MAX_ENTRIES)                     { fail_close(c); return RP_ERR_COUNT; }
    if (index_off != RP_VRP_HEADER_SIZE)            { fail_close(c); return RP_ERR_BAD_INDEX; }
    idx_end = (uint64_t)RP_VRP_HEADER_SIZE + (uint64_t)count * RP_VRP_ENTRY_SIZE;
    if (idx_end > declared || data_off != idx_end)  { fail_close(c); return RP_ERR_BAD_INDEX; }

    c->count = count;
    c->data_offset = data_off;
    if (count == 0)
        return RP_OK;

    idx = (uint8_t *)rp_malloc((size_t)count * RP_VRP_ENTRY_SIZE);
    c->entries = (rp_entry *)rp_malloc((size_t)count * sizeof(rp_entry));
    if (!idx || !c->entries) {
        if (idx) rp_free(idx);
        fail_close(c);
        return RP_ERR_NOMEM;
    }
    e = rp_read_at(c->fp, c->file_size, index_off, idx, (size_t)count * RP_VRP_ENTRY_SIZE);
    if (e == RP_OK && rp_crc32(idx, (size_t)count * RP_VRP_ENTRY_SIZE) != idx_crc)
        e = RP_ERR_BAD_INDEX_CRC;

    prev_end = data_off;
    for (i = 0; e == RP_OK && i < count; i++) {
        const uint8_t *p = idx + (size_t)i * RP_VRP_ENTRY_SIZE;
        rp_entry *en = &c->entries[i];
        en->offset = rp_rd32(p);       en->stored_size = rp_rd32(p + 4);
        en->raw_size = rp_rd32(p + 8); en->flags = rp_rd32(p + 12);
        en->crc32 = rp_rd32(p + 16);   en->reserved = rp_rd32(p + 20);

        if (en->flags & ~RP_VRP_FLAG_ZLIB)                                      e = RP_ERR_BAD_INDEX;
        else if (en->offset < data_off)                                         e = RP_ERR_RANGE;
        else if ((uint64_t)en->offset + en->stored_size > declared)             e = RP_ERR_RANGE;
        else if ((uint64_t)en->offset < prev_end)                               e = RP_ERR_OVERLAP;
        else if (en->raw_size > RP_MAX_ENTRY_RAW)                               e = RP_ERR_LIMIT;
        else if (!(en->flags & RP_VRP_FLAG_ZLIB) && en->stored_size != en->raw_size) e = RP_ERR_BAD_INDEX;
        prev_end = (uint64_t)en->offset + en->stored_size;
    }
    rp_free(idx);
    if (e != RP_OK) { fail_close(c); return e; }
    return RP_OK;
}

rp_err rp_container_load(rp_container *c, uint32_t i, uint8_t **out, uint32_t *out_len)
{
    const rp_entry *en;
    uint8_t *stored, *raw;
    rp_err e;

    if (!c || !c->fp || !out || !out_len || i >= c->count)
        return RP_ERR_ARG;
    *out = NULL; *out_len = 0;
    en = &c->entries[i];
    if (en->raw_size == 0)
        return en->crc32 == 0 ? RP_OK : RP_ERR_CRC;   /* CRC-32 of empty data is 0 */

    stored = (uint8_t *)rp_malloc(en->stored_size ? en->stored_size : 1);
    if (!stored)
        return RP_ERR_NOMEM;
    e = rp_read_at(c->fp, c->file_size, en->offset, stored, en->stored_size);
    if (e != RP_OK) { rp_free(stored); return e; }

    if (en->flags & RP_VRP_FLAG_ZLIB) {
        raw = (uint8_t *)rp_malloc(en->raw_size);
        if (!raw) { rp_free(stored); return RP_ERR_NOMEM; }
        e = rp_inflate_exact(stored, en->stored_size, raw, en->raw_size);
        rp_free(stored);
        if (e != RP_OK) { rp_free(raw); return e; }
    } else {
        raw = stored;
    }
    if (rp_crc32(raw, en->raw_size) != en->crc32) {
        rp_free(raw);
        return RP_ERR_CRC;
    }
    *out = raw;
    *out_len = en->raw_size;
    return RP_OK;
}

void rp_container_close(rp_container *c)
{
    if (c) fail_close(c);
}

void rp_container_fix_crcs(uint8_t *buf, size_t len)
{
    uint32_t count;
    size_t idx_bytes;
    if (len < RP_VRP_HEADER_SIZE)
        return;
    count = rp_rd32(buf + 8);
    idx_bytes = (size_t)count * RP_VRP_ENTRY_SIZE;
    if (RP_VRP_HEADER_SIZE + idx_bytes <= len)
        rp_wr32(buf + 24, rp_crc32(buf + RP_VRP_HEADER_SIZE, idx_bytes));
    rp_wr32(buf + 28, rp_crc32(buf, 28));
}

rp_err rp_container_build(const rp_item *items, uint32_t n, uint8_t **out, size_t *out_len)
{
    uint8_t **stored = NULL;
    size_t *stored_len = NULL;
    size_t total, pos;
    uint32_t i;
    uint8_t *buf;
    rp_err e = RP_OK;

    if (!out || !out_len || (n && !items) || n > RP_MAX_ENTRIES)
        return RP_ERR_ARG;
    if (n) {
        stored = (uint8_t **)rp_malloc(n * sizeof(*stored));
        stored_len = (size_t *)rp_malloc(n * sizeof(*stored_len));
        if (!stored || !stored_len) { e = RP_ERR_NOMEM; goto done; }
        memset(stored, 0, n * sizeof(*stored));
    }
    total = RP_VRP_HEADER_SIZE + (size_t)n * RP_VRP_ENTRY_SIZE;
    for (i = 0; i < n; i++) {
        if (items[i].compress && items[i].size) {
            e = rp_deflate_alloc(items[i].data, items[i].size, 9, &stored[i], &stored_len[i]);
            if (e != RP_OK) goto done;
        } else {
            stored_len[i] = items[i].size;
        }
        total += stored_len[i];
    }
    if (total > 0xFFFFFFFFu) { e = RP_ERR_LIMIT; goto done; }

    buf = (uint8_t *)rp_malloc(total);
    if (!buf) { e = RP_ERR_NOMEM; goto done; }
    memset(buf, 0, RP_VRP_HEADER_SIZE + (size_t)n * RP_VRP_ENTRY_SIZE);
    memcpy(buf, "VRP1", 4);
    wr16(buf + 4, RP_VRP_VERSION);
    wr16(buf + 6, 0);
    rp_wr32(buf + 8, n);
    rp_wr32(buf + 12, RP_VRP_HEADER_SIZE);
    rp_wr32(buf + 16, (uint32_t)total);
    rp_wr32(buf + 20, RP_VRP_HEADER_SIZE + n * RP_VRP_ENTRY_SIZE);

    pos = RP_VRP_HEADER_SIZE + (size_t)n * RP_VRP_ENTRY_SIZE;
    for (i = 0; i < n; i++) {
        uint8_t *ent = buf + RP_VRP_HEADER_SIZE + (size_t)i * RP_VRP_ENTRY_SIZE;
        int z = items[i].compress && items[i].size;
        rp_wr32(ent, (uint32_t)pos);
        rp_wr32(ent + 4, (uint32_t)stored_len[i]);
        rp_wr32(ent + 8, items[i].size);
        rp_wr32(ent + 12, z ? RP_VRP_FLAG_ZLIB : 0);
        rp_wr32(ent + 16, items[i].size ? rp_crc32(items[i].data, items[i].size) : 0);
        rp_wr32(ent + 20, 0);
        if (stored_len[i])
            memcpy(buf + pos, z ? stored[i] : items[i].data, stored_len[i]);
        pos += stored_len[i];
    }
    rp_container_fix_crcs(buf, total);
    *out = buf;
    *out_len = total;

done:
    if (stored) {
        for (i = 0; i < n; i++) if (stored[i]) rp_free(stored[i]);
        rp_free(stored);
    }
    if (stored_len) rp_free(stored_len);
    return e;
}
