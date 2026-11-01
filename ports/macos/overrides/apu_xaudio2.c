/* Keep the actual SDL audio queue and record only accepted game PCM buffers.
 * Raw s16le stereo 48 kHz stays local, including when SDL uses a dummy device.
 * No test tone is injected. License: same as the included runtime source. */
#define xa2_submit_samples black_SDLSubmitSamples
#include "../runtime/src/apu/apu_xaudio2.c"
#undef xa2_submit_samples

int black_AudioQueuedFrames(void)
{
    return g_audio_initialized ? (int)(SDL_GetQueuedAudioSize(g_audio_device) / 4) : -1;
}

int xa2_submit_samples(const int16_t *samples, int frames)
{
    static uint64_t attempts, rejected, empty;
    static uint32_t last_report;
    int diagnostics = getenv("RECOMP_AUDIO_DIAG") != NULL;
    if (diagnostics && g_audio_initialized) {
        uint32_t queued = SDL_GetQueuedAudioSize(g_audio_device);
        if (attempts > 20 && !queued) ++empty;
    }
    int accepted = black_SDLSubmitSamples(samples, frames);
    if (diagnostics) {
        ++attempts;
        if (!accepted) ++rejected;
        uint32_t now = SDL_GetTicks();
        if (now - last_report >= 2000) {
            last_report = now;
            fprintf(stderr, "[AUDIO-QUEUE] attempts=%llu rejected=%llu empty_observations=%llu queued_bytes=%u\n",
                (unsigned long long)attempts, (unsigned long long)rejected,
                (unsigned long long)empty,
                g_audio_initialized ? SDL_GetQueuedAudioSize(g_audio_device) : 0);
        }
    }
    static FILE *capture;
    static int initialized;
    if (!initialized) {
        const char *path = getenv("RECOMP_AUDIO_DUMP");
        initialized = 1;
        if (path && *path) {
            capture = fopen(path, "wb");
            if (capture)
                setvbuf(capture, NULL, _IONBF, 0);
            else
                perror("[PCM] capture");
        }
    }
    if (accepted && capture && samples && frames > 0) {
        if (fwrite(samples, 2 * sizeof(int16_t), (size_t)frames, capture) != (size_t)frames) {
            perror("[PCM] write");
            fclose(capture);
            capture = NULL;
        }
    }
    return accepted;
}
