/*
 * bench_report.c - CSV writer. newlib's fopen/mkdir map to sceIoOpen /
 * sceIoMkdir in the VitaSDK, so standard C I/O works on ux0:.
 */
#include <stdio.h>
#include <sys/stat.h>

#include "bench_report.h"

#define REPORT_DIR "ux0:data/VitaBench"

static FILE *s_fp = NULL;
static char  s_path[96];
static int   s_ok = 0;

int report_open(int msaa, int swap_interval)
{
    mkdir("ux0:data", 0777);     /* normally exists; errors ignored */
    mkdir(REPORT_DIR, 0777);
    snprintf(s_path, sizeof(s_path), REPORT_DIR "/results_msaa%d.csv", msaa);

    s_fp = fopen(s_path, "w");
    s_ok = (s_fp != NULL);
    if (!s_ok)
        return -1;

    fprintf(s_fp, "# VitaBench results, build MSAA=%d, swap_interval=%d (0 = vsync off)\n",
            msaa, swap_interval);
    fprintf(s_fp, "# Original synthetic content only. Frame time = interval between frame starts.\n");
    fprintf(s_fp, "id,test,variant,triangles,draws,frames,avg_ms,min_ms,max_ms,p95_ms,avg_fps,"
                  "ram_free_kb_before,ram_free_kb_after,vram_free_kb_before,vram_free_kb_after,note\n");
    fflush(s_fp);
    return 0;
}

void report_row(const BenchResult *r)
{
    if (!s_fp)
        return;
    fprintf(s_fp, "%d,%s,%s,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.2f,%lu,%lu,%lu,%lu,%s\n",
            r->id, r->test, r->variant, r->triangles, r->draws, r->frames,
            r->avg_ms, r->min_ms, r->max_ms, r->p95_ms, r->avg_fps,
            r->ram_before_kb, r->ram_after_kb, r->vram_before_kb, r->vram_after_kb,
            r->note);
    fflush(s_fp);
}

void report_close(void)
{
    if (s_fp)
        fclose(s_fp);
    s_fp = NULL;
}

const char *report_path(void) { return s_path; }
int report_ok(void) { return s_ok; }
