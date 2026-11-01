#include "../../ports/macos/overrides/game_clock.h"
#include <stdio.h>

int main(void)
{
    for (unsigned rate = 30; rate <= 120; rate *= 2) {
        BlackGameClock state = {0};
        black_frame_delta(&state, 0, 1000000000ull, 1.0f / 30);
        double total = 0;
        for (unsigned frame = 1; frame <= rate; ++frame) {
            float delta = black_frame_delta(&state, frame,
                1000000000ull + (uint64_t)frame * 1000000000ull / rate, 1.0f / 30);
            total += delta;
            /* Another timer in the SAME guest frame uses the same dt. */
            if (delta != black_frame_delta(&state, frame,
                1000000000ull + (uint64_t)frame * 1000000000ull / rate + 500000, 1.0f / 30)) return 2;
        }
        if (fabs(total - 1.0) > 0.00001) return 3;
    }
    BlackGameClock state = {0};
    black_frame_delta(&state, 1, 1000000000, 1.0f / 30);
    float delayed = black_frame_delta(&state, 2, 1200000000, 1.0f / 30);
    if (fabsf(delayed - 1.0f / 30) > 0.000001f) return 4;
    BlackConsolePacer p = {0};
    const uint64_t period = 33333333, start = 1000000000;
    if (black_console_wait(&p, 0, start, period)) return 5;
    uint64_t now = start;
    for (uint32_t frame = 1; frame <= 300; ++frame) {
        now += 21000000; /* rendering, input and game work */
        uint64_t wait = black_console_wait(&p, frame, now, period);
        if (!wait || wait > period) return 6;
        now += wait;
        if (black_console_wait(&p, frame, now, period)) return 7;
    }
    if (now - start != 300 * period) return 8;
    now += 500000000; /* loader stall must not produce catch-up frames */
    if (black_console_wait(&p, 301, now, period)) return 9;
    if (black_console_wait(&p, 302, now + 10000000, period) != period - 10000000) return 10;
    puts("Frame-clock rate independence and pause clamp: PASS");
    return 0;
}
