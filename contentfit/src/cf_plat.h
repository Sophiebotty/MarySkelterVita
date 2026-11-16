/*
 * cf_plat.h - the platform services ContentFit uses.
 *
 * cf_plat_vita.c implements them on the PS Vita. The host tests provide a
 * stand-in (tests/host/contentfit/cf_plat_host.c) so the same suite code can
 * run on a PC; host timings are not measurements.
 */
#ifndef CF_PLAT_H
#define CF_PLAT_H

#include "cf_budget.h"

typedef struct {
    int     arm_mhz, bus_mhz, gpu_mhz, xbar_mhz;   /* -1 when unavailable */
    int     battery_pct;                            /* -1 when unavailable */
    int     charging;                               /* 1, 0, or -1 */
    int     battery_temp;                           /* as reported by scePowerGetBatteryTemp, -1 if unavailable */
    double  uptime_s;                               /* time since the system booted */
    int64_t ux0_free;                               /* free bytes on ux0:, -1 if unknown */
    uint64_t vgl_total[5], vgl_free[5];             /* VRAM, RAM, PHYCONT, BUDGET, EXTERNAL */
    uint64_t heap_total;                            /* C library heap memblock size, 0 if unknown */
    uint64_t heap_in_use;                           /* mallinfo uordblks */
    uint64_t heap_arena;                            /* mallinfo arena */
    int64_t kernel_free_user, kernel_free_cdram, kernel_free_phycont;   /* -1 if unavailable */
} cf_env;

/* Pool snapshot (passive; allocates nothing). */
void cf_plat_pools(cf_pools *out);
void cf_plat_env(cf_env *out);

/* Raw file reads (sceIoOpen / sceIoRead / sceIoPread on the Vita). The open
 * refuses paths outside cf_path_read_ok. These calls never allocate, so a
 * worker thread may use them. Return values < 0 are errors. */
int     cf_raw_open(const char *path);
int64_t cf_raw_read(int fd, void *buf, uint32_t n);
int64_t cf_raw_pread(int fd, void *buf, uint32_t n, uint64_t off);
void    cf_raw_close(int fd);

/* Threads and counting semaphores (handles >= 0; < 0 = failure). */
typedef int (*cf_thread_fn)(void *arg);
int  cf_thread_start(cf_thread_fn fn, void *arg, uint32_t stack_bytes);
int  cf_thread_join(int handle);                 /* returns the thread's result */
int  cf_sema_create(int initial, int max);
void cf_sema_wait(int sema);
int  cf_sema_try(int sema);                      /* 1 if a unit was taken without waiting */
void cf_sema_signal(int sema);
void cf_sema_delete(int sema);

/* Display. cf_plat_attach_window takes the SDL_Window* (Vita only). */
void cf_plat_attach_window(void *sdl_window);
int  cf_plat_set_interval(int n);                /* SDL_GL_SetSwapInterval; 0 = success */
void cf_plat_swap(void);
int  cf_plat_vcount(void);                       /* vertical blank counter */
void cf_plat_wait_vblank(void);                  /* blocks until the next vertical blank starts */
void cf_plat_sleep_ms(double ms);

/* Pumps events; 1 if START was pressed or the app was asked to quit. */
int  cf_plat_poll_abort(void);

/* Calls the link-time wrappers (cf_iowrap.c) redirected from vitaGL's default
 * shader-cache folder into ContentFit's, and calls they refused. */
void cf_plat_redirect_counts(int *opens, int *mkdirs, int *refused);

#endif /* CF_PLAT_H */
