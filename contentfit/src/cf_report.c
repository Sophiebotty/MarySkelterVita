/*
 * cf_report.c - result files (no overwriting; flushed after every write).
 */
#include <stdarg.h>
#include <string.h>

#include "cf_report.h"
#include "rp_io.h"

static void track(cf_report *r, FILE *f, int wrote_ok)
{
    if (!f) return;
    if (!wrote_ok || fflush(f) != 0)
        r->ok = 0;
}

gp_status cf_report_open(cf_report *r)
{
    gp_status st;
    int run;

    memset(r, 0, sizeof(*r));
    rp_mkdir(CF_OUT_DIR);
    run = cf_run_pick(cf_exists_stat, NULL);
    if (run == 0) {
        r->open_status = CF_ERR_EXISTS;
        return r->open_status;
    }
    cf_run_files_for(run, &r->files);
    st = cf_create_new(r->files.progress, &r->progress);
    if (st == RP_OK) st = cf_create_new(r->files.csv, &r->csv);
    if (st == RP_OK) st = cf_create_new(r->files.log, &r->log);
    if (st == RP_OK) st = cf_create_new(r->files.frames, &r->frames);
    if (st != RP_OK) {
        /* Close what this call created; files are left in place, never deleted. */
        cf_report_close(r);
        memset(r, 0, sizeof(*r));
        r->open_status = st;
        return st;
    }
    r->run_no = run;
    r->ok = 1;
    r->open_status = RP_OK;
    track(r, r->csv,
          fprintf(r->csv, "# Vita ContentFit results, run %03d. Synthetic content only; no game files. "
                          "Results describe this Vita and this project's SDL2 + vitaGL stack, not Mary Skelter Finale.\n"
                          "iteration,id,name,mode,status,ok,verdict,frames,avg_ms,p50_ms,p95_ms,p99_ms,max_ms,"
                          "over_16_7,over_34,over_51,ms,mib,mib_per_s,vram_kib,glram_kib,heap_kib,check,detail\n",
                  run) > 0);
    track(r, r->frames,
          fprintf(r->frames, "# Vita ContentFit frame intervals, run %03d (ms between consecutive frame starts).\n"
                             "iteration,id,mode,frame,ms\n", run) > 0);
    track(r, r->progress, fprintf(r->progress, "run %03d started\n", run) > 0);
    return RP_OK;
}

void cf_report_close(cf_report *r)
{
    FILE **fs[4] = {&r->csv, &r->log, &r->frames, &r->progress};
    int i;
    for (i = 0; i < 4; i++) {
        if (*fs[i]) {
            if (fclose(*fs[i]) != 0) r->ok = 0;
            *fs[i] = NULL;
        }
    }
}

static void vline(cf_report *r, FILE *f, const char *fmt, va_list ap)
{
    int a, b;
    if (!f) return;
    a = vfprintf(f, fmt, ap);
    b = fputc('\n', f);
    track(r, f, a >= 0 && b != EOF);
}

void cf_report_progress(cf_report *r, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vline(r, r->progress, fmt, ap);
    va_end(ap);
}

void cf_report_log(cf_report *r, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vline(r, r->log, fmt, ap);
    va_end(ap);
}

/* CSV text field: commas and quotes are replaced so every row keeps its columns. */
static void csv_text(FILE *f, const char *s)
{
    for (; *s; s++)
        fputc((*s == ',' || *s == '"' || *s == '\n') ? ';' : *s, f);
}

void cf_report_row(cf_report *r, int iteration, const cf_result *x)
{
    FILE *f = r->csv;
    int ok;
    if (!f) return;
    fprintf(f, "%d,%s,%s,%s,%s,%d,%s,", iteration, x->id, x->name, x->mode, cf_status_name(x->status), x->ok,
            x->verdict);
    if (x->has_frames)
        fprintf(f, "%d,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d,%d,", x->fs.n, x->fs.avg, x->fs.p50, x->fs.p95, x->fs.p99,
                x->fs.max, x->fs.over_16_7, x->fs.over_34, x->fs.over_51);
    else
        fputs(",,,,,,,,,", f);
    ok = fprintf(f, "%.3f,%.3f,%.3f,%lld,%lld,%lld,%08x,", x->ms, x->mbytes, x->mbps, (long long)x->vram_kib,
                 (long long)x->glram_kib, (long long)x->heap_kib, (unsigned)x->check) > 0;
    csv_text(f, x->detail);
    ok = (fputc('\n', f) != EOF) && ok;
    track(r, f, ok);
}

void cf_report_frames(cf_report *r, int iteration, const char *id, const char *mode, const float *ms, int n)
{
    int i, ok = 1;
    if (!r->frames) return;
    for (i = 0; i < n; i++)
        if (fprintf(r->frames, "%d,%s,%s,%d,%.9g\n", iteration, id, mode, i, (double)ms[i]) < 0)
            ok = 0;
    track(r, r->frames, ok);
}
