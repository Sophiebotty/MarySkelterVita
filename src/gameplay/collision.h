/*
 * collision.h - circle vs. grid-cell collision on the XZ plane.
 *
 * The player is a circle of radius r; every solid room cell is an
 * axis-aligned square. Movement is split into sub-steps no longer than
 * r/2, and after each sub-step the circle is pushed out of any overlapping
 * cell along the contact normal. This gives:
 *   - wall sliding (only the into-wall component is removed),
 *   - smooth rounding around convex corners,
 *   - no tunnelling at any speed the controller can produce.
 *
 * Cells outside the map count as solid (room_is_solid), so the map edge
 * is a wall too.
 */
#ifndef COLLISION_H
#define COLLISION_H

/* Moves (*x, *z) by (dx, dz) with collision. Requires radius < cell size.
 * Returns 1 if the circle touched any wall during the move, else 0. */
int collision_move_circle(float *x, float *z, float dx, float dz, float radius);

/* 1 if a circle at (x, z) overlaps any solid cell. */
int collision_circle_overlaps(float x, float z, float radius);

#endif /* COLLISION_H */
