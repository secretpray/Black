/* Included after the pinned APU core, whose old throttle was renamed only
 * in the build tree. Audio-device stalls still permit guest notifications. */
#include "audio_pacing.h"
extern int black_AudioQueuedFrames(void);

static void throttle(MCPXAPUState *d)
{
    if (getenv("BLACK_LEGACY_AUDIO_PACING") || black_AudioQueuedFrames() < 0) {
        black_OriginalThrottle(d);
        return;
    }
    if (d->ep_frame_div % 8) return;
    static BlackAudioPacer pacer;
    int64_t start = qemu_clock_get_us(QEMU_CLOCK_REALTIME);
    while (!d->pause_requested) {
        int64_t wait = black_audio_pace(&pacer,
            qemu_clock_get_us(QEMU_CLOCK_REALTIME), black_AudioQueuedFrames(), 512, 1024);
        if (wait <= 0) break;
        qemu_cond_timedwait(&d->cond, &d->lock, (int)((wait + 999) / 1000));
    }
    pacer.next_us += EP_FRAME_US;
    d->sleep_acc_us += (int)(qemu_clock_get_us(QEMU_CLOCK_REALTIME) - start);
}
