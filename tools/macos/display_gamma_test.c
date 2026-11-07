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

    puts("display gamma: ok");
    return 0;
}
