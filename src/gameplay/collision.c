/*
 * collision.c - circle vs. grid collision with sub-stepping and push-out.
 * Cost per frame: a few sub-steps x at most 3x3 cells x 3 iterations.
 */
#include <math.h>

#include "engine/vmath.h"
#include "gameplay/collision.h"
#include "gameplay/room_map.h"

#define MAX_SUBSTEPS      16
#define RESOLVE_ITERS     3
#define CONTACT_EPSILON   1e-4f

/* Cell index containing world coordinate v (floor division). */
static int cell_of(float v)
{
    return (int)floorf(v / ROOM_CELL_SIZE);
}

/* Pushes the circle out of one cell's square if they overlap.
 * Returns 1 if a push happened. */
static int push_out_of_cell(float *x, float *z, float r, int cx, int cz)
{
    const float x0 = cx * ROOM_CELL_SIZE, x1 = x0 + ROOM_CELL_SIZE;
    const float z0 = cz * ROOM_CELL_SIZE, z1 = z0 + ROOM_CELL_SIZE;
    float qx = vm_clampf(*x, x0, x1);
    float qz = vm_clampf(*z, z0, z1);
    float dx = *x - qx, dz = *z - qz;
    float d2 = dx * dx + dz * dz;

    if (d2 >= r * r)
        return 0;

    if (d2 > CONTACT_EPSILON * CONTACT_EPSILON) {
        /* Centre outside the square: push along the closest-point normal. */
        float d = sqrtf(d2);
        float k = (r - d) / d;
        *x += dx * k;
        *z += dz * k;
    } else {
        /* Centre inside the square (should not happen with sub-stepping):
         * leave by the nearest edge. */
        float left = *x - x0, right = x1 - *x;
        float near_ = *z - z0, far_ = z1 - *z;
        float m = left;
        int side = 0;
        if (right < m) { m = right; side = 1; }
        if (near_ < m) { m = near_; side = 2; }
        if (far_  < m) { m = far_;  side = 3; }
        switch (side) {
        case 0: *x = x0 - r; break;
        case 1: *x = x1 + r; break;
        case 2: *z = z0 - r; break;
        default: *z = z1 + r; break;
        }
    }
    return 1;
}

/* Resolves overlaps against the solid cells around (x, z). */
static int resolve(float *x, float *z, float r)
{
    int touched = 0, iter;

    for (iter = 0; iter < RESOLVE_ITERS; iter++) {
        int cx0 = cell_of(*x - r), cx1 = cell_of(*x + r);
        int cz0 = cell_of(*z - r), cz1 = cell_of(*z + r);
        int cx, cz, pushed = 0;

        for (cz = cz0; cz <= cz1; cz++)
            for (cx = cx0; cx <= cx1; cx++)
                if (room_is_solid(cx, cz))
                    pushed |= push_out_of_cell(x, z, r, cx, cz);

        touched |= pushed;
        if (!pushed)
            break;
    }
    return touched;
}

int collision_move_circle(float *x, float *z, float dx, float dz, float radius)
{
    const float max_step = radius * 0.5f;
    float len = sqrtf(dx * dx + dz * dz);
    int steps, i, touched = 0;

    if (len <= 0.0f)
        return resolve(x, z, radius);

    steps = (int)ceilf(len / max_step);
    if (steps < 1) steps = 1;
    if (steps > MAX_SUBSTEPS) steps = MAX_SUBSTEPS;

    for (i = 0; i < steps; i++) {
        *x += dx / (float)steps;
        *z += dz / (float)steps;
        touched |= resolve(x, z, radius);
    }
    return touched;
}

int collision_circle_overlaps(float x, float z, float radius)
{
    int cx0 = cell_of(x - radius), cx1 = cell_of(x + radius);
    int cz0 = cell_of(z - radius), cz1 = cell_of(z + radius);
    int cx, cz;

    for (cz = cz0; cz <= cz1; cz++) {
        for (cx = cx0; cx <= cx1; cx++) {
            float qx, qz, ddx, ddz;
            if (!room_is_solid(cx, cz))
                continue;
            qx = vm_clampf(x, cx * ROOM_CELL_SIZE, (cx + 1) * ROOM_CELL_SIZE);
            qz = vm_clampf(z, cz * ROOM_CELL_SIZE, (cz + 1) * ROOM_CELL_SIZE);
            ddx = x - qx; ddz = z - qz;
            if (ddx * ddx + ddz * ddz < radius * radius - CONTACT_EPSILON)
                return 1;
        }
    }
    return 0;
}
