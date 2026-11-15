/*
 * bench_main.c - VitaBench: automated Vita performance-budget benchmark.
 *
 * Runs every test in bench_tests.c with vsync OFF: 1 s warm-up (absorbs
 * vitaGL shader compilation), then 5 s of measurement. Frame time is the
 * interval between consecutive frame starts. Results are written to
 * ux0:data/VitaBench/results_msaa<N>.csv after every test.
 *
 * Controls:  START  - abort run (partial CSV kept) / exit from summary
 *            CIRCLE - re-run the suite from the summary screen
 *
 * All content is original and synthetic. No immediate-mode GL (SDL2
 * initialises vitaGL with legacy_pool_size = 0).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <vitaGL.h>

#include "bench_report.h"
#include "bench_tests.h"
#include "graphics/hud2d.h"
#include "input/input.h"

#ifndef VITA_MSAA
#define VITA_MSAA 0
#endif

#define SCREEN_W 960
#define SCREEN_H 544

#define WARMUP_S   1.0
#define MEASURE_S  5.0
#define MAX_SAMPLES 16384
#define MAX_TESTS   32

typedef enum { ST_RUN, ST_DONE } State;
typedef enum { PH_WARMUP, PH_MEASURE } Phase;

static float       s_samples[MAX_SAMPLES];
static float       s_sorted[MAX_SAMPLES];
static BenchResult s_results[MAX_TESTS];
static int         s_result_count = 0;

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static void compute_stats(BenchResult *r, int n)
{
    int i, p95i;
    double sum = 0.0;

    r->frames = n;
    if (n <= 0) {
        r->avg_ms = r->min_ms = r->max_ms = r->p95_ms = r->avg_fps = 0.0f;
        return;
    }
    r->min_ms = r->max_ms = s_samples[0];
    for (i = 0; i < n; i++) {
        float v = s_samples[i];
        sum += v;
        if (v < r->min_ms) r->min_ms = v;
        if (v > r->max_ms) r->max_ms = v;
    }
    memcpy(s_sorted, s_samples, (size_t)n * sizeof(float));
    qsort(s_sorted, (size_t)n, sizeof(float), cmp_float);
    p95i = (int)((double)n * 0.95);
    if (p95i >= n) p95i = n - 1;
    r->avg_ms = (float)(sum / n);
    r->p95_ms = s_sorted[p95i];
    r->avg_fps = r->avg_ms > 0.0f ? 1000.0f / r->avg_ms : 0.0f;
}

static unsigned long free_kb(vglMemType t) { return (unsigned long)(vglMemFree(t) / 1024); }

static void draw_run_hud(const BenchTestDef *t, int idx, int count, Phase ph,
                         double elapsed, const char *res_note)
{
    char line[128];
    const float s = 2.0f;

    hud2d_begin(SCREEN_W, SCREEN_H);
    hud2d_rect(0.0f, 0.0f, (float)SCREEN_W, 74.0f, 0.04f, 0.04f, 0.07f);

    SDL_snprintf(line, sizeof(line), "VITABENCH  MSAA %dX  VSYNC OFF  CSV %s",
                 VITA_MSAA, report_ok() ? "OK" : "FAILED");
    hud2d_text(10.0f, 6.0f, s, line, 0.95f, 0.85f, 0.45f);

    SDL_snprintf(line, sizeof(line), "TEST %d/%d  %s  %s  %s %.1f S",
                 idx + 1, count, t->test, t->variant,
                 ph == PH_WARMUP ? "WARMUP" : "MEASURE", elapsed);
    hud2d_text(10.0f, 28.0f, s, line, 0.80f, 0.90f, 1.00f);

    hud2d_text(10.0f, 50.0f, s, res_note, 0.60f, 0.75f, 0.85f);
    hud2d_end();
}

static void draw_summary(int aborted, const char *res_note)
{
    char line[160];
    const float s = 1.5f;
    const float lh = 9.0f * s + 4.0f;
    float y = 8.0f;
    int i;

    hud2d_begin(SCREEN_W, SCREEN_H);
    hud2d_rect(0.0f, 0.0f, (float)SCREEN_W, (float)SCREEN_H, 0.03f, 0.03f, 0.05f);

    SDL_snprintf(line, sizeof(line), "VITABENCH %s  MSAA %dX  %d TESTS",
                 aborted ? "ABORTED" : "COMPLETE", VITA_MSAA, s_result_count);
    hud2d_text(10.0f, y, 2.0f, line, 0.95f, 0.85f, 0.45f);
    y += 24.0f;

    SDL_snprintf(line, sizeof(line), "CSV %s  %s",
                 report_ok() ? "SAVED" : "NOT SAVED", report_ok() ? report_path() : "");
    hud2d_text(10.0f, y, s, line, report_ok() ? 0.6f : 1.0f, report_ok() ? 1.0f : 0.5f, 0.6f);
    y += lh;
    hud2d_text(10.0f, y, s, res_note, 0.6f, 0.75f, 0.85f);
    y += lh + 4.0f;

    hud2d_text(10.0f, y, s, "ID TEST    VARIANT            TRIS  DRAWS   AVG MS   P95 MS    MAX MS    FPS",
               0.9f, 0.9f, 0.9f);
    y += lh;

    for (i = 0; i < s_result_count; i++) {
        const BenchResult *r = &s_results[i];
        if (r->frames > 0)
            SDL_snprintf(line, sizeof(line), "%2d %-7s %-17s %6d %6d %8.2f %8.2f %9.2f %6.1f",
                         r->id, r->test, r->variant, r->triangles, r->draws,
                         r->avg_ms, r->p95_ms, r->max_ms, r->avg_fps);
        else
            SDL_snprintf(line, sizeof(line), "%2d %-7s %-17s  SEE CSV NOTE", r->id, r->test, r->variant);
        hud2d_text(10.0f, y, s, line, 0.80f, 0.90f, 1.00f);
        y += lh;
    }

    hud2d_text(10.0f, (float)SCREEN_H - 22.0f, s, "O RERUN   START EXIT", 0.7f, 0.7f, 0.7f);
    hud2d_end();
}

static void start_run(int *cur, Phase *ph, Uint64 *phase_start, int *nsamp, int *aborted)
{
    report_close();
    report_open(VITA_MSAA, SDL_GL_GetSwapInterval());
    *cur = 0;
    *ph = PH_WARMUP;
    *phase_start = SDL_GetPerformanceCounter();
    *nsamp = 0;
    *aborted = 0;
    s_result_count = 0;
}

int main(int argc, char *argv[])
{
    SDL_Window *window;
    SDL_GLContext context;
    InputState in;
    char res_note[128];
    const int count = bench_test_count();
    const double freq = (double)SDL_GetPerformanceFrequency();
    State state = ST_RUN;
    Phase phase = PH_WARMUP;
    Uint64 phase_start, prev = 0;
    int cur = 0, nsamp = 0, aborted = 0, measured_last_frame = 0;
    int running = 1;

    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0)
        return 1;
#if VITA_MSAA > 0
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, VITA_MSAA);
#endif
    window = SDL_CreateWindow("Vita Bench", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              SCREEN_W, SCREEN_H, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!window) { SDL_Quit(); return 1; }
    context = SDL_GL_CreateContext(window);
    if (!context) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

    SDL_GL_SetSwapInterval(0); /* vsync off: measure real frame cost */
    input_init();

    glViewport(0, 0, SCREEN_W, SCREEN_H);
    glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
    glClearDepthf(1.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    bench_resources_init(res_note, sizeof(res_note));

    start_run(&cur, &phase, &phase_start, &nsamp, &aborted);

    while (running) {
        SDL_Event ev;
        Uint64 now;
        double elapsed;
        const BenchTestDef *t = (state == ST_RUN) ? bench_test_get(cur) : NULL;

        while (SDL_PollEvent(&ev))
            input_handle_event(&ev);
        input_poll(&in);

        now = SDL_GetPerformanceCounter();
        if (measured_last_frame && prev != 0 && nsamp < MAX_SAMPLES)
            s_samples[nsamp++] = (float)((double)(now - prev) * 1000.0 / freq);
        prev = now;
        measured_last_frame = 0;

        if (in.quit_requested) {
            running = 0;
            break;
        }

        if (state == ST_RUN) {
            if (in.pressed[INPUT_BTN_START]) {
                aborted = 1;
                report_close();
                state = ST_DONE;
                continue;
            }

            if (t->kind == KIND_TEXMEM) {
                BenchResult *r = &s_results[s_result_count];
                memset(r, 0, sizeof(*r));
                r->id = t->id; r->test = t->test; r->variant = t->variant;
                bench_texmem_run(r);
                report_row(r);
                s_result_count++;
                cur++;
                phase = PH_WARMUP;
                phase_start = SDL_GetPerformanceCounter();
                nsamp = 0;
            } else {
                elapsed = (double)(now - phase_start) / freq;
                if (phase == PH_WARMUP && elapsed >= WARMUP_S) {
                    BenchResult *r = &s_results[s_result_count];
                    memset(r, 0, sizeof(*r));
                    r->ram_before_kb = free_kb(VGL_MEM_RAM);
                    r->vram_before_kb = free_kb(VGL_MEM_VRAM);
                    phase = PH_MEASURE;
                    phase_start = now;
                    nsamp = 0;
                } else if (phase == PH_MEASURE && elapsed >= MEASURE_S) {
                    BenchResult *r = &s_results[s_result_count];
                    r->id = t->id; r->test = t->test; r->variant = t->variant;
                    r->triangles = bench_triangles(t);
                    r->draws = bench_draw_calls(t);
                    compute_stats(r, nsamp);
                    r->ram_after_kb = free_kb(VGL_MEM_RAM);
                    r->vram_after_kb = free_kb(VGL_MEM_VRAM);
                    bench_test_note(t, r->note, sizeof(r->note));
                    report_row(r);
                    s_result_count++;
                    cur++;
                    phase = PH_WARMUP;
                    phase_start = now;
                    nsamp = 0;
                }
            }

            if (cur >= count) {
                report_close();
                state = ST_DONE;
                continue;
            }
            t = bench_test_get(cur);
        } else {
            if (in.pressed[INPUT_BTN_START]) {
                running = 0;
                break;
            }
            if (in.pressed[INPUT_BTN_CIRCLE]) {
                start_run(&cur, &phase, &phase_start, &nsamp, &aborted);
                state = ST_RUN;
                continue;
            }
        }

        /* ---- render ---- */
        glDepthMask(GL_TRUE);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (state == ST_RUN) {
            elapsed = (double)(SDL_GetPerformanceCounter() - phase_start) / freq;
            bench_draw(t);
            draw_run_hud(t, cur, count, phase, elapsed, res_note);
            measured_last_frame = (phase == PH_MEASURE);
        } else {
            draw_summary(aborted, res_note);
        }

        SDL_GL_SwapWindow(window);
    }

    report_close();
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    bench_resources_free();
    input_shutdown();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
