#include "ps2re/types.h"
#include <string.h>

/*
 * Controller input — replaces PS2 SIO2 pad interface.
 *
 * PS2 pad: digital/analog modes, pressure-sensitive buttons,
 *          3.3V SPI interface, ~250Hz polling.
 *
 * ARM64: evdev/libinput on Linux, event-driven.
 */

typedef struct {
    /* Buttons (digital) */
    bool cross, circle, square, triangle;
    bool l1, r1, l2, r2;
    bool l3, r3;
    bool start, select;
    bool dpad_up, dpad_down, dpad_left, dpad_right;

    /* Analog sticks */
    f32 lx, ly;   /* -1.0 to 1.0 */
    f32 rx, ry;

    /* Pressure sensitive (0-255 like PS2) */
    u8  cross_pressure, square_pressure;
    u8  circle_pressure, triangle_pressure;
    u8  l1_pressure, r1_pressure;
    u8  l2_pressure, r2_pressure;
} PadState;

void pad_init(PadState* pad)
{
    memset(pad, 0, sizeof(*pad));
}

/* Read from evdev — called as async task */
void pad_read(PadState* pad, int fd)
{
    /* In production: read input events from /dev/input/eventN */
    (void)pad; (void)fd;
}