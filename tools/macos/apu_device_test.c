/* Asset-free tests of the actual VP translation unit and core IRQ function.
 * Both production overlay and original pinned sources run the same cases. */
#include BLACK_VP_SOURCE

MCPXAPUState *g_state;
struct McpxApuDebug g_dbg, g_dbg_cache;
int g_dbg_voice_monitor = -1;
uint64_t g_dbg_muted_voices[4];
uint8_t *g_apu_ram_ptr;
uint64_t g_f7_apu_vp_voices_processed, g_f7_apu_vp_nonzero_voices;
uint64_t g_f7_apu_vp_nonzero_samples_read;
/* This harness has plain fake RAM and never performs platform file I/O. */
void xbox_dirty_prepare_write(void *ptr, size_t length) { (void)ptr; (void)length; }
#ifdef BLACK_APU_CUSTOM_WAKE
static void black_KernelWakeDeviceIRQ(void);
#else
static void black_KernelWakeDeviceIRQ(void) {}
#endif
#include BLACK_IRQ_SOURCE
#ifdef BLACK_APU_PUBLISH_SOURCE
#include BLACK_APU_PUBLISH_SOURCE
#else
#include "../../ports/macos/overrides/apu_publish_irq.inc"
#endif

enum { RAM_SIZE = 0x100000, VOICES = 0x10000, SGE = 0x20000,
       DATA = 0x30000, SSL = 0x40000 };
static MCPXAPUState d;
static uint8_t ram[RAM_SIZE];
static unsigned failures;
#define CHECK(cond, name) do { if (!(cond)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, name); ++failures; \
} } while (0)

static void init(void)
{
    memset(&d, 0, sizeof d);
    memset(ram, 0, sizeof ram);
    memset(s_resample, 0, sizeof s_resample);
    g_apu_ram_ptr = ram;
    d.ram_ptr = ram;
    d.regs[NV_PAPU_VPVADDR] = VOICES;
    d.regs[NV_PAPU_VPSGEADDR] = SGE;
    d.regs[NV_PAPU_VPSSLADDR] = SSL;
    d.regs[NV_PAPU_FENADDR] = 0x50000;
    for (unsigned i = 0; i < 16; ++i)
        stl_le_phys(NULL, SGE + i * 8, DATA + i * 4096);
    for (unsigned i = 0; i < 3; ++i) d.regs[voice_list_regs[i].top] = 0xffff;
}

static void field(unsigned v, unsigned reg, unsigned mask, unsigned value)
{ voice_set_mask(&d, v, reg, mask, value); }

static void pcm(unsigned v, unsigned length, int loop)
{
    field(v, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE,
          NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE_S16);
    field(v, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE, 1);
    field(v, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_LOOP, loop);
    field(v, NV_PAVS_VOICE_PAR_STATE, NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE, 1);
    field(v, NV_PAVS_VOICE_PAR_NEXT, NV_PAVS_VOICE_PAR_NEXT_EBO, length - 1);
    field(v, NV_PAVS_VOICE_TAR_PITCH_LINK,
          NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE, 0xffff);
    /* Only bin 0 audible. Envelopes OFF mean unity in the pinned VP. */
    field(v, NV_PAVS_VOICE_TAR_VOLA, 0xffffffffu, 0xffff000f);
    field(v, NV_PAVS_VOICE_TAR_VOLB, 0xffffffffu, 0xffffffffu);
    field(v, NV_PAVS_VOICE_TAR_VOLC, 0xffffffffu, 0xffffffffu);
}

static void ack(unsigned bits)
{
    /* The production ISTS MMIO branch uses this W1C + update sequence. */
    qatomic_and(&d.regs[NV_PAPU_ISTS], ~bits);
    update_irq(&d);
}

static void latch(void)
{
    init();
    d.regs[NV_PAPU_FETFORCE1] = NV_PAPU_FETFORCE1_SE2FE_IDLE_VOICE;
    d.regs[NV_PAPU_IEN] = NV_PAPU_ISTS_GINTSTS | NV_PAPU_ISTS_FETINTSTS;
    fe_method(&d, SE2FE_IDLE_VOICE, 65);
    CHECK(d.regs[NV_PAPU_ISTS] & NV_PAPU_ISTS_FETINTSTS, "trap latches before publication");
    update_irq(&d);
    CHECK(d.regs[NV_PAPU_ISTS] & NV_PAPU_ISTS_GINTSTS, "enabled trap asserts IRQ");
    CHECK(d.regs[NV_PAPU_FEDECPARAM] == 65 && d.set_irq, "real trap payload retained");
    ack(~0u);
    CHECK(d.regs[NV_PAPU_ISTS] == 0, "ack before resume clears trap permanently");
    for (unsigned i = 0; i < 10; ++i) update_irq(&d);
    CHECK(d.regs[NV_PAPU_ISTS] == 0, "polling while trapped does not replay IRQ");
    d.regs[NV_PAPU_FECTL] = NV_PAPU_FECTL_FETRAPREASON_REQUESTED;
    d.regs[NV_PAPU_ISTS] |= NV_PAPU_ISTS_FENINTSTS;
    update_irq(&d);
    CHECK(!(d.regs[NV_PAPU_ISTS] & NV_PAPU_ISTS_FETINTSTS), "later notification cannot replay old method");
    ack(~0u);
    d.regs[NV_PAPU_FECTL] |= NV_PAPU_FECTL_FEMETHMODE_HALTED;
    update_irq(&d);
    CHECK(d.regs[NV_PAPU_ISTS] == 0, "guest halt is not an idle trap");
    d.regs[NV_PAPU_FECTL] = 0;
    d.regs[NV_PAPU_IEN] = 0;
    fe_method(&d, SE2FE_IDLE_VOICE, 66);
    update_irq(&d);
    CHECK((d.regs[NV_PAPU_ISTS] & NV_PAPU_ISTS_FETINTSTS) &&
          !(d.regs[NV_PAPU_ISTS] & NV_PAPU_ISTS_GINTSTS), "masked event latched without line");
    d.regs[NV_PAPU_IEN] = NV_PAPU_ISTS_GINTSTS | NV_PAPU_ISTS_FETINTSTS;
    update_irq(&d);
    CHECK(d.regs[NV_PAPU_ISTS] & NV_PAPU_ISTS_GINTSTS, "unmask delivers pending event");
    ack(~0u);
    d.regs[NV_PAPU_FECTL] = 0;
    d.regs[NV_PAPU_FETFORCE1] = 0;
    fe_method(&d, SE2FE_IDLE_VOICE, 67);
    update_irq(&d);
    CHECK(d.regs[NV_PAPU_ISTS] == 0 && d.regs[NV_PAPU_FECTL] == 0, "unarmed method does not trap");
}

static void trap(void)
{
    init();
    float bins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME] = {{0}};
    for (unsigned n = 0; n < 256; ++n) stw_le_phys(NULL, DATA + n * 2, 4000);
    for (unsigned v = 64; v <= 69; ++v) pcm(v, 256, 1);
    field(65, NV_PAVS_VOICE_PAR_STATE, NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE, 0);
    field(67, NV_PAVS_VOICE_PAR_STATE, NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE, 0);
    for (unsigned v = 64; v <= 66; ++v)
        field(v, NV_PAVS_VOICE_TAR_PITCH_LINK,
              NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE, v + 1);
    d.regs[NV_PAPU_TVL2D] = 64;
    d.regs[NV_PAPU_TVL3D] = 68;
    d.regs[NV_PAPU_TVLMP] = 69;
    for (unsigned list = 1; list < 3; ++list) {
        d.regs[voice_list_regs[list].current] = 0xeeee;
        d.regs[voice_list_regs[list].next] = 0xdddd;
    }
    d.regs[NV_PAPU_FETFORCE1] = NV_PAPU_FETFORCE1_SE2FE_IDLE_VOICE;
    mcpx_apu_vp_frame(&d, bins);
    CHECK(d.regs[NV_PAPU_FEDECPARAM] == 65 &&
          d.regs[NV_PAPU_FEDECMETH] == SE2FE_IDLE_VOICE, "first idle trap owns payload");
    CHECK(d.regs[NV_PAPU_CVL2D] == 65 && d.regs[NV_PAPU_NVL2D] == 66, "payload paired with unlink cursors");
    CHECK(d.regs[NV_PAPU_CVL3D] == 0xeeee && d.regs[NV_PAPU_NVL3D] == 0xdddd &&
          d.regs[NV_PAPU_CVLMP] == 0xeeee && d.regs[NV_PAPU_NVLMP] == 0xdddd, "all later cursors frozen");
    for (unsigned n = 0; n < NUM_SAMPLES_PER_FRAME; ++n)
        CHECK(bins[0][n] == 4 * (4000 / 32768.0f), "active voices in all lists retain full tick");
    uint64_t processed = g_f7_apu_vp_voices_processed;
    mcpx_apu_vp_frame(&d, bins);
    CHECK(g_f7_apu_vp_voices_processed == processed &&
          d.regs[NV_PAPU_FEDECPARAM] == 65, "pending trap prevents next tick advancing");
    /* ISR unlinks the first voice. The second must get its own trap. */
    field(64, NV_PAVS_VOICE_TAR_PITCH_LINK,
          NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE, 66);
    ack(~0u); d.regs[NV_PAPU_FECTL] = 0;
    memset(bins, 0, sizeof bins);
    mcpx_apu_vp_frame(&d, bins);
    CHECK(d.regs[NV_PAPU_FEDECPARAM] == 67 && d.regs[NV_PAPU_CVL2D] == 67 &&
          d.regs[NV_PAPU_NVL2D] == 0xffff, "next idle delivered after ISR resume");
    CHECK(g_f7_apu_vp_voices_processed == processed + 4, "each active voice processed once after resume");
    /* No armed trap: all idle nodes traversed without blocking other lists. */
    ack(~0u); d.regs[NV_PAPU_FECTL] = 0; d.regs[NV_PAPU_FETFORCE1] = 0;
    mcpx_apu_vp_frame(&d, bins);
    CHECK(d.regs[NV_PAPU_CVL2D] == 0xffff &&
          g_f7_apu_vp_voices_processed == processed + 8, "unarmed idle traversal completes");
    /* Invalid cycle is bounded and does not starve later active lists. */
    d.regs[NV_PAPU_TVL2D] = 65;
    field(65, NV_PAVS_VOICE_TAR_PITCH_LINK,
          NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE, 65);
    d.regs[NV_PAPU_FETFORCE1] = NV_PAPU_FETFORCE1_SE2FE_IDLE_VOICE;
    mcpx_apu_vp_frame(&d, bins);
    CHECK(d.regs[NV_PAPU_FEDECPARAM] == 65 &&
          g_f7_apu_vp_voices_processed == processed + 10, "cycle bound retains later active lists");
}

static void ebo(void)
{
    float sample[1][2];
    for (unsigned length = 1; length <= 5; ++length) {
        init(); pcm(0, length, 0);
        for (unsigned n = 0; n < length; ++n) stw_le_phys(NULL, DATA + n * 2, 1000 * (n + 1));
        for (unsigned n = 0; n < length; ++n) {
            CHECK(voice_resample(&d, 0, sample, 1, 1) == 1, "one-shot returns every source sample");
            CHECK(sample[0][0] == (1000 * (n + 1)) / 32768.0f, "one-shot includes EBO sample");
        }
        CHECK(voice_resample(&d, 0, sample, 1, 1) < 0, "one-shot completes after EBO");
    }
    init(); pcm(0, 4, 1);
    field(0, NV_PAVS_VOICE_CUR_PSH_SAMPLE, NV_PAVS_VOICE_CUR_PSH_SAMPLE_LBO, 1);
    for (unsigned n = 0; n < 4; ++n) stw_le_phys(NULL, DATA + n * 2, 1000 * (n + 1));
    for (unsigned n = 0; n < 20; ++n) {
        unsigned index = n < 4 ? n : 1 + (n - 4) % 3;
        CHECK(voice_resample(&d, 0, sample, 1, 1) == 1 &&
              sample[0][0] == (1000 * (index + 1)) / 32768.0f, "loop includes EBO then resumes at nonzero LBO");
    }
    /* Two SSL segments: final sample must precede next-segment notification. */
    init(); pcm(0, 3, 0);
    field(0, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_DATA_TYPE, 1);
    field(0, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_PERSIST, 1);
    d.vp.ssl[0].count[0] = 2;
    for (unsigned seg = 0; seg < 2; ++seg) {
        stl_le_phys(NULL, SSL + seg * 8, DATA + seg * 16);
        stl_le_phys(NULL, SSL + seg * 8 + 4, 3 | (1 << 16));
        for (unsigned n = 0; n < 3; ++n) stw_le_phys(NULL, DATA + seg * 16 + n * 2, 1000 * (seg * 3 + n + 1));
    }
    for (unsigned n = 0; n < 6; ++n) {
        CHECK(voice_resample(&d, 0, sample, 1, 1) == 1 &&
              sample[0][0] == (1000 * (n + 1)) / 32768.0f, "SSL segments retain both endpoints");
        unsigned notify = d.regs[NV_PAPU_FENADDR] +
            16 * (MCPX_HW_NOTIFIER_BASE_OFFSET + MCPX_HW_NOTIFIER_SSLA_DONE) + 15;
        CHECK(ram[notify] == (n == 5 ? NV1BA0_NOTIFICATION_STATUS_DONE_SUCCESS : 0),
              "SSL completion follows final playback sample, not prefetch cursor");
    }
}

static void adpcm(void)
{
    for (unsigned stream = 0; stream <= 1; ++stream)
    for (unsigned channels = 1; channels <= 2; ++channels)
    for (unsigned blocks = 1; blocks <= 32; ++blocks) {
        init();
        field(0, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_STEREO, channels - 1);
        field(0, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE,
              NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE_ADPCM);
        field(0, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_SAMPLES_PER_BLOCK, blocks - 1);
        field(0, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_DATA_TYPE, stream);
        field(0, NV_PAVS_VOICE_CFG_FMT, NV_PAVS_VOICE_CFG_FMT_PERSIST, 1);
        field(0, NV_PAVS_VOICE_PAR_NEXT, NV_PAVS_VOICE_PAR_NEXT_EBO, 63);
        /* Exercise the final channel-interleaved sample in the first block. */
        field(0, NV_PAVS_VOICE_PAR_OFFSET, NV_PAVS_VOICE_PAR_OFFSET_CBO, 63);
        d.vp.ssl[0].count[0] = 1;
        stl_le_phys(NULL, SSL, DATA);
        stl_le_phys(NULL, SSL + 4, 64 | (2 << 16));
        for (unsigned n = 0; n < 36 * blocks; ++n) ram[DATA + n] = n * 37 + 11;
        for (unsigned ch = 0; ch < channels; ++ch) {
            ram[DATA + ch * 4 + 2] = 20;
            ram[DATA + ch * 4 + 3] = 0;
        }
        int16_t expected[65 * 32 * 2];
        adpcm_decode_block(expected, ram + DATA, 36 * blocks, channels);
        float sample[1][2] = {{0}};
        /* A single 36-byte block cannot hold a complete stereo ADPCM
         * source block. Test its rejection separately, never compare the
         * uninitialized tail of either decoder buffer as expected PCM. */
        if (blocks < channels) {
#ifdef BLACK_TEST_RESAMPLE_FIXED
            CHECK(voice_get_samples(&d, 0, sample, 1) < 0, "incomplete stereo ADPCM block rejected");
#endif
            continue;
        }
        CHECK(voice_get_samples(&d, 0, sample, 1) == 1, "all descriptor widths decode safely");
        CHECK(sample[0][0] == int16_to_float(expected[63 * channels]), "ADPCM left equals reference decoder");
        CHECK(sample[0][1] == int16_to_float(expected[63 * channels + (channels == 2)]), "ADPCM right or mono duplicate equals reference");
    }
}

#ifndef BLACK_APU_DEVICE_HELPERS_ONLY
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "latch")) latch();
    else if (!strcmp(argv[1], "trap")) trap();
    else if (!strcmp(argv[1], "ebo")) ebo();
    else if (!strcmp(argv[1], "adpcm")) adpcm();
    else return 2;
    printf("APU device %s: %u failures\n", argv[1], failures);
    return failures ? 1 : 0;
}
#endif
