/*
 * main.c - PS Vita 3D dungeon renderer, phase 5 (SDL2 + vitaGL).
 *
 * Phase 5 scope: rendering stability and tuning on hardware.
 *   - linear distance fog matched to the clear colour
 *   - shader warm-up: every fixed-function state combination used at
 *     runtime is drawn once before the first frame, so vitaGL compiles
 *     (or loads from ux0:data/shader_cache) its shaders up front
 *   - vsync toggle (SDL_GL_SetSwapInterval)
 *   - MSAA chosen at build time: VITA_MSAA = 0, 2 or 4 (see CMakeLists.txt)
 *   - RGBA8 / RGB565 texture format toggle
 *
 * Controls:
 *   Left stick  - move / strafe       Right stick - turn / look up-down
 *   D-pad       - walk / turn
 *   TRIANGLE    - toggle textures     CIRCLE      - toggle VBO / client arrays
 *   SQUARE      - trilinear/nearest   CROSS       - toggle collision (debug)
 *   L           - toggle fog          R           - toggle vsync
 *   SELECT      - RGBA8 / RGB565      START       - exit
 *
 * IMPORTANT: SDL2 calls vglInitExtended() with legacy_pool_size = 0, so
 * vitaGL's immediate mode (glBegin, glVertex2f etc., glEnd) has no buffer
 * and glVertex2f writes through a NULL pointer. All drawing uses vertex
 * arrays. Do not use glBegin or glVertex in this project.
 *
 * All content here is original placeholder material.
 */
#include <stdio.h>
#include <SDL2/SDL.h>
#include <vitaGL.h>

#include "engine/timing.h"
#include "engine/vmath.h"
#include "gameplay/player.h"
#include "gameplay/room_map.h"
#include "graphics/camera.h"
#include "graphics/hud2d.h"
#include "graphics/level_mesh.h"
#include "graphics/texgen.h"
#include "input/input.h"

#ifndef VITA_MSAA
#define VITA_MSAA 0
#endif

#define SCREEN_W 960
#define SCREEN_H 544

#define START_CELL_X 6
#define START_CELL_Z 6

/* Fog and clear share one colour so distant geometry fades into it. */
static const GLfloat FOG_COLOR[4] = {0.02f, 0.02f, 0.04f, 1.0f};
#define FOG_START 4.0f
#define FOG_END   18.0f

typedef struct {
    int textured;
    int use_vbo;
    int trilinear;
    int collision;
    int fog;
    int vsync;
    int tex565;
} DebugToggles;

typedef struct {
    int vert_count;
    int tri_count;
    int tex_ok;
    int vbo_ok;
    float warmup_ms;
} SceneInfo;

static void set_fog(int on)
{
    if (on) glEnable(GL_FOG); else glDisable(GL_FOG);
}

static void begin_3d(const Camera *cam, int fog)
{
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    set_fog(fog);
    camera_apply(cam, (float)SCREEN_W / (float)SCREEN_H);
}

/* Draw every fixed-function state combination once (textures x fog, then
 * the HUD) so shader compilation happens here and not mid-play. The result
 * is cleared by the first real frame and never presented. */
static float warm_up_shaders(const Camera *cam, const GLuint tex[TEX_COUNT],
                             const SceneInfo *si)
{
    Uint64 t0 = SDL_GetPerformanceCounter();
    int textured, fog;

    for (textured = 0; textured <= 1; textured++) {
        for (fog = 0; fog <= 1; fog++) {
            if (textured && !si->tex_ok)
                continue;
            begin_3d(cam, fog);
            level_mesh_draw(tex, textured, 0);
        }
    }
    hud2d_begin(SCREEN_W, SCREEN_H);
    hud2d_rect(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f);
    hud2d_end();
    glFinish();

    return (float)((double)(SDL_GetPerformanceCounter() - t0) * 1000.0 /
                   (double)SDL_GetPerformanceFrequency());
}

static void draw_hud(const Player *pl, const InputState *in,
                     const DebugToggles *t, const SceneInfo *si)
{
    char line[112];
    const float s = 2.0f;
    const float lh = 9.0f * s + 4.0f;
    float y = 10.0f;

    hud2d_begin(SCREEN_W, SCREEN_H);

    hud2d_rect(6.0f, 6.0f, 700.0f, 9.0f * lh + 8.0f, 0.05f, 0.05f, 0.08f);

    hud2d_text(12.0f, y, s, "PHASE 5 - STABILITY AND TUNING", 0.95f, 0.85f, 0.45f);
    y += lh;

    SDL_snprintf(line, sizeof(line), "FPS %.1f  AVG %.1f MS  MAX %.1f MS",
                 timing_fps(), timing_avg_ms(), timing_max_ms());
    hud2d_text(12.0f, y, s, line, 0.80f, 0.90f, 1.00f);
    y += lh;

    SDL_snprintf(line, sizeof(line), "VSYNC %d  MSAA %dX  FOG %s  WARMUP %.0f MS",
                 SDL_GL_GetSwapInterval(), VITA_MSAA, t->fog ? "ON" : "OFF",
                 si->warmup_ms);
    hud2d_text(12.0f, y, s, line, 0.80f, 0.90f, 1.00f);
    y += lh;

    SDL_snprintf(line, sizeof(line), "TEX %s %s %uK  %s",
                 !si->tex_ok ? "ERROR" : (t->textured ? "ON" : "OFF"),
                 t->tex565 ? "RGB565" : "RGBA8",
                 (unsigned)(TEX_COUNT * texgen_bytes_per_texture(t->tex565) / 1024),
                 t->trilinear ? "TRILINEAR" : "NEAREST");
    hud2d_text(12.0f, y, s, line, 0.80f, 0.90f, 1.00f);
    y += lh;

    SDL_snprintf(line, sizeof(line), "%s  TRIS %d  DRAWS %d",
                 !si->vbo_ok ? "VBO UNAVAILABLE" : (t->use_vbo ? "VBO" : "CLIENT ARRAYS"),
                 si->tri_count, TEX_COUNT);
    hud2d_text(12.0f, y, s, line, 0.80f, 0.90f, 1.00f);
    y += lh;

    SDL_snprintf(line, sizeof(line), "POS %.2f %.2f  YAW %.0f  PITCH %.0f  %s",
                 pl->x, pl->z, pl->yaw, pl->pitch,
                 !t->collision ? "NOCLIP" : (pl->touching ? "CONTACT" : ""));
    hud2d_text(12.0f, y, s, line, 0.80f, 0.90f, 1.00f);
    y += lh;

    SDL_snprintf(line, sizeof(line), "RAM %luK  VRAM %luK  %s",
                 (unsigned long)(vglMemFree(VGL_MEM_RAM) / 1024),
                 (unsigned long)(vglMemFree(VGL_MEM_VRAM) / 1024),
                 in->connected ? "" : "NO CONTROLLER");
    hud2d_text(12.0f, y, s, line, 0.60f, 0.75f, 0.85f);
    y += lh;

    hud2d_text(12.0f, y, s, "TRI TEX  O VBO  SQR FILTER  X COLLIDE",
               0.70f, 0.70f, 0.70f);
    y += lh;
    hud2d_text(12.0f, y, s, "L FOG  R VSYNC  SELECT 565  START EXIT",
               0.70f, 0.70f, 0.70f);

    hud2d_end();
}

int main(int argc, char *argv[])
{
    SDL_Window *window;
    SDL_GLContext context;
    DebugToggles toggles = {1, 0, 1, 1, 1, 1, 0};
    SceneInfo info;
    GLuint textures[TEX_COUNT] = {0};
    InputState in;
    Player player;
    Camera cam;
    int running = 1;

    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    /* Must be set before the GL window/context is created: SDL reads it
     * when it initialises vitaGL (2 -> 2x, 4 -> 4x, otherwise none). */
#if VITA_MSAA > 0
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, VITA_MSAA);
#endif

    window = SDL_CreateWindow("Vita Dungeon Test",
                              SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              SCREEN_W, SCREEN_H,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    context = SDL_GL_CreateContext(window);
    if (!context) {
        SDL_Log("SDL_GL_CreateContext failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_SetSwapInterval(toggles.vsync);
    input_init();

    /* ---- one-time scene setup ---- */
    info.vert_count = level_mesh_build();
    if (info.vert_count < 0) {
        SDL_Log("level_mesh_build: buffer overflow");
        info.vert_count = 0;
    }
    info.tri_count = level_mesh_triangle_count();
    info.vbo_ok = (level_mesh_upload_vbo() == 0);
    info.tex_ok = (texgen_create_all(textures, toggles.tex565, toggles.trilinear) == 0);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    player_init(&player, room_cell_center(START_CELL_X),
                room_cell_center(START_CELL_Z), 0.0f);
    camera_init(&cam, 0.0f, 0.0f, 0.0f, 0.0f);
    player_to_camera(&player, &cam);

    glViewport(0, 0, SCREEN_W, SCREEN_H);
    glClearColor(FOG_COLOR[0], FOG_COLOR[1], FOG_COLOR[2], FOG_COLOR[3]);
    glClearDepthf(1.0f);
    glDepthFunc(GL_LEQUAL);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, FOG_START);
    glFogf(GL_FOG_END, FOG_END);
    glFogfv(GL_FOG_COLOR, FOG_COLOR);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    info.warmup_ms = warm_up_shaders(&cam, textures, &info);

    timing_init();

    while (running) {
        SDL_Event ev;
        float dt;

        while (SDL_PollEvent(&ev))
            input_handle_event(&ev);

        dt = timing_tick();
        input_poll(&in);

        if (in.quit_requested || in.pressed[INPUT_BTN_START])
            running = 0;
        if (in.pressed[INPUT_BTN_TRIANGLE])
            toggles.textured = !toggles.textured;
        if (in.pressed[INPUT_BTN_CIRCLE])
            toggles.use_vbo = !toggles.use_vbo;
        if (in.pressed[INPUT_BTN_SQUARE]) {
            toggles.trilinear = !toggles.trilinear;
            texgen_set_filter(textures, toggles.trilinear);
        }
        if (in.pressed[INPUT_BTN_CROSS])
            toggles.collision = !toggles.collision;
        if (in.pressed[INPUT_BTN_L])
            toggles.fog = !toggles.fog;
        if (in.pressed[INPUT_BTN_R]) {
            toggles.vsync = !toggles.vsync;
            SDL_GL_SetSwapInterval(toggles.vsync);
        }
        if (in.pressed[INPUT_BTN_SELECT]) {
            toggles.tex565 = !toggles.tex565;
            glFinish(); /* GPU must be done with the old textures */
            texgen_delete_all(textures);
            info.tex_ok = (texgen_create_all(textures, toggles.tex565,
                                             toggles.trilinear) == 0);
        }

        player_update(&player, &in, dt, toggles.collision);
        player_to_camera(&player, &cam);

        /* ---- 3D pass ---- */
        begin_3d(&cam, toggles.fog);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        level_mesh_draw(textures, toggles.textured && info.tex_ok, toggles.use_vbo);

        /* ---- 2D overlay ---- */
        draw_hud(&player, &in, &toggles, &info);

        SDL_GL_SwapWindow(window);
    }

    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    texgen_delete_all(textures);
    level_mesh_free();
    input_shutdown();

    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
