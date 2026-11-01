#ifndef BLACK_MENU_INPUT_H
#define BLACK_MENU_INPUT_H
#include "port_menu.h"
typedef struct { unsigned previous; int draining; } BlackMenuPad;
/* Compact UI vocabulary, independent of SDL/Xbox struct layout. */
enum { BLACK_PAD_CHORD = 1, BLACK_PAD_ACCEPT = 2, BLACK_PAD_BACK = 4,
       BLACK_PAD_LEFT = 8, BLACK_PAD_RIGHT = 16, BLACK_PAD_HELD = 32,
       BLACK_PAD_UP = 64, BLACK_PAD_DOWN = 128,
       BLACK_PAD_PREV_TAB = 256, BLACK_PAD_NEXT_TAB = 512 };
static inline unsigned black_menu_pad(BlackMenuPad *pad, unsigned down, int open, int *capture)
{
    unsigned pressed = down & ~pad->previous, command = 0;
    pad->previous = down;
    if (pressed & BLACK_PAD_CHORD) command = BLACK_MENU_TOGGLE;
    else if (open) {
        if (pressed & BLACK_PAD_BACK) command = BLACK_MENU_BACK;
        else if (pressed & BLACK_PAD_UP) command = BLACK_MENU_UP;
        else if (pressed & BLACK_PAD_DOWN) command = BLACK_MENU_DOWN;
        else if (pressed & BLACK_PAD_ACCEPT) command = BLACK_MENU_CHANGE;
        else if (pressed & BLACK_PAD_LEFT) command = BLACK_MENU_OFF;
        else if (pressed & BLACK_PAD_RIGHT) command = BLACK_MENU_ON;
        else if (pressed & BLACK_PAD_PREV_TAB) command = BLACK_MENU_PREV_TAB;
        else if (pressed & BLACK_PAD_NEXT_TAB) command = BLACK_MENU_NEXT_TAB;
    }
    /* Opening and closing inputs must not reach the underlying game.
     * Drain until every UI button has been released, even after closing. */
    if (open || command) pad->draining = 1;
    *capture = open || command || pad->draining;
    if (!down && !open) pad->draining = 0;
    return command;
}
#endif
