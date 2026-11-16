/*
 * cf_plat_host.c - host stand-in for cf_plat.h and rp_now_ms (tests only).
 *
 * Time: real monotonic time plus a virtual offset. Swaps and vertical-blank
 * waits advance the virtual clock (60 Hz vblanks; a vsync-off swap adds
 * 4 ms), so frame loops run quickly but follow the same paths as on the
 * Vita. Host timings are NOT measurements.
 * Memory: vitaGL pools come from the GL stand-in; the heap is a simulated
 * 128 MiB block whose use is 2 MiB plus the tracked rp_malloc bytes.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <vitaGL.h>

#include "cf_paths.h"
#include "cf_plat.h"
#include "cf_plat_host.h"
#include "rp_mem.h"

static pthread_mutex_t s_clock_lock = PTHREAD_MUTEX_INITIALIZER;
static double s_virtual_ms;
static int s_interval = 1, s_int2_fails, s_abort_after = -1, s_polls;
static uint64_t s_heap_total = 128ull << 20, s_heap_base = 2ull << 20;
static int s_heap_known = 1;

void host_set_interval2_fails(int fails) { s_int2_fails = fails; }
void host_abort_after_polls(int polls) { s_abort_after = polls; s_polls = 0; }
void host_set_heap(uint64_t total, uint64_t base_used, int known)
{
    s_heap_total = total;
    s_heap_base = base_used;
    s_heap_known = known;
}

static double real_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

double rp_now_ms(void)
{
    double v;
    pthread_mutex_lock(&s_clock_lock);
    v = s_virtual_ms;
    pthread_mutex_unlock(&s_clock_lock);
    return real_ms() + v;
}

static void advance(double ms)
{
    pthread_mutex_lock(&s_clock_lock);
    s_virtual_ms += ms;
    pthread_mutex_unlock(&s_clock_lock);
}

#define VBLANK_MS (1000.0 / 60.0)

static void to_next_vblank(void)
{
    double now = rp_now_ms();
    double next = ((double)(long long)(now / VBLANK_MS) + 1.0) * VBLANK_MS;
    advance(next - now + 0.01);
}

void cf_plat_pools(cf_pools *out)
{
    rp_mem_stats m = rp_mem_get();
    uint64_t used = s_heap_base + m.current;
    memset(out, 0, sizeof(*out));
    out->p[CF_POOL_VRAM].known = vglMemTotal(VGL_MEM_VRAM) > 0;
    out->p[CF_POOL_VRAM].total = vglMemTotal(VGL_MEM_VRAM);
    out->p[CF_POOL_VRAM].free = vglMemFree(VGL_MEM_VRAM);
    out->p[CF_POOL_GLRAM].known = vglMemTotal(VGL_MEM_RAM) > 0;
    out->p[CF_POOL_GLRAM].total = vglMemTotal(VGL_MEM_RAM);
    out->p[CF_POOL_GLRAM].free = vglMemFree(VGL_MEM_RAM);
    out->p[CF_POOL_PHYCONT].known = vglMemTotal(VGL_MEM_PHYCONT) > 0;
    out->p[CF_POOL_PHYCONT].total = vglMemTotal(VGL_MEM_PHYCONT);
    out->p[CF_POOL_PHYCONT].free = vglMemFree(VGL_MEM_PHYCONT);
    if (s_heap_known && used < s_heap_total) {
        out->p[CF_POOL_HEAP].known = 1;
        out->p[CF_POOL_HEAP].total = s_heap_total;
        out->p[CF_POOL_HEAP].free = s_heap_total - used;
    }
    out->p[CF_POOL_KERNEL].known = 1;
    out->p[CF_POOL_KERNEL].free = 12ull << 20;
}

void cf_plat_env(cf_env *e)
{
    static const vglMemType V[5] = {VGL_MEM_VRAM, VGL_MEM_RAM, VGL_MEM_PHYCONT, VGL_MEM_BUDGET, VGL_MEM_EXTERNAL};
    int i;
    memset(e, 0, sizeof(*e));
    e->arm_mhz = e->bus_mhz = e->gpu_mhz = e->xbar_mhz = -1;
    e->battery_pct = e->charging = e->battery_temp = -1;
    e->uptime_s = rp_now_ms() / 1000.0;
    e->ux0_free = -1;
    for (i = 0; i < 5; i++) {
        e->vgl_total[i] = vglMemTotal(V[i]);
        e->vgl_free[i] = vglMemFree(V[i]);
    }
    e->heap_total = s_heap_known ? s_heap_total : 0;
    e->heap_in_use = s_heap_base + rp_mem_get().current;
    e->kernel_free_user = 12ll << 20;
    e->kernel_free_cdram = e->kernel_free_phycont = 0;
}

int cf_raw_open(const char *path)
{
    if (!cf_path_read_ok(path)) return -1;
    return open(path, O_RDONLY);
}
int64_t cf_raw_read(int fd, void *buf, uint32_t n) { return read(fd, buf, n); }
int64_t cf_raw_pread(int fd, void *buf, uint32_t n, uint64_t off) { return pread(fd, buf, n, (off_t)off); }
void cf_raw_close(int fd) { if (fd >= 0) close(fd); }

/* ------------------------------------------------------------- threads -- */
#define MAX_THREADS 8
#define MAX_SEMAS 16
typedef struct { int used; pthread_t th; cf_thread_fn fn; void *arg; int ret; } host_thread;
static host_thread s_th[MAX_THREADS];
static struct { int used, max; sem_t s; } s_sem[MAX_SEMAS];
static pthread_mutex_t s_tab_lock = PTHREAD_MUTEX_INITIALIZER;

static void *thread_main(void *p)
{
    host_thread *t = (host_thread *)p;
    t->ret = t->fn(t->arg);
    return NULL;
}

int cf_thread_start(cf_thread_fn fn, void *arg, uint32_t stack_bytes)
{
    pthread_attr_t at;
    int i, h = -1;
    pthread_mutex_lock(&s_tab_lock);
    for (i = 0; i < MAX_THREADS && h < 0; i++)
        if (!s_th[i].used) { s_th[i].used = 1; h = i; }
    pthread_mutex_unlock(&s_tab_lock);
    if (h < 0) return -1;
    s_th[h].fn = fn;
    s_th[h].arg = arg;
    pthread_attr_init(&at);
    /* host stacks get extra room for sanitizer instrumentation */
    pthread_attr_setstacksize(&at, (size_t)stack_bytes * 8u);
    if (pthread_create(&s_th[h].th, &at, thread_main, &s_th[h]) != 0) {
        pthread_attr_destroy(&at);
        s_th[h].used = 0;
        return -1;
    }
    pthread_attr_destroy(&at);
    return h;
}

int cf_thread_join(int h)
{
    int r;
    if (h < 0 || h >= MAX_THREADS || !s_th[h].used) return -1;
    pthread_join(s_th[h].th, NULL);
    r = s_th[h].ret;
    s_th[h].used = 0;
    return r;
}

int cf_sema_create(int initial, int max)
{
    int i, h = -1;
    pthread_mutex_lock(&s_tab_lock);
    for (i = 0; i < MAX_SEMAS && h < 0; i++)
        if (!s_sem[i].used) { s_sem[i].used = 1; h = i; }
    pthread_mutex_unlock(&s_tab_lock);
    if (h < 0) return -1;
    s_sem[h].max = max;
    sem_init(&s_sem[h].s, 0, (unsigned)initial);
    return h;
}

void cf_sema_wait(int h)
{
    while (sem_wait(&s_sem[h].s) != 0 && errno == EINTR) { }
}

int cf_sema_try(int h) { return sem_trywait(&s_sem[h].s) == 0; }

void cf_sema_signal(int h)
{
    int v = 0;
    sem_getvalue(&s_sem[h].s, &v);
    if (v < s_sem[h].max)            /* like the Vita: signalling past the maximum fails */
        sem_post(&s_sem[h].s);
}

void cf_sema_delete(int h)
{
    if (h < 0 || h >= MAX_SEMAS || !s_sem[h].used) return;
    sem_destroy(&s_sem[h].s);
    s_sem[h].used = 0;
}

/* ------------------------------------------------------------- display -- */
void cf_plat_attach_window(void *w) { (void)w; }

int cf_plat_set_interval(int n)
{
    if (n == 2 && s_int2_fails) return -1;
    s_interval = n;
    return 0;
}

void cf_plat_swap(void)
{
    if (s_interval <= 0) {
        advance(4.0);
    } else {
        int k;
        for (k = 0; k < s_interval; k++) to_next_vblank();
    }
}

int cf_plat_vcount(void) { return (int)(long long)(rp_now_ms() / VBLANK_MS); }
void cf_plat_wait_vblank(void) { to_next_vblank(); }
void cf_plat_sleep_ms(double ms) { if (ms > 0) advance(ms); }

int cf_plat_poll_abort(void)
{
    if (s_abort_after < 0) return 0;
    return ++s_polls > s_abort_after;
}

/* The dry runs do not link cf_iowrap.c (test_cf_iowrap.c tests it). */
void cf_plat_redirect_counts(int *opens, int *mkdirs, int *refused)
{
    if (opens) *opens = 0;
    if (mkdirs) *mkdirs = 0;
    if (refused) *refused = 0;
}
