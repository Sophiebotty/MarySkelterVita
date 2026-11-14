/*
 * timing.c - frame timing using SDL's high-resolution counter.
 */
#include <SDL2/SDL.h>

#include "engine/timing.h"

#define STATS_WINDOW_S 0.5

static Uint64 s_freq;
static Uint64 s_last;

/* window accumulators */
static double s_win_time;
static double s_win_max;
static int    s_win_frames;

/* published stats */
static float s_avg_ms;
static float s_max_ms;
static float s_fps;

void timing_init(void)
{
    s_freq = SDL_GetPerformanceFrequency();
    s_last = SDL_GetPerformanceCounter();
    s_win_time = s_win_max = 0.0;
    s_win_frames = 0;
    s_avg_ms = s_max_ms = s_fps = 0.0f;
}

float timing_tick(void)
{
    Uint64 now = SDL_GetPerformanceCounter();
    double dt = (double)(now - s_last) / (double)s_freq;
    s_last = now;

    s_win_time += dt;
    s_win_frames++;
    if (dt > s_win_max)
        s_win_max = dt;

    if (s_win_time >= STATS_WINDOW_S) {
        s_avg_ms = (float)(s_win_time * 1000.0 / s_win_frames);
        s_max_ms = (float)(s_win_max * 1000.0);
        s_fps    = (float)(s_win_frames / s_win_time);
        s_win_time = s_win_max = 0.0;
        s_win_frames = 0;
    }

    if (dt > TIMING_MAX_DT)
        dt = TIMING_MAX_DT;
    return (float)dt;
}

float timing_avg_ms(void) { return s_avg_ms; }
float timing_max_ms(void) { return s_max_ms; }
float timing_fps(void)    { return s_fps; }
