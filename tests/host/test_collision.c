/* Host-side test of src/gameplay/collision.c against the real room map.
 * Run with tests/host/run_collision_test.sh (WSL). Not part of the Vita build. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "gameplay/collision.h"
#include "gameplay/room_map.h"

#define R      0.3f
#define SPEED  3.0f
#define MAXDT  (1.0f / 15.0f)   /* worst-case frame (timing.c clamp) */

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static int in_floor_cell(float x, float z)
{
    return !room_is_solid((int)floorf(x / ROOM_CELL_SIZE), (int)floorf(z / ROOM_CELL_SIZE));
}

int main(void)
{
    float x, z;
    int i, touched;
    const float step = SPEED * MAXDT; /* 0.2 units: the largest per-frame move */

    /* 1. Walk straight into the west outer wall (x = 2.0) at max step. */
    printf("1. head-on into outer wall\n");
    x = room_cell_center(3); z = room_cell_center(6);
    for (i = 0, touched = 0; i < 200; i++) {
        touched |= collision_move_circle(&x, &z, -step, 0.0f, R);
        CHECK(!collision_circle_overlaps(x, z, R), "overlap at frame %d (%.4f, %.4f)", i, x, z);
    }
    CHECK(touched, "never reported contact");
    CHECK(fabsf(x - (2.0f + R)) < 1e-3f, "stopped at x=%.4f, expected %.4f", x, 2.0f + R);
    printf("   stopped at x=%.4f (wall face 2.0 + r 0.3)\n", x);

    /* 2. Diagonal into the same wall: must slide along Z. */
    printf("2. diagonal slide\n");
    x = room_cell_center(1); z = room_cell_center(5);
    {
        float z_before = z;
        for (i = 0; i < 20; i++)
            collision_move_circle(&x, &z, -step * 0.7071f, step * 0.7071f, R);
        CHECK(!collision_circle_overlaps(x, z, R), "overlap after slide");
        CHECK(z - z_before > 1.5f, "did not slide: dz=%.4f", z - z_before);
        printf("   x=%.4f  slid dz=%.4f\n", x, z - z_before);
    }

    /* 3. Round the convex corner of the 2x2 pillar (cells 4-5, rows 3-4;
     *    corner at world (8, 6)) moving +X just above its north face. */
    printf("3. convex corner of pillar\n");
    x = room_cell_center(2); z = 6.0f - 0.25f; /* overlaps corner region slightly when passing */
    for (i = 0; i < 60; i++) {
        collision_move_circle(&x, &z, step, 0.0f, R);
        CHECK(!collision_circle_overlaps(x, z, R), "overlap at frame %d (%.4f, %.4f)", i, x, z);
    }
    printf("   ended at (%.4f, %.4f)\n", x, z);

    /* 4. Inner corner: push diagonally into the NW room corner. */
    printf("4. inner corner\n");
    x = room_cell_center(2); z = room_cell_center(2);
    for (i = 0; i < 100; i++)
        collision_move_circle(&x, &z, -step * 0.7071f, -step * 0.7071f, R);
    CHECK(!collision_circle_overlaps(x, z, R), "overlap in corner");
    CHECK(fabsf(x - 2.3f) < 1e-3f && fabsf(z - 2.3f) < 1e-3f, "corner pos (%.4f, %.4f)", x, z);
    printf("   settled at (%.4f, %.4f)\n", x, z);

    /* 5. Fuzz: 200k random max-speed frames from the start cell. */
    printf("5. random walk fuzz (200000 frames)\n");
    srand(12345);
    x = room_cell_center(6); z = room_cell_center(6);
    for (i = 0; i < 200000; i++) {
        float a = (float)rand() / RAND_MAX * 6.2831853f;
        float dt = (float)rand() / RAND_MAX * MAXDT;
        collision_move_circle(&x, &z, cosf(a) * SPEED * dt, sinf(a) * SPEED * dt, R);
        if (collision_circle_overlaps(x, z, R) || !in_floor_cell(x, z)) {
            CHECK(0, "fuzz escaped/overlapped at frame %d (%.4f, %.4f)", i, x, z);
            break;
        }
    }
    printf("   final (%.4f, %.4f)\n", x, z);

    printf(fails ? "RESULT: %d FAILURE(S)\n" : "RESULT: ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
