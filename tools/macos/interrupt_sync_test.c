/* Production HAL IRQL/dispatch, bridge callback bodies, scoped wrappers and
 * completion helpers. Guest routines/RAM are deterministic stand-ins. */
#include "kernel.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#define xbox_log(...) ((void)0)
#include BLACK_INTERRUPT_IRQL_SOURCE
#include "../../ports/macos/overrides/dispatch_guard.inc"
#include BLACK_INTERRUPT_GATE_SOURCE
void xbox_dirty_prepare_write(void *pointer, size_t length) { (void)pointer; (void)length; }
static _Thread_local uint32_t g_eax, g_ecx, g_edx, g_esp;
static uint32_t ram[4096];
#define MEM32(a) ram[(uint32_t)(a) / 4]
#define BRIDGE_MEM32(a) MEM32(a)
#define STACK_ARG(n) MEM32(g_esp + 4 + 4 * (n))
typedef void (*recomp_func_t)(void);
enum { OBJECT=0x200, SYNC=0x1000, ISR=0x1010, OWNER=0x300, VOICE=0x900,
       HEAD=OWNER+0x4A0, MAIN_STACK=0x3000, OTHER_STACK=0x3500 };
static uint32_t connected = OBJECT;
static atomic_uint failures;
static unsigned completion_calls, observed_generation;
static unsigned generation, callback_mode;
static KIRQL expected_level = 7;
static HANDLE entered, release_callback, contender_started, owner_ready, owner_release, owner_done, iteration_done;
static atomic_uint active_callbacks, max_callbacks, callback_done;
static void check(int pass, const char *name)
{ if (!pass) { ++failures; fprintf(stderr, "FAIL %s\n",name); } }
static void require_event(HANDLE event)
{
    if (WaitForSingleObject(event, 2000) != WAIT_OBJECT_0) {
        fprintf(stderr, "FAIL bounded fixture event\n"); exit(1);
    }
}
static void sync_routine(void);
static void isr_routine(void);
static recomp_func_t recomp_lookup(uint32_t address)
{ return address == SYNC ? sync_routine : address == ISR ? isr_routine : NULL; }
static recomp_func_t recomp_lookup_manual(uint32_t address) { (void)address; return NULL; }
static uint32_t xbox_GetConnectedInterrupt(uint32_t vector)
{ return vector == 5 ? connected : 0; }
#include BLACK_INTERRUPT_BRIDGE_BODIES
#ifndef BLACK_INTERRUPT_BASELINE
#include "../../ports/macos/overrides/interrupt_bridge.inc"
#else
#define bridge_KeInitializeInterrupt black_OriginalInitializeInterrupt
#define bridge_KeSynchronizeExecution black_OriginalSynchronizeExecution
#define kernel_raise_interrupt black_OriginalRaiseInterrupt
#endif
static uint32_t black_audio_queue_completion(uint32_t voice);
static void black_OriginalAudioCompletion(void)
{
    ++completion_calls;
    observed_generation = MEM32(VOICE + 0x10);
    black_audio_queue_completion(g_ecx);
    g_eax = 1;
    g_esp += 8;
}
#include BLACK_INTERRUPT_COMPLETION_SOURCE

static void invoke_sync(void)
{
    uint32_t saved = g_esp;
    g_esp -= 16;
    MEM32(g_esp + 4) = OBJECT;
    MEM32(g_esp + 8) = SYNC;
    MEM32(g_esp + 12) = 0x678;
    uint32_t before = g_esp;
    bridge_KeSynchronizeExecution();
    check(g_esp == before, "synchronize callback preserves bridge stack contract");
    g_esp = saved;
}
static void completion(void)
{
    uint32_t saved = g_esp;
    g_esp -= 8; MEM32(g_esp + 4) = 7;
    g_ecx = VOICE; sub_0021C7EF();
    check(g_esp == saved, "completion keeps native early/callee ABI");
}
static void begin_callback(void)
{
    unsigned active = atomic_fetch_add(&active_callbacks, 1) + 1;
    unsigned previous = atomic_load(&max_callbacks);
    while (active > previous && !atomic_compare_exchange_weak(&max_callbacks, &previous, active)) {}
}
static void sync_routine(void)
{
    begin_callback();
    check(g_current_irql == expected_level, "synchronized routine executes at initialized device IRQL");
    if (callback_mode == 1) { SetEvent(entered); require_event(release_callback); }
    atomic_fetch_sub(&active_callbacks, 1);
    g_eax = 0xA5; g_esp += 8;
}
static void isr_routine(void)
{
    begin_callback();
    check(g_current_irql == expected_level, "ISR executes at actual device IRQL, not worker PASSIVE or vector");
    if (callback_mode == 2) { SetEvent(entered); require_event(release_callback); }
    if (callback_mode == 3) {
        completion();
        invoke_sync();
        check(g_current_irql == 7, "nested synchronize restores ISR IRQL");
    }
    if (callback_mode == 4) {
        xbox_KfLowerIrql(DISPATCH_LEVEL);
        check(g_current_irql == 7, "guest cannot lower below active ISR entry IRQL");
    }
    if (callback_mode == 5) completion();
    atomic_fetch_sub(&active_callbacks, 1);
    g_eax = 0xAA; g_esp += 12;
}
static void initialize(void)
{
    memset(ram, 0, sizeof ram);
    g_esp = MAIN_STACK;
    MEM32(g_esp+4)=OBJECT; MEM32(g_esp+8)=ISR; MEM32(g_esp+12)=0x456;
    MEM32(g_esp+16)=5; MEM32(g_esp+20)=7;
    bridge_KeInitializeInterrupt();
    check(MEM32(OBJECT) == ISR && MEM32(OBJECT+4) == 0x456 && MEM32(OBJECT+8) == 5,
          "pinned interrupt object layout retained");
    MEM32(VOICE+8)=OWNER;
    MEM32(VOICE+0x54)=MEM32(VOICE+0x58)=VOICE+0x54;
    MEM32(HEAD)=MEM32(HEAD+4)=HEAD;
    entered=CreateEventA(NULL,FALSE,FALSE,NULL);
    release_callback=CreateEventA(NULL,FALSE,FALSE,NULL);
    contender_started=CreateEventA(NULL,FALSE,FALSE,NULL);
    owner_ready=CreateEventA(NULL,FALSE,FALSE,NULL);
    owner_release=CreateEventA(NULL,FALSE,FALSE,NULL);
    owner_done=CreateEventA(NULL,FALSE,FALSE,NULL);
    iteration_done=CreateEventA(NULL,FALSE,FALSE,NULL);
    if(!entered||!release_callback||!contender_started||!owner_ready||!owner_release||!owner_done||!iteration_done) exit(2);
}
static void *call_sync(void *unused)
{
    (void)unused; g_esp=OTHER_STACK;
    KIRQL old=xbox_KeRaiseIrqlToDpcLevel();
    SetEvent(contender_started);
    invoke_sync();
    check(g_current_irql==DISPATCH_LEVEL, "synchronize returns to owning DISPATCH context");
    xbox_KfLowerIrql(old);
    atomic_store(&callback_done,1);
    return NULL;
}
static void *call_isr(void *unused)
{
    (void)unused; g_esp=MAIN_STACK;
    SetEvent(contender_started);
    int result=kernel_raise_interrupt(5);
    check(result==0 && g_eax==0xAA && g_esp==MAIN_STACK, "ISR FALSE result and stdcall ABI retained");
    check(g_current_irql==PASSIVE_LEVEL, "ISR returns to worker's original IRQL");
    atomic_store(&callback_done,1);
    return NULL;
}
static void exclusion(int reverse)
{
    initialize(); callback_mode=reverse?2:1;
    pthread_t first,second;
    pthread_create(&first,NULL,reverse?call_isr:call_sync,NULL);
    require_event(entered);
    ResetEvent(contender_started);
    atomic_store(&callback_done,0);
    pthread_create(&second,NULL,reverse?call_sync:call_isr,NULL);
    require_event(contender_started);
    uint64_t until=GetTickCount64()+30;
    while(!atomic_load(&callback_done)&&GetTickCount64()<until) Sleep(1);
    check(!atomic_load(&callback_done), "ISR and synchronized routine exclude each other in both directions");
    SetEvent(release_callback);
    pthread_join(first,NULL); pthread_join(second,NULL);
    check(atomic_load(&max_callbacks)==1, "at most one non-nested callback executes");
}
static void irql_cases(void)
{
    initialize(); callback_mode=3;
    KIRQL old=xbox_KeRaiseIrqlToDpcLevel();
    check(kernel_raise_interrupt(5)==0, "ISR callback invoked");
    check(g_current_irql==DISPATCH_LEVEL, "outer dispatch IRQL restored after ISR scope");
    /* Synthetic ISR nesting does not release or take the outer DISPATCH lock. */
    check(black_IrqlTryEnterDispatch(), "same-thread dispatch recursion works in scoped ISR caller");
    black_IrqlLeaveDispatch();
    xbox_KfLowerIrql(old);
    callback_mode=4; kernel_raise_interrupt(5);
    check(g_current_irql==PASSIVE_LEVEL, "device floor restores passive on exit");
    callback_mode=0;
    MEM32(g_esp+4)=OBJECT; MEM32(g_esp+8)=ISR; MEM32(g_esp+12)=0x456;
    MEM32(g_esp+16)=5; MEM32(g_esp+20)=9;
    bridge_KeInitializeInterrupt(); expected_level=9;
    kernel_raise_interrupt(5);
    check(g_current_irql==PASSIVE_LEVEL, "reused interrupt metadata updates without leaking IRQL");
    MEM32(OBJECT)=0xDEAD;
    check(kernel_raise_interrupt(5)==-1 && g_current_irql==PASSIVE_LEVEL,
          "missing ISR target releases gate and restores IRQL");
    MEM32(g_esp+4)=OBJECT; MEM32(g_esp+8)=0xDEAD; MEM32(g_esp+12)=0;
    bridge_KeSynchronizeExecution();
    check(g_eax==0 && g_current_irql==PASSIVE_LEVEL, "missing sync target releases gate and returns FALSE");
    connected=0;
    check(kernel_raise_interrupt(5)==-1 && g_current_irql==PASSIVE_LEVEL, "unconnected ISR returns without scope leak");
}
static unsigned owner_iterations=1;
static void *dispatch_owner(void *unused)
{
    (void)unused;
    for(unsigned i=0;i<owner_iterations;++i) {
        KIRQL old=xbox_KeRaiseIrqlToDpcLevel();
        MEM32(VOICE+0x10)=0; /* partially updated guest state */
        SetEvent(owner_ready); require_event(owner_release);
        MEM32(VOICE+0x10)=i+1;
        xbox_KfLowerIrql(old);
        SetEvent(owner_done);
        require_event(iteration_done);
    }
    return NULL;
}
static void foreign_owner(int stress)
{
    initialize(); callback_mode=stress?5:3;
    owner_iterations=stress?1024:1;
    pthread_t owner; pthread_create(&owner,NULL,dispatch_owner,NULL);
    for(generation=1;generation<=owner_iterations;++generation) {
        require_event(owner_ready);
        unsigned before=completion_calls;
        kernel_raise_interrupt(5);
        check(completion_calls==before, "ISR defers list mutation while foreign DISPATCH owner is updating");
        SetEvent(owner_release); require_event(owner_done);
        kernel_raise_interrupt(5);
        check(completion_calls==before+1 && observed_generation==generation,
              "retry sees only committed generation after DISPATCH owner releases");
        kernel_raise_interrupt(5); /* second completion before callback drain */
        uint32_t node=VOICE+0x54;
        check(MEM32(HEAD)==node && MEM32(HEAD+4)==node && MEM32(node)==HEAD && MEM32(node+4)==HEAD,
              "reused voice has one reciprocal completion node despite duplicate ISR");
        /* Drain before reusing the same object for the next native voice. */
        MEM32(HEAD)=MEM32(HEAD+4)=HEAD;
        MEM32(node)=MEM32(node+4)=node;
        SetEvent(iteration_done);
    }
    pthread_join(owner,NULL);
}
int main(int argc,char **argv)
{
    if(argc!=2) return 2;
    if(!strcmp(argv[1],"sync_blocks_isr")) exclusion(0);
    else if(!strcmp(argv[1],"isr_blocks_sync")) exclusion(1);
    else if(!strcmp(argv[1],"irql")) irql_cases();
    else if(!strcmp(argv[1],"foreign")) foreign_owner(0);
    else if(!strcmp(argv[1],"reuse")) foreign_owner(1);
    else return 2;
    printf("Interrupt sync %s: %u failures\n",argv[1],failures);
    return failures?1:0;
}
