#include "../../ports/macos/overrides/menu_repeat.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    BlackMenuRepeat s = {0};
    enum { P = BLACK_MENU_REPEAT_PRESS, D = BLACK_MENU_REPEAT_DELAY, E = BLACK_MENU_REPEAT_EVERY };
    /* Outside menus a held direction passes through; in the menu it waits for a release. */
    assert(black_menu_repeat(&s, 0, 1, 0) == 1);
    assert(black_menu_repeat(&s, 1, 1, 10) == 0);
    assert(black_menu_repeat(&s, 1, 0, 20) == 0);
    /* One press at once, a pause, then one press per period. */
    uint32_t t0 = 1000;
    assert(black_menu_repeat(&s, 1, 1, t0) == 1);
    assert(black_menu_repeat(&s, 1, 1, t0 + P - 1) == 1);
    assert(black_menu_repeat(&s, 1, 1, t0 + P) == 0);
    assert(black_menu_repeat(&s, 1, 1, t0 + D - 1) == 0);
    assert(black_menu_repeat(&s, 1, 1, t0 + D) == 1);
    assert(black_menu_repeat(&s, 1, 1, t0 + D + P) == 0);
    assert(black_menu_repeat(&s, 1, 1, t0 + D + E) == 1);
    /* Count presses over two seconds of holding: 1 + repeats. */
    BlackMenuRepeat r = {0}; int presses = 0, was = 0;
    for (uint32_t t = 0; t < 2000; t += 16) { int on = black_menu_repeat(&r, 1, 2, t) & 2; presses += on && !was; was = on; }
    assert(presses == 1 + (2000 - D + E - 1) / E);
    /* Directions are independent; a release restarts the timing. */
    assert(black_menu_repeat(&s, 1, 3, t0 + D + E + 1) == 3);
    assert(black_menu_repeat(&s, 1, 0, 9000) == 0);
    assert(black_menu_repeat(&s, 1, 4, 9001) == 4);
    puts("Menu d-pad repeat: PASS");
    return 0;
}
