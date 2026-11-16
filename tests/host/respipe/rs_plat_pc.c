/*
 * rs_plat_pc.c - PC stand-in for ResPipe's platform services (see
 * rs_plat_pc.h). POSIX files, pthreads and unnamed POSIX semaphores.
 *
 * Thread safety: the worker calls cf_raw_pread, the semaphore functions and
 * rp_now_ms; everything the worker reads is set before cf_thread_start, and
 * its counters and the clock use atomic operations. Opening, closing,
 * creating and deleting happen on the main thread only (cf_ring_start and
 * cf_ring_stop), so the tables need no lock.
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cf_plat.h"
#include "rp_time.h"
#include "rs_plat_pc.h"

#define PC_PREFIX "app0:fixtures/"
#define PC_THREADS 8
#define PC_SEMAS 16

typedef struct {
    int used;
    pthread_t th;
    cf_thread_fn fn;
    void *arg;
    int ret;
} pc_thread;

static pc_thread g_th[PC_THREADS];
static struct { int used, max; sem_t s; } g_sem[PC_SEMAS];
static rs_pc_live g_live;
static rs_pc_totals g_tot;                      /* preads, short/held reads, overflows: atomic */
static int g_fail_open, g_fail_sema_nth, g_fail_thread;
static int64_t g_short_off = -1, g_hold_off = -1, g_sub_off = -1;
static const uint8_t *g_sub_data;
static size_t g_sub_len;
static int g_hold_ms, g_held;                   /* g_held: atomic */
static cf_pools g_pools;
static int g_clock_manual;                      /* atomic */
static int64_t g_clock_us;                      /* atomic */

void rs_pc_default_pools(cf_pools *p)
{
    const uint64_t MIB = 1024u * 1024u;
    memset(p, 0, sizeof *p);
    /* free memory at ContentFit's start on the Vita (RESOURCE_PIPELINE.md M1) */
    p->p[CF_POOL_VRAM].known = 1;    p->p[CF_POOL_VRAM].total = 96 * MIB;   p->p[CF_POOL_VRAM].free = 87975526u;
    p->p[CF_POOL_GLRAM].known = 1;   p->p[CF_POOL_GLRAM].total = 100 * MIB; p->p[CF_POOL_GLRAM].free = 66584576u;
    p->p[CF_POOL_PHYCONT].known = 1; p->p[CF_POOL_PHYCONT].total = 26 * MIB; p->p[CF_POOL_PHYCONT].free = 26 * MIB;
    p->p[CF_POOL_CDLG].known = 0;
    p->p[CF_POOL_HEAP].known = 1;    p->p[CF_POOL_HEAP].total = 128 * MIB;  p->p[CF_POOL_HEAP].free = 128 * MIB - 57680u;
    p->p[CF_POOL_KERNEL].known = 1;  p->p[CF_POOL_KERNEL].total = 12 * MIB; p->p[CF_POOL_KERNEL].free = 12 * MIB;
}

void rs_pc_reset(void)
{
    memset(&g_tot, 0, sizeof g_tot);
    g_fail_open = g_fail_sema_nth = g_fail_thread = 0;
    g_short_off = g_hold_off = g_sub_off = -1;
    g_sub_data = NULL;
    g_sub_len = 0;
    g_hold_ms = 0;
    __atomic_store_n(&g_held, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&g_clock_manual, 0, __ATOMIC_RELEASE);
    rs_pc_default_pools(&g_pools);
}

rs_pc_live rs_pc_live_now(void)
{
    return g_live;
}

rs_pc_totals rs_pc_totals_now(void)
{
    rs_pc_totals t;
    /* main-thread fields read plainly; the worker's counters atomically */
    t.opens = g_tot.opens;
    t.open_refused = g_tot.open_refused;
    t.threads_started = g_tot.threads_started;
    t.semas_created = g_tot.semas_created;
    t.preads = __atomic_load_n(&g_tot.preads, __ATOMIC_ACQUIRE);
    t.short_reads = __atomic_load_n(&g_tot.short_reads, __ATOMIC_ACQUIRE);
    t.held_reads = __atomic_load_n(&g_tot.held_reads, __ATOMIC_ACQUIRE);
    t.sema_overflows = __atomic_load_n(&g_tot.sema_overflows, __ATOMIC_ACQUIRE);
    return t;
}

void rs_pc_fail_next_open(void) { g_fail_open = 1; }
void rs_pc_fail_sema_create(int nth) { g_fail_sema_nth = nth; }
void rs_pc_fail_next_thread(void) { g_fail_thread = 1; }
void rs_pc_short_read_at(int64_t offset) { g_short_off = offset; }

void rs_pc_substitute_read(int64_t offset, const uint8_t *data, size_t len)
{
    g_sub_off = data ? offset : -1;
    g_sub_data = data;
    g_sub_len = len;
}

void rs_pc_hold_read_at(int64_t offset, int ms)
{
    g_hold_off = offset;
    g_hold_ms = ms;
    __atomic_store_n(&g_held, 0, __ATOMIC_RELEASE);
}

int rs_pc_read_held(void)
{
    return __atomic_load_n(&g_held, __ATOMIC_ACQUIRE);
}

void rs_pc_set_pools(const cf_pools *p) { g_pools = *p; }

void rs_pc_pools(void *ctx, cf_pools *out)
{
    (void)ctx;
    *out = g_pools;
}

/* ------------------------------------------------------------------ clock */
void rs_pc_clock_manual(int on) { __atomic_store_n(&g_clock_manual, on, __ATOMIC_RELEASE); }
void rs_pc_clock_set(double ms) { __atomic_store_n(&g_clock_us, (int64_t)(ms * 1000.0), __ATOMIC_RELEASE); }
void rs_pc_clock_advance(double ms) { __atomic_add_fetch(&g_clock_us, (int64_t)(ms * 1000.0), __ATOMIC_ACQ_REL); }

double rp_now_ms(void)
{
    struct timespec ts;
    if (__atomic_load_n(&g_clock_manual, __ATOMIC_ACQUIRE))
        return (double)__atomic_load_n(&g_clock_us, __ATOMIC_ACQUIRE) / 1000.0;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1.0e6;
}

/* ------------------------------------------------------------------ raw reads */
int cf_raw_open(const char *path)
{
    int fd;
    if (!path || strncmp(path, PC_PREFIX, sizeof PC_PREFIX - 1) != 0 || strstr(path, "..")) {
        g_tot.open_refused++;
        return -1;
    }
    if (g_fail_open) {
        g_fail_open = 0;
        g_tot.open_refused++;
        return -1;
    }
    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        g_tot.opens++;
        g_live.open_files++;
    }
    return fd;
}

int64_t cf_raw_pread(int fd, void *buf, uint32_t n, uint64_t off)
{
    uint32_t want = n;
    ssize_t got;
    __atomic_add_fetch(&g_tot.preads, 1u, __ATOMIC_ACQ_REL);
    if (g_hold_off >= 0 && (int64_t)off == g_hold_off) {
        __atomic_add_fetch(&g_tot.held_reads, 1u, __ATOMIC_ACQ_REL);
        __atomic_store_n(&g_held, 1, __ATOMIC_RELEASE);
        usleep((useconds_t)g_hold_ms * 1000u);
    }
    if (g_short_off >= 0 && (int64_t)off == g_short_off && n > 0) {
        __atomic_add_fetch(&g_tot.short_reads, 1u, __ATOMIC_ACQ_REL);
        want = n - 1u;
    }
    got = pread(fd, buf, want, (off_t)off);
    if (got > 0 && g_sub_off >= 0 && (int64_t)off == g_sub_off)
        memcpy(buf, g_sub_data, (size_t)got < g_sub_len ? (size_t)got : g_sub_len);
    return (int64_t)got;
}

void cf_raw_close(int fd)
{
    if (fd >= 0 && close(fd) == 0)
        g_live.open_files--;
}

/* ------------------------------------------------------------------ threads */
static void *thread_main(void *p)
{
    pc_thread *t = (pc_thread *)p;
    t->ret = t->fn(t->arg);
    return NULL;
}

int cf_thread_start(cf_thread_fn fn, void *arg, uint32_t stack_bytes)
{
    pthread_attr_t at;
    int h;
    if (g_fail_thread) {
        g_fail_thread = 0;
        return -1;
    }
    for (h = 0; h < PC_THREADS && g_th[h].used; h++) {
    }
    if (h == PC_THREADS)
        return -1;
    g_th[h].used = 1;
    g_th[h].fn = fn;
    g_th[h].arg = arg;
    g_th[h].ret = -1;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, (size_t)stack_bytes * 8u);
    if (pthread_create(&g_th[h].th, &at, thread_main, &g_th[h]) != 0) {
        pthread_attr_destroy(&at);
        g_th[h].used = 0;
        return -1;
    }
    pthread_attr_destroy(&at);
    g_tot.threads_started++;
    g_live.threads++;
    return h;
}

int cf_thread_join(int h)
{
    int ret;
    if (h < 0 || h >= PC_THREADS || !g_th[h].used)
        return -1;
    pthread_join(g_th[h].th, NULL);
    ret = g_th[h].ret;
    g_th[h].used = 0;
    g_live.threads--;
    return ret;
}

/* ------------------------------------------------------------------ semaphores */
int cf_sema_create(int initial, int max)
{
    int h;
    if (g_fail_sema_nth > 0 && --g_fail_sema_nth == 0)
        return -1;
    for (h = 0; h < PC_SEMAS && g_sem[h].used; h++) {
    }
    if (h == PC_SEMAS || initial < 0 || max < 1 || initial > max)
        return -1;
    if (sem_init(&g_sem[h].s, 0, (unsigned)initial) != 0)
        return -1;
    g_sem[h].used = 1;
    g_sem[h].max = max;
    g_tot.semas_created++;
    g_live.semaphores++;
    return h;
}

void cf_sema_wait(int h)
{
    if (h < 0 || h >= PC_SEMAS)
        return;
    while (sem_wait(&g_sem[h].s) != 0 && errno == EINTR) {
    }
}

int cf_sema_try(int h)
{
    if (h < 0 || h >= PC_SEMAS)
        return 0;
    return sem_trywait(&g_sem[h].s) == 0;
}

void cf_sema_signal(int h)
{
    int v = 0;
    if (h < 0 || h >= PC_SEMAS)
        return;
    sem_getvalue(&g_sem[h].s, &v);
    if (v < g_sem[h].max)
        sem_post(&g_sem[h].s);
    else
        __atomic_add_fetch(&g_tot.sema_overflows, 1u, __ATOMIC_ACQ_REL);
}

void cf_sema_delete(int h)
{
    if (h < 0 || h >= PC_SEMAS || !g_sem[h].used)
        return;
    sem_destroy(&g_sem[h].s);
    g_sem[h].used = 0;
    g_live.semaphores--;
}
