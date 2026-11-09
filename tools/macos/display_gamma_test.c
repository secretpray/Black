#include "../../ports/macos/overrides/display_gamma.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* BLACK's ramp at 0x1762B0: 255 * 2.2 * (i / 255)^1.2 * (1 + brightness) * fade,
 * clamped to 255, the same value for red, green and blue at tint 255. */
static void game_ramp(uint8_t ramp[BLACK_GAMMA_ENTRIES * 3], double fade)
{
    for (unsigned i = 0; i < BLACK_GAMMA_ENTRIES; ++i) {
        double v = pow(i / 255.0, 1.2) * 2.2 * 255.0 * fade;
        uint8_t c = v > 255.0 ? 255 : (uint8_t)v;
        ramp[i] = ramp[BLACK_GAMMA_ENTRIES + i] = ramp[BLACK_GAMMA_ENTRIES * 2 + i] = c;
    }
}

int main(void)
{
    uint8_t table[BLACK_GAMMA_ENTRIES * 3], ramp[BLACK_GAMMA_ENTRIES * 3];

    /* Before the game writes one, the table maps every entry to itself. */
    black_GammaReset();
    if (black_GammaSnapshot(table) != 0 || !black_GammaIsIdentity(table)) return 1;

    /* D3DGAMMARAMP is planar; the table is interleaved R,G,B. */
    for (unsigned i = 0; i < BLACK_GAMMA_ENTRIES; ++i) {
        ramp[i] = (uint8_t)i;
        ramp[BLACK_GAMMA_ENTRIES + i] = (uint8_t)(255 - i);
        ramp[BLACK_GAMMA_ENTRIES * 2 + i] = (uint8_t)(i / 2);
    }
    black_GammaSetRamp(ramp);
    uint32_t version = black_GammaSnapshot(table);
    if (version == 0 || black_GammaIsIdentity(table)) return 2;
    for (unsigned i = 0; i < BLACK_GAMMA_ENTRIES; ++i)
        if (table[i * 3] != i || table[i * 3 + 1] != 255 - i || table[i * 3 + 2] != i / 2) return 3;

    /* The same ramp again (the game re-queues it) keeps the version, so the
     * presenter does not upload it again. */
    black_GammaSetRamp(ramp);
    if (black_GammaSnapshot(table) != version) return 4;

    /* Every step of a fade is a new table, and the last one stays. */
    uint32_t last = version;
    for (int step = 0; step <= 30; ++step) {
        game_ramp(ramp, step / 30.0);
        black_GammaSetRamp(ramp);
        uint32_t now = black_GammaSnapshot(table);
        if (now == last) return 5;
        last = now;
    }
    if (table[0] != 0 || table[64 * 3] != 106 || table[128 * 3] != 245 || table[192 * 3] != 255 ||
        table[255 * 3 + 2] != 255) return 6;

    /* A fade to black is a ramp of zeros, not identity. */
    game_ramp(ramp, 0.0);
    black_GammaSetRamp(ramp);
    black_GammaSnapshot(table);
    for (unsigned i = 0; i < BLACK_GAMMA_ENTRIES * 3; ++i)
        if (table[i]) return 7;

    /* An identity ramp from the game reads as identity. */
    for (unsigned i = 0; i < BLACK_GAMMA_ENTRIES; ++i)
        ramp[i] = ramp[BLACK_GAMMA_ENTRIES + i] = ramp[BLACK_GAMMA_ENTRIES * 2 + i] = (uint8_t)i;
    black_GammaSetRamp(ramp);
    black_GammaSnapshot(table);
    if (!black_GammaIsIdentity(table)) return 8;

    /* The player's curve over the game's ramp (display_gamma.h). */
    uint16_t out[BLACK_GAMMA_ENTRIES * 3];
    game_ramp(ramp, 1.0);
    black_GammaSetRamp(ramp);
    black_GammaSnapshot(table);

    /* Neutral is the game's table exactly, at 16 bits. */
    if (!black_GammaUserNeutral(BLACK_BRIGHTNESS_NEUTRAL, BLACK_GAMMA_NEUTRAL) || black_GammaUserNeutral(1, 100) ||
        black_GammaUserNeutral(0, 105)) return 9;
    black_GammaCompose(table, BLACK_BRIGHTNESS_NEUTRAL, BLACK_GAMMA_NEUTRAL, out);
    for (unsigned i = 0; i < BLACK_GAMMA_ENTRIES * 3; ++i)
        if (out[i] != table[i] * 257u) return 10;

    /* Every setting keeps the curve monotonic and white at white; only
     * positive brightness lifts black, and nothing clips below white. */
    for (int b = BLACK_BRIGHTNESS_MIN; b <= BLACK_BRIGHTNESS_MAX; ++b)
        for (int g = BLACK_GAMMA_MIN; g <= BLACK_GAMMA_MAX; g += 5) {
            double previous = -1;
            for (unsigned i = 0; i < BLACK_GAMMA_ENTRIES; ++i) {
                double y = black_GammaUserCurve(i / 255.0, b, g);
                if (y < previous || y < 0 || y > 1) return 11;
                previous = y;
            }
            if (fabs(black_GammaUserCurve(1.0, b, g) - 1.0) > 1e-12) return 12;
            double black = black_GammaUserCurve(0.0, b, g);
            if (b > 0 ? fabs(black - b / 100.0) > 1e-12 : black != 0.0) return 13;
            if (black_GammaUserCurve(254 / 255.0, b, g) >= 1.0) return 14;
        }

    /* Gamma above neutral brightens the middle grey, below darkens it; the
     * ends stay. Brightness moves it the same way. */
    double mid = black_GammaUserCurve(0.5, 0, 100);
    if (fabs(mid - 0.5) > 1e-12) return 15;
    if (black_GammaUserCurve(0.5, 0, 130) <= mid || black_GammaUserCurve(0.5, 0, 70) >= mid) return 16;
    if (black_GammaUserCurve(0.5, 10, 100) <= mid || black_GammaUserCurve(0.5, -10, 100) >= mid) return 17;

    /* Applied after the game's table: a fade to black stays black unless the
     * player lifted black, and the composed table follows the curve. */
    black_GammaCompose(table, 0, 130, out);
    if (out[0] != 0 || out[255 * 3] != 65535) return 18;
    for (unsigned i = 0; i < BLACK_GAMMA_ENTRIES * 3; ++i)
        if (out[i] != (uint16_t)lround(black_GammaUserCurve(table[i] / 255.0, 0, 130) * 65535.0)) return 19;
    game_ramp(ramp, 0.0);
    black_GammaSetRamp(ramp);
    black_GammaSnapshot(table);
    black_GammaCompose(table, -10, 80, out);
    for (unsigned i = 0; i < BLACK_GAMMA_ENTRIES * 3; ++i)
        if (out[i]) return 20;
    black_GammaCompose(table, 10, 100, out);
    if (out[0] != (uint16_t)lround(0.1 * 65535.0)) return 21;

    /* Out-of-range values are clamped, not extrapolated. */
    if (black_GammaUserCurve(0.5, 99, 999) != black_GammaUserCurve(0.5, BLACK_BRIGHTNESS_MAX, BLACK_GAMMA_MAX)) return 22;

    puts("display gamma: ok");
    return 0;
}
