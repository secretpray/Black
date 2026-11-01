/* Keep the pinned bridge and measure the host timer's sleep/work separately.
 * License: same as the included runtime source. */
#include "../runtime/src/kernel/kernel.h"
#include <time.h>
#include "video_flags.h"
#include "interrupt_gate.h"
static int kernel_raise_interrupt(uint32_t vector);
static void bridge_KeInitializeInterrupt(void);
static void bridge_KeSynchronizeExecution(void);
static DWORD black_TimerWait(HANDLE handle, DWORD milliseconds);
static NTSTATUS black_QueryNonVolatileSetting(ULONG index, PULONG type, PVOID value,
    ULONG length, PULONG result);
#define WaitForSingleObject black_TimerWait
#define xbox_ExQueryNonVolatileSetting black_QueryNonVolatileSetting
#include BLACK_KERNEL_BRIDGE_SOURCE
#undef xbox_ExQueryNonVolatileSetting
#undef WaitForSingleObject
#include "interrupt_bridge.inc"
#include "audio_irq_wake.h"

/* The title reads the video flags through this bridge; give it the XDK
 * widescreen bit for the presenter's shape (see video_flags.h). */
static NTSTATUS black_QueryNonVolatileSetting(ULONG index, PULONG type, PVOID value,
    ULONG length, PULONG result)
{
    NTSTATUS st = xbox_ExQueryNonVolatileSetting(index, type, value, length, result);
    if (index == XC_VIDEO && value && length >= sizeof(ULONG) && st >= 0) {
        *(PULONG)value = black_video_flags(getenv("RECOMP_ASPECT"));
        static int logged;
        if (!logged++)
            fprintf(stderr, "[VIDEO] XC_VIDEO 0x%08X (%s)\n", (unsigned)*(PULONG)value,
                *(PULONG)value ? "widescreen" : "4:3");
    }
    return st;
}

void black_KernelWakeDeviceIRQ(void)
{
    black_audio_irq_wake(g_timer_wake);
}

static uint64_t black_timer_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + now.tv_nsec;
}

static DWORD black_TimerWait(HANDLE handle, DWORD milliseconds)
{
    if (handle != g_timer_wake || !getenv("BLACK_SYNC_LOG"))
        return WaitForSingleObject(handle, milliseconds);
    static _Thread_local uint64_t previous_end;
    uint64_t start = black_timer_ns();
    if (previous_end && start - previous_end >= 40000000ull)
        fprintf(stderr, "[TIMER] work=%.2fms vblank=%u\n",
            (start - previous_end) / 1000000.0, xbox_VblankCount());
    DWORD result = WaitForSingleObject(handle, milliseconds);
    uint64_t end = black_timer_ns();
    if (end - start >= 40000000ull)
        fprintf(stderr, "[TIMER] wait=%.2fms requested=%u result=%u vblank=%u\n",
            (end - start) / 1000000.0, (unsigned)milliseconds,
            (unsigned)result, xbox_VblankCount());
    previous_end = end;
    return result;
}
