#include "../../ports/macos/overrides/black_audio_mix.h"
#include <stdio.h>
#include <string.h>
int main(void)
{
    float bins[32] = {0}, left, right;
    bins[19] = 0.5f; /* BLACK front left, wrongly sent right by parity routing */
    black_mix_stereo(bins, &left, &right);
    if (left != 0.5f || right != 0) return 1;
    memset(bins, 0, sizeof bins);
    bins[20] = 0.5f;
    black_mix_stereo(bins, &left, &right);
    if (left != 0 || right != 0.5f) return 2;
    bins[11] = 0.9f; /* effect send must not be added again as dry output */
    black_mix_stereo(bins, &left, &right);
    if (left != 0 || right != 0.5f) return 3;
    memset(bins, 0, sizeof bins);
    bins[13] = 0.5f; /* inverted front right */
    black_mix_stereo(bins, &left, &right);
    if (left != 0 || right != -0.5f) return 4;
    memset(bins, 0, sizeof bins);
    bins[2] = 0.5f;
    black_mix_stereo(bins, &left, &right);
    if (fabsf(left - 0.35355339f) > 0.000001f || left != right) return 5;
    bins[0] = NAN; /* one invalid voice/bin must not poison all output */
    black_mix_stereo(bins, &left, &right);
    if (!isfinite(left) || !isfinite(right)) return 6;
    puts("BLACK speaker routing / effect-send exclusion: PASS");
    return 0;
}
