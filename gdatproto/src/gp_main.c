/*
 * gp_main.c - Vita GDAT Proto: synthetic GDAT/BILZ/CSH-like reader tests.
 *
 * Reads ONLY the synthetic fixtures shipped in the VPK (ux0:app/VDNG00004/
 * fixtures/ or app0:fixtures/), runs the suite three times, compares the
 * runs (G7) and writes ux0:data/GdatProto/runNNN.csv/.log.
 * It makes no claim of compatibility with real Mary Skelter Finale archives.
 *
 * Controls: CIRCLE run again, START exit.
 *
 * Drawing uses the shared hud2d module (vertex arrays only; SDL2 starts
 * vitaGL with legacy_pool_size = 0, so immediate mode must not be used).
 */
#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <vitaGL.h>
#include <zlib.h>

#include "graphics/hud2d.h"
#include "input/input.h"
#include "gp_report.h"
#include "gp_tests.h"

#define SCREEN_W 960
#define SCREEN_H 544
#define ITERATIONS 3

static SDL_Window *s_win;
static gp_suite s_runs[ITERATIONS];
static char s_status[32];
static char s_g7[96];
static int  s_g7_pass;
static char s_csv[96];
static int  s_csv_ok;
static int  s_iter_now;

static void frame_text(const char *title, const char *line2)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    hud2d_begin(SCREEN_W, SCREEN_H);
    hud2d_text(20.0f, 200.0f, 3.0f, title, 0.95f, 0.85f, 0.45f);
    if (line2) hud2d_text(20.0f, 250.0f, 2.0f, line2, 0.8f, 0.9f, 1.0f);
    hud2d_end();
    SDL_GL_SwapWindow(s_win);
}

static void progress(const char *msg)
{
    char l[96];
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) input_handle_event(&ev);
    snprintf(l, sizeof(l), "RUN %d/%d  %s", s_iter_now, ITERATIONS, msg);
    frame_text("VITA GDAT PROTO", l);
}

static void run_all(void)
{
    int run_no, k;
    for (k = 0; k < ITERATIONS; k++) {
        s_iter_now = k + 1;
        gp_suite_run(&s_runs[k], progress);
    }
    s_iter_now = 0;
    s_g7_pass = gp_suite_compare(s_runs, ITERATIONS, s_g7, sizeof(s_g7));
    run_no = gp_report_next_run();
    s_csv_ok = run_no ? gp_report_write(run_no, s_runs, ITERATIONS, s_g7_pass, s_g7, s_csv, sizeof(s_csv)) : 0;
    snprintf(s_status, sizeof(s_status), "%s", s_csv_ok ? "RESULTS SAVED" : "RESULTS NOT SAVED");
}

static void draw_summary(void)
{
    const gp_suite *s = &s_runs[0];
    const float sc = 1.5f, lh = 7.0f * 1.5f + 3.0f;
    const int per_col = 35;
    char line[160];
    int i, passed_all = 0;

    for (i = 0; i < ITERATIONS; i++) passed_all += s_runs[i].passed;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    hud2d_begin(SCREEN_W, SCREEN_H);
    hud2d_rect(0, 0, SCREEN_W, SCREEN_H, 0.03f, 0.03f, 0.05f);

    snprintf(line, sizeof(line), "VITA GDAT PROTO  RUN 1: %d/%d PASS  G7 %s  ZLIB %s", s->passed, s->count,
             s_g7_pass ? "PASS" : "FAIL", zlibVersion());
    hud2d_text(10, 6, 2.0f, line, 0.95f, 0.85f, 0.45f);
    snprintf(line, sizeof(line), "%.17s %.40s  ALL RUNS %d/%d  SYNTHETIC DATA ONLY", s_status, s_csv_ok ? s_csv : "",
             passed_all, s->count * ITERATIONS);
    hud2d_text(10, 28, sc, line, s_csv_ok ? 0.6f : 1.0f, s_csv_ok ? 1.0f : 0.5f, 0.6f);

    for (i = 0; i < s->count && i < per_col * 3; i++) {
        const gp_result *r = &s->r[i];
        float x = 10.0f + (float)(i / per_col) * 318.0f;
        float y = 50.0f + (float)(i % per_col) * lh;
        if (r->mbps > 0)
            snprintf(line, sizeof(line), "%-6.6s %s %-.14s %.1f", r->id, r->pass ? "OK" : "NG", r->name, r->mbps);
        else
            snprintf(line, sizeof(line), "%-6.6s %s %-.22s", r->id, r->pass ? "OK" : "NG", r->name);
        hud2d_text(x, y, sc, line, r->pass ? 0.75f : 1.0f, r->pass ? 0.9f : 0.4f, r->pass ? 0.8f : 0.4f);
    }
    hud2d_text(10, SCREEN_H - 18, sc, "O RERUN  START EXIT   (MB/S SHOWN FOR G6; DETAILS IN THE LOG)", 0.7f, 0.7f,
               0.7f);
    hud2d_end();
    SDL_GL_SwapWindow(s_win);
}

int main(int argc, char *argv[])
{
    SDL_GLContext ctx;
    InputState in;
    int running = 1;

    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0)
        return 1;
    s_win = SDL_CreateWindow("Vita GDAT Proto", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_W,
                             SCREEN_H, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!s_win) { SDL_Quit(); return 1; }
    ctx = SDL_GL_CreateContext(s_win);
    if (!ctx) { SDL_DestroyWindow(s_win); SDL_Quit(); return 1; }
    SDL_GL_SetSwapInterval(1);
    input_init();

    glViewport(0, 0, SCREEN_W, SCREEN_H);
    glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    frame_text("VITA GDAT PROTO", "STARTING");
    run_all();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) input_handle_event(&ev);
        input_poll(&in);
        if (in.quit_requested || in.pressed[INPUT_BTN_START]) running = 0;
        else if (in.pressed[INPUT_BTN_CIRCLE]) run_all();
        draw_summary();
    }

    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    input_shutdown();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(s_win);
    SDL_Quit();
    return 0;
}
