/* Queue/clock pacing adapted from sp00nznet/xboxrecomp f0f8a461,
 * src/apu/apu_core.c, mcpx_apu_pace_step. No runtime-structure ABI dependency. */
#ifndef BLACK_AUDIO_PACING_H
#define BLACK_AUDIO_PACING_H
#include <stdint.h>
typedef struct {
    int64_t next_us, drain_us;
    int previous_queue, stalled;
} BlackAudioPacer;

static inline int64_t black_audio_pace(BlackAudioPacer *p, int64_t now,
    int queued, int low, int high)
{
    const int64_t period = 5333; /* 256 stereo frames at 48 kHz */
    int64_t ahead = 0;
    int mid = 0;
    if (queued >= 0) {
        if (queued < high || queued < p->previous_queue || !p->drain_us) {
            p->drain_us = now;
            p->stalled = 0;
        }
        p->previous_queue = queued;
        if (!p->stalled && queued >= high && now - p->drain_us >= 250000)
            p->stalled = 1;
        if (!p->stalled) {
            if (queued >= high) return period;
            if (queued <= low) ahead = (int64_t)high * period / 256;
            else mid = 1;
        }
    } else p->stalled = 0;
    if (!p->next_us || now - p->next_us > period) p->next_us = now;
    if (mid && p->next_us - now > period) p->next_us = now + period;
    return p->next_us - now > ahead ? p->next_us - now - ahead : 0;
}
#endif
