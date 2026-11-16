/*
 * cf_scene.h - the synthetic scenes and their tiers (Stage 4 design tables).
 *
 *  Dungeon  (C3-D-S..XL)  textured corridor, VBO, fog, back-face culling,
 *                         draws sorted by material; camera moves and sways.
 *             tier      S       M       L        XL
 *             draws     20      60      150      300
 *             triangles 10,000  30,000  60,000   100,800
 *             materials 4x256^2 8x256^2 16x512^2 32x512^2 (RGBA8, mipmaps)
 *  Battle   (C3-B-*)     dungeon S backdrop + 1/3/5/5 enemies of 2K/5K/8K/12K
 *                         triangles animated on the CPU every frame, 0/100/300/600
 *                         blended particles, 4 blended party portraits, and a
 *                         blended full-screen flash in L and XL.
 *  Dialogue (C3-V-*)     opaque 960x544 background + 1/2/3/4 blended 288x544
 *                         character layers (512x1024 RGBA8) + blended text box.
 *  Menu     (C3-U-*)     opaque panel + 500/1,500/3,000/6,000 blended 12x12 icons,
 *                         rebuilt on the CPU every frame, batched 1,000 per draw.
 *  Combined (C3-X)       battle M + a 2-layer dialogue cut-in (no background).
 *  Formats  (C3-T-*)     dungeon M with its 8 materials in RGBA8, RGB565, DXT1,
 *                         DXT5, ETC1 or PVRTC 4bpp (compressed: synthetic blocks,
 *                         full mip chains uploaded with glCompressedTexImage2D).
 *  Empty    (C3-P)       clear + HUD only: the 30 fps pacing calibration.
 *
 * All content is synthetic. One scene exists at a time (static storage).
 */
#ifndef CF_SCENE_H
#define CF_SCENE_H

#include "cf_budget.h"
#include "cf_geom.h"

typedef enum { CF_SC_EMPTY = 0, CF_SC_DUNGEON, CF_SC_BATTLE, CF_SC_DIALOGUE, CF_SC_MENU, CF_SC_COMBINED } cf_scene_kind;

typedef struct {
    const char   *id;
    const char   *name;
    cf_scene_kind kind;
    int           tier;           /* 0..3 = S, M, L, XL; -1 = none */
    int           texfmt;         /* CF_TF_* of the dungeon materials */
    int segs, pps, cols, rows, materials, mat_dim;   /* dungeon or battle backdrop */
    int enemies, ecols, erows, particles, flash;     /* battle */
    int layers, bg;                                  /* dialogue (or cut-in) */
    int icons;                                       /* menu */
} cf_scene_def;

int                 cf_scene_count(void);
const cf_scene_def *cf_scene_get(int i);
const cf_scene_def *cf_scene_find(const char *id);

/* Memory the scene will use: VRAM (textures and buffers, from format sizes)
 * and HEAP (CPU-side arrays and texture scratch). Upper estimates. */
void cf_scene_need(const cf_scene_def *d, cf_need *need);

/* Creates every GL and CPU resource. On failure everything created so far is
 * released. RP_OK, CF_ERR_GL, CF_ERR_UNSUPPORTED or RP_ERR_NOMEM. */
gp_status cf_scene_create(const cf_scene_def *d, char *why, size_t why_len);

/* CRC-32 of all generated content (vertices, indices, texture data). */
uint32_t cf_scene_crc(void);

/* Draws one frame of the scene (no clear, no swap, no HUD). The frame index
 * drives all animation, so every run draws the same sequence. Leaves texture,
 * blending, fog and the texcoord array off and buffers unbound, as hud2d needs. */
void cf_scene_draw(uint32_t frame);

void cf_scene_destroy(void);

/* Bytes of VRAM the scene's textures take according to the format sizes. */
uint64_t cf_scene_texture_bytes(const cf_scene_def *d);

#endif /* CF_SCENE_H */
