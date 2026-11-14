/*
 * hud2d.c - batched 2D overlay. Moved from main.c (phase 0 test app).
 */
#include <SDL2/SDL.h>
#include <vitaGL.h>

#include "font5x7.h"
#include "graphics/hud2d.h"

#define BATCH_MAX_VERTS 12288 /* multiple of 6; ~2048 rects per flush */

static GLfloat batch_pos[BATCH_MAX_VERTS * 2];
static GLfloat batch_col[BATCH_MAX_VERTS * 4];
static int batch_count = 0;

static void batch_flush(void)
{
    if (batch_count == 0)
        return;
    glVertexPointer(2, GL_FLOAT, 0, batch_pos);
    glColorPointer(4, GL_FLOAT, 0, batch_col);
    glDrawArrays(GL_TRIANGLES, 0, batch_count);
    batch_count = 0;
}

static void batch_vertex(float x, float y, float r, float g, float b)
{
    GLfloat *p = &batch_pos[batch_count * 2];
    GLfloat *c = &batch_col[batch_count * 4];
    p[0] = x; p[1] = y;
    c[0] = r; c[1] = g; c[2] = b; c[3] = 1.0f;
    batch_count++;
}

void hud2d_begin(int screen_w, int screen_h)
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE); /* the Y-flipped ortho reverses winding */
    glDisable(GL_FOG);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, screen_w, screen_h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    batch_count = 0;
}

void hud2d_rect(float x, float y, float w, float h, float r, float g, float b)
{
    if (batch_count + 6 > BATCH_MAX_VERTS)
        batch_flush();
    batch_vertex(x,     y,     r, g, b);
    batch_vertex(x + w, y,     r, g, b);
    batch_vertex(x + w, y + h, r, g, b);
    batch_vertex(x,     y,     r, g, b);
    batch_vertex(x + w, y + h, r, g, b);
    batch_vertex(x,     y + h, r, g, b);
}

void hud2d_text(float x, float y, float scale, const char *s,
                float r, float g, float b)
{
    float pen = x;

    for (; *s; s++) {
        const Glyph5x7 *gl = font5x7_find(*s);
        if (gl) {
            int row, col;
            for (row = 0; row < FONT_GLYPH_H; row++) {
                for (col = 0; col < FONT_GLYPH_W; col++) {
                    if (gl->rows[row] & (1u << (FONT_GLYPH_W - 1 - col)))
                        hud2d_rect(pen + col * scale, y + row * scale, scale, scale, r, g, b);
                }
            }
        }
        pen += (FONT_GLYPH_W + 1) * scale;
    }
}

float hud2d_text_width(const char *s, float scale)
{
    size_t n = SDL_strlen(s);
    return n ? (float)(n * (FONT_GLYPH_W + 1) - 1) * scale : 0.0f;
}

void hud2d_end(void)
{
    batch_flush();
}
