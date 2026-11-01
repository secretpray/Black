#include "../../ports/macos/overrides/audio_fade.h"
#include <stdio.h>
#include <stdlib.h>

enum { LENGTH = 960 };

/* The largest change between neighbouring frames of one channel. */
static int max_step(const int16_t *s, int frames, int16_t before)
{
    int step = 0, previous = before;
    for (int i = 0; i < frames; ++i) {
        int d = abs(s[i * 2] - previous);
        if (d > step) step = d;
        previous = s[i * 2];
    }
    return step;
}

int main(void)
{
    /* A full-scale constant (DC) signal is the worst case for a cut. */
    int16_t s[LENGTH * 2 + 512];
    for (int i = 0; i < LENGTH + 256; ++i) { s[i * 2] = 32000; s[i * 2 + 1] = -32000; }
    BlackAudioFade f = {LENGTH, 0, {0, 0}};
    black_audio_fade_apply(&f, s, LENGTH + 256);
    if (s[0] != 32000 || s[1] != -32000) return 1;
    for (int i = 1; i < LENGTH; ++i)
        if (s[i * 2] > s[i * 2 - 2] || s[i * 2 + 1] < s[i * 2 - 1]) return 2;
    for (int i = LENGTH; i < LENGTH + 256; ++i) if (s[i * 2] || s[i * 2 + 1]) return 3;
    if (max_step(s, LENGTH + 256, 32000) > 64) return 4;
    if (f.position != LENGTH) return 5;

    /* The game stops halfway: the tail continues from the last frame queued. */
    f = (BlackAudioFade){LENGTH, 0, {0, 0}};
    for (int i = 0; i < LENGTH / 2; ++i) { s[i * 2] = 32000; s[i * 2 + 1] = -32000; }
    black_audio_fade_apply(&f, s, LENGTH / 2);
    f.last[0] = s[LENGTH - 2]; f.last[1] = s[LENGTH - 1];
    int16_t tail[LENGTH * 2];
    int frames = black_audio_fade_tail(&f, tail);
    if (frames != LENGTH / 2 || f.position != LENGTH) return 6;
    if (max_step(tail, frames, f.last[0] ? f.last[0] : s[LENGTH - 2]) > 64) return 7;
    if (abs(tail[frames * 2 - 2]) > 64 || abs(tail[frames * 2 - 1]) > 64) return 8;
    for (int i = 1; i < frames; ++i) if (tail[i * 2] > tail[i * 2 - 2]) return 9;

    /* The game never started fading: the whole ramp comes from the tail. */
    f = (BlackAudioFade){LENGTH, 0, {-20000, 12000}};
    frames = black_audio_fade_tail(&f, tail);
    if (frames != LENGTH || abs(tail[0] + 20000) > 64 || abs(tail[1] - 12000) > 64) return 10;
    if (abs(tail[LENGTH * 2 - 2]) > 64 || black_audio_fade_tail(&f, tail)) return 11;
    puts("audio fade: monotonic ramp, continuous tail, silence after: PASS");
    return 0;
}
