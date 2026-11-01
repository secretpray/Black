/* Deadline sleep on the guest thread, with interactive QoS to avoid the
 * default timer coalescing window. No spin loop or real-time priority. */
#include "frame_wait.h"
#include <mach/mach_time.h>
#include <mach/mach.h>
#include <pthread/qos.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
void black_frame_wait(uint64_t ns, int legacy)
{
    if (legacy) {
        struct timespec delay = {(time_t)(ns / 1000000000ull), (long)(ns % 1000000000ull)};
        while (nanosleep(&delay, &delay) && errno == EINTR) {}
        return;
    }
    static _Thread_local mach_timebase_info_data_t timebase;
    if (!timebase.denom) {
        mach_timebase_info(&timebase);
        int result = pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
        fprintf(stderr, "[PACING] guest deadline wait, interactive QoS result=%d\n", result);
    }
    uint64_t deadline = mach_absolute_time() + ns * timebase.denom / timebase.numer;
    while (mach_wait_until(deadline) == KERN_ABORTED) {}
}
