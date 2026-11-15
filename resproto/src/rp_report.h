/*
 * rp_report.h - per-run CSV + log files in ux0:data/ResProto/.
 */
#ifndef RP_REPORT_H
#define RP_REPORT_H

#include "rp_tests.h"

/* Picks the next free run number (run001..run999). Returns 0 if none. */
int rp_report_next_run(void);

/* Writes runNNN.csv and runNNN.log. Returns 1 if both files were written. */
int rp_report_write(int run_no, const rp_fixture_set *fx, const rp_suite *runs, int n_runs,
                    int t14_pass, const char *t14_detail, char *csv_path, size_t csv_path_len);

#endif
