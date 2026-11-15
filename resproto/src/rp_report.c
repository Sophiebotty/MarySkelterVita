/*
 * rp_report.c - writes results with standard C I/O.
 */
#include <stdio.h>
#include <zlib.h>

#include "rp_io.h"
#include "rp_report.h"

static const char *expect_name(int e)
{
    return e == RP_EXPECT_ANY_ERROR ? "ANY_ERROR" : rp_err_name((rp_err)e);
}

int rp_report_next_run(void)
{
    char path[96];
    uint64_t sz;
    int i;
    for (i = 1; i <= 999; i++) {
        snprintf(path, sizeof(path), "%s/run%03d.csv", "ux0:data/ResProto", i);
        if (rp_file_size(path, &sz) != RP_OK)
            return i;
    }
    return 0;
}

int rp_report_write(int run_no, const rp_fixture_set *fx, const rp_suite *runs, int n_runs,
                    int t14_pass, const char *t14_detail, char *csv_path, size_t csv_path_len)
{
    char path[96];
    FILE *f;
    int k, i, ok = 1;

    snprintf(csv_path, csv_path_len, "ux0:data/ResProto/run%03d.csv", run_no);
    f = fopen(csv_path, "w");
    if (!f) return 0;
    fprintf(f, "# Vita ResProto results, run %03d. Synthetic data only; no game files.\n", run_no);
    fprintf(f, "iteration,id,name,expected,actual,pass,ms,mbps,check,detail\n");
    for (k = 0; k < n_runs; k++)
        for (i = 0; i < runs[k].count; i++) {
            const rp_result *r = &runs[k].r[i];
            fprintf(f, "%d,%s,%s,%s,%s,%d,%.3f,%.2f,%08x,%s\n", k + 1, r->id, r->name,
                    expect_name(r->expect), rp_err_name(r->actual), r->pass, r->ms, r->mbps,
                    (unsigned)r->check, r->detail);
        }
    fprintf(f, "%d,T14,REPEATABILITY,OK,%s,%d,0,0,0,%s\n", n_runs, t14_pass ? "OK" : "MISMATCH",
            t14_pass, t14_detail);
    if (fclose(f) != 0) ok = 0;

    snprintf(path, sizeof(path), "ux0:data/ResProto/run%03d.log", run_no);
    f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "Vita ResProto run %03d\n", run_no);
    fprintf(f, "zlib version (runtime): %s\n", zlibVersion());
    fprintf(f, "PC fixture directory: %s\n", runs[0].pc_fixture_dir[0] ? runs[0].pc_fixture_dir : "(not found)");
    fprintf(f, "Fixture write: %llu bytes in %.1f ms (%.1f MB/s), status %s\n",
            (unsigned long long)fx->write_bytes, fx->write_ms,
            fx->write_ms > 0 ? (fx->write_bytes / 1048576.0) / (fx->write_ms / 1000.0) : 0.0,
            rp_err_name(fx->status));
    for (k = 0; k < n_runs; k++) {
        const rp_suite *s = &runs[k];
        fprintf(f, "\nIteration %d: %d/%d passed\n", k + 1, s->passed, s->count);
        fprintf(f, "  heap arena %u -> %u bytes, heap in use %u -> %u bytes\n",
                (unsigned)s->mem_before.heap_arena, (unsigned)s->mem_after.heap_arena,
                (unsigned)s->mem_before.heap_in_use, (unsigned)s->mem_after.heap_in_use);
        fprintf(f, "  free USER_RW %ld -> %ld, CDRAM %ld -> %ld, PHYCONT %ld -> %ld bytes\n",
                s->mem_before.free_user, s->mem_after.free_user,
                s->mem_before.free_cdram, s->mem_after.free_cdram,
                s->mem_before.free_phycont, s->mem_after.free_phycont);
        fprintf(f, "  tracked: leaked %u, peak %u, allocs %u, frees %u, refused %u; largest malloc %u bytes\n",
                (unsigned)s->stats_end.current, (unsigned)s->stats_end.peak, (unsigned)s->stats_end.allocs,
                (unsigned)s->stats_end.frees, (unsigned)s->stats_end.failed, (unsigned)s->probe_max_bytes);
        for (i = 0; i < s->count; i++) {
            const rp_result *r = &s->r[i];
            fprintf(f, "  %-5s %-4s %-24s expect %-15s got %-15s %s\n", r->id, r->pass ? "PASS" : "FAIL",
                    r->name, expect_name(r->expect), rp_err_name(r->actual), r->detail);
        }
    }
    fprintf(f, "\nT14 repeatability: %s (%s)\n", t14_pass ? "PASS" : "FAIL", t14_detail);
    if (fclose(f) != 0) ok = 0;
    return ok;
}
