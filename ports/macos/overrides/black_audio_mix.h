/* BLACK's bin-to-speaker routing from TGP's src/sound/mixer.cpp, expressed
 * independently of its C++ runtime ABI. Effect sends are not dry speakers.
 * This is a dry stereo downmix; GP filters/reverb still need implementation. */
#ifndef BLACK_AUDIO_MIX_H
#define BLACK_AUDIO_MIX_H
#include <stdint.h>
#include <math.h>
static inline void black_mix_stereo(const float bins[32], float *left, float *right)
{
    enum { FL, FR, C, LFE, BL, BR, NONE = -1 };
    static const int8_t speaker[32] = {
        FL,FR,C,LFE,BL,BR,FL,FR,BL,BR,NONE,NONE,NONE,
        FR,BL,BR,C,NONE,NONE,FL,FR,BL,BR,C,LFE,FL,
        NONE,NONE,NONE,NONE,NONE,NONE
    };
    const uint32_t inverted = (1u<<13)|(1u<<14)|(1u<<15)|(1u<<16)|(1u<<25);
    float out[6] = {0};
    for (unsigned b = 0; b < 32; ++b)
        if (speaker[b] >= 0 && isfinite(bins[b]))
            out[speaker[b]] += (inverted >> b & 1) ? -bins[b] : bins[b];
    *left = out[FL] + 0.70710678f * (out[C] + out[BL]);
    *right = out[FR] + 0.70710678f * (out[C] + out[BR]);
}
#endif
