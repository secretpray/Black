/* Drive the real guest-event implementation through the lost-wake schedule.
 * The baseline binary must reproduce a timeout; the patched binary must wake.
 * No retail game files or invented GPU completions are involved. */
#include "kernel.h"
#include "xbox_memory_layout.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static unsigned char guest_memory[4096];
void *xbox_GetMemoryBase(void) { return guest_memory; }
void xbox_log(int level, const char *category, const char *fmt, ...)
{ (void)level; (void)category; (void)fmt; }

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static int started, attempting, finished;
static void *event;

void black_sync_test_before_reset(void)
{
    pthread_mutex_lock(&lock);
    started = 1;
    pthread_cond_broadcast(&changed);
#ifdef BLACK_TEST_OLD_RACE
    while (!finished) pthread_cond_wait(&changed, &lock);
#else
    while (!attempting) pthread_cond_wait(&changed, &lock);
#endif
    pthread_mutex_unlock(&lock);
}

#ifdef BLACK_TEST_OLD_RACE
static BOOL reset_after_concurrent_set(HANDLE handle)
{
    black_sync_test_before_reset();
    return ResetEvent(handle);
}
#define ResetEvent reset_after_concurrent_set
#include "../../ports/macos/runtime/src/kernel/kernel_sync.c"
#undef ResetEvent
#else
#define BLACK_SYNC_TEST_HOOKS 1
#include "../../ports/macos/overrides/kernel_sync.c"
#endif

static void *setter(void *unused)
{
    (void)unused;
    pthread_mutex_lock(&lock);
    while (!started) pthread_cond_wait(&changed, &lock);
    attempting = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
    xbox_KeSetEvent(event, 0, FALSE);
    pthread_mutex_lock(&lock);
    finished = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
    return NULL;
}

int main(void)
{
    event = guest_memory + 256;  /* guest notification-event header */
    memset(event, 0, 16);
    ((unsigned char *)event)[2] = 4;
    LARGE_INTEGER timeout;
    timeout.QuadPart = -1000000; /* 100 ms, bounded even for the buggy code */
    pthread_t thread;
    if (pthread_create(&thread, NULL, setter, NULL)) return 2;
    NTSTATUS status = xbox_KeWaitForSingleObject(event, 6, 0, FALSE, &timeout);
    pthread_join(thread, NULL);
#ifdef BLACK_TEST_OLD_RACE
    printf("Baseline race status: %08X (expected timeout)\n", (unsigned)status);
    return status == STATUS_TIMEOUT ? 0 : 1;
#else
    printf("Serialized event status: %08X (expected success)\n", (unsigned)status);
    if (status != STATUS_SUCCESS) return 1;
    /* A notification event remains signalled until the guest resets it. */
    timeout.QuadPart = 0;
    if (xbox_KeWaitForSingleObject(event, 6, 0, FALSE, &timeout) != STATUS_SUCCESS) return 3;
    *(volatile LONG *)((unsigned char *)event + 4) = 0;
    /* Disable the scheduling hook for the ordinary reset/timeout check. */
    if (xbox_KeWaitForSingleObject(event, 6, 0, FALSE, &timeout) != STATUS_TIMEOUT) return 4;
    /* Reinitializing a previously mapped guest event must update the host in
     * both directions, even when the guest wrote the header directly. */
    *(volatile LONG *)((unsigned char *)event + 4) = 1;
    if (xbox_KeWaitForSingleObject(event, 6, 0, FALSE, &timeout) != STATUS_SUCCESS) return 5;
    return 0;
#endif
}
