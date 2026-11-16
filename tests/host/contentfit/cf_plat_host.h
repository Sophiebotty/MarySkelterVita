/* Controls of the host platform stand-in (tests only). */
#ifndef CF_PLAT_HOST_H
#define CF_PLAT_HOST_H

#include <stdint.h>

/* 1: SDL_GL_SetSwapInterval(2) fails, so calibration must pick VBLANK2. */
void host_set_interval2_fails(int fails);
/* START "pressed" after this many polls (-1 = never). */
void host_abort_after_polls(int polls);
/* Simulated heap: block size, bytes used before ContentFit, known flag. */
void host_set_heap(uint64_t total, uint64_t base_used, int known);

#endif
