#include "../../ports/macos/overrides/menu_input.h"
#include <stdio.h>
int main(void)
{
    BlackMenuPad pad = {0}; int capture;
    if (black_menu_pad(&pad, BLACK_PAD_CHORD, 0, &capture) != BLACK_MENU_TOGGLE || !capture) return 1;
    if (black_menu_pad(&pad, BLACK_PAD_CHORD, 1, &capture) || !capture) return 2;
    black_menu_pad(&pad, 0, 1, &capture);
    if (black_menu_pad(&pad, BLACK_PAD_ACCEPT, 1, &capture) != BLACK_MENU_CHANGE || !capture) return 3;
    if (black_menu_pad(&pad, BLACK_PAD_ACCEPT, 1, &capture)) return 4;
    black_menu_pad(&pad, 0, 1, &capture);
    if (black_menu_pad(&pad, BLACK_PAD_BACK, 1, &capture) != BLACK_MENU_BACK || !capture) return 5;
    if (black_menu_pad(&pad, BLACK_PAD_HELD, 0, &capture) || !capture) return 6;
    black_menu_pad(&pad, 0, 0, &capture);
    black_menu_pad(&pad, 0, 0, &capture);
    if (capture) return 7;
    if (black_menu_pad(&pad, BLACK_PAD_ACCEPT, 0, &capture) || capture) return 8;
    black_menu_pad(&pad, 0, 1, &capture);
    if (black_menu_pad(&pad, BLACK_PAD_LEFT, 1, &capture) != BLACK_MENU_OFF) return 9;
    if (black_menu_pad(&pad, BLACK_PAD_RIGHT, 1, &capture) != BLACK_MENU_ON) return 10;
    if (black_menu_pad(&pad, BLACK_PAD_UP, 1, &capture) != BLACK_MENU_UP) return 11;
    if (black_menu_pad(&pad, BLACK_PAD_DOWN, 1, &capture) != BLACK_MENU_DOWN) return 12;
    if (black_menu_pad(&pad, BLACK_PAD_PREV_TAB, 1, &capture) != BLACK_MENU_PREV_TAB) return 13;
    if (black_menu_pad(&pad, BLACK_PAD_NEXT_TAB, 1, &capture) != BLACK_MENU_NEXT_TAB) return 14;
    if (black_menu_pad(&pad, BLACK_PAD_NEXT_TAB, 0, &capture)) return 15;
    puts("Menu chord edges, actions, game input capture and release drain: PASS");
    return 0;
}
