/*
 * cf_plat_vita.c - PS Vita implementation of cf_plat.h.
 *
 * Memory figures are passive queries: vitaGL's own pool counters
 * (vglMemTotal / vglMemFree), the kernel's free-memory query, mallinfo, and
 * the size of the memory block that holds the C library heap. Nothing here
 * allocates except one 64-byte malloc, made once, to locate that block.
 */
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>
#include <psp2/display.h>
#include <psp2/io/devctl.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <vitaGL.h>

#include "cf_paths.h"
#include "cf_plat.h"
#include "input/input.h"

static SDL_Window *s_win;

void cf_plat_attach_window(void *sdl_window) { s_win = (SDL_Window *)sdl_window; }

/* Size of the memory block holding the C library heap, found from the
 * address of a small allocation. 0 when the query fails or looks wrong. */
static uint64_t heap_block_size(void)
{
    static int tried;
    static uint64_t size;
    if (!tried) {
        void *p = malloc(64);
        tried = 1;
        if (p) {
            memset(p, 0, 64);              /* the query only uses the address; keep the bytes defined */
            SceKernelMemBlockInfo info;
            memset(&info, 0, sizeof(info));
            info.size = sizeof(info);
            if (sceKernelFindMemBlockByAddr(p, 0) >= 0 && sceKernelGetMemBlockInfoByAddr(p, &info) >= 0 &&
                info.mappedSize >= CF_MIB && info.mappedSize <= 512u * CF_MIB)
                size = info.mappedSize;
            free(p);
        }
    }
    return size;
}

static void vgl_pool(cf_pool *p, vglMemType t)
{
    size_t total = vglMemTotal(t);
    p->known = total > 0;
    p->total = total;
    p->free = total > 0 ? vglMemFree(t) : 0;
}

void cf_plat_pools(cf_pools *out)
{
    struct mallinfo mi;
    SceKernelFreeMemorySizeInfo k;
    uint64_t heap = heap_block_size();

    memset(out, 0, sizeof(*out));
    vgl_pool(&out->p[CF_POOL_VRAM], VGL_MEM_VRAM);
    vgl_pool(&out->p[CF_POOL_GLRAM], VGL_MEM_RAM);
    vgl_pool(&out->p[CF_POOL_PHYCONT], VGL_MEM_PHYCONT);
    vgl_pool(&out->p[CF_POOL_CDLG], VGL_MEM_BUDGET);

    mi = mallinfo();
    if (heap > 0 && (uint64_t)mi.uordblks < heap) {
        out->p[CF_POOL_HEAP].known = 1;
        out->p[CF_POOL_HEAP].total = heap;
        out->p[CF_POOL_HEAP].free = heap - (uint64_t)mi.uordblks;
    }

    memset(&k, 0, sizeof(k));
    k.size = sizeof(k);
    if (sceKernelGetFreeMemorySize(&k) >= 0 && k.size_user >= 0) {
        out->p[CF_POOL_KERNEL].known = 1;
        out->p[CF_POOL_KERNEL].free = (uint64_t)k.size_user;
    }
}

void cf_plat_env(cf_env *e)
{
    static const vglMemType T[5] = {VGL_MEM_VRAM, VGL_MEM_RAM, VGL_MEM_PHYCONT, VGL_MEM_BUDGET, VGL_MEM_EXTERNAL};
    struct mallinfo mi = mallinfo();
    SceKernelFreeMemorySizeInfo k;
    SceIoDevInfo dev;
    int i;

    memset(e, 0, sizeof(*e));
    e->arm_mhz = scePowerGetArmClockFrequency();
    e->bus_mhz = scePowerGetBusClockFrequency();
    e->gpu_mhz = scePowerGetGpuClockFrequency();
    e->xbar_mhz = scePowerGetGpuXbarClockFrequency();
    e->battery_pct = scePowerGetBatteryLifePercent();
    e->charging = scePowerIsBatteryCharging() ? 1 : 0;
    e->battery_temp = scePowerGetBatteryTemp();
    e->uptime_s = (double)sceKernelGetSystemTimeWide() / 1e6;
    memset(&dev, 0, sizeof(dev));
    e->ux0_free = sceIoDevctl("ux0:", 0x3001, NULL, 0, &dev, sizeof(dev)) >= 0 ? (int64_t)dev.free_size : -1;
    for (i = 0; i < 5; i++) {
        e->vgl_total[i] = vglMemTotal(T[i]);
        e->vgl_free[i] = vglMemFree(T[i]);
    }
    e->heap_total = heap_block_size();
    e->heap_in_use = (uint64_t)mi.uordblks;
    e->heap_arena = (uint64_t)mi.arena;
    memset(&k, 0, sizeof(k));
    k.size = sizeof(k);
    if (sceKernelGetFreeMemorySize(&k) >= 0) {
        e->kernel_free_user = k.size_user;
        e->kernel_free_cdram = k.size_cdram;
        e->kernel_free_phycont = k.size_phycont;
    } else {
        e->kernel_free_user = e->kernel_free_cdram = e->kernel_free_phycont = -1;
    }
}

/* ------------------------------------------------------------- raw I/O -- */
int cf_raw_open(const char *path)
{
    if (!cf_path_read_ok(path))
        return -1;
    return sceIoOpen(path, SCE_O_RDONLY, 0);
}

int64_t cf_raw_read(int fd, void *buf, uint32_t n) { return sceIoRead(fd, buf, n); }

int64_t cf_raw_pread(int fd, void *buf, uint32_t n, uint64_t off)
{
    return sceIoPread(fd, buf, n, (SceOff)off);
}

void cf_raw_close(int fd)
{
    if (fd >= 0) sceIoClose(fd);
}

/* ------------------------------------------------------------- threads -- */
typedef struct {
    cf_thread_fn fn;
    void *arg;
} thread_start;

static int thread_entry(SceSize args, void *argp)
{
    thread_start st;
    if (args != sizeof(st))
        return -1;
    memcpy(&st, argp, sizeof(st));
    return st.fn(st.arg);
}

int cf_thread_start(cf_thread_fn fn, void *arg, uint32_t stack_bytes)
{
    thread_start st;
    SceUID th = sceKernelCreateThread("cf_worker", thread_entry, 0x10000100, stack_bytes, 0,
                                      SCE_KERNEL_CPU_MASK_USER_1, NULL);
    if (th < 0)
        return -1;
    st.fn = fn;
    st.arg = arg;
    if (sceKernelStartThread(th, sizeof(st), &st) < 0) {
        sceKernelDeleteThread(th);
        return -1;
    }
    return th;
}

int cf_thread_join(int handle)
{
    int stat = -1;
    if (handle < 0)
        return -1;
    if (sceKernelWaitThreadEnd(handle, &stat, NULL) < 0)
        stat = -1;
    sceKernelDeleteThread(handle);
    return stat;
}

int cf_sema_create(int initial, int max) { return sceKernelCreateSema("cf_sema", 0, initial, max, NULL); }
void cf_sema_wait(int s) { sceKernelWaitSema(s, 1, NULL); }
int cf_sema_try(int s) { return sceKernelPollSema(s, 1) >= 0; }
void cf_sema_signal(int s) { sceKernelSignalSema(s, 1); }
void cf_sema_delete(int s)
{
    if (s >= 0) sceKernelDeleteSema(s);
}

/* ------------------------------------------------------------- display -- */
int cf_plat_set_interval(int n) { return SDL_GL_SetSwapInterval(n); }
void cf_plat_swap(void) { SDL_GL_SwapWindow(s_win); }
int cf_plat_vcount(void) { return sceDisplayGetVcount(); }
void cf_plat_wait_vblank(void) { sceDisplayWaitVblankStart(); }

void cf_plat_sleep_ms(double ms)
{
    if (ms > 0)
        sceKernelDelayThread((SceUInt)(ms * 1000.0));
}

int cf_plat_poll_abort(void)
{
    SDL_Event ev;
    InputState in;
    while (SDL_PollEvent(&ev))
        input_handle_event(&ev);
    input_poll(&in);
    return in.quit_requested || in.pressed[INPUT_BTN_START];
}
