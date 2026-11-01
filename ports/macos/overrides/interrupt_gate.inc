#include <stdio.h>
/* Global recursive gate models serialization of ISR and synchronized driver
 * callbacks on the single Xbox CPU. It is never acquired by the APU thread.
 * Lock order: guest DISPATCH (if already owned) -> ISR gate -> APU calls.
 * ISR completion uses try-dispatch, so it never waits in the reverse order. */
static pthread_mutex_t black_interrupt_gate;
static pthread_once_t black_interrupt_once = PTHREAD_ONCE_INIT;
static struct { uint32_t object; KIRQL level; } black_interrupt_objects[64];
static void black_interrupt_init(void)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&black_interrupt_gate, &attr);
    pthread_mutexattr_destroy(&attr);
}
static void black_interrupt_lock(void)
{
    pthread_once(&black_interrupt_once, black_interrupt_init);
    pthread_mutex_lock(&black_interrupt_gate);
}

BlackInterruptScope black_InterruptEnter(KIRQL level)
{
    black_interrupt_lock();
    BlackInterruptScope previous = {g_current_irql, black_interrupt_floor, black_interrupt_depth};
    ++black_interrupt_depth;
    if (level > g_current_irql) g_current_irql = level;
    black_interrupt_floor = g_current_irql;
    return previous;
}

void black_InterruptLeave(BlackInterruptScope previous)
{
    g_current_irql = previous.irql;
    black_interrupt_floor = previous.floor;
    black_interrupt_depth = previous.depth;
    pthread_mutex_unlock(&black_interrupt_gate);
}

void black_InterruptRemember(uint32_t interrupt, KIRQL level)
{
    if (!interrupt) return;
    black_interrupt_lock();
    unsigned slot = 64;
    for (unsigned i = 0; i < 64; ++i) {
        if (black_interrupt_objects[i].object == interrupt) { slot = i; break; }
        if (!black_interrupt_objects[i].object && slot == 64) slot = i;
    }
    if (slot < 64) {
        black_interrupt_objects[slot].object = interrupt;
        black_interrupt_objects[slot].level = level;
    } else {
        fprintf(stderr, "[IRQ] interrupt metadata table full; using vector IRQL fallback\n");
    }
    pthread_mutex_unlock(&black_interrupt_gate);
}

KIRQL black_InterruptLevel(uint32_t interrupt, KIRQL fallback)
{
    black_interrupt_lock();
    KIRQL level = fallback;
    for (unsigned i = 0; i < 64; ++i)
        if (black_interrupt_objects[i].object == interrupt && interrupt) {
            level = black_interrupt_objects[i].level;
            break;
        }
    pthread_mutex_unlock(&black_interrupt_gate);
    return level;
}

BlackInterruptScope black_InterruptEnterObject(uint32_t interrupt, KIRQL fallback)
{
    /* Keep metadata resolution and gate entry atomic with reinitialization
     * of a reused KINTERRUPT object. Recursive acquisition is balanced. */
    black_interrupt_lock();
    BlackInterruptScope scope = black_InterruptEnter(black_InterruptLevel(interrupt, fallback));
    pthread_mutex_unlock(&black_interrupt_gate);
    return scope;
}
