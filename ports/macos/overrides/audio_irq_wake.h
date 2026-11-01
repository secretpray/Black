#pragma once
/* Wake the existing kernel worker for a device line, as the Windows donor
 * does for its IRQ worker. APU notifications must not wait for a 10-ms poll. */
static inline void black_audio_irq_wake(HANDLE wake)
{
    if (wake) SetEvent(wake);
}
