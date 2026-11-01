#include "../../ports/macos/overrides/controller_axes.h"
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    for (int v = -32768; v <= 32767; ++v)
        if (black_invert_axis(v, 0) != v || black_invert_axis(v, 1) != -1 - v ||
            black_invert_axis(black_invert_axis(v, 1), 1) != v) return 90;

    int16_t x, y;
    black_title_stick(0, 0, 3200, &x, &y);
    if (x || y) return 1;
    black_title_stick(500, -1000, 3200, &x, &y);
    if (x || y) return 2;
    black_title_stick(16384, 0, 3200, &x, &y);
    if (x <= 0 || y) return 3;
    black_title_stick(0, 16384, 3200, &x, &y);
    if (x || y >= 0) return 4;
    black_title_stick(0, -32768, 3200, &x, &y);
    if (x || y != 32767) return 5;
    black_title_stick(-32768, 0, 3200, &x, &y);
    if (x != -32767 || y) return 6;
    black_title_stick(32767, -32768, 3200, &x, &y);
    if (x != 32767 || y != 32767) return 7;
    /* Deflection at boot cannot poison the later neutral state. */
    for (int i = 0; i < 100; ++i)
        black_title_stick(20000, -18000, 3200, &x, &y);
    black_title_stick(0, 0, 3200, &x, &y);
    if (x || y) return 8;
    black_title_stick(0, 0, 0, &x, &y);
    if (x || y) return 9;
    /* Test the complete host + title response, not just a host deadzone.
     * It must begin at the requested threshold and preserve the game's
     * existing 0.9 saturation (pad+0xC8), for cardinal/diagonal travel. */
    for (int dz = 1600; dz <= 12000; dz += 1600) {
        double previous = 0;
        for (int raw = 0; raw <= 32767; ++raw) {
            int out = black_title_axis(raw, dz);
            double response = fmax(0, (fmin(out / 32767.0, 0.9) - 0.2) / 0.7);
            double expected = raw <= dz ? 0 : fmin(1, (raw - dz) / (32767.0 - dz) * 0.8 / 0.7);
            if (fabs(response - expected) > 0.00004 || response < previous) return 10;
            if (black_title_axis(-raw, dz) != -out) return 11;
            previous = response;
        }
    }
    for (int raw = -32768; raw <= 32767; ++raw)
        if (black_title_axis(raw, 0) != raw) return 12;
    black_title_stick(32767, -32768, 3200, &x, &y);
    if (x != 32767 || y != 32767) return 13;
    black_title_stick(385, 128, 3200, &x, &y);
    if (x || y) return 14;
    const int thresholds[] = {-1, 0, 2621, 6553, 16384};
    for (unsigned d = 0; d < sizeof thresholds / sizeof thresholds[0]; ++d) {
        int threshold = thresholds[d];
        double previous = -1;
        for (int raw = -32768; raw <= 32767; ++raw) {
            int16_t mapped = black_configured_axis(raw, threshold);
            double actual = black_game_axis(mapped), magnitude = fmin(abs(raw), 32767);
            double expected;
            if (threshold < 0) expected = fmax(0, (fmin(magnitude / 32767.0, .9) - .2) / .7);
            else expected = magnitude <= threshold ? 0 :
                fmin(1, (magnitude - threshold) / (32767 - threshold) * .8 / .7);
            if (raw < 0) expected = -expected;
            if (fabs(actual - expected) > .00005 || actual < previous) return 15;
            if (threshold < 0 && mapped != raw) return 16;
            previous = actual;
        }
    }
    for (int dz = 0; dz <= 50; ++dz) {
        int previous = 0, threshold = (int)lround(32767.0 * dz / 100.0);
        for (int raw = -32768; raw <= 32767; ++raw) {
            int result = black_trigger(raw, dz);
            if (result < previous || (raw <= threshold && result) || (raw == 32767 && result != 255)) return 17;
            if (!dz && result != (raw < 0 ? 0 : raw >> 7)) return 18;
            previous = result;
        }
    }
    puts("Centered/full-range SDL axes, configurable zones and trigger response: PASS");
    return 0;
}
