/*
 * vmath.h - minimal math helpers for the renderer.
 * Matrices are handled by vitaGL's fixed-function matrix stack.
 */
#ifndef VMATH_H
#define VMATH_H

#include <math.h>

#define VM_PI 3.14159265358979323846f

typedef struct {
    float x, y, z;
} Vec3;

static inline float vm_deg_to_rad(float d) { return d * (VM_PI / 180.0f); }

static inline float vm_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Wrap an angle in degrees into [0, 360). */
static inline float vm_wrap_deg(float d)
{
    d = fmodf(d, 360.0f);
    return d < 0.0f ? d + 360.0f : d;
}

#endif /* VMATH_H */
