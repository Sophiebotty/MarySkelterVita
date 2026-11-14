/*
 * hud2d.h - batched 2D overlay (rectangles + 5x7 bitmap text).
 *
 * Uses client-side vertex arrays + glDrawArrays only. SDL2 initialises
 * vitaGL with legacy_pool_size = 0, so glBegin/glVertex* must never be used.
 *
 * Usage per frame (after the 3D pass):
 *   hud2d_begin(960, 544);
 *   hud2d_rect(...); hud2d_text(...);
 *   hud2d_end();
 *
 * Requires GL_VERTEX_ARRAY and GL_COLOR_ARRAY client states to be enabled.
 */
#ifndef HUD2D_H
#define HUD2D_H

/* Sets an orthographic top-left-origin projection, disables depth test,
 * face culling and fog, and starts a new batch. */
void  hud2d_begin(int screen_w, int screen_h);

void  hud2d_rect(float x, float y, float w, float h, float r, float g, float b);
void  hud2d_text(float x, float y, float scale, const char *s,
                 float r, float g, float b);
float hud2d_text_width(const char *s, float scale);

/* Draws everything queued since hud2d_begin(). */
void  hud2d_end(void);

#endif /* HUD2D_H */
