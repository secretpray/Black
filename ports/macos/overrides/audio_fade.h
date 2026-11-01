/* Exit fade for the SDL queue: quitting used to end the process mid-buffer,
 * cutting the waveform at whatever amplitude it had, which speakers play as a
 * click. The game's last PCM ramps to silence first, and the device closes
 * only after the ramp has played. */
#ifndef BLACK_AUDIO_FADE_H
#define BLACK_AUDIO_FADE_H
#include <stdint.h>

typedef struct {
    unsigned length, position; /* stereo frames; position stops at length */
    int16_t last[2];           /* the last frame queued, after any gain */
} BlackAudioFade;

static inline int16_t black_audio_fade_scale(int16_t sample, unsigned position, unsigned length)
{
    if (position >= length) return 0;
    return (int16_t)((int32_t)sample * (int32_t)(length - position) / (int32_t)length);
}

/* Fading: ramp interleaved stereo frames in place; silence once complete. */
static inline void black_audio_fade_apply(BlackAudioFade *f, int16_t *samples, int frames)
{
    for (int i = 0; i < frames; ++i) {
        samples[i * 2] = black_audio_fade_scale(samples[i * 2], f->position, f->length);
        samples[i * 2 + 1] = black_audio_fade_scale(samples[i * 2 + 1], f->position, f->length);
        if (f->position < f->length) ++f->position;
    }
}

/* The rest of the ramp when the game has stopped producing PCM: from the last
 * queued frame (already faded) straight down to silence, so it continues
 * without a step. out holds length frames; returns the frames written. */
static inline int black_audio_fade_tail(BlackAudioFade *f, int16_t *out)
{
    int frames = (int)(f->length - f->position);
    for (int k = 0; k < frames; ++k)
        for (int c = 0; c < 2; ++c)
            out[k * 2 + c] = (int16_t)((int32_t)f->last[c] * (frames - k) / (frames + 1));
    f->position = f->length;
    if (frames) f->last[0] = f->last[1] = 0;
    return frames;
}
#endif
