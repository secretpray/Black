/* Gain ramp adapted from sp00nznet/xboxrecomp v0.16.0 apu_vp.c.
 * Preserve the target level at voice start/reuse, interpolate later changes
 * across the existing 32-sample VP block. No APU struct/ABI changes. */
#ifndef BLACK_VOICE_GAIN_H
#define BLACK_VOICE_GAIN_H
#include <stdint.h>
typedef struct {
    float previous[8];
    uint8_t valid;
} BlackVoiceGain;

static inline float black_voice_gain_begin(BlackVoiceGain *state, unsigned bin, float target)
{
    float start = state->valid & (1u << bin) ? state->previous[bin] : target;
    state->previous[bin] = target;
    state->valid |= 1u << bin;
    return start;
}

static inline float black_voice_gain_sample(float start, float target, unsigned sample, unsigned count)
{
    return start + (target - start) * ((sample + 1.0f) / count);
}
#endif
