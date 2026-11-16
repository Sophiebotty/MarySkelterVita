/*
 * cf_io.c - C1: storage reads.
 *
 * Every read is verified against the CRC the PC generator recorded. The copy
 * in ux0:data/ContentFit/work/ is created once (never replaced or deleted)
 * so the same file can be read from app0: and from ux0:data.
 */
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include "cf_fixdefs.h"
#include "cf_paths.h"
#include "cf_suite.h"
#include "rp_inflate.h"
#include "rp_io.h"
#include "rp_mem.h"
#include "rp_time.h"

#define IO_APP0  "app0:fixtures/io16.bin"
#define IO_WORK  "ux0:data/ContentFit/work/io16.bin"
#define BUF_SIZE (8u * CF_MIB)
#define BLOCK    (16u * CF_KIB)
#define NBLOCKS  512u

static gp_status read_stdio(const char *path, uint32_t chunk, uint8_t *buf, uint32_t *crc, uint64_t *len,
                            double *ms)
{
    double t0 = rp_now_ms();
    uLong c = crc32(0L, Z_NULL, 0);
    uint64_t total = 0;
    FILE *f;
    size_t got;
    if (!cf_path_read_ok(path))
        return CF_ERR_PATH;
    f = fopen(path, "rb");
    if (!f)
        return RP_ERR_OPEN;
    while ((got = fread(buf, 1, chunk, f)) > 0) {
        c = crc32(c, buf, (uInt)got);
        total += got;
    }
    fclose(f);
    *ms = rp_now_ms() - t0;
    *crc = (uint32_t)c;
    *len = total;
    return RP_OK;
}

static gp_status read_raw(const char *path, uint32_t chunk, uint8_t *buf, uint32_t *crc, uint64_t *len, double *ms)
{
    double t0 = rp_now_ms();
    uLong c = crc32(0L, Z_NULL, 0);
    uint64_t total = 0;
    int64_t got;
    int fd = cf_raw_open(path);
    if (fd < 0)
        return cf_path_read_ok(path) ? RP_ERR_OPEN : CF_ERR_PATH;
    while ((got = cf_raw_read(fd, buf, chunk)) > 0) {
        c = crc32(c, buf, (uInt)got);
        total += (uint64_t)got;
    }
    cf_raw_close(fd);
    *ms = rp_now_ms() - t0;
    *crc = (uint32_t)c;
    *len = total;
    return got < 0 ? RP_ERR_SHORT_READ : RP_OK;
}

static void finish_read(cf_ctx *c, cf_result *r, gp_status st, uint32_t crc, uint64_t len, double ms)
{
    r->status = st;
    if (st == RP_OK && (len != cf_fix_io_size() || crc != cf_fix_io_crc()))
        r->status = RP_ERR_CRC;
    r->ok = r->status == RP_OK;
    r->ms = ms;
    r->mbytes = cf_mib(len);
    r->mbps = ms > 0.0 ? r->mbytes / (ms / 1000.0) : 0.0;
    r->check = r->ok ? crc : 0;
    if (r->detail[0] == '\0')
        snprintf(r->detail, sizeof(r->detail), "%llu bytes; CRC %08x (expected %08x)", (unsigned long long)len,
                 (unsigned)crc, (unsigned)cf_fix_io_crc());
    cf_done(c, r);
}

static void one_read(cf_ctx *c, uint8_t *buf, const char *id, const char *name, const char *mode, const char *path,
                     int raw, uint32_t chunk)
{
    cf_result *r;
    uint32_t crc = 0;
    uint64_t len = 0;
    double ms = 0.0;
    gp_status st;
    if (cf_should_stop(c)) return;
    cf_progress(c, id, name);
    r = cf_add(c, id, name, mode);
    st = raw ? read_raw(path, chunk, buf, &crc, &len, &ms) : read_stdio(path, chunk, buf, &crc, &len, &ms);
    finish_read(c, r, st, crc, len, ms);
}

/* C1-09: make sure ux0:data/ContentFit/work/io16.bin holds the fixture. */
static void work_copy(cf_ctx *c, uint8_t *buf)
{
    cf_result *r;
    uint32_t crc = 0;
    uint64_t len = 0;
    double ms = 0.0;
    cf_env e;

    cf_progress(c, "C1-09", "WORK COPY ON UX0");
    r = cf_add(c, "C1-09", "WORK_COPY_UX0", "UX0");
    if (c->work_state != 0) {
        r->status = c->work_state == 1 ? RP_OK : CF_ERR_SKIPPED;
        r->ok = 1;
        r->check = c->work_state == 1 ? cf_fix_io_crc() : 0;
        snprintf(r->verdict, sizeof(r->verdict), c->work_state == 1 ? "READY" : "UNAVAILABLE");
        snprintf(r->detail, sizeof(r->detail), "checked in iteration 1: %s", c->work_note);
        cf_done(c, r);
        return;
    }
    rp_mkdir(CF_OUT_DIR);
    rp_mkdir(CF_WORK_DIR);
    if (cf_exists_stat(IO_WORK, NULL)) {
        gp_status st = read_raw(IO_WORK, CF_MIB, buf, &crc, &len, &ms);
        if (st == RP_OK && len == cf_fix_io_size() && crc == cf_fix_io_crc()) {
            c->work_state = 1;
            snprintf(c->work_note, sizeof(c->work_note), "existing copy verified (CRC %08x)", (unsigned)crc);
            snprintf(r->verdict, sizeof(r->verdict), "VERIFIED");
        } else {
            c->work_state = -1;
            snprintf(c->work_note, sizeof(c->work_note),
                     "existing copy does not match the fixture; left unchanged; ux0 reads skipped");
            snprintf(r->verdict, sizeof(r->verdict), "MISMATCH");
        }
    } else {
        FILE *f = NULL;
        gp_status st;
        cf_plat_env(&e);
        if (e.ux0_free >= 0 && (uint64_t)e.ux0_free < cf_fix_io_size() + 24ull * CF_MIB) {
            c->work_state = -1;
            snprintf(c->work_note, sizeof(c->work_note), "ux0 has %.1f MiB free; copy not made",
                     cf_mib((uint64_t)e.ux0_free));
            snprintf(r->verdict, sizeof(r->verdict), "NO_SPACE");
        } else if ((st = cf_create_new(IO_WORK, &f)) != RP_OK) {
            c->work_state = -1;
            snprintf(c->work_note, sizeof(c->work_note), "could not create the copy: %s", cf_status_name(st));
            snprintf(r->verdict, sizeof(r->verdict), "NOT_CREATED");
        } else {
            int fd = cf_raw_open(IO_APP0);
            int64_t got;
            double t0 = rp_now_ms();
            int okw = fd >= 0;
            while (okw && (got = cf_raw_read(fd, buf, CF_MIB)) > 0)
                okw = fwrite(buf, 1, (size_t)got, f) == (size_t)got;
            cf_raw_close(fd);
            okw = (fclose(f) == 0) && okw;
            ms = rp_now_ms() - t0;
            r->ms = ms;
            r->mbytes = cf_mib(cf_fix_io_size());
            r->mbps = ms > 0.0 ? r->mbytes / (ms / 1000.0) : 0.0;
            if (okw && read_raw(IO_WORK, CF_MIB, buf, &crc, &len, &ms) == RP_OK && len == cf_fix_io_size() &&
                crc == cf_fix_io_crc()) {
                c->work_state = 1;
                snprintf(c->work_note, sizeof(c->work_note), "copied and verified (CRC %08x)", (unsigned)crc);
                snprintf(r->verdict, sizeof(r->verdict), "COPIED");
            } else {
                c->work_state = -1;
                snprintf(c->work_note, sizeof(c->work_note), "copy written but did not verify; ux0 reads skipped");
                snprintf(r->verdict, sizeof(r->verdict), "BAD_COPY");
            }
        }
    }
    r->status = c->work_state == 1 ? RP_OK : CF_ERR_SKIPPED;
    r->ok = 1;
    r->check = c->work_state == 1 ? cf_fix_io_crc() : 0;
    snprintf(r->detail, sizeof(r->detail), "%s (write rate shown only when the copy was made now)", c->work_note);
    cf_done(c, r);
}

static void ux0_read(cf_ctx *c, uint8_t *buf, const char *id, const char *name, int raw)
{
    cf_result *r;
    if (cf_should_stop(c)) return;
    if (c->work_state == 1) {
        one_read(c, buf, id, name, "UX0", IO_WORK, raw, CF_MIB);
        return;
    }
    r = cf_add(c, id, name, "UX0");
    r->status = CF_ERR_SKIPPED;
    r->ok = 1;
    snprintf(r->verdict, sizeof(r->verdict), "SKIPPED");
    snprintf(r->detail, sizeof(r->detail), "no verified copy on ux0: %s", c->work_note);
    cf_done(c, r);
}

/* C1-12 / C1-13: 512 reads of 16 KiB covering the first 8 MiB, in order or
 * in a fixed shuffled order; blocks land at their own offsets in buf. */
static void block_reads(cf_ctx *c, uint8_t *buf, int shuffled)
{
    static uint16_t order[NBLOCKS];
    const char *id = shuffled ? "C1-13" : "C1-12";
    cf_result *r;
    uint32_t i, seed = 0x2545F491u;
    double t0;
    int fd;

    if (cf_should_stop(c)) return;
    cf_progress(c, id, shuffled ? "RANDOM 16 KIB READS" : "SEQUENTIAL 16 KIB READS");
    r = cf_add(c, id, shuffled ? "RANDOM_512x16K" : "SEQUENTIAL_512x16K", "APP0");
    for (i = 0; i < NBLOCKS; i++) order[i] = (uint16_t)i;
    if (shuffled) {
        for (i = NBLOCKS - 1; i > 0; i--) {
            uint32_t j;
            uint16_t t;
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            j = seed % (i + 1);
            t = order[i]; order[i] = order[j]; order[j] = t;
        }
    }
    fd = cf_raw_open(IO_APP0);
    if (fd < 0) {
        r->status = RP_ERR_OPEN;
        cf_done(c, r);
        return;
    }
    t0 = rp_now_ms();
    for (i = 0; i < NBLOCKS && r->status == RP_OK; i++)
        if (cf_raw_pread(fd, buf + (size_t)order[i] * BLOCK, BLOCK, (uint64_t)order[i] * BLOCK) != (int64_t)BLOCK)
            r->status = RP_ERR_SHORT_READ;
    r->ms = rp_now_ms() - t0;
    cf_raw_close(fd);
    r->mbytes = cf_mib((uint64_t)NBLOCKS * BLOCK);
    r->mbps = r->ms > 0.0 ? r->mbytes / (r->ms / 1000.0) : 0.0;
    if (r->status == RP_OK) {
        uint32_t crc = rp_crc32(buf, (size_t)NBLOCKS * BLOCK);
        r->status = crc == cf_fix_io_half_crc() ? RP_OK : RP_ERR_CRC;
        r->check = r->status == RP_OK ? crc : 0;
    }
    r->ok = r->status == RP_OK;
    snprintf(r->detail, sizeof(r->detail), "%.3f ms per read; first 8 MiB CRC %s", r->ms / NBLOCKS,
             r->ok ? "matches" : "DIFFERS");
    cf_done(c, r);
}

void cf_group_io(cf_ctx *c)
{
    static const struct { const char *id, *name; int raw; uint32_t chunk; } T[] = {
        {"C1-01", "STDIO_16K", 0, 16u * CF_KIB}, {"C1-02", "STDIO_64K", 0, 64u * CF_KIB},
        {"C1-03", "STDIO_256K", 0, 256u * CF_KIB}, {"C1-04", "STDIO_1M", 0, CF_MIB},
        {"C1-05", "STDIO_4M", 0, 4u * CF_MIB}, {"C1-06", "RAW_64K", 1, 64u * CF_KIB},
        {"C1-07", "RAW_1M", 1, CF_MIB}, {"C1-08", "RAW_4M", 1, 4u * CF_MIB},
    };
    cf_pools now;
    cf_need need;
    char why[128];
    uint8_t *buf;
    unsigned i;
    gp_status st;

    cf_plat_pools(&now);
    memset(&need, 0, sizeof(need));
    need.b[CF_POOL_HEAP] = BUF_SIZE;
    st = cf_precheck(&now, &need, why, sizeof(why));
    buf = st == RP_OK ? (uint8_t *)rp_malloc(BUF_SIZE) : NULL;
    if (!buf) {
        cf_result *r = cf_add(c, "C1-01", "STORAGE_READS", "-");
        r->status = st != RP_OK ? st : RP_ERR_NOMEM;
        r->ok = st == CF_ERR_SKIPPED || st == CF_ERR_UNKNOWN_SIZE;
        snprintf(r->verdict, sizeof(r->verdict), "SKIPPED");
        snprintf(r->detail, sizeof(r->detail), "8 MiB read buffer: %s", st != RP_OK ? why : "allocation failed");
        cf_done(c, r);
        return;
    }
    for (i = 0; i < sizeof(T) / sizeof(T[0]); i++)
        one_read(c, buf, T[i].id, T[i].name, "APP0", IO_APP0, T[i].raw, T[i].chunk);
    if (!cf_should_stop(c)) work_copy(c, buf);
    ux0_read(c, buf, "C1-10", "RAW_1M_UX0", 1);
    ux0_read(c, buf, "C1-11", "STDIO_1M_UX0", 0);
    block_reads(c, buf, 0);
    block_reads(c, buf, 1);
    one_read(c, buf, "C1-14", "FIRST_OF_PAIR_RAW_1M", "APP0", IO_APP0, 1, CF_MIB);
    one_read(c, buf, "C1-15", "REPEAT_RAW_1M", "APP0", IO_APP0, 1, CF_MIB);
    rp_free(buf);
}
