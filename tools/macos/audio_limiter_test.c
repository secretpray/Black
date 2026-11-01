#include "../../ports/macos/runtime/src/apu/apu_limiter.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    ApuOutputLimiter limiter = {0};
    float input[256][2] = {{0}};
    int16_t output[256][2];
    /* Quiet signals retain unity gain after the one-block delay. */
    for (int i = 0; i < 256; ++i) { input[i][0] = .25f; input[i][1] = -.125f; }
    apu_limit_pcm(&limiter, input, output);
    for (int i = 0; i < 256; ++i) assert(!output[i][0] && !output[i][1]);
    apu_limit_pcm(&limiter, input, output);
    for (int i = 0; i < 256; ++i) { assert(output[i][0] == 8191); assert(output[i][1] == -4095); }
    /* Cross-block overload stays below full scale with one shared stereo gain. */
    memset(&limiter, 0, sizeof limiter);
    for (int block = 0; block < 20; ++block) {
        for (int i = 0; i < 256; ++i) {
            input[i][0] = (i & 1 ? -1 : 1) * (block & 1 ? 12.f : .1f);
            input[i][1] = input[i][0] * .5f;
        }
        apu_limit_pcm(&limiter, input, output);
        for (int i = 0; i < 256; ++i) {
            assert(abs(output[i][0]) <= 31129);
            assert(abs(output[i][1] * 2 - output[i][0]) <= 1);
        }
    }
    input[0][0] = NAN; input[2][1] = INFINITY;
    apu_limit_pcm(&limiter, input, output);
    memset(input, 0, sizeof input);
    apu_limit_pcm(&limiter, input, output);
    assert(output[0][0] == 0 && output[2][1] == 0);
    puts("Output limiter: latency, unity gain, overload ceiling, linked stereo and nonfinite checks passed");
}
