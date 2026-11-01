#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>
static inline int black_audio_stages_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) enabled = getenv("BLACK_AUDIO_STAGES") != NULL;
    return enabled;
}
typedef struct { uint64_t samples, saturated; float peak; unsigned voice; } BlackStageStats;
static BlackStageStats black_stage_stats[2];
static inline void black_voice_stage(unsigned stage, unsigned voice, float sample)
{
    if (!black_audio_stages_enabled()) return;
    BlackStageStats *s = &black_stage_stats[stage];
    ++s->samples;
    if (fabsf(sample) >= 1) ++s->saturated;
    if (fabsf(sample) > s->peak) { s->peak = fabsf(sample); s->voice = voice; }
}
static inline void black_voice_stage_report(void)
{
    if (!black_audio_stages_enabled()) return;
    static time_t previous;
    time_t now = time(NULL);
    if (now == previous) return;
    previous = now;
    for (unsigned i = 0; i < 2; ++i) {
        BlackStageStats *s = &black_stage_stats[i];
        fprintf(stderr, "[AUDIO-STAGE] stage=%s samples=%llu saturated=%llu peak=%.6f voice=%u\n",
            i ? "filter-before-clamp" : "decoded-resampled", (unsigned long long)s->samples,
            (unsigned long long)s->saturated, s->peak, s->voice);
        *s = (BlackStageStats){0};
    }
}
static inline void black_bins_probe(float bins[32][32], float input_gain)
{
    if (!black_audio_stages_enabled()) return;
    static uint64_t saturated[32], samples, bus_clipped;
    static float peak[32];
    static time_t previous;
    for (unsigned b = 0; b < 32; ++b) for (unsigned i = 0; i < 32; ++i) {
        float value = fabsf(bins[b][i]);
        if (value >= 1) ++saturated[b];
        if (value * input_gain >= 1) ++bus_clipped;
        if (value > peak[b]) peak[b] = value;
    }
    samples += 32;
    time_t now = time(NULL);
    if (now == previous) return;
    previous = now;
    fprintf(stderr, "[AUDIO-BINS] frames=%llu dsp_gain=%.6f bus_clipped=%llu", (unsigned long long)samples, input_gain, (unsigned long long)bus_clipped);
    for (unsigned b = 0; b < 32; ++b) if (peak[b] > 0)
        fprintf(stderr, " b%u=%.4f/%llu", b, peak[b], (unsigned long long)saturated[b]);
    fputc('\n', stderr);
    memset(saturated, 0, sizeof saturated); memset(peak, 0, sizeof peak); samples = bus_clipped = 0;
}
