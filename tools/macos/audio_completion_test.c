/* Asset-free: actual pinned POSIX IRQL lock, production completion wrapper and
 * helper; only the generated guest callee and voice data are synthetic. */
#include "kernel.h"
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#define xbox_log(...) ((void)0)
#include BLACK_IRQL_SOURCE
#include "../../ports/macos/overrides/dispatch_guard.inc"

static _Thread_local uint32_t g_eax, g_ecx, g_edx, g_esp;
static uint32_t guest[2048];
#define MEM32(a) guest[(uint32_t)(a) / 4]
#define OWNER 0x200u
#define HEAD (OWNER + 0x4A0u)
#define VOICE 0x800u
#define STACK 0x100u
static unsigned failures, calls;
static uint32_t black_audio_queue_completion(uint32_t voice);
static void black_OriginalAudioCompletion(void)
{
    ++calls;
    black_audio_queue_completion(g_ecx);
    g_eax = 123;
    g_edx = 456;
    g_esp += 8;
}
#include BLACK_COMPLETION_SOURCE
static void check(int pass, const char *name)
{
    if (!pass) { ++failures; fprintf(stderr, "FAIL %s\n", name); }
}
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static int held, release_owner;
static void *owner(void *unused)
{
    (void)unused;
    KIRQL old = xbox_KeRaiseIrqlToDpcLevel();
    pthread_mutex_lock(&gate);
    held = 1; pthread_cond_broadcast(&cond);
    while (!release_owner) pthread_cond_wait(&cond, &gate);
    pthread_mutex_unlock(&gate);
    xbox_KfLowerIrql(old);
    return NULL;
}
static void init_voice(uint32_t voice)
{
    MEM32(voice + 8) = OWNER;
    MEM32(voice + 0x54) = MEM32(voice + 0x58) = voice + 0x54;
}
static void init_head(void) { MEM32(HEAD) = MEM32(HEAD + 4) = HEAD; }
static void prepare_call(void)
{
    g_esp = STACK; MEM32(STACK + 4) = 7;
    g_eax = 999; g_ecx = VOICE; g_edx = 888;
}
static void *other_try(void *arg)
{
    int acquired = black_IrqlTryEnterDispatch();
    *(int *)arg = acquired;
    if (acquired) black_IrqlLeaveDispatch();
    return NULL;
}
static void observe(int expected)
{
    int acquired = -1; pthread_t t;
    pthread_create(&t, NULL, other_try, &acquired);
    pthread_join(t, NULL);
    check(acquired == expected, "dispatch ownership balanced across threads");
}
int main(void)
{
    pthread_t t;
    init_head(); init_voice(VOICE);
    pthread_create(&t, NULL, owner, NULL);
    pthread_mutex_lock(&gate);
    while (!held) pthread_cond_wait(&cond, &gate);
    pthread_mutex_unlock(&gate);
    /* The pinned ISR does not raise through HAL; simulate its foreign context
     * without taking dispatch, exactly the race guarded by the wrapper. */
    g_current_irql = 5;
    prepare_call(); sub_0021C7EF();
    check(calls == 0, "foreign dispatch owner defers completion without waiting");
    check(g_esp == STACK + 8 && g_eax == 7 && g_ecx == VOICE && g_edx == 888,
        "native busy return preserves stack and untouched registers");
    check(MEM32(HEAD) == HEAD, "deferred ISR leaves voice work untouched");
    check(g_current_irql == 5, "ownership guard preserves caller IRQL");
    pthread_mutex_lock(&gate); release_owner = 1; pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&gate); pthread_join(t, NULL);
    g_current_irql = PASSIVE_LEVEL;
    prepare_call(); unsigned before = calls; sub_0021C7EF();
    check(calls == before + 1 && g_eax == 123 && g_edx == 456 && g_esp == STACK + 8,
        "retry executes real wrapper callee once with callee ABI results");
    observe(1);
    KIRQL old = xbox_KeRaiseIrqlToDpcLevel();
    prepare_call(); sub_0021C7EF();
    check(g_current_irql == DISPATCH_LEVEL, "recursive completion retains outer dispatch");
    observe(0); xbox_KfLowerIrql(old); observe(1);

    init_head();
    uint32_t voices[] = {VOICE, VOICE + 0x100, VOICE + 0x200};
    for (unsigned i = 0; i < 3; ++i) init_voice(voices[i]);
    for (unsigned i = 0; i < 3; ++i)
        check(black_audio_queue_completion(voices[i]) == HEAD, "completion returns native head");
    black_audio_queue_completion(voices[2]);
    black_audio_queue_completion(voices[1]);
    black_audio_queue_completion(voices[0]);
    uint32_t previous = HEAD, node = MEM32(HEAD);
    unsigned drained = 0;
    while (node != HEAD && drained < 4) {
        if (drained < 3) check(node == voices[drained] + 0x54, "stable completion order");
        check(MEM32(node + 4) == previous, "reciprocal previous link");
        previous = node; node = MEM32(node); ++drained;
    }
    check(node == HEAD && drained == 3 && MEM32(HEAD + 4) == previous,
        "duplicate completion neither cycles nor duplicates callbacks");
    if (node == HEAD && drained == 3) {
        for (unsigned i = 0; i < 3; ++i) init_voice(voices[i]);
        init_head(); black_audio_queue_completion(VOICE);
        check(MEM32(HEAD) == VOICE + 0x54 && MEM32(VOICE + 0x54) == HEAD,
            "drained voice can be queued again");
    }
    printf("Audio completion: %u failures\n", failures);
    return failures ? 1 : 0;
}
