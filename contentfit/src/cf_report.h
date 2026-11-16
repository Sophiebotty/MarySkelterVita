/*
 * cf_report.h - ux0:data/ContentFit/runNNN.{csv,log}, runNNN_frames.csv and
 * runNNN_progress.txt.
 *
 * All four files are created at the start of a run, each only after a check
 * that it does not exist (cf_create_new), and are flushed after every
 * write so a crash keeps everything written so far. The progress file gets
 * a line before each test starts, so the last line names the test that was
 * running if the app stops unexpectedly.
 */
#ifndef CF_REPORT_H
#define CF_REPORT_H

#include <stdio.h>

#include "cf_paths.h"

typedef struct {
    int           run_no;     /* 0 = results are not being saved */
    int           ok;         /* 1 while every write has succeeded */
    gp_status     open_status;
    cf_run_files  files;
    FILE         *csv, *log, *frames, *progress;
} cf_report;

/* Picks the next free run number and creates the four files. On failure
 * nothing that existed is touched and r->run_no is 0. */
gp_status cf_report_open(cf_report *r);
void      cf_report_close(cf_report *r);

void cf_report_progress(cf_report *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void cf_report_log(cf_report *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void cf_report_row(cf_report *r, int iteration, const cf_result *res);
void cf_report_frames(cf_report *r, int iteration, const char *id, const char *mode, const float *ms, int n);

#endif /* CF_REPORT_H */
