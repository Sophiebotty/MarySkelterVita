/*
 * player.h - first-person player controller (movement + look).
 *
 * Analog movement relative to view yaw, analog look, D-pad fallback, and
 * circle-vs-grid wall collision with sliding (phase 4).
 */
#ifndef PLAYER_H
#define PLAYER_H

#include "graphics/camera.h"
#include "input/input.h"

typedef struct {
    float x, z;      /* feet position on the floor plane */
    float yaw;       /* degrees; 0 looks down -Z, + turns left */
    float pitch;     /* degrees; + looks up */
    int   touching;  /* touched a wall during the last update */
} Player;

#define PLAYER_EYE_HEIGHT   1.2f
#define PLAYER_RADIUS       0.3f
#define PLAYER_MOVE_SPEED   3.0f    /* world units per second */
#define PLAYER_TURN_SPEED   120.0f  /* degrees per second at full stick */
#define PLAYER_PITCH_SPEED  90.0f   /* degrees per second at full stick */

void player_init(Player *p, float x, float z, float yaw);
/* collide = 0 is a debug "no-clip" mode: walls are ignored and the
 * position is only kept inside the map rectangle. */
void player_update(Player *p, const InputState *in, float dt, int collide);

/* Copies the player's eye pose into the camera. */
void player_to_camera(const Player *p, Camera *cam);

#endif /* PLAYER_H */
