/*
 * gp_csh.c - CSH-like wrapper validation. Records stay opaque.
 */
#include <string.h>

#include "gp_csh.h"
#include "gp_gdat.h"
#include "rp_inflate.h"
#include "rp_io.h"
#include "rp_mem.h"

gp_status gp_csh_check(const char *csh_path, const char *pair_path, gp_mode mode, gp_csh_info *ci)
{
    uint8_t hdr[GP_WRAP_TOTAL];
    uint8_t *recs = NULL;
    size_t recs_len = 0;
    FILE *fp = NULL;
    uint64_t size = 0, expect;
    gp_gdat pair;
    gp_status st;
    int i, field_ok;

    memset(ci, 0, sizeof(*ci));
    st = gp_open_checked(csh_path, &fp, &size);
    if (st != RP_OK)
        return st;
    if (size < GP_WRAP_TOTAL) { st = RP_ERR_TRUNCATED; goto done; }
    if (size > 0xFFFFFFFFu) { st = RP_ERR_LIMIT; goto done; }
    st = rp_read_at(fp, size, 0, hdr, sizeof(hdr));
    if (st != RP_OK) goto done;
    if (memcmp(hdr, "\0hsc", 4) != 0) { st = RP_ERR_BAD_MAGIC; goto done; }

    for (i = 0; i < 3; i++) ci->field[i] = gp_be32(hdr + 4 + 4 * i);
    ci->byte_2b = hdr[0x2B];
    field_ok = ci->field[0] == 2u && ci->field[1] == 13u && ci->field[2] == 2u && ci->byte_2b == 1u;
    for (i = 0x10; i < (int)GP_WRAP_HDR && field_ok; i++)
        if (i != 0x2B && hdr[i] != 0) field_ok = 0;
    if (!field_ok) {
        if (mode == GP_STRICT) { st = GP_ERR_CSH_FIELD; goto done; }
        ci->obs |= GP_OBS_CSH_FIELD;
    }

    st = gp_wrap_parse(hdr, (uint32_t)size, mode, &ci->w);
    if (st != RP_OK) goto done;
    ci->obs |= ci->w.obs;

    /* Size relation with the paired archive (only the count is used). */
    st = gp_gdat_open(&pair, pair_path, mode);
    if (st != RP_OK) goto done;
    ci->pair_count = pair.count;
    gp_gdat_close(&pair);
    expect = 12u + 28u * (uint64_t)ci->pair_count;
    if ((uint64_t)ci->w.usize != expect) {
        if (mode == GP_STRICT) { st = GP_ERR_CSH_PAIR; goto done; }
        ci->obs |= GP_OBS_CSH_PAIR;
    }

    /* Inflate only to validate the stream; the bytes are not interpreted. */
    st = gp_wrap_inflate(fp, size, GP_WRAP_TOTAL, &ci->w, &recs, &recs_len, NULL);
    if (st != RP_OK) goto done;
    ci->records_size = (uint32_t)recs_len;
    ci->records_crc = rp_crc32(recs, recs_len);
    rp_free(recs);

done:
    if (fp) fclose(fp);
    return st;
}
