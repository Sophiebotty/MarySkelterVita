/*
 * input.c - SDL GameController wrapper with radial deadzone.
 */
#include <math.h>

#include "engine/vmath.h"
#include "input/input.h"

static SDL_GameController *s_pad = NULL;
static int s_pressed[INPUT_BTN_COUNT];
static int s_quit = 0;

static void open_first_pad(void)
{
    int i;
    if (s_pad)
        return;
    for (i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            s_pad = SDL_GameControllerOpen(i);
            if (s_pad)
                return;
        }
    }
}

void input_init(void)
{
    /* Touch should never act as a mouse in this app. (The Vita-specific
     * SDL_HINT_VITA_TOUCH_MOUSE_DEVICE only selects a panel; it cannot
     * disable synthetic mouse events.) */
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_memset(s_pressed, 0, sizeof(s_pressed));
    s_quit = 0;
    open_first_pad();
}

void input_shutdown(void)
{
    if (s_pad)
        SDL_GameControllerClose(s_pad);
    s_pad = NULL;
}

static int map_button(Uint8 b)
{
    switch (b) {
    case SDL_CONTROLLER_BUTTON_Y:             return INPUT_BTN_TRIANGLE;
    case SDL_CONTROLLER_BUTTON_B:             return INPUT_BTN_CIRCLE;
    case SDL_CONTROLLER_BUTTON_A:             return INPUT_BTN_CROSS;
    case SDL_CONTROLLER_BUTTON_X:             return INPUT_BTN_SQUARE;
    case SDL_CONTROLLER_BUTTON_START:         return INPUT_BTN_START;
    case SDL_CONTROLLER_BUTTON_BACK:          return INPUT_BTN_SELECT;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return INPUT_BTN_L;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return INPUT_BTN_R;
    default:                                  return -1;
    }
}

void input_handle_event(const SDL_Event *ev)
{
    switch (ev->type) {
    case SDL_QUIT:
        s_quit = 1;
        break;
    case SDL_CONTROLLERDEVICEADDED:
        open_first_pad();
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (s_pad && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_pad)) == ev->cdevice.which) {
            SDL_GameControllerClose(s_pad);
            s_pad = NULL;
            open_first_pad();
        }
        break;
    case SDL_CONTROLLERBUTTONDOWN: {
        int b = map_button(ev->cbutton.button);
        if (b >= 0)
            s_pressed[b] = 1;
        break;
    }
    default:
        break;
    }
}

static float axis_raw(SDL_GameControllerAxis a)
{
    float v = (float)SDL_GameControllerGetAxis(s_pad, a) / (float)SDL_JOYSTICK_AXIS_MAX;
    return vm_clampf(v, -1.0f, 1.0f);
}

/* Radial deadzone with rescale; 'curve' > 1 gives finer control near centre. */
static void stick(SDL_GameControllerAxis ax, SDL_GameControllerAxis ay,
                  float curve, float *out_x, float *out_y)
{
    float x = axis_raw(ax);
    float y = -axis_raw(ay); /* SDL: up is negative -> make up positive */
    float mag = sqrtf(x * x + y * y);
    float scaled;

    if (mag <= INPUT_STICK_DEADZONE) {
        *out_x = *out_y = 0.0f;
        return;
    }
    scaled = (mag - INPUT_STICK_DEADZONE) / (1.0f - INPUT_STICK_DEADZONE);
    if (scaled > 1.0f)
        scaled = 1.0f;
    scaled = powf(scaled, curve);

    *out_x = x / mag * scaled;
    *out_y = y / mag * scaled;
}

void input_poll(InputState *out)
{
    SDL_memset(out, 0, sizeof(*out));
    SDL_memcpy(out->pressed, s_pressed, sizeof(s_pressed));
    SDL_memset(s_pressed, 0, sizeof(s_pressed));
    out->quit_requested = s_quit;

    if (!s_pad)
        return;
    out->connected = 1;

    stick(SDL_CONTROLLER_AXIS_LEFTX,  SDL_CONTROLLER_AXIS_LEFTY,  1.0f, &out->move_x, &out->move_y);
    stick(SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY, 1.6f, &out->look_x, &out->look_y);

    out->dpad_up    = SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
    out->dpad_down  = SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    out->dpad_left  = SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    out->dpad_right = SDL_GameControllerGetButton(s_pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
}
