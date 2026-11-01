/* Execute the production frame loop with a synthetic VP and audio sink.
 * The old loop is extracted from the pinned core for a negative control. */
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../../ports/macos/runtime/src/apu/apu_regs.h"
#define NUM_SAMPLES_PER_FRAME 32
#define GET_MASK(value, mask) (((value) & (mask)) >> __builtin_ctz(mask))
#define qatomic_read(p) (*(p))
#define MCPX_APU_DEVICE(p) ((MCPXAPUState *)(p))
typedef struct {
    pthread_mutex_t lock;
    int cond, idle_cond;
    uint32_t regs[0x20000];
    unsigned ep_frame_div;
    bool exiting, pause_requested, is_idle, set_irq;
    struct { int16_t frame_buf[256][2]; bool dsp_wrote_frame; } monitor;
} MCPXAPUState;
static struct { int active; } g_test_tone;
static int g_mixer_active_count;
static unsigned failures, native_ticks, monitor_ticks, waits, throttle_calls, packets, irq, scenario;
static unsigned paused_waits;
static bool native_scenario(void) { return scenario <= 1 || scenario == 4; }
static void check(int pass, const char *name)
{ if (!pass) { ++failures; fprintf(stderr, "FAIL %s\n", name); } }
static void qemu_mutex_lock(pthread_mutex_t *m) { pthread_mutex_lock(m); }
static void qemu_mutex_unlock(pthread_mutex_t *m) { pthread_mutex_unlock(m); }
static void qemu_cond_signal(int *c) { (void)c; }
static void qemu_cond_wait(int *c, pthread_mutex_t *m);
static void update_irq(MCPXAPUState *d) { (void)d; ++irq; }
static MCPXAPUState *current;
static void qemu_cond_wait(int *c, pthread_mutex_t *m)
{
    (void)c;
    pthread_mutex_unlock(m);
    int available = pthread_mutex_trylock(m) == 0;
    check(available && current->is_idle, "pause publishes idle and releases lock");
    if (available) pthread_mutex_unlock(m);
    pthread_mutex_lock(m);
    ++paused_waits;
    current->exiting = true;
}
static void qemu_cond_timedwait(int *c, pthread_mutex_t *m, int ms)
{
    (void)c; check(ms > 0 && ms <= 5, "bounded FE wait");
    pthread_mutex_unlock(m);
    int available = pthread_mutex_trylock(m) == 0;
    check(available, "APU lock available to guest ISR while gated");
    if (available) pthread_mutex_unlock(m);
    pthread_mutex_lock(m);
    ++waits;
    check(current->ep_frame_div == 3, "trap/halt/OFF retain partial epoch index");
    check(current->monitor.frame_buf[0][0] == 1 && current->monitor.frame_buf[64][0] == 3,
        "partial PCM retained while gated");
    if (scenario == 1) { current->exiting = true; return; }
    if (scenario == 4) { current->pause_requested = true; return; }
    if (waits == 1) current->regs[NV_PAPU_FECTL] = NV_PAPU_FECTL_FEMETHMODE_HALTED;
    if (waits == 2) { current->regs[NV_PAPU_SECTL] = NV_PAPU_SECTL_XCNTMODE_OFF; current->regs[NV_PAPU_FECTL] = 0; }
    if (waits == 3) current->regs[NV_PAPU_SECTL] = NV_PAPU_SECTL_XCNTMODE;
}
static void throttle(MCPXAPUState *d)
{
    ++throttle_calls;
    if (native_scenario() && throttle_calls == 4) {
        /* Control changes while throttle released the lock. */
        d->regs[NV_PAPU_FECTL] = NV_PAPU_FECTL_FEMETHMODE_TRAPPED;
        d->set_irq = true;
    }
    if (throttle_calls > 40) d->exiting = true; /* bound the old negative control */
}
static void se_frame(MCPXAPUState *d)
{
    unsigned off = d->ep_frame_div % 8 * 32;
    ++native_ticks;
    for (unsigned i = 0; i < 32; ++i)
        d->monitor.frame_buf[off+i][0] = d->monitor.frame_buf[off+i][1] = (int16_t)native_ticks;
    d->monitor.dsp_wrote_frame = true;
    ++d->ep_frame_div;
    if (!(d->ep_frame_div % 8)) {
        for (unsigned i = 0; i < 256; ++i)
            check(d->monitor.frame_buf[i][0] == (int)(native_ticks - 7 + i / 32), "chronological native packet");
        ++packets;
    }
    if (native_ticks == 16) d->exiting = true;
}
static void mcpx_apu_monitor_frame(MCPXAPUState *d, void *mix)
{
    (void)mix;
    ++monitor_ticks;
    if (native_scenario()) check(false, "no stale native packet submitted while FE gated");
    else {
        unsigned off = d->ep_frame_div % 8 * 32;
        check(!d->monitor.dsp_wrote_frame && !d->monitor.frame_buf[off][0], "host output never reuses stale native slice");
        if (monitor_ticks == 16) d->exiting = true;
    }
}
#include BLACK_EPOCH_LOOP
int main(void)
{
    unsetenv("RECOMP_AUDIO_DIAG");
    for (scenario = 0; scenario < 5; ++scenario) {
        MCPXAPUState *d = calloc(1, sizeof *d); current = d;
        pthread_mutex_init(&d->lock, NULL);
        native_ticks = monitor_ticks = waits = throttle_calls = packets = irq = paused_waits = 0;
        g_test_tone.active = scenario == 3;
        g_mixer_active_count = scenario == 2;
        if (native_scenario()) d->regs[NV_PAPU_SECTL] = NV_PAPU_SECTL_XCNTMODE;
        else { memset(d->monitor.frame_buf, 0x55, sizeof d->monitor.frame_buf); d->monitor.dsp_wrote_frame = true; }
        mcpx_apu_frame_thread(d);
        if (scenario == 0) {
            check(waits == 3 && irq == 1, "trap/halt/OFF serviced before resume");
            check(native_ticks == 16 && packets == 2 && !monitor_ticks, "native packet chronology after resumed epoch");
        } else if (scenario == 1) check(native_ticks == 3 && waits == 1 && !monitor_ticks, "shutdown during gate consumes no source/PCM");
        else if (scenario == 4) check(native_ticks == 3 && waits == 1 && paused_waits == 1 && !monitor_ticks, "pause during gate consumes no source/PCM");
        else check(monitor_ticks == 16 && !native_ticks && !waits, "explicit host output allowed while OFF");
        check(pthread_mutex_trylock(&d->lock) == 0, "lock released on thread exit");
        pthread_mutex_unlock(&d->lock); pthread_mutex_destroy(&d->lock); free(d);
    }
    printf("APU epoch loop: %u failed checks\n", failures);
    return failures ? 1 : 0;
}
