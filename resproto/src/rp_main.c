/*
 * rp_main.c - Vita ResProto: standalone resource-loading prototype.
 *
 * Generates synthetic fixtures in ux0:data/ResProto/work/, runs the test
 * suite three times, compares the runs (T14) and writes runNNN.csv/.log.
 * It reads no game files and cannot load Mary Skelter Finale archives.
 *
 * Controls (summary screen):
 *   CIRCLE   - run the suite again (re-uses the fixtures; regenerates them
 *              first if any are missing, e.g. after SELECT)
 *   TRIANGLE - regenerate fixtures, then run again
 *   SELECT   - delete the generated work files
 *   START    - exit
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
#include "rp_fixture.h"
#include "rp_report.h"
#include "rp_tests.h"

#define SCREEN_W 960
#define SCREEN_H 544
#define ITERATIONS 3

static SDL_Window *s_win;
static rp_suite s_runs[ITERATIONS];
static rp_fixture_set s_fx;
static char s_status[192];
static char s_t14[96];
static int  s_t14_pass;
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
    if (s_iter_now > 0) snprintf(l, sizeof(l), "RUN %d/%d  %s", s_iter_now, ITERATIONS, msg);
    else                snprintf(l, sizeof(l), "WRITING %s", msg);
    frame_text("VITA RESPROTO", l);
}

static void run_all(int regenerate)
{
    char missing[48] = "";
    char regen_note[128] = "";
    int run_no, k;

    /* O (regenerate == 0) re-uses the work files, but if any were deleted
     * (SELECT) or are otherwise missing, regenerate them first. */
    if (!regenerate && !rp_fixture_present(missing, sizeof(missing))) {
        regenerate = 1;
        snprintf(regen_note, sizeof(regen_note), "%s MISSING - REGENERATED  ", missing);
    }
    if (regenerate) {
        s_iter_now = 0;
        frame_text("VITA RESPROTO", "GENERATING TEST FILES");
        if (rp_fixture_generate(&s_fx, progress) != RP_OK) {
            /* the suite still runs and reports T0 FIXTURE GENERATION as failed */
            snprintf(regen_note, sizeof(regen_note), "REGENERATION FAILED AT %s: %s  ",
                     s_fx.failed_file, rp_err_name(s_fx.status));
        }
    }
    for (k = 0; k < ITERATIONS; k++) {
        s_iter_now = k + 1;
        rp_suite_run(&s_runs[k], &s_fx, progress);
    }
    s_iter_now = 0;
    s_t14_pass = rp_suite_compare(s_runs, ITERATIONS, s_t14, sizeof(s_t14), NULL, NULL);
    run_no = rp_report_next_run();
    s_csv_ok = run_no ? rp_report_write(run_no, &s_fx, s_runs, ITERATIONS, s_t14_pass, s_t14,
                                        s_csv, sizeof(s_csv)) : 0;
    snprintf(s_status, sizeof(s_status), "%s%s", regen_note,
             s_csv_ok ? "RESULTS SAVED" : "RESULTS NOT SAVED");
}

static void draw_summary(void)
{
    const rp_suite *s = &s_runs[0];
    const float sc = 1.5f, lh = 9.0f * 1.5f + 3.0f;
    char line[320];
    int i, passed_all = 0;
    float y;

    for (i = 0; i < ITERATIONS; i++) passed_all += s_runs[i].passed;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    hud2d_begin(SCREEN_W, SCREEN_H);
    hud2d_rect(0, 0, SCREEN_W, SCREEN_H, 0.03f, 0.03f, 0.05f);

    snprintf(line, sizeof(line), "VITA RESPROTO  RUN 1: %d/%d PASS  T14 %s  ZLIB %s",
             s->passed, s->count, s_t14_pass ? "PASS" : "FAIL", zlibVersion());
    hud2d_text(10, 8, 2.0f, line, 0.95f, 0.85f, 0.45f);
    snprintf(line, sizeof(line), "%s %s  ALL RUNS %d/%d", s_status, s_csv_ok ? s_csv : "", passed_all,
             s->count * ITERATIONS);
    hud2d_text(10, 32, sc, line, s_csv_ok ? 0.6f : 1.0f, s_csv_ok ? 1.0f : 0.5f, 0.6f);

    y = 56.0f;
    for (i = 0; i < s->count; i++) {
        const rp_result *r = &s->r[i];
        float x = (i < 26) ? 10.0f : 490.0f;
        float yy = (i < 26) ? y + (float)i * lh : y + (float)(i - 26) * lh;
        double v = r->mbps > 0 ? r->mbps : r->ms;           /* MB/s if measured, else ms */
        long tenths = (v < 0) ? 0 : (v > 9999999.0 ? 99999990L : (long)(v * 10.0 + 0.5));
        snprintf(line, sizeof(line), "%-5.5s %s %-.22s %ld.%ld", r->id, r->pass ? "OK  " : "FAIL", r->name,
                 tenths / 10, tenths % 10);
        hud2d_text(x, yy, sc, line, r->pass ? 0.75f : 1.0f, r->pass ? 0.9f : 0.4f, r->pass ? 0.8f : 0.4f);
    }
    hud2d_text(10, SCREEN_H - 20, sc, "O RERUN  TRI REGENERATE  SELECT DELETE FILES  START EXIT", 0.7f, 0.7f, 0.7f);
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
    s_win = SDL_CreateWindow("Vita ResProto", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             SCREEN_W, SCREEN_H, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!s_win) { SDL_Quit(); return 1; }
    ctx = SDL_GL_CreateContext(s_win);
    if (!ctx) { SDL_DestroyWindow(s_win); SDL_Quit(); return 1; }
    SDL_GL_SetSwapInterval(1);
    input_init();

    glViewport(0, 0, SCREEN_W, SCREEN_H);
    glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    frame_text("VITA RESPROTO", "GENERATING TEST FILES");
    run_all(1);

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) input_handle_event(&ev);
        input_poll(&in);
        if (in.quit_requested || in.pressed[INPUT_BTN_START]) running = 0;
        else if (in.pressed[INPUT_BTN_CIRCLE]) run_all(0);
        else if (in.pressed[INPUT_BTN_TRIANGLE]) run_all(1);
        else if (in.pressed[INPUT_BTN_SELECT])
            snprintf(s_status, sizeof(s_status), "DELETED %d WORK FILES", rp_fixture_delete());
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
