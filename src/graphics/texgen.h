/*
 * texgen.h - original procedural placeholder textures, generated at startup.
 *
 * No image decoder is installed in the VitaSDK, and no external assets are
 * used: all textures are computed here (stone bricks, floor flagstones,
 * ceiling planks). 128x128 with mipmaps, uploaded either as RGBA8
 * (64 KB base level) or RGB565 (32 KB base level; the textures are opaque).
 */
#ifndef TEXGEN_H
#define TEXGEN_H

#include <vitaGL.h>

enum {
    TEX_WALL = 0,
    TEX_FLOOR,
    TEX_CEILING,
    TEX_COUNT
};

#define TEXGEN_SIZE 128

/* Generates and uploads all textures into out[TEX_COUNT].
 * use_565 != 0 uploads GL_RGB / GL_UNSIGNED_SHORT_5_6_5 instead of RGBA8.
 * Applies the given filter mode. Returns 0 on success, -1 on GL error. */
int  texgen_create_all(GLuint out[TEX_COUNT], int use_565, int trilinear);

/* trilinear != 0: GL_LINEAR_MIPMAP_LINEAR; otherwise GL_NEAREST (no mips). */
void texgen_set_filter(const GLuint tex[TEX_COUNT], int trilinear);

void texgen_delete_all(GLuint tex[TEX_COUNT]);

/* Approximate GPU bytes used by one texture including its mip chain. */
unsigned texgen_bytes_per_texture(int use_565);

#endif /* TEXGEN_H */
