/*
 * input.h - controller input for the Vita (built-in pad via SDL GameController).
 *
 * Sticks are returned as -1..1 after a radial deadzone, rescaled so motion
 * starts smoothly from zero at the deadzone edge. Y axes are flipped so that
 * pushing a stick UP gives a POSITIVE value.
 */
#ifndef INPUT_H
#define INPUT_H

#include <SDL2/SDL.h>

/* SDL headers: resting sticks report up to ~8000 of 32767 (~0.25). */
#define INPUT_STICK_DEADZONE 0.25f

typedef enum {
    INPUT_BTN_TRIANGLE = 0,  /* SDL_CONTROLLER_BUTTON_Y */
    INPUT_BTN_CIRCLE,        /* SDL_CONTROLLER_BUTTON_B */
    INPUT_BTN_CROSS,         /* SDL_CONTROLLER_BUTTON_A */
    INPUT_BTN_SQUARE,        /* SDL_CONTROLLER_BUTTON_X */
    INPUT_BTN_START,
    INPUT_BTN_SELECT,
    INPUT_BTN_L,
    INPUT_BTN_R,
    INPUT_BTN_COUNT
} InputButton;

typedef struct {
    float move_x;   /* strafe: +right */
    float move_y;   /* walk:   +forward */
    float look_x;   /* turn:   +right */
    float look_y;   /* pitch:  +up */
    int   dpad_up, dpad_down, dpad_left, dpad_right; /* held */
    int   pressed[INPUT_BTN_COUNT];                  /* went down this frame */
    int   connected;
    int   quit_requested;
} InputState;

void input_init(void);
void input_shutdown(void);

/* Feed every SDL event here (handles controller hot-plug, button edges,
 * SDL_QUIT). */
void input_handle_event(const SDL_Event *ev);

/* Call once per frame after event polling. Fills *out and clears the
 * per-frame "pressed" edges. */
void input_poll(InputState *out);

#endif /* INPUT_H */
