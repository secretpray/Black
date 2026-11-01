/* Keep the pinned implementation and serialize notification-event state
 * reconciliation with KeSetEvent. Otherwise the following order loses a wake:
 * wait reads guest state=0; setter signals host and publishes guest state=1;
 * waiter resets host, then sleeps forever. See tests/sync_event_test.c.
 * License: same as the included runtime source. */
#include <stdlib.h>
#include <time.h>
#define xbox_KeSetEvent black_OriginalKeSetEvent
#define xbox_KeWaitForSingleObject black_OriginalKeWaitForSingleObject
#include "../runtime/src/kernel/kernel_sync.c"
#undef xbox_KeSetEvent
#undef xbox_KeWaitForSingleObject

static int black_notification_event(PVOID object)
{
    if (!object || !is_guest_pointer(object))
        return 0;
    unsigned char type = DISP_TYPE(object);
    return type == DISP_TYPE_NOTIFICATION_EVENT || type == DISP_TYPE_NOTIFICATION_TIMER;
}

LONG __stdcall xbox_KeSetEvent(PVOID event, LONG increment, BOOLEAN wait)
{
    if (!black_notification_event(event))
        return black_OriginalKeSetEvent(event, increment, wait);
    HANDLE host = xbox_dispatcher_handle(event);
    if (!host)
        return 0;
    EnterCriticalSection(&s_guest_disp_cs);
    LONG previous = DISP_SIGNAL_STATE(event);
    xbox_dispatcher_publish(event, 1);
    SetEvent(host);
    LeaveCriticalSection(&s_guest_disp_cs);
    return previous;
}

NTSTATUS __stdcall xbox_KeWaitForSingleObject(PVOID object, ULONG reason,
    KPROCESSOR_MODE mode, BOOLEAN alertable, PLARGE_INTEGER timeout)
{
    if (!black_notification_event(object))
        return black_OriginalKeWaitForSingleObject(object, reason, mode, alertable, timeout);
    HANDLE host = xbox_dispatcher_handle(object);
    if (!host)
        return STATUS_INVALID_HANDLE;
    EnterCriticalSection(&s_guest_disp_cs);
    if (DISP_SIGNAL_STATE(object) == 0) {
#ifdef BLACK_SYNC_TEST_HOOKS
        extern void black_sync_test_before_reset(void);
        black_sync_test_before_reset();
#endif
        ResetEvent(host);
    } else {
        /* A title may initialize/reinitialize the notification header itself.
         * Reconcile both directions; a signalled guest event must wake. */
        SetEvent(host);
    }
    LeaveCriticalSection(&s_guest_disp_cs);
    DWORD ms = xbox_nt_timeout_to_ms(timeout);
    DWORD result;
    struct timespec wait_start = {0};
    int timing = getenv("BLACK_SYNC_LOG") != NULL;
    if (timing)
        clock_gettime(CLOCK_MONOTONIC, &wait_start);
    if (ms == INFINITE && getenv("BLACK_SYNC_LOG")) {
        unsigned elapsed = 0;
        do {
            result = WaitForSingleObjectEx(host, 1000, alertable);
            if (result == WAIT_TIMEOUT) {
                ++elapsed;
                if (elapsed <= 3 || elapsed % 10 == 0)
                    fprintf(stderr, "[SYNC] guest event %08X state=%ld wait=%us\n",
                        (unsigned)((uintptr_t)object - (uintptr_t)xbox_GetMemoryBase()),
                        (long)DISP_SIGNAL_STATE(object), elapsed);
            }
        } while (result == WAIT_TIMEOUT);
    } else {
        result = WaitForSingleObjectEx(host, ms, alertable);
    }
    if (timing) {
        struct timespec end;
        clock_gettime(CLOCK_MONOTONIC, &end);
        double elapsed_ms = (end.tv_sec - wait_start.tv_sec) * 1000.0 +
            (end.tv_nsec - wait_start.tv_nsec) / 1000000.0;
        if (elapsed_ms >= 40.0)
            fprintf(stderr, "[WAIT] event=%08X elapsed=%.2fms timeout=%u result=%u state=%ld\n",
                (unsigned)((uintptr_t)object - (uintptr_t)xbox_GetMemoryBase()),
                elapsed_ms, (unsigned)ms, (unsigned)result, (long)DISP_SIGNAL_STATE(object));
    }
    return xbox_wait_result_to_ntstatus(result, 1);
}
