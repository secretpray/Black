/* Real VP, control-register writes, IRQ publication and SE tick with fake
 * guest RAM. Platform mutex/condition/event implementations are production. */
#include <stdatomic.h>
#include <pthread.h>
#define BLACK_APU_DEVICE_HELPERS_ONLY
#define BLACK_APU_CUSTOM_WAKE
#include "apu_device_test.c"
#include BLACK_CONTROL_SOURCE

static HANDLE irq_event, started_event;
static atomic_uint worker_done, irq_wakes, writer_done;
static unsigned dsp_ticks, monitor_ticks;
uint64_t g_f7_apu_vp_mixbin_nonzero_samples;
static float bins[32][32];
static int use_se;
static void black_KernelWakeDeviceIRQ(void)
{
    atomic_fetch_add(&irq_wakes, 1);
    if (irq_event) SetEvent(irq_event);
}
void mcpx_debug_begin_frame(void) {}
void mcpx_debug_end_frame(void) {}
void mcpx_apu_update_dsp_preference(MCPXAPUState *state) { (void)state; }
void mcpx_apu_dsp_frame(MCPXAPUState *state, float mix[32][32])
{ (void)state; (void)mix; ++dsp_ticks; }
void mcpx_apu_monitor_frame(MCPXAPUState *state, float mix[32][32])
{ (void)state; (void)mix; ++monitor_ticks; }
#include BLACK_SE_SOURCE

static void setup(void)
{
    init();
    qemu_mutex_init(&d.lock);
    qemu_cond_init(&d.cond);
    qemu_cond_init(&d.idle_cond);
    memset(bins, 0, sizeof bins);
    g_f7_apu_vp_voices_processed = 0;
    dsp_ticks = monitor_ticks = 0;
    atomic_store(&worker_done, 0); atomic_store(&irq_wakes, 0);
    atomic_store(&writer_done, 0);
    use_se = 0;
    irq_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    started_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!irq_event || !started_event) exit(2);
    d.regs[NV_PAPU_SECTL] = NV_PAPU_SECTL_XCNTMODE;
    d.regs[NV_PAPU_IEN] = NV_PAPU_ISTS_GINTSTS | NV_PAPU_ISTS_FETINTSTS;
    d.regs[NV_PAPU_TVL2D] = 64;
    for (unsigned n = 0; n < 128; ++n) stw_le_phys(NULL, DATA + n * 2, 4000);
    pcm(64, 128, 0);
    pcm(66, 128, 0);
    field(64, NV_PAVS_VOICE_TAR_PITCH_LINK,
          NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE, 66);
    field(66, NV_PAVS_VOICE_CFG_VBIN, NV_PAVS_VOICE_CFG_VBIN_V0BIN, 1);
    voice_lock(&d, 64, true);
}

static void teardown(void)
{
    CHECK(d.vp.voice_work_dispatch.queue_len == 0, "work queue drained or cancelled");
    CloseHandle(irq_event); CloseHandle(started_event);
    qemu_mutex_destroy(&d.lock);
}

static void *frame_worker(void *unused)
{
    (void)unused;
    qemu_mutex_lock(&d.lock);
    SetEvent(started_event);
    if (use_se) se_frame(&d); else mcpx_apu_vp_frame(&d, bins);
    atomic_store(&worker_done, 1);
    qemu_mutex_unlock(&d.lock);
    return NULL;
}

static pthread_t start_frame(void)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, frame_worker, NULL)) exit(2);
    CHECK(WaitForSingleObject(started_event, 1000) == WAIT_OBJECT_0, "frame worker started under APU lock");
    return thread;
}

/* No unbounded negative-control thread: request cancellation and join only
 * after completion is observed. A broken cancellation exits this test's own
 * process with a normal failure, leaving no detached workers behind. */
static void join_frame(pthread_t thread)
{
    uint64_t deadline = GetTickCount64() + 1000;
    while (!atomic_load(&worker_done) && GetTickCount64() < deadline) Sleep(1);
    if (!atomic_load(&worker_done)) {
        qemu_mutex_lock(&d.lock);
        d.exiting = true;
        qemu_cond_broadcast(&d.cond);
        qemu_mutex_unlock(&d.lock);
        deadline = GetTickCount64() + 1000;
        while (!atomic_load(&worker_done) && GetTickCount64() < deadline) Sleep(1);
    }
    if (!atomic_load(&worker_done)) {
        fprintf(stderr, "FAIL bounded worker cancellation\n");
        exit(1);
    }
    pthread_join(thread, NULL);
}

static void check_pcm(void)
{
    for (unsigned n = 0; n < 32; ++n) {
        CHECK(bins[0][n] == 4000 / 32768.0f, "previously locked voice retains full tick");
        CHECK(bins[1][n] == 4000 / 32768.0f, "continuing voice retains full tick");
    }
    CHECK(g_f7_apu_vp_voices_processed == 2, "each continuing voice processed once");
}

static void wait_unlock(void)
{
    setup();
    for (unsigned n = 0; n < 128; ++n) stw_le_phys(NULL, DATA + n * 2, (uint16_t)-4000);
    pthread_t frame = start_frame();
    /* Acquiring the mutex after the started event proves that the VP has
     * either entered its wait or returned. No scheduling sleep is needed. */
    qemu_mutex_lock(&d.lock);
    CHECK(!atomic_load(&worker_done), "locked voice really blocks VP until guest edit completes");
    CHECK(!g_f7_apu_vp_voices_processed && !s_resample[64].fetched && !s_resample[66].fetched,
          "no voice consumes source before all queued locks release");
    /* Reconfigure the samples during the protected edit. */
    for (unsigned n = 0; n < 128; ++n) stw_le_phys(NULL, DATA + n * 2, 4000);
    voice_lock(&d, 64, false);
    qemu_mutex_unlock(&d.lock);
    join_frame(frame);
    check_pcm();
    teardown();
}

static void stopped_during_edit(void)
{
    setup();
    pthread_t frame = start_frame();
    qemu_mutex_lock(&d.lock);
    CHECK(!atomic_load(&worker_done), "edit starts while VP is waiting");
    /* Lock ownership may move between queued voices. Recheck all entries
     * after every wake rather than assuming the first unlock completes it. */
    voice_lock(&d, 66, true);
    mcpx_apu_vp_write(&d, NV1BA0_PIO_VOICE_OFF, 64, 4);
    voice_lock(&d, 64, false);
    qemu_mutex_unlock(&d.lock);
    qemu_mutex_lock(&d.lock);
    CHECK(!atomic_load(&worker_done) && !s_resample[66].fetched,
          "another queued lock keeps source and envelopes frozen");
    voice_lock(&d, 66, false);
    qemu_mutex_unlock(&d.lock);
    join_frame(frame);
    CHECK(g_f7_apu_vp_voices_processed == 1 && !s_resample[64].fetched,
          "voice stopped by guest during wait is not processed from stale queue");
    for (unsigned n = 0; n < 32; ++n)
        CHECK(bins[0][n] == 0 && bins[1][n] == 4000 / 32768.0f,
              "stopped voice silent; other voice retains complete tick");
    teardown();
}

static void irq_unlock(void)
{
    setup();
    pcm(65, 128, 0);
    field(65, NV_PAVS_VOICE_PAR_STATE, NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE, 0);
    field(64, NV_PAVS_VOICE_TAR_PITCH_LINK,
          NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE, 65);
    field(65, NV_PAVS_VOICE_TAR_PITCH_LINK,
          NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE, 66);
    d.regs[NV_PAPU_FETFORCE1] = NV_PAPU_FETFORCE1_SE2FE_IDLE_VOICE;
    pthread_t frame = start_frame();
    int delivered = WaitForSingleObject(irq_event, 250) == WAIT_OBJECT_0;
    CHECK(delivered, "fresh idle trap publishes enabled IRQ before waiting on earlier voice");
    qemu_mutex_lock(&d.lock);
    if (delivered) {
        CHECK(!atomic_load(&worker_done) && !s_resample[64].fetched,
              "ISR gets APU mutex before any locked source is consumed");
        CHECK(d.regs[NV_PAPU_FEDECPARAM] == 65 && d.regs[NV_PAPU_FEDECMETH] == SE2FE_IDLE_VOICE &&
              d.regs[NV_PAPU_CVL2D] == 65 && d.regs[NV_PAPU_NVL2D] == 66,
              "ISR receives matching trap payload and unlink cursors");
        /* Only this IRQ handler is allowed to unlock voice 64. Real PIO
         * calls recursively enter the same device mutex. */
        mcpx_apu_vp_write(&d, NV1BA0_PIO_SET_CURRENT_VOICE, 64, 4);
        mcpx_apu_vp_write(&d, NV1BA0_PIO_VOICE_LOCK, 0, 4);
        CHECK(d.regs[NV_PAPU_FEDECPARAM] == 65 && d.regs[NV_PAPU_FEDECMETH] == SE2FE_IDLE_VOICE,
              "ISR cleanup PIO cannot overwrite pending trap payload");
        mcpx_apu_write(&d, NV_PAPU_ISTS, ~0u, 4);
        CHECK(d.regs[NV_PAPU_ISTS] == 0, "real control W1C acknowledges without relatch");
        mcpx_apu_write(&d, NV_PAPU_FECTL, NV_PAPU_FECTL_FEMETHMODE_FREE_RUNNING, 4);
        CHECK(!d.set_irq && atomic_load(&irq_wakes) == 1, "pending IRQ published exactly once before continuation");
    } else {
        /* Do not manually unlock a no-publication control: cancellation is
         * its only exit, so it cannot falsely qualify IRQ-dependent progress. */
        d.pause_requested = true;
        qemu_cond_broadcast(&d.cond);
    }
    qemu_mutex_unlock(&d.lock);
    join_frame(frame);
    if (delivered) check_pcm();
    teardown();
}

static void cancel_wait(int shutdown)
{
    setup(); use_se = 1;
    d.ep_frame_div = 3;
    memset(d.monitor.frame_buf, 0x55, sizeof d.monitor.frame_buf);
    pthread_t frame = start_frame();
    qemu_mutex_lock(&d.lock);
    CHECK(!atomic_load(&worker_done), "cancel scenario actually entered locked-voice wait");
    if (shutdown) d.exiting = true; else d.pause_requested = true;
    qemu_cond_broadcast(&d.cond);
    qemu_mutex_unlock(&d.lock);
    join_frame(frame);
    CHECK(!g_f7_apu_vp_voices_processed && !s_resample[64].fetched && !s_resample[66].fetched,
          "pause or shutdown cancels whole tick without consuming source");
    CHECK(!dsp_ticks && !monitor_ticks && d.ep_frame_div == 3,
          "cancelled SE tick retains partial DSP epoch and submits no audio");
    for (unsigned i = 0; i < 256; ++i)
        CHECK(d.monitor.frame_buf[i][0] == 0x5555 && d.monitor.frame_buf[i][1] == 0x5555,
              "partial PCM epoch not overwritten by cancelled tick");
    teardown();
}

static int write_control;
static void *writer(void *unused)
{
    (void)unused;
    SetEvent(started_event);
    if (write_control) mcpx_apu_write(&d, NV_PAPU_FECTL, NV_PAPU_FECTL_FEMETHMODE_HALTED, 4);
    else mcpx_apu_vp_write(&d, NV1BA0_PIO_SET_CURRENT_VOICE, 77, 4);
    atomic_store(&writer_done, 1);
    return NULL;
}

static void serialization(int control)
{
    setup(); write_control = control;
    pthread_t thread;
    qemu_mutex_lock(&d.lock);
    if (pthread_create(&thread, NULL, writer, NULL)) exit(2);
    CHECK(WaitForSingleObject(started_event, 1000) == WAIT_OBJECT_0, "guest writer started");
    uint64_t deadline = GetTickCount64() + 30;
    while (!atomic_load(&writer_done) && GetTickCount64() < deadline) Sleep(1);
    CHECK(!atomic_load(&writer_done), "guest register writer waits for device mutex");
    /* A negative-control writer may be racing, so inspect registers only
     * after join. The pre-unlock observation uses the atomic marker. */
    qemu_mutex_unlock(&d.lock);
    pthread_join(thread, NULL);
    CHECK(control ? d.regs[NV_PAPU_FECTL] == NV_PAPU_FECTL_FEMETHMODE_HALTED :
                    d.regs[NV_PAPU_FECV] == 77, "writer completes after mutex release");
    teardown();
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "wait")) wait_unlock();
    else if (!strcmp(argv[1], "edit")) stopped_during_edit();
    else if (!strcmp(argv[1], "irq")) irq_unlock();
    else if (!strcmp(argv[1], "pause")) cancel_wait(0);
    else if (!strcmp(argv[1], "shutdown")) cancel_wait(1);
    else if (!strcmp(argv[1], "pio")) serialization(0);
    else if (!strcmp(argv[1], "control")) serialization(1);
    else return 2;
    printf("VP locks %s: %u failures\n", argv[1], failures);
    return failures ? 1 : 0;
}
