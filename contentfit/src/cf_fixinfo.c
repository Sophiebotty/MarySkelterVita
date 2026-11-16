/*
 * cf_fixinfo.c - the only translation unit that includes the generated
 * fixtures/cf_fixtures.h; everything else uses these accessors.
 */
#include "cf_fixdefs.h"
#include "cf_fixtures.h"

int cf_fix_pack_count(void) { return CF_FIX_PACK_COUNT; }

const cf_fix_pack *cf_fix_pack_get(int i)
{
    return (i >= 0 && i < CF_FIX_PACK_COUNT) ? &CF_FIX_PACKS[i] : 0;
}

const char *cf_fix_io_name(void) { return CF_FIX_IO_NAME; }
uint32_t cf_fix_io_size(void) { return CF_FIX_IO_SIZE; }
uint32_t cf_fix_io_crc(void) { return CF_FIX_IO_CRC; }
uint32_t cf_fix_io_half_crc(void) { return CF_FIX_IO_HALF_CRC; }
int cf_fix_portrait_pack(void) { return CF_FIX_PORTRAIT_PACK; }
uint32_t cf_fix_portrait_entry(void) { return CF_FIX_PORTRAIT_ENTRY; }
