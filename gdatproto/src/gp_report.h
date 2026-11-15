/*
 * gp_report.h - ux0:data/GdatProto/runNNN.csv and runNNN.log.
 */
#ifndef GP_REPORT_H
#define GP_REPORT_H

#include "gp_tests.h"

#define GP_REPORT_DIR "ux0:data/GdatProto"

int gp_report_next_run(void);     /* 1..999, or 0 if all numbers are used */

/* Returns 1 if both files were written and closed without error. */
int gp_report_write(int run_no, const gp_suite *runs, int n_runs, int g7_pass, const char *g7_detail,
                    char *csv_path, size_t csv_path_len);

#endif /* GP_REPORT_H */
