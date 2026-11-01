#ifndef BLACK_DISPLAY_ASPECT_H
#define BLACK_DISPLAY_ASPECT_H
#include <stdio.h>
#include <stdlib.h>

/* The presenter's RECOMP_ASPECT: "w:h" or a number, 16:9 when unset or out of
 * range. Every guest-side widescreen rule follows this one shape. */
static inline double black_display_aspect(const char *aspect)
{
    double n = 0.0, d = 0.0, v = 0.0;
    if (aspect && sscanf(aspect, "%lf:%lf", &n, &d) == 2 && n > 0.0 && d > 0.0)
        v = n / d;
    else if (aspect)
        v = atof(aspect);
    return v > 0.5 && v < 4.0 ? v : 16.0 / 9.0;
}

static inline int black_display_wide(double aspect)
{
    return aspect > 4.0 / 3.0 + 0.01;
}
#endif
