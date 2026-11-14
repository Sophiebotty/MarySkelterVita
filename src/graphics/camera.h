/*
 * camera.h - first-person perspective camera (fixed-function matrices).
 *
 * Convention: right-handed, +Y up. yaw = 0 looks down -Z, positive yaw turns
 * left (counter-clockwise seen from above). Positive pitch looks up.
 */
#ifndef CAMERA_H
#define CAMERA_H

typedef struct {
    float x, y, z;     /* eye position (world units) */
    float yaw;         /* degrees */
    float pitch;       /* degrees, clamp to +/- CAMERA_MAX_PITCH */
    float fov_y;       /* vertical field of view, degrees */
    float z_near;
    float z_far;
} Camera;

#define CAMERA_MAX_PITCH 60.0f

void camera_init(Camera *cam, float x, float y, float z, float yaw);

/* Loads GL_PROJECTION and GL_MODELVIEW for this camera. */
void camera_apply(const Camera *cam, float aspect);

#endif /* CAMERA_H */
