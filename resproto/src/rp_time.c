/*
 * rp_time.c - millisecond timer based on SDL_GetPerformanceCounter.
 */
#include <SDL2/SDL.h>

#include "rp_time.h"

double rp_now_ms(void)
{
    static double inv = 0.0;
    if (inv == 0.0)
        inv = 1000.0 / (double)SDL_GetPerformanceFrequency();
    return (double)SDL_GetPerformanceCounter() * inv;
}
