/*
 * cf_main.c - Vita ContentFit (Stage 4 content-fit benchmark, VDNG00005).
 *
 * Measures how much synthetic dungeon, battle, dialogue and menu content
 * fits this Vita at a stable 30 fps, how large areas load, and how memory
 * behaves, using only synthetic data packaged with the app. It reads no
 * Mary Skelter file and makes no claim about Finale itself.
 *
 * Start screen:  CROSS full run (3 iterations)   SQUARE quick run (1)   START exit
 * During a run:  START stops after the current step (results so far are kept)
 * Results:       CIRCLE new run (new run number)  START exit
 *
 * Output: ux0:data/ContentFit/runNNN.csv, .log, _frames.csv, _progress.txt
 * (never overwritten). SDL log messages go into the run's log instead of
 * SDL's default ux0:/data/SDL_Log.txt.
 * vitaGL's shader cache is kept in ux0:data/ContentFit/shader_cache: the
 * installed vitaGL uses fixed paths below ux0:data/shader_cache (its
 * vglSetShaderCachePath is an empty function), so ContentFit is linked with
 * --wrap=sceIoOpen/--wrap=sceIoMkdir and cf_iowrap.c rewrites that prefix.
 * ux0:data/ContentFit must exist before SDL creates the window (which starts
 * vitaGL and creates the cache folders); main() creates it first.
 * Drawing uses the shared hud2d module (vertex arrays only; SDL2 starts
 * vitaGL with legacy_pool_size = 0).
 */
#include <stdio.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <vitaGL.h>

#include "cf_suite.h"
#include "graphics/hud2d.h"
#include "input/input.h"
#include "rp_io.h"

static SDL_Window *s_win;
static cf_ctx s_ctx;

/* SDL's Vita log output would write ux0:/data/SDL_Log.txt; keep its
 * messages in the current run's log instead (dropped between runs). */
static void sdl_log_to_report(void *user, int category, SDL_LogPriority priority, const char *message)
{
    (void)user;
    if (s_ctx.rep.log)
        cf_report_log(&s_ctx.rep, "SDL log (category %d, priority %d): %s", category, (int)priority, message);
}

static void text_frame(const char *const *lines, int n, const float *col)
{
    int i;
    glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    hud2d_begin(CF_SCREEN_W, CF_SCREEN_H);
    for (i = 0; i < n; i++)
        if (lines[i])
            hud2d_text(20.0f, 20.0f + 26.0f * (float)i, i == 0 ? 2.5f : 2.0f, lines[i], col[0], col[1], col[2]);
    hud2d_end();
    SDL_GL_SwapWindow(s_win);
}

static void ui_progress(cf_ctx *c, const char *l1, const char *l2)
{
    const char *lines[4];
    static const float col[3] = {0.8f, 0.9f, 1.0f};
    char l3[96];
    (void)c;
    snprintf(l3, sizeof(l3), "RESULTS: %s", c->rep.run_no ? c->rep.files.csv : "NOT BEING SAVED");
    lines[0] = "VITA CONTENTFIT";
    lines[1] = l1;
    lines[2] = l2;
    lines[3] = l3;
    text_frame(lines, 4, col);
}

static int poll(InputState *in)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) input_handle_event(&ev);
    input_poll(in);
    return !in->quit_requested;
}

/* 0 exit, 1 quick, 3 full */
static int start_screen(void)
{
    static const char *const lines[] = {
        "VITA CONTENTFIT  (STAGE 4 CONTENT-FIT BENCHMARK)",
        "SYNTHETIC CONTENT ONLY. NO GAME FILES ARE READ.",
        "",
        "X  FULL RUN: 3 ITERATIONS, ABOUT 30 MINUTES",
        "[] QUICK RUN: 1 ITERATION, ABOUT 10 MINUTES",
        "START  EXIT",
        "",
        "PLEASE CONNECT THE CHARGER. START STOPS A RUN EARLY.",
        "RESULTS GO TO UX0:DATA/CONTENTFIT/ (NEVER OVERWRITTEN).",
    };
    static const float col[3] = {0.95f, 0.85f, 0.45f};
    InputState in;
    for (;;) {
        if (!poll(&in) || in.pressed[INPUT_BTN_START]) return 0;
        if (in.pressed[INPUT_BTN_CROSS]) return 3;
        if (in.pressed[INPUT_BTN_SQUARE]) return 1;
        text_frame(lines, (int)(sizeof(lines) / sizeof(lines[0])), col);
    }
}

static void draw_results(const cf_ctx *c)
{
    const float sc = 1.5f, lh = 7.0f * 1.5f + 4.0f;
    char line[160];
    int i, row = 0, it = c->iter;

    glClearColor(0.03f, 0.03f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    hud2d_begin(CF_SCREEN_W, CF_SCREEN_H);
    snprintf(line, sizeof(line), "VITA CONTENTFIT  %s  C5 %s", c->aborted ? "STOPPED EARLY" : "COMPLETE",
             c->c5_pass > 0 ? "PASS" : (c->c5_pass == 0 ? "FAIL" : "N/A"));
    hud2d_text(10, 6, 2.0f, line, 0.95f, 0.85f, 0.45f);
    snprintf(line, sizeof(line), "%s %s", c->rep.run_no && c->rep.ok ? "RESULTS SAVED" : "RESULTS NOT SAVED",
             c->rep.run_no ? c->rep.files.csv : "");
    hud2d_text(10, 28, sc, line, c->rep.ok ? 0.6f : 1.0f, c->rep.ok ? 1.0f : 0.5f, 0.6f);
    /* verdict rows of the last iteration that has rows, two columns */
    for (i = 0; i < c->nres[it] && row < 70; i++) {
        const cf_result *r = &c->res[it][i];
        float x, y;
        int bad = !r->ok;
        if (strcmp(r->mode, "VERDICT") != 0 && strncmp(r->id, "C2", 2) != 0 && strncmp(r->id, "C4", 2) != 0 &&
            strncmp(r->id, "C3-T", 4) != 0 && strncmp(r->id, "C5", 2) != 0)
            continue;
        x = 10.0f + (float)(row / 35) * 470.0f;
        y = 50.0f + (float)(row % 35) * lh;
        snprintf(line, sizeof(line), "%-8.8s %-6.6s %-12.12s %s", r->id, r->mode, r->verdict, bad ? "NOT OK" : "");
        hud2d_text(x, y, sc, line, bad ? 1.0f : 0.75f, bad ? 0.4f : 0.9f, bad ? 0.4f : 0.8f);
        row++;
    }
    hud2d_text(10, CF_SCREEN_H - 18, sc, "O NEW RUN   START EXIT   (DETAILS IN THE LOG AND CSV)", 0.7f, 0.7f, 0.7f);
    hud2d_end();
    SDL_GL_SwapWindow(s_win);
}

int main(int argc, char *argv[])
{
    SDL_GLContext ctx;
    InputState in;
    int mode;

    (void)argc;
    (void)argv;
    /* Before SDL starts: ContentFit's folder must exist for the redirected
     * shader cache, and SDL log messages go to the run's log, not to
     * ux0:/data/SDL_Log.txt. */
    rp_mkdir(CF_OUT_DIR);
    SDL_LogSetOutputFunction(sdl_log_to_report, NULL);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0)
        return 1;
    s_win = SDL_CreateWindow("Vita ContentFit", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, CF_SCREEN_W,
                             CF_SCREEN_H, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!s_win) { SDL_Quit(); return 1; }
    ctx = SDL_GL_CreateContext(s_win);
    if (!ctx) { SDL_DestroyWindow(s_win); SDL_Quit(); return 1; }
    SDL_GL_SetSwapInterval(1);
    input_init();
    cf_plat_attach_window(s_win);

    glViewport(0, 0, CF_SCREEN_W, CF_SCREEN_H);
    glClearDepthf(1.0f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    mode = start_screen();
    while (mode) {
        cf_suite_run(&s_ctx, mode, ui_progress);
        mode = 0;
        for (;;) {
            if (!poll(&in) || in.pressed[INPUT_BTN_START]) break;
            if (in.pressed[INPUT_BTN_CIRCLE]) {
                mode = start_screen();
                break;
            }
            draw_results(&s_ctx);
        }
    }

    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    input_shutdown();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(s_win);
    SDL_Quit();
    return 0;
}
