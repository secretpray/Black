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
    /* Home: a tap toggles the settings, a hold opens the quit dialog once. */
    {
        BlackHomeButton home = {0};
        if (black_home_button(&home, 1, 1, 1000)) return 20;
        if (black_home_button(&home, 1, 1, 1400)) return 21;
        if (black_home_button(&home, 0, 1, 1450) != BLACK_MENU_TOGGLE) return 22;
        if (black_home_button(&home, 0, 1, 1500)) return 23;
        if (black_home_button(&home, 1, 1, 2000)) return 24;
        if (black_home_button(&home, 1, 1, 2000 + BLACK_HOME_HOLD_MS - 1)) return 25;
        if (black_home_button(&home, 1, 1, 2000 + BLACK_HOME_HOLD_MS) != BLACK_MENU_QUIT) return 26;
        for (unsigned t = 0; t < 5000; t += 100) if (black_home_button(&home, 1, 1, 3600 + t)) return 27;
        if (black_home_button(&home, 0, 1, 9000)) return 28;              /* release after a hold */
        /* Focus lost during a hold cancels it; held through focus return waits for release. */
        black_home_button(&home, 1, 1, 10000);
        if (black_home_button(&home, 1, 0, 10500)) return 29;
        if (black_home_button(&home, 1, 1, 12000)) return 30;
        if (black_home_button(&home, 0, 1, 12100)) return 31;
        /* Out of focus nothing fires, also a hold. */
        if (black_home_button(&home, 1, 0, 20000) || black_home_button(&home, 1, 0, 30000) ||
            black_home_button(&home, 0, 0, 30100)) return 32;
        /* A tap works again after all that. */
        black_home_button(&home, 1, 1, 40000);
        if (black_home_button(&home, 0, 1, 40100) != BLACK_MENU_TOGGLE) return 33;
    }
    puts("Menu chord edges, actions, game input capture and release drain, Home tap and hold: PASS");
    return 0;
}
