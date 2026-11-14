/*
 * camera.c - perspective + view setup through vitaGL's matrix stack.
 * Uses only gluPerspective, glRotatef, glTranslatef (verified in libvitaGL).
 */
#include <vitaGL.h>

#include "engine/vmath.h"
#include "graphics/camera.h"

void camera_init(Camera *cam, float x, float y, float z, float yaw)
{
    cam->x = x;
    cam->y = y;
    cam->z = z;
    cam->yaw = yaw;
    cam->pitch = 0.0f;
    cam->fov_y = 60.0f;
    cam->z_near = 0.05f;
    cam->z_far = 40.0f;
}

void camera_apply(const Camera *cam, float aspect)
{
    float pitch = vm_clampf(cam->pitch, -CAMERA_MAX_PITCH, CAMERA_MAX_PITCH);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(cam->fov_y, aspect, cam->z_near, cam->z_far);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(-pitch, 1.0f, 0.0f, 0.0f);
    glRotatef(-cam->yaw, 0.0f, 1.0f, 0.0f);
    glTranslatef(-cam->x, -cam->y, -cam->z);
}
