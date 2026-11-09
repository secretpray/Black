#undef NDEBUG
#include "../../ports/macos/overrides/window_geometry.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static int near(double a, double b) { return fabs(a - b) < 0.01; }
int main(void)
{
    BlackRect laptop = {0, 0, 1512, 944}, external = {1512, -200, 2560, 1415}, out;
    BlackRect both[2] = {laptop, external};
    const double aspect = 16.0 / 9.0, title = 28;
    /* A window that fits comes back as it was. */
    assert(black_window_fit((BlackRect){100, 100, 960, 540}, aspect, 240, title, both, 2, &out));
    assert(near(out.x, 100) && near(out.y, 100) && near(out.w, 960) && near(out.h, 540));
    /* On the external monitor it stays there. */
    assert(black_window_fit((BlackRect){2000, 100, 1280, 720}, aspect, 240, title, both, 2, &out) && near(out.x, 2000));
    /* That monitor gone: no screen overlaps, the window is centred instead. */
    assert(!black_window_fit((BlackRect){2000, 100, 1280, 720}, aspect, 240, title, &laptop, 1, &out));
    /* Too big for the laptop: shrunk to fit with its title bar, aspect kept. */
    assert(black_window_fit((BlackRect){0, 0, 2400, 1350}, aspect, 240, title, &laptop, 1, &out));
    assert(out.h <= 944 - title + 0.01 && out.w <= 1512 + 0.01 && near(out.w / out.h, aspect));
    assert(out.y + out.h + title <= 944 + 0.01 && out.x >= 0 && out.y >= 0);
    /* Half off the right edge: moved back in, size kept. */
    assert(black_window_fit((BlackRect){1200, 100, 800, 450}, aspect, 240, title, &laptop, 1, &out));
    assert(near(out.x, 1512 - 800) && near(out.w, 800));
    /* A different aspect since (4:3 then): the height is kept, the width follows. */
    assert(black_window_fit((BlackRect){100, 100, 720, 540}, aspect, 240, title, &laptop, 1, &out) && near(out.w, 960));
    /* Nonsense is refused. */
    assert(!black_window_fit((BlackRect){0, 0, 0, 0}, aspect, 240, title, &laptop, 1, &out));
    puts("Window geometry: kept, moved back in, shrunk, monitor gone: PASS");
    return 0;
}
