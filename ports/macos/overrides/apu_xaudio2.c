/* Keep the actual SDL audio queue and record only accepted game PCM buffers.
 * Raw s16le stereo 48 kHz stays local, including when SDL uses a dummy device.
 * No test tone is injected. License: same as the included runtime source. */
#define xa2_submit_samples black_SDLSubmitSamples
#include "../runtime/src/apu/apu_xaudio2.c"
#undef xa2_submit_samples
#include "audio_fade.h"
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

/* 20 ms ramp to silence before the device closes on quit (audio_fade.h). */
enum { BLACK_FADE_FRAMES = SDL_AUDIO_RATE / 50 };
static pthread_mutex_t fade_lock = PTHREAD_MUTEX_INITIALIZER;
static BlackAudioFade fade = {BLACK_FADE_FRAMES, 0, {0, 0}};
static atomic_int fading, closed;

static void sleep_ms(unsigned ms)
{
    struct timespec delay = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&delay, NULL);
}

/* Fade the queue to silence, let it play out, then close the device, before
 * the process exits. Bounded to about 300 ms, so a stalled game thread never
 * holds up quitting. */
void black_AudioFadeOut(void)
{
    if (!g_audio_initialized || atomic_load(&closed)) return;
    atomic_store(&fading, 1);
    /* The APU thread fades its next 20 ms; if it is not producing, finish
     * the ramp from the last frame here. */
    for (int waited = 0; waited < 60; waited += 5) {
        pthread_mutex_lock(&fade_lock);
        int done = fade.position >= fade.length;
        pthread_mutex_unlock(&fade_lock);
        if (done) break;
        sleep_ms(5);
    }
    pthread_mutex_lock(&fade_lock);
    int16_t tail[BLACK_FADE_FRAMES * 2];
    int frames = black_audio_fade_tail(&fade, tail);
    if (frames) SDL_QueueAudio(g_audio_device, tail, (Uint32)frames * 4);
    uint32_t queued = SDL_GetQueuedAudioSize(g_audio_device);
    atomic_store(&closed, 1);
    pthread_mutex_unlock(&fade_lock);
    /* Play what is queued (4 bytes a frame) plus the device's own buffer. */
    sleep_ms(SDL_min(200u, queued / (SDL_AUDIO_RATE * 4 / 1000) + 25));
    SDL_PauseAudioDevice(g_audio_device, 1);
    SDL_CloseAudioDevice(g_audio_device);
    fprintf(stderr, "[SDL-AUDIO] faded out and closed for quit (%d tail frames, %u bytes drained)\n", frames, queued);
}

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
    pthread_mutex_lock(&fade_lock);
    if (atomic_load(&closed)) { pthread_mutex_unlock(&fade_lock); return 0; }
    /* While fading, the ramp advances only for buffers SDL accepts. */
    int16_t faded[SDL_AUDIO_BUF_SAMPLES * 8 * 2];
    BlackAudioFade next = fade;
    if (atomic_load(&fading) && samples && frames > 0 && frames <= SDL_AUDIO_BUF_SAMPLES * 8) {
        memcpy(faded, samples, (size_t)frames * 4);
        black_audio_fade_apply(&next, faded, frames);
        samples = faded;
    }
    int accepted = black_SDLSubmitSamples(samples, frames);
    if (accepted && frames > 0) {
        fade = next;
        fade.last[0] = samples[frames * 2 - 2]; fade.last[1] = samples[frames * 2 - 1];
    }
    pthread_mutex_unlock(&fade_lock);
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
