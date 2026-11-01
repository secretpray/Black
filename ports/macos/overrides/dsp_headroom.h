#ifndef BLACK_DSP_HEADROOM_H
#define BLACK_DSP_HEADROOM_H
#include <math.h>
#include <stdlib.h>
/* Fixed headroom before the 24-bit bus, compensated after DSP. This is an
 * explicit experimental option: nonlinear DSP effects need listening tests. */
static inline float black_dsp_input_gain(void)
{
    static float gain;
    if (!gain) {
        const char *value = getenv("BLACK_DSP_HEADROOM_DB");
        float db = value ? strtof(value, NULL) : 0;
        if (!isfinite(db) || db < 0 || db > 18) db = 0;
        gain = powf(10.0f, -db / 20.0f);
    }
    return gain;
}
#endif
