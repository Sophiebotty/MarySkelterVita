/*
 * rs_host_plat.c - host stand-ins needed to link the unmodified ContentFit
 * and Phase 8 modules into the stage 10.3 PC tests.
 *
 * cf_pack.c (for cf_payload_check) refers to ContentFit's platform
 * functions, which only its background reader (cf_ring) calls. Stage 10.3
 * never starts that reader, so these are inert: each fails and counts the
 * call, and the tests require the count to stay 0. The real PC stand-in for
 * the ring belongs to stage 10.4. rp_now_ms (Phase 8 rp_time.c needs SDL) is
 * a monotonic clock here.
 */
#include <time.h>

#include "cf_plat.h"
#include "rp_time.h"
#include "rs_host_plat.h"

static int g_calls;

int rs_host_plat_calls(void)
{
    return g_calls;
}

int cf_raw_open(const char *path)
{
    (void)path;
    g_calls++;
    return -1;
}

int64_t cf_raw_pread(int fd, void *buf, uint32_t n, uint64_t off)
{
    (void)fd; (void)buf; (void)n; (void)off;
    g_calls++;
    return -1;
}

void cf_raw_close(int fd)
{
    (void)fd;
    g_calls++;
}

int cf_thread_start(cf_thread_fn fn, void *arg, uint32_t stack_bytes)
{
    (void)fn; (void)arg; (void)stack_bytes;
    g_calls++;
    return -1;
}

int cf_thread_join(int handle)
{
    (void)handle;
    g_calls++;
    return -1;
}

int cf_sema_create(int initial, int max)
{
    (void)initial; (void)max;
    g_calls++;
    return -1;
}

void cf_sema_wait(int sema)
{
    (void)sema;
    g_calls++;
}

int cf_sema_try(int sema)
{
    (void)sema;
    g_calls++;
    return 0;
}

void cf_sema_signal(int sema)
{
    (void)sema;
    g_calls++;
}

void cf_sema_delete(int sema)
{
    (void)sema;
    g_calls++;
}

double rp_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}
