/*
 * bench_report.h - CSV result file on the Vita (ux0:data/VitaBench/).
 * Rows are flushed after every test so partial runs are preserved.
 */
#ifndef BENCH_REPORT_H
#define BENCH_REPORT_H

typedef struct {
    int         id;
    const char *test;
    const char *variant;
    int         triangles;    /* per frame, 0 if n/a */
    int         draws;        /* draw calls per frame for the test scene */
    int         frames;       /* frames measured */
    float       avg_ms, min_ms, max_ms, p95_ms, avg_fps;
    unsigned long ram_before_kb, ram_after_kb;
    unsigned long vram_before_kb, vram_after_kb;
    char        note[96];
} BenchResult;

/* Opens ux0:data/VitaBench/results_msaa<N>.csv (overwrites) and writes the
 * header. Returns 0 on success, -1 if the file could not be created. */
int  report_open(int msaa, int swap_interval);
void report_row(const BenchResult *r);
void report_close(void);

const char *report_path(void);
int  report_ok(void);

#endif /* BENCH_REPORT_H */
