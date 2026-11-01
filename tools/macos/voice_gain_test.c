#include "../../ports/macos/overrides/voice_gain.h"
#include <math.h>
#include <stdio.h>

int main(void)
{
    BlackVoiceGain state = {0};
    if (black_voice_gain_begin(&state, 0, 0.25f) != 0.25f) return 1;
    float start = black_voice_gain_begin(&state, 0, 0.75f);
    if (start != 0.25f) return 2;
    float previous = start;
    for (unsigned i = 0; i < 32; ++i) {
        float value = black_voice_gain_sample(start, 0.75f, i, 32);
        if (value <= previous || fabsf(value - previous - 0.5f / 32) > 0.000001f) return 3;
        previous = value;
    }
    if (previous != 0.75f) return 4;
    start = black_voice_gain_begin(&state, 0, 0);
    if (start != 0.75f || black_voice_gain_sample(start, 0, 31, 32) != 0) return 5;
    if (black_voice_gain_begin(&state, 7, 0.5f) != 0.5f) return 6;
    state.valid = 0; /* VOICE_ON resets history when a voice slot is reused */
    if (black_voice_gain_begin(&state, 0, 0.1f) != 0.1f) return 7;
    puts("Voice gain continuity / exact target / slot reuse: PASS");
    return 0;
}
