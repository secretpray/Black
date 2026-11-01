#ifndef BLACK_AUDIO_PROBE_H
#define BLACK_AUDIO_PROBE_H
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
/* Captures the actual floating-point mix BEFORE clipping/conversion. */
static inline void black_audio_probe(const float samples[][2], unsigned frames)
{
    static int configured, diagnostics;
    static FILE *file;
    static uint64_t count, clipped, nonfinite;
    static float peak;
    if (!configured) {
        const char *path = getenv("BLACK_AUDIO_FLOAT_DUMP");
        if (path) { file = fopen(path, "wb"); if (file) setvbuf(file, NULL, _IOFBF, 262144); }
        diagnostics = getenv("RECOMP_AUDIO_DIAG") != NULL;
        configured = 1;
    }
    if (file) fwrite(samples, sizeof(float) * 2, frames, file);
    if (!diagnostics && !file) return;
    for (unsigned i = 0; i < frames; ++i) for (unsigned ch = 0; ch < 2; ++ch) {
        float s = samples[i][ch];
        if (!isfinite(s)) ++nonfinite;
        else { if (fabsf(s) > peak) peak = fabsf(s); if (fabsf(s) >= 1) ++clipped; }
    }
    count += frames;
    if (count >= 48000) {
        fprintf(stderr, "[AUDIO-MIX] frames=%llu peak=%.6f over_full_scale=%llu nonfinite=%llu\n",
            (unsigned long long)count, peak, (unsigned long long)clipped, (unsigned long long)nonfinite);
        if (file) fflush(file);
        count = clipped = nonfinite = 0; peak = 0;
    }
}
#endif
