/*
 * rs_synth.h - synthetic packs built inside the ResPipe PC tests (stages
 * 10.3 and 10.4). They exercise the worker-slot rule of RESOURCE_PIPELINE.md
 * section 8.2: U = max(largest payload_size, 48 + 32 x entry_count) + 32.
 * The committed fixtures (respipe/fixtures/) and the 10.2 tools are not used
 * or changed; every pack is written into the test's work directory.
 */
#ifndef RS_SYNTH_H
#define RS_SYNTH_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;           /* short name, e.g. "toc_big" */
    char        path[96];       /* app0:fixtures/synth/<name>.gdat (inside the work directory) */
    int         policy_code;    /* expected with the room rule (section 5.8) */
    int         policy_entry;
    int         format_code;    /* expected without it (format checks only) */
    int         format_entry;
    uint32_t    expect_u;       /* section 8.2 U for this pack */
    uint32_t    rows_only_u;    /* what U would be from the resource rows alone (the old wording) */
    uint32_t    toc_inflated;   /* Ut */
} rs_synth_case;

#define RS_SYNTH_CASES 7

/* Builds every synthetic pack under app0:fixtures/synth/ (relative to the
 * current directory) and fills cases[RS_SYNTH_CASES]. 0 on success. */
int rs_synth_build_all(rs_synth_case *cases);

#endif /* RS_SYNTH_H */
