#include "../../ports/macos/overrides/audio_pacing.h"
#include <stdio.h>
int main(void)
{
    BlackAudioPacer p = {0};
    const int64_t now = 1000000;
    if (black_audio_pace(&p, now, 0, 512, 1024)) return 1;
    /* Empty queue can prefill, but never indefinitely ahead of guest time. */
    int admitted = 0;
    while (!black_audio_pace(&p, now, 0, 512, 1024) && admitted < 20) {
        p.next_us += 5333;
        ++admitted;
    }
    if (admitted < 2 || admitted > 5) return 2;
    p = (BlackAudioPacer){0};
    if (black_audio_pace(&p, now, 1024, 512, 1024) <= 0) return 3;
    if (black_audio_pace(&p, now + 300000, 1024, 512, 1024) || !p.stalled) return 4;
    /* A resumed device gives queue pacing control back immediately. */
    black_audio_pace(&p, now + 301000, 768, 512, 1024);
    if (p.stalled) return 5;
    /* An old schedule is not rendered as an unbounded burst after a stall. */
    p.next_us = now;
    if (black_audio_pace(&p, now + 1000000, -1, 512, 1024)) return 6;
    if (p.next_us != now + 1000000) return 7;
    puts("Audio queue prefill / device stall / recovery: PASS");
    return 0;
}
