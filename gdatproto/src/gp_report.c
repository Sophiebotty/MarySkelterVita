/*
 * gp_report.c - writes results with standard C I/O (outside the read allowlist,
 * which applies to reading fixtures only).
 */
#include <stdio.h>
#include <zlib.h>

#include "gp_report.h"
#include "rp_io.h"

int gp_report_next_run(void)
{
    char path[96];
    uint64_t sz;
    int i;
    rp_mkdir(GP_REPORT_DIR);
    for (i = 1; i <= 999; i++) {
        snprintf(path, sizeof(path), "%s/run%03d.csv", GP_REPORT_DIR, i);
        if (rp_file_size(path, &sz) != RP_OK)
            return i;
    }
    return 0;
}

/* rp_mem_snapshot_take stores -1 in the free_* fields when the Vita kernel
 * query is unavailable (non-Vita build) or fails; print that explicitly. */
static const char *free_text(char *buf, size_t n, long v)
{
    if (v < 0) snprintf(buf, n, "unavailable");
    else snprintf(buf, n, "%ld", v);
    return buf;
}

static void csv_text(FILE *f, const char *s)
{
    /* details never contain quotes; commas are replaced to keep columns */
    for (; *s; s++) fputc(*s == ',' ? ';' : *s, f);
}

int gp_report_write(int run_no, const gp_suite *runs, int n_runs, int g7_pass, const char *g7_detail,
                    char *csv_path, size_t csv_path_len)
{
    char path[96], eo[64], ao[64], fb[6][24];
    FILE *f;
    int k, i, ok = 1;

    snprintf(csv_path, csv_path_len, "%s/run%03d.csv", GP_REPORT_DIR, run_no);
    f = fopen(csv_path, "w");
    if (!f) return 0;
    fprintf(f, "# Vita GDAT Proto results, run %03d. Synthetic fixtures only; no game files. "
               "Passing shows agreement with the documented observations, not Finale compatibility.\n", run_no);
    fprintf(f, "iteration,id,name,mode,expected,actual,pass,expected_obs,obs,ms,mbps,check,detail\n");
    for (k = 0; k < n_runs; k++)
        for (i = 0; i < runs[k].count; i++) {
            const gp_result *r = &runs[k].r[i];
            gp_obs_names(r->expect_obs, eo, sizeof(eo));
            gp_obs_names(r->obs, ao, sizeof(ao));
            fprintf(f, "%d,%s,%s,%s,%s,%s,%d,%s,%s,%.3f,%.2f,%08x,", k + 1, r->id, r->name, r->mode,
                    gp_status_name(r->expect), gp_status_name(r->actual), r->pass, eo, ao, r->ms, r->mbps,
                    (unsigned)r->check);
            csv_text(f, r->detail);
            fputc('\n', f);
        }
    fprintf(f, "%d,G7,REPEATABILITY,-,OK,%s,%d,-,-,0,0,00000000,", n_runs, g7_pass ? "OK" : "MISMATCH", g7_pass);
    csv_text(f, g7_detail);
    fputc('\n', f);
    if (fclose(f) != 0) ok = 0;

    snprintf(path, sizeof(path), "%s/run%03d.log", GP_REPORT_DIR, run_no);
    f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "Vita GDAT Proto run %03d (title VDNG00004)\n", run_no);
    fprintf(f, "Synthetic fixtures only. No Mary Skelter file was read. CSH records were not decoded.\n");
    fprintf(f, "zlib version (runtime): %s\n", zlibVersion());
    fprintf(f, "Fixture directory: %s\n", runs[0].fixture_dir[0] ? runs[0].fixture_dir : "(not found)");
    fprintf(f, "Manifest: %s\n", runs[0].manifest_note);
    fprintf(f, "Memory: passive measurements only. No largest-allocation probe is performed, "
               "so no maximum allocation size is reported.\n");
    for (k = 0; k < n_runs; k++) {
        const gp_suite *s = &runs[k];
        fprintf(f, "\nIteration %d: %d/%d passed\n", k + 1, s->passed, s->count);
        fprintf(f, "  newlib heap (mallinfo), before -> after this iteration: obtained from system %lu -> %lu bytes, "
                   "in use %lu -> %lu bytes\n",
                (unsigned long)s->mem_before.heap_arena, (unsigned long)s->mem_after.heap_arena,
                (unsigned long)s->mem_before.heap_in_use, (unsigned long)s->mem_after.heap_in_use);
        fprintf(f, "  free memory (sceKernelGetFreeMemorySize), before -> after this iteration, bytes: "
                   "USER_RW %s -> %s, CDRAM %s -> %s, PHYCONT %s -> %s\n",
                free_text(fb[0], sizeof(fb[0]), s->mem_before.free_user),
                free_text(fb[1], sizeof(fb[1]), s->mem_after.free_user),
                free_text(fb[2], sizeof(fb[2]), s->mem_before.free_cdram),
                free_text(fb[3], sizeof(fb[3]), s->mem_after.free_cdram),
                free_text(fb[4], sizeof(fb[4]), s->mem_before.free_phycont),
                free_text(fb[5], sizeof(fb[5]), s->mem_after.free_phycont));
        fprintf(f, "  tracked rp_malloc bytes: still allocated at end %lu, peak this iteration %lu, "
                   "peak during G6 perf loads %lu\n",
                (unsigned long)s->stats_end.current, (unsigned long)s->peak_suite_bytes,
                (unsigned long)s->peak_load_bytes);
        fprintf(f, "  allocator counters since app start: allocations %lu, frees %lu, refused %lu\n",
                (unsigned long)s->stats_end.allocs, (unsigned long)s->stats_end.frees,
                (unsigned long)s->stats_end.failed);
        for (i = 0; i < s->count; i++) {
            const gp_result *r = &s->r[i];
            gp_obs_names(r->expect_obs, eo, sizeof(eo));
            gp_obs_names(r->obs, ao, sizeof(ao));
            fprintf(f, "  %-7s %-4s %s %-26s expect %-13s got %-13s obs %s/%s %9.3f ms %8.2f MB/s  %s\n", r->id,
                    r->pass ? "PASS" : "FAIL", r->mode, r->name, gp_status_name(r->expect),
                    gp_status_name(r->actual), eo, ao, r->ms, r->mbps, r->detail);
        }
    }
    fprintf(f, "\nG7 repeatability: %s (%s)\n", g7_pass ? "PASS" : "FAIL", g7_detail);
    if (fclose(f) != 0) ok = 0;
    return ok;
}
