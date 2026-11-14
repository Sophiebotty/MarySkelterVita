/*
 * player.c - first-person controller.
 *
 * Direction vectors for yaw (degrees, 0 = -Z, + = turn left):
 *   forward = (-sin(yaw), 0, -cos(yaw))
 *   right   = ( cos(yaw), 0, -sin(yaw))
 * These match camera_apply(), which rotates the world by -yaw about +Y.
 */
#include <math.h>

#include "engine/vmath.h"
#include "gameplay/collision.h"
#include "gameplay/player.h"
#include "gameplay/room_map.h"

void player_init(Player *p, float x, float z, float yaw)
{
    p->x = x;
    p->z = z;
    p->yaw = yaw;
    p->pitch = 0.0f;
    p->touching = 0;
}

void player_update(Player *p, const InputState *in, float dt, int collide)
{
    float fwd = in->move_y;
    float strafe = in->move_x;
    float turn = in->look_x;
    float yaw_rad, sy, cy;

    /* D-pad fallback: up/down walk, left/right turn (digital = full speed). */
    if (in->dpad_up)    fwd += 1.0f;
    if (in->dpad_down)  fwd -= 1.0f;
    if (in->dpad_left)  turn -= 1.0f;
    if (in->dpad_right) turn += 1.0f;
    fwd = vm_clampf(fwd, -1.0f, 1.0f);
    turn = vm_clampf(turn, -1.0f, 1.0f);

    /* Look. Stick right turns right, which is negative yaw. */
    p->yaw = vm_wrap_deg(p->yaw - turn * PLAYER_TURN_SPEED * dt);
    p->pitch = vm_clampf(p->pitch + in->look_y * PLAYER_PITCH_SPEED * dt,
                         -CAMERA_MAX_PITCH, CAMERA_MAX_PITCH);

    /* Move relative to yaw only (pitch does not affect walking). */
    yaw_rad = vm_deg_to_rad(p->yaw);
    sy = sinf(yaw_rad);
    cy = cosf(yaw_rad);
    {
        float dx = (-sy * fwd + cy * strafe);
        float dz = (-cy * fwd - sy * strafe);
        float len = sqrtf(dx * dx + dz * dz);
        if (len > 1.0f) { dx /= len; dz /= len; } /* D-pad + stick combined */
        dx *= PLAYER_MOVE_SPEED * dt;
        dz *= PLAYER_MOVE_SPEED * dt;

        if (collide) {
            p->touching = collision_move_circle(&p->x, &p->z, dx, dz, PLAYER_RADIUS);
        } else {
            /* Debug no-clip: ignore walls, stay inside the map rectangle. */
            p->x = vm_clampf(p->x + dx, PLAYER_RADIUS, ROOM_W * ROOM_CELL_SIZE - PLAYER_RADIUS);
            p->z = vm_clampf(p->z + dz, PLAYER_RADIUS, ROOM_D * ROOM_CELL_SIZE - PLAYER_RADIUS);
            p->touching = 0;
        }
    }
}

void player_to_camera(const Player *p, Camera *cam)
{
    cam->x = p->x;
    cam->y = PLAYER_EYE_HEIGHT;
    cam->z = p->z;
    cam->yaw = p->yaw;
    cam->pitch = p->pitch;
}
