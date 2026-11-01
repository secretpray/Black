/* Execute the production output adapter. Only the DSP engine is a stand-in:
 * a running GP produces known samples, a reset GP leaves its output alone. */
#include BLACK_DSP_SOURCE
uint64_t g_f7_apu_dsp_frames_processed;
struct McpxApuDebug g_dbg, g_dbg_cache;
uint8_t *g_apu_ram_ptr;
float black_real_dsp_output[256][2];
int mcpx_apu_real_dsp_enabled(void) { return 1; }
void mcpx_apu_real_dsp_init(MCPXAPUState *d) { (void)d; }
void mcpx_apu_real_dsp_frame(MCPXAPUState *d, float bins[32][32])
{
    (void)bins;
    if ((d->gp.regs[NV_PAPU_GPRST] & 3) != 3) return;
    unsigned off = (d->ep_frame_div % 8) * 32;
    for (unsigned i = off; i < off + 32; ++i) {
        black_real_dsp_output[i][0] = 0.25f;
        black_real_dsp_output[i][1] = -0.5f;
    }
    d->monitor.dsp_wrote_frame = true;
}
static unsigned failures;
#define CHECK(cond, name) do { if (!(cond)) { \
    fprintf(stderr, "FAIL %s\n", name); ++failures; \
} } while (0)
int main(void)
{
    static MCPXAPUState d;
    float bins[32][32];
    for (unsigned b = 0; b < 32; ++b)
        for (unsigned i = 0; i < 32; ++i) bins[b][i] = 0.9f;
    setenv("BLACK_LEGACY_AUDIO_CLIP", "1", 1);
    /* Starting with GP stopped, and stopping after running, must both own
     * their silent slice. Other slices in a partial epoch must survive. */
    for (unsigned rst = 0; rst < 4; ++rst) {
        for (unsigned i = 0; i < 256; ++i)
            black_real_dsp_output[i][0] = black_real_dsp_output[i][1] = 0.75f;
        for (unsigned slice = 0; slice < 8; ++slice) {
            d.ep_frame_div = slice;
            d.gp.regs[NV_PAPU_GPRST] = rst;
            d.monitor.dsp_wrote_frame = false;
            mcpx_apu_dsp_frame(&d, bins);
            unsigned off = slice * 32;
            float l = rst == 3 ? 0.25f : 0, r = rst == 3 ? -0.5f : 0;
            CHECK(d.monitor.dsp_wrote_frame, "real path owns silence; monitor cannot use dry bins");
            for (unsigned i = off; i < off + 32; ++i) {
                CHECK(black_real_dsp_output[i][0] == l && black_real_dsp_output[i][1] == r, "float slice has current GP output or silence");
                CHECK(d.monitor.frame_buf[i][0] == (int16_t)(l * 32767) &&
                      d.monitor.frame_buf[i][1] == (int16_t)(r * 32767), "clip path receives no stale PCM");
            }
            if (slice < 7) CHECK(black_real_dsp_output[off + 32][0] == 0.75f, "future slices untouched");
        }
    }
    unsetenv("BLACK_LEGACY_AUDIO_CLIP");
    memset(&black_output_limiter, 0, sizeof black_output_limiter);
    /* Flush limiter lookahead over two silent epochs after stopping GP. */
    d.gp.regs[NV_PAPU_GPRST] = 0;
    for (unsigned slice = 0; slice < 16; ++slice) {
        d.ep_frame_div = slice;
        mcpx_apu_dsp_frame(&d, bins);
    }
    for (unsigned i = 0; i < 256; ++i)
        CHECK(d.monitor.frame_buf[i][0] == 0 && d.monitor.frame_buf[i][1] == 0,
              "limiter drains to silence without stale GP replay");
    printf("DSP slices: %u failures\n", failures);
    return failures ? 1 : 0;
}
