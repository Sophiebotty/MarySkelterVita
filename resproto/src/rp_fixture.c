/*
 * rp_fixture.c - writes the synthetic test files used by the suite.
 */
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include "rp_container.h"
#include "rp_fixture.h"
#include "rp_inflate.h"
#include "rp_io.h"
#include "rp_mem.h"
#include "rp_time.h"

static const char *const IO_NAMES[RP_IO_FILES] = { "io_4k.bin", "io_1m.bin", "io_8m.bin", "io_16m.bin" };
static const uint32_t IO_SIZES[RP_IO_FILES] = { 4096u, 1u << 20, 8u << 20, 16u << 20 };

/* Names of every file this module writes (used by rp_fixture_delete). */
static const char *const ALL_FILES[] = {
    "io_4k.bin", "io_1m.bin", "io_8m.bin", "io_16m.bin",
    "valid.vrp", "bad_magic.vrp", "bad_version.vrp", "bad_hcrc.vrp", "bad_icrc.vrp",
    "truncated.vrp", "padded.vrp", "range.vrp", "overlap.vrp", "overflow.vrp",
    "hugecount.vrp", "bomb.vrp", "zero.vrp", "empty.bin",
};

const char *rp_work_path(const char *name, char *buf, size_t n)
{
    snprintf(buf, n, "%s/%s", RP_WORK_DIR, name);
    return buf;
}

/* ---- deterministic generators ---- */

static uint32_t mix32(uint32_t x)
{
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/* Random-access pseudo-random bytes: byte at absolute position 'pos' depends
 * only on (seed, pos), so any range can be regenerated for verification. */
void rp_gen_random_at(uint32_t seed, uint64_t pos, uint8_t *buf, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        uint64_t p = pos + i;
        uint32_t w = mix32(seed ^ (uint32_t)(p >> 2) * 0x9E3779B1u ^ (uint32_t)(p >> 34));
        buf[i] = (uint8_t)(w >> ((p & 3u) * 8u));
    }
}

/* Compressible text-like data built from an arbitrary syllable list. */
void rp_gen_text(uint32_t seed, uint8_t *buf, size_t n)
{
    static const char *const SYL[] = {
        "ka", "ri", "to", "mu", "sen", "dal", "vo", "ne", "lis", "par", "qu", "eb",
        "or", "tin", "sa", "bel", "go", "fy", "ram", "il",
    };
    uint32_t s = seed ? seed : 1u;
    size_t i = 0;
    while (i < n) {
        int syl = 1 + (int)(mix32(s++) % 3u), k;
        for (k = 0; k < syl && i < n; k++) {
            const char *t = SYL[mix32(s++) % (sizeof(SYL) / sizeof(SYL[0]))];
            while (*t && i < n) buf[i++] = (uint8_t)*t++;
        }
        if (i < n) {
            uint32_t r = mix32(s++) % 16u;
            buf[i++] = (uint8_t)(r == 0 ? '.' : (r == 1 ? '\n' : ' '));
        }
    }
}

void rp_gen_payload(rp_payload_kind kind, uint32_t seed, uint8_t *buf, size_t n)
{
    if (kind == RP_PAY_TEXT)        rp_gen_text(seed, buf, n);
    else if (kind == RP_PAY_RANDOM) rp_gen_random_at(seed, 0, buf, n);
    else                            memset(buf, 0, n);
}

/* ---- writers ---- */

static rp_err write_io_file(rp_io_file *f)
{
    char path[96];
    const size_t chunk = 256u * 1024u;
    uint8_t *buf = (uint8_t *)rp_malloc(chunk);
    FILE *fp;
    uint64_t done = 0;
    uLong crc = crc32(0L, Z_NULL, 0);
    rp_err e = RP_OK;

    if (!buf)
        return RP_ERR_NOMEM;
    fp = fopen(rp_work_path(f->name, path, sizeof(path)), "wb");
    if (!fp) { rp_free(buf); return RP_ERR_WRITE; }
    while (done < f->size) {
        size_t n = (f->size - done) < chunk ? (size_t)(f->size - done) : chunk;
        rp_gen_random_at(f->seed, done, buf, n);
        if (fwrite(buf, 1, n, fp) != n) { e = RP_ERR_WRITE; break; }
        crc = crc32(crc, buf, (uInt)n);     /* incremental CRC-32 of what was written */
        done += n;
    }
    if (fclose(fp) != 0 && e == RP_OK)
        e = RP_ERR_WRITE;
    rp_free(buf);
    f->crc = (uint32_t)crc;
    return e;
}

static rp_err write_buf(const char *name, const uint8_t *data, size_t n)
{
    char path[96];
    return rp_write_file(rp_work_path(name, path, sizeof(path)), data, n);
}

/* Writes a patched copy of the valid container. 'fix' = 0 none, 1 both CRCs,
 * 2 header CRC only. */
static rp_err write_variant(const char *name, const uint8_t *valid, size_t len, size_t write_len,
                            void (*patch)(uint8_t *), int fix)
{
    uint8_t *b = (uint8_t *)rp_malloc(len);
    rp_err e;
    if (!b) return RP_ERR_NOMEM;
    memcpy(b, valid, len);
    if (patch) patch(b);
    if (fix == 1) rp_container_fix_crcs(b, len);
    if (fix == 2) rp_wr32(b + 28, rp_crc32(b, 28));
    e = write_buf(name, b, write_len);
    rp_free(b);
    return e;
}

#define ENT(b, i) ((b) + RP_VRP_HEADER_SIZE + (size_t)(i) * RP_VRP_ENTRY_SIZE)
static void p_magic(uint8_t *b)    { b[0] = 'X'; }
static void p_version(uint8_t *b)  { b[4] = 2; b[5] = 0; }
static void p_hcrc(uint8_t *b)     { b[28] ^= 0xFF; }
static void p_icrc(uint8_t *b)     { ENT(b, 0)[20] ^= 0x01; }           /* reserved byte; index CRC left stale */
static void p_range(uint8_t *b)    { rp_wr32(ENT(b, RP_VALID_ENTRIES - 2), rp_rd32(b + 16) - 4u); }
static void p_overlap(uint8_t *b)  { rp_wr32(ENT(b, 1), rp_rd32(ENT(b, 0))); }
static void p_overflow(uint8_t *b) { rp_wr32(ENT(b, 0), 0xFFFFFF00u); rp_wr32(ENT(b, 0) + 4, 0x200u); }
static void p_hugecount(uint8_t *b){ rp_wr32(b + 8, 0x10000000u); }
static void p_bomb(uint8_t *b)     { rp_wr32(ENT(b, 0) + 8, 0x7FFFFFFFu); }

rp_err rp_fixture_generate(rp_fixture_set *fs, rp_progress_fn progress)
{
    rp_item items[RP_VALID_ENTRIES];
    uint8_t *payload[RP_VALID_ENTRIES];
    uint8_t *valid = NULL, *pad = NULL;
    size_t vlen = 0;
    double t0;
    int i;
    rp_err e = RP_OK;

    memset(fs, 0, sizeof(*fs));
    memset(payload, 0, sizeof(payload));
    rp_mkdir("ux0:data");
    rp_mkdir(RP_BASE_DIR);
    rp_mkdir(RP_WORK_DIR);

#define FAIL(nm) do { snprintf(fs->failed_file, sizeof(fs->failed_file), "%s", nm); goto out; } while (0)

    /* 1. plain I/O files */
    t0 = rp_now_ms();
    for (i = 0; i < RP_IO_FILES; i++) {
        fs->io[i].name = IO_NAMES[i];
        fs->io[i].size = IO_SIZES[i];
        fs->io[i].seed = 0x1000u + (uint32_t)i;
        if (progress) progress(IO_NAMES[i]);
        e = write_io_file(&fs->io[i]);
        if (e != RP_OK) FAIL(IO_NAMES[i]);
        fs->write_bytes += IO_SIZES[i];
    }
    fs->write_ms = rp_now_ms() - t0;

    /* 2. valid container: 48 entries, 1 KB .. 128 KB, mixed kinds;
     *    text/zero stored zlib-compressed, random stored raw; last entry empty */
    if (progress) progress("valid.vrp");
    for (i = 0; i < RP_VALID_ENTRIES; i++) {
        rp_payload_kind k = (rp_payload_kind)(i % RP_PAY_KINDS);
        uint32_t sz = (i == RP_VALID_ENTRIES - 1) ? 0u : (1024u << (i % 8));
        payload[i] = sz ? (uint8_t *)rp_malloc(sz) : NULL;
        if (sz && !payload[i]) { e = RP_ERR_NOMEM; FAIL("valid.vrp"); }
        if (sz) rp_gen_payload(k, 0x2000u + (uint32_t)i, payload[i], sz);
        items[i].data = payload[i];
        items[i].size = sz;
        items[i].compress = (k != RP_PAY_RANDOM);
    }
    e = rp_container_build(items, RP_VALID_ENTRIES, &valid, &vlen);
    if (e != RP_OK) FAIL("valid.vrp");
    if ((e = write_buf("valid.vrp", valid, vlen)) != RP_OK) FAIL("valid.vrp");

    /* 3. invalid variants, each isolating one validation failure */
    if (progress) progress("invalid variants");
    if ((e = write_variant("bad_magic.vrp",   valid, vlen, vlen, p_magic, 0)) != RP_OK) FAIL("bad_magic.vrp");
    if ((e = write_variant("bad_version.vrp", valid, vlen, vlen, p_version, 1)) != RP_OK) FAIL("bad_version.vrp");
    if ((e = write_variant("bad_hcrc.vrp",    valid, vlen, vlen, p_hcrc, 0)) != RP_OK) FAIL("bad_hcrc.vrp");
    if ((e = write_variant("bad_icrc.vrp",    valid, vlen, vlen, p_icrc, 2)) != RP_OK) FAIL("bad_icrc.vrp");
    if ((e = write_variant("truncated.vrp",   valid, vlen, vlen - 1000u, NULL, 0)) != RP_OK) FAIL("truncated.vrp");
    if ((e = write_variant("range.vrp",       valid, vlen, vlen, p_range, 1)) != RP_OK) FAIL("range.vrp");
    if ((e = write_variant("overlap.vrp",     valid, vlen, vlen, p_overlap, 1)) != RP_OK) FAIL("overlap.vrp");
    if ((e = write_variant("overflow.vrp",    valid, vlen, vlen, p_overflow, 1)) != RP_OK) FAIL("overflow.vrp");
    if ((e = write_variant("hugecount.vrp",   valid, vlen, vlen, p_hugecount, 1)) != RP_OK) FAIL("hugecount.vrp");
    if ((e = write_variant("bomb.vrp",        valid, vlen, vlen, p_bomb, 1)) != RP_OK) FAIL("bomb.vrp");

    pad = (uint8_t *)rp_malloc(vlen + 16u);           /* actual size > declared */
    if (!pad) { e = RP_ERR_NOMEM; FAIL("padded.vrp"); }
    memcpy(pad, valid, vlen);
    memset(pad + vlen, 0, 16u);
    if ((e = write_buf("padded.vrp", pad, vlen + 16u)) != RP_OK) FAIL("padded.vrp");

    {   /* zero-entry container and an empty file */
        uint8_t *z = NULL; size_t zl = 0;
        if ((e = rp_container_build(NULL, 0, &z, &zl)) != RP_OK) FAIL("zero.vrp");
        e = write_buf("zero.vrp", z, zl);
        rp_free(z);
        if (e != RP_OK) FAIL("zero.vrp");
    }
    if ((e = write_buf("empty.bin", NULL, 0)) != RP_OK) FAIL("empty.bin");

out:
#undef FAIL
    for (i = 0; i < RP_VALID_ENTRIES; i++) if (payload[i]) rp_free(payload[i]);
    if (valid) rp_free(valid);
    if (pad) rp_free(pad);
    fs->status = e;
    return e;
}

int rp_fixture_present(char *missing_name, size_t missing_len)
{
    char path[96];
    uint64_t sz;
    size_t i;
    for (i = 0; i < sizeof(ALL_FILES) / sizeof(ALL_FILES[0]); i++) {
        if (rp_file_size(rp_work_path(ALL_FILES[i], path, sizeof(path)), &sz) != RP_OK) {
            if (missing_name && missing_len)
                snprintf(missing_name, missing_len, "%s", ALL_FILES[i]);
            return 0;
        }
    }
    return 1;
}

int rp_fixture_delete(void)
{
    char path[96];
    size_t i;
    int removed = 0;
    for (i = 0; i < sizeof(ALL_FILES) / sizeof(ALL_FILES[0]); i++)
        if (remove(rp_work_path(ALL_FILES[i], path, sizeof(path))) == 0)
            removed++;
    return removed;
}
