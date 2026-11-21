/* Pad minimal: port 0 saja. */
#include <string.h>

#include <io/pad.h>

#include "pad.h"

static uint32_t prev, held;

void pad_init(void)
{
    ioPadInit(7);
    prev = held = 0;
}

static uint32_t read_buttons(void)
{
    padInfo info;
    padData d;
    uint32_t b = 0;
    if (ioPadGetInfo(&info) != 0) return 0;
    if (!info.status[0]) return 0;
    if (ioPadGetData(0, &d) != 0 || d.len == 0) return held;
    if (d.BTN_UP) b |= PAD_UP;
    if (d.BTN_DOWN) b |= PAD_DOWN;
    if (d.BTN_LEFT) b |= PAD_LEFT;
    if (d.BTN_RIGHT) b |= PAD_RIGHT;
    if (d.BTN_CROSS) b |= PAD_CROSS;
    if (d.BTN_CIRCLE) b |= PAD_CIRCLE;
    if (d.BTN_SQUARE) b |= PAD_SQUARE;
    if (d.BTN_TRIANGLE) b |= PAD_TRIANGLE;
    if (d.BTN_START) b |= PAD_START;
    if (d.BTN_SELECT) b |= PAD_SELECT;
    if (d.BTN_L1) b |= PAD_L1;
    if (d.BTN_R1) b |= PAD_R1;
    return b;
}

uint32_t pad_pressed(void)
{
    uint32_t now = read_buttons();
    uint32_t pressed = now & ~prev;
    prev = held = now;
    return pressed;
}

uint32_t pad_held(void)
{
    return held;
}
