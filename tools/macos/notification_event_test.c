/* Two registered notification-event waiters must BOTH be released by SetEvent,
 * even if ResetEvent runs before either waiter can reacquire its mutex.
 * The recursive test mutex lets the setter hold one outer lock across the two
 * real APIs; production mutexes and implementation are otherwise unchanged. */
#include "platform/win32_compat.h"
#include <pthread.h>
#include <stdio.h>

static pthread_cond_t *test_condition;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t registered = PTHREAD_COND_INITIALIZER;
static int waiter_count;
static int tracked_wait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                        const struct timespec *deadline);
#define pthread_cond_timedwait tracked_wait
#include BLACK_EVENT_SOURCE
#undef pthread_cond_timedwait

static int tracked_wait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                        const struct timespec *deadline)
{
    if (cond == test_condition) {
        pthread_mutex_lock(&gate);
        ++waiter_count;
        pthread_cond_broadcast(&registered);
        pthread_mutex_unlock(&gate);
    }
    return pthread_cond_timedwait(cond, mutex, deadline);
}

static DWORD results[2];
static void *waiter(void *arg)
{
    results[(size_t)arg] = WaitForSingleObject((HANDLE)((char *)test_condition -
        offsetof(w32_object, cond)), 500);
    return NULL;
}

int main(void)
{
    HANDLE event = CreateEventA(NULL, TRUE, FALSE, NULL);
    w32_object *object = event;
    pthread_mutex_destroy(&object->lock);
    pthread_mutexattr_t attributes;
    pthread_mutexattr_init(&attributes);
    pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&object->lock, &attributes);
    pthread_mutexattr_destroy(&attributes);
    test_condition = &object->cond;
    pthread_t threads[2];
    for (size_t i = 0; i < 2; ++i)
        if (pthread_create(&threads[i], NULL, waiter, (void *)i)) return 2;
    pthread_mutex_lock(&gate);
    while (waiter_count < 2)
        pthread_cond_wait(&registered, &gate);
    pthread_mutex_unlock(&gate);
    pthread_mutex_lock(&object->lock);
    SetEvent(event);
    ResetEvent(event);
    pthread_mutex_unlock(&object->lock);
    for (int i = 0; i < 2; ++i)
        pthread_join(threads[i], NULL);
    printf("Set/reset releases: %u, %u\n", (unsigned)results[0], (unsigned)results[1]);
#ifdef BLACK_TEST_OLD_RACE
    if (results[0] != WAIT_TIMEOUT || results[1] != WAIT_TIMEOUT) return 3;
#else
    if (results[0] != WAIT_OBJECT_0 || results[1] != WAIT_OBJECT_0) return 3;
#endif
    /* The release must not leak to a thread that starts AFTER the reset. */
    if (WaitForSingleObject(event, 0) != WAIT_TIMEOUT) return 4;
    SetEvent(event);
    if (WaitForSingleObject(event, 0) != WAIT_OBJECT_0 ||
        WaitForSingleObject(event, 0) != WAIT_OBJECT_0) return 5;
    ResetEvent(event);
    if (WaitForSingleObject(event, 0) != WAIT_TIMEOUT) return 6;
    CloseHandle(event);
    /* Auto-reset retains its one-consumer behavior. */
    event = CreateEventA(NULL, FALSE, TRUE, NULL);
    if (WaitForSingleObject(event, 0) != WAIT_OBJECT_0 ||
        WaitForSingleObject(event, 0) != WAIT_TIMEOUT) return 7;
    CloseHandle(event);
    return 0;
}
